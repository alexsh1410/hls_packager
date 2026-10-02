#pragma once

#include <string>

#include "encoder.h"

namespace hls {

// Encodes one rendition by running the ffmpeg CLI as a subprocess
// (H.264 + AAC in MPEG-TS).
class FfmpegEncoder : public IEncoder {
public:
    struct Options {
        std::string ffmpegPath = "ffmpeg";
        std::string x264Preset = "veryfast";
        // Threads per ffmpeg process; 0 lets ffmpeg decide. Several encodes run
        // at once, so capping this avoids oversubscribing the CPU.
        int threads = 0;
    };

    FfmpegEncoder() = default;
    explicit FfmpegEncoder(Options options) : options_(std::move(options)) {}

    EncodeResult encode(const EncodeRequest& request, const CancelToken& cancel) override;

private:
    std::vector<std::string> buildArgs(const EncodeRequest& request,
                                       const std::filesystem::path& segmentList) const;

    Options options_;
};

}  // namespace hls
