#pragma once

#include "playlist_writer.h"

namespace hls {

// RFC 8216 VOD playlists, EXT-X-VERSION 3.
class HlsPlaylistWriter : public IPlaylistWriter {
public:
    void writeMedia(const EncodeResult& result, const std::filesystem::path& path) override;
    void writeMaster(const std::vector<Variant>& variants,
                     const std::filesystem::path& path) override;
};

}  // namespace hls
