#pragma once

// Playlist layer contract.

#include <filesystem>
#include <string>
#include <vector>

#include "model.h"

namespace hls {

// A rendition as referenced from the master playlist.
struct Variant {
    EncodeResult result;
    std::string mediaPlaylistUri;  // relative to the master, e.g. "high/index.m3u8"
};

// Writes playlists describing already-encoded media. Never touches media files:
// everything it needs (durations, sizes) is in EncodeResult.
class IPlaylistWriter {
public:
    virtual ~IPlaylistWriter() = default;
    virtual void writeMedia(const EncodeResult& result, const std::filesystem::path& path) = 0;
    // Variants are listed in the given order; the first is the player's default start.
    virtual void writeMaster(const std::vector<Variant>& variants,
                             const std::filesystem::path& path) = 0;
};

}  // namespace hls
