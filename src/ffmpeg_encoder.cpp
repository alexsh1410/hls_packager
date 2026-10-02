#include "ffmpeg_encoder.h"

#include <cstdio>
#include <fstream>
#include <system_error>

#include "process.h"

namespace hls {
namespace {

constexpr const char* kSegmentList = ".segments.csv";
constexpr const char* kLogFile = "ffmpeg.log";

std::string fmt(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", v);
    return buf;
}

// ffmpeg's segment muxer writes "filename,start,end" per line.
std::vector<Segment> parseSegmentList(const std::filesystem::path& listPath,
                                      const std::filesystem::path& dir) {
    std::ifstream in(listPath);
    if (!in) throw EncodeError("ffmpeg produced no segment list");

    std::vector<Segment> segments;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        auto c2 = line.rfind(',');
        auto c1 = c2 == std::string::npos ? c2 : line.rfind(',', c2 - 1);
        if (c1 == std::string::npos) throw EncodeError("malformed segment list line: " + line);

        Segment s;
        s.uri = line.substr(0, c1);
        double start = std::stod(line.substr(c1 + 1, c2 - c1 - 1));
        double end = std::stod(line.substr(c2 + 1));
        s.durationSec = end - start;

        std::error_code ec;
        s.bytes = std::filesystem::file_size(dir / s.uri, ec);
        if (ec) throw EncodeError("segment listed but missing: " + s.uri);
        segments.push_back(std::move(s));
    }
    if (segments.empty()) throw EncodeError("ffmpeg produced no segments");
    return segments;
}

}  // namespace

std::vector<std::string> FfmpegEncoder::buildArgs(const EncodeRequest& req,
                                                  const std::filesystem::path& segmentList) const {
    const Rendition& r = req.rendition;
    const std::string w = std::to_string(r.width), h = std::to_string(r.height);
    const std::string seg = fmt(req.segmentSeconds);

    // Scale to fit and pad, so the output is exactly WxH whatever the source aspect.
    const std::string vf = "scale=" + w + ":" + h + ":force_original_aspect_ratio=decrease," +
                           "pad=" + w + ":" + h + ":(ow-iw)/2:(oh-ih)/2,setsar=1,format=yuv420p";

    std::vector<std::string> args = {
        options_.ffmpegPath, "-nostdin", "-hide_banner", "-y",
        "-i", req.input.string(),
        "-map", "0:v:0", "-map", "0:a:0?",
        "-vf", vf,
        "-c:v", "libx264", "-preset", options_.x264Preset, "-profile:v", "high",
        "-b:v", std::to_string(r.videoKbps) + "k",
        "-maxrate", std::to_string(r.videoKbps * 107 / 100) + "k",
        "-bufsize", std::to_string(r.videoKbps * 3 / 2) + "k",
        // IDR exactly every segmentSeconds, and no extra ones on scene cuts:
        // segment boundaries then line up across all renditions.
        "-force_key_frames", "expr:gte(t,n_forced*" + seg + ")",
        "-sc_threshold", "0",
        "-c:a", "aac", "-b:a", std::to_string(r.audioKbps) + "k", "-ac", "2", "-ar", "48000",
    };
    if (options_.threads > 0) {
        args.insert(args.end(), {"-threads", std::to_string(options_.threads)});
    }
    args.insert(args.end(), {
        "-f", "segment",
        "-segment_format", "mpegts",
        "-segment_time", seg,
        "-segment_time_delta", "0.05",
        "-segment_list", segmentList.string(),
        "-segment_list_type", "csv",
        (req.outputDir / "seg%03d.ts").string(),
    });
    return args;
}

EncodeResult FfmpegEncoder::encode(const EncodeRequest& req, const CancelToken& cancel) {
    const auto listPath = req.outputDir / kSegmentList;
    const auto logPath = req.outputDir / kLogFile;

    ProcessResult res;
    try {
        res = runProcess(buildArgs(req, listPath), {logPath, &cancel});
    } catch (const std::exception& e) {
        throw EncodeError(e.what());
    }
    if (res.cancelled) throw EncodeError("cancelled");
    if (res.signalled) throw EncodeError("ffmpeg killed by signal (see " + logPath.string() + ")");
    if (res.exitCode != 0) {
        throw EncodeError("ffmpeg exited with code " + std::to_string(res.exitCode) +
                          " (see " + logPath.string() + ")");
    }

    EncodeResult result;
    result.rendition = req.rendition;
    result.segments = parseSegmentList(listPath, req.outputDir);

    // Success: leave only media in the rendition directory.
    std::error_code ec;
    std::filesystem::remove(listPath, ec);
    std::filesystem::remove(logPath, ec);
    return result;
}

}  // namespace hls
