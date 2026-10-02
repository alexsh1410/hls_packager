#pragma once

// Plain data types shared by all layers. No behaviour, no dependencies on
// ffmpeg or on the HLS text format.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace hls {

// One rung of the ABR ladder: what we ask the encoder to produce.
struct Rendition {
    std::string name;  // also used as the output sub-directory name
    int width = 0;
    int height = 0;
    int videoKbps = 0;
    int audioKbps = 0;
};

// One media segment as produced by an encoder.
struct Segment {
    std::string uri;        // relative to the rendition's directory, e.g. "seg000.ts"
    double durationSec = 0; // actual duration, not the target
    std::uint64_t bytes = 0;
};

// Everything the playlist layer needs to know about a finished rendition.
struct EncodeResult {
    Rendition rendition;
    std::vector<Segment> segments;
};

enum class FailurePolicy {
    BestEffort,    // publish whatever renditions succeeded
    AllOrNothing,  // any failure cancels the rest and fails the job
};

struct Job {
    std::filesystem::path input;
    std::filesystem::path outputDir;
    std::vector<Rendition> ladder;
    double segmentSeconds = 6.0;
    FailurePolicy policy = FailurePolicy::BestEffort;
};

struct RenditionReport {
    std::string name;
    bool ok = false;
    std::string error;  // empty when ok
    std::size_t segmentCount = 0;
};

struct JobReport {
    enum class Status { Success, Partial, Failed };
    Status status = Status::Failed;
    std::vector<RenditionReport> renditions;  // in ladder order
    std::filesystem::path masterPlaylist;     // empty if none was written
    std::string error;                        // job-level error (validation etc.)
};

// The ladder from the assignment.
inline std::vector<Rendition> defaultLadder() {
    return {
        {"high", 1920, 1080, 4000, 128},
        {"medium", 1280, 720, 2000, 128},
        {"low", 640, 360, 800, 96},
    };
}

}  // namespace hls
