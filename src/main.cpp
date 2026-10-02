#include <cstring>
#include <iostream>
#include <memory>
#include <string>

#include "ffmpeg_encoder.h"
#include "hls_playlist_writer.h"
#include "packager.h"

namespace {

// Exit codes: 0 = all renditions, 2 = partial ladder published, 1 = failed / usage.
constexpr int kExitOk = 0;
constexpr int kExitFailed = 1;
constexpr int kExitPartial = 2;

void printUsage(const char* prog) {
    std::cerr << "Usage: " << prog << " --input <source_video> --output <output_dir> [--strict]\n"
              << "\n"
              << "  --input   source video file\n"
              << "  --output  directory for master.m3u8 and one sub-directory per rendition\n"
              << "  --strict  fail the whole job if any rendition fails (default: publish\n"
              << "            the renditions that succeeded and exit with code 2)\n";
}

const char* statusName(hls::JobReport::Status s) {
    switch (s) {
        case hls::JobReport::Status::Success: return "success";
        case hls::JobReport::Status::Partial: return "partial";
        case hls::JobReport::Status::Failed: return "failed";
    }
    return "?";
}

}  // namespace

int main(int argc, char** argv) {
    hls::Job job;
    job.ladder = hls::defaultLadder();

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "--input" || arg == "-i") && i + 1 < argc) {
            job.input = argv[++i];
        } else if ((arg == "--output" || arg == "-o") && i + 1 < argc) {
            job.outputDir = argv[++i];
        } else if (arg == "--strict") {
            job.policy = hls::FailurePolicy::AllOrNothing;
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return kExitOk;
        } else {
            std::cerr << "Unknown or incomplete argument: " << arg << "\n\n";
            printUsage(argv[0]);
            return kExitFailed;
        }
    }
    if (job.input.empty() || job.outputDir.empty()) {
        printUsage(argv[0]);
        return kExitFailed;
    }

    hls::Packager packager(std::make_shared<hls::FfmpegEncoder>(),
                           std::make_shared<hls::HlsPlaylistWriter>());

    std::cout << "Packaging " << job.input << " into " << job.outputDir << " ("
              << job.ladder.size() << " renditions in parallel)...\n";
    const hls::JobReport report = packager.run(job);

    for (const auto& r : report.renditions) {
        std::cout << "  " << r.name << ": ";
        if (r.ok) {
            std::cout << "ok, " << r.segmentCount << " segments\n";
        } else {
            std::cout << "FAILED - " << r.error << "\n";
        }
    }
    std::cout << "Result: " << statusName(report.status) << "\n";
    if (!report.error.empty()) std::cerr << "Error: " << report.error << "\n";
    if (!report.masterPlaylist.empty()) std::cout << "Master playlist: " << report.masterPlaylist.string() << "\n";

    switch (report.status) {
        case hls::JobReport::Status::Success: return kExitOk;
        case hls::JobReport::Status::Partial: return kExitPartial;
        case hls::JobReport::Status::Failed: return kExitFailed;
    }
    return kExitFailed;
}
