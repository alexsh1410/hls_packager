#pragma once

// Encoding layer contract.

#include <atomic>
#include <filesystem>
#include <stdexcept>
#include <string>

#include "model.h"

namespace hls {

// Shared flag the orchestrator uses to ask in-flight encodes to stop.
// Implementations must poll it and return promptly (by throwing EncodeError).
class CancelToken {
public:
    void cancel() { cancelled_.store(true); }
    bool isCancelled() const { return cancelled_.load(); }

private:
    std::atomic<bool> cancelled_{false};
};

class EncodeError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct EncodeRequest {
    std::filesystem::path input;
    Rendition rendition;
    std::filesystem::path outputDir;  // exists and is empty; segments go here
    double segmentSeconds = 6.0;
};

// Turns a source into segmented media for one rendition.
//
// Contract:
//  - Called concurrently from several threads, once per rendition, so
//    implementations must not share mutable state between calls.
//  - On success every returned Segment::uri exists inside request.outputDir,
//    and segments start on an IDR frame aligned to segmentSeconds so that
//    renditions are switchable at segment boundaries.
//  - On failure or cancellation throws EncodeError. Partial files may remain.
class IEncoder {
public:
    virtual ~IEncoder() = default;
    virtual EncodeResult encode(const EncodeRequest& request, const CancelToken& cancel) = 0;
};

}  // namespace hls
