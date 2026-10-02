#include "packager.h"

#include <cmath>
#include <future>
#include <set>
#include <stdexcept>
#include <system_error>

namespace hls {
namespace {

constexpr const char* kMasterName = "master.m3u8";
constexpr const char* kMediaName = "index.m3u8";

// Returns an error message, or empty if the job is runnable.
std::string validate(const Job& job) {
    if (job.ladder.empty()) return "ladder is empty";
    if (job.segmentSeconds <= 0) return "segment duration must be positive";
    if (!std::filesystem::is_regular_file(job.input)) {
        return "input not found: " + job.input.string();
    }
    std::set<std::string> names;
    for (const auto& r : job.ladder) {
        if (r.name.empty() || r.name.find('/') != std::string::npos || r.name == "." ||
            r.name == "..") {
            return "invalid rendition name '" + r.name + "'";
        }
        if (!names.insert(r.name).second) return "duplicate rendition name '" + r.name + "'";
        if (r.width <= 0 || r.height <= 0 || r.width % 2 || r.height % 2) {
            return "rendition '" + r.name + "' needs positive, even dimensions";
        }
        if (r.videoKbps <= 0 || r.audioKbps <= 0) {
            return "rendition '" + r.name + "' needs positive bitrates";
        }
    }
    return "";
}

// Checks the encoder kept to the job's segment length. Segments are cut on frame
// boundaries, so durations are compared after rounding to whole seconds, which is
// also what keeps EXT-X-TARGETDURATION equal to the requested length. The last
// segment holds the remainder of the video and may be shorter.
// Returns an error message, or empty if all segments conform.
std::string checkSegmentDurations(const EncodeResult& result, double segmentSeconds) {
    const long expected = std::lround(segmentSeconds);
    for (std::size_t i = 0; i < result.segments.size(); ++i) {
        const Segment& s = result.segments[i];
        const long actual = std::lround(s.durationSec);
        const bool isLast = i + 1 == result.segments.size();
        if (isLast ? actual > expected : actual != expected) {
            return "segment " + s.uri + " is " + std::to_string(s.durationSec) + "s, expected " +
                   std::to_string(expected) + "s";
        }
    }
    return "";
}

// Outcome of one rendition's encode task. Carries its own rendition so results
// never have to be matched back to the ladder by position.
struct Attempt {
    Rendition rendition;
    EncodeResult result;
    std::string error;  // empty on success
};

}  // namespace

Packager::Packager(std::shared_ptr<IEncoder> encoder, std::shared_ptr<IPlaylistWriter> playlists)
    : encoder_(std::move(encoder)), playlists_(std::move(playlists)) {
    if (!encoder_ || !playlists_) throw std::invalid_argument("Packager needs an encoder and a playlist writer");
}

JobReport Packager::run(const Job& job) {
    JobReport report;
    if (auto err = validate(job); !err.empty()) {
        report.error = err;
        return report;
    }

    // Fresh output: stale segments from an earlier, longer run must not linger,
    // and an old master must not survive a failed run.
    const auto masterPath = job.outputDir / kMasterName;
    try {
        std::filesystem::create_directories(job.outputDir);
        std::filesystem::remove(masterPath);
        for (const auto& r : job.ladder) {
            std::filesystem::remove_all(job.outputDir / r.name);
            std::filesystem::create_directories(job.outputDir / r.name);
        }
    } catch (const std::filesystem::filesystem_error& e) {
        report.error = std::string("cannot prepare output directory: ") + e.what();
        return report;
    }

    // One task per rendition. Each ffmpeg process is itself multi-threaded, so
    // the threads here mostly wait on child processes; std::async is enough.
    CancelToken cancel;
    const bool cancelOthersOnFailure = job.policy == FailurePolicy::AllOrNothing;
    std::vector<std::future<Attempt>> futures;
    futures.reserve(job.ladder.size());
    for (const auto& r : job.ladder) {
        EncodeRequest req{job.input, r, job.outputDir / r.name, job.segmentSeconds};
        futures.push_back(std::async(std::launch::async, [this, req, &cancel, cancelOthersOnFailure] {
            Attempt a;
            a.rendition = req.rendition;
            try {
                a.result = encoder_->encode(req, cancel);
            } catch (const std::exception& e) {
                a.error = e.what();
            } catch (...) {
                a.error = "unknown error";
            }
            if (!a.error.empty() && cancelOthersOnFailure) cancel.cancel();
            return a;
        }));
    }

    std::vector<Attempt> attempts;
    for (auto& f : futures) attempts.push_back(f.get());

    // Media playlists for the renditions that encoded.
    std::vector<Variant> variants;
    for (Attempt& a : attempts) {
        const Rendition& r = a.rendition;
        if (a.error.empty() && job.policy == FailurePolicy::AllOrNothing && cancel.isCancelled()) {
            a.error = "discarded: another rendition failed";
        }
        if (a.error.empty()) a.error = checkSegmentDurations(a.result, job.segmentSeconds);
        if (a.error.empty()) {
            try {
                playlists_->writeMedia(a.result, job.outputDir / r.name / kMediaName);
                variants.push_back({a.result, r.name + "/" + kMediaName});
            } catch (const std::exception& e) {
                a.error = std::string("writing media playlist: ") + e.what();
            }
        }
        report.renditions.push_back({r.name, a.error.empty(), a.error,
                                     a.error.empty() ? a.result.segments.size() : 0});
    }

    const bool allOk = variants.size() == job.ladder.size();
    if (variants.empty() || (!allOk && job.policy == FailurePolicy::AllOrNothing)) {
        report.status = JobReport::Status::Failed;
        report.error = "no usable renditions";
        return report;
    }

    // Master lists only renditions that fully succeeded, in ladder order, so a
    // player is never pointed at a missing or broken media playlist.
    try {
        playlists_->writeMaster(variants, masterPath);
    } catch (const std::exception& e) {
        report.status = JobReport::Status::Failed;
        report.error = std::string("writing master playlist: ") + e.what();
        return report;
    }
    report.masterPlaylist = masterPath;
    report.status = allOk ? JobReport::Status::Success : JobReport::Status::Partial;
    return report;
}

}  // namespace hls
