#pragma once

// Orchestration layer: runs one packaging job using an encoder and a playlist
// writer it only knows through their interfaces.

#include <memory>

#include "encoder.h"
#include "model.h"
#include "playlist_writer.h"

namespace hls {

class Packager {
public:
    Packager(std::shared_ptr<IEncoder> encoder, std::shared_ptr<IPlaylistWriter> playlists);

    // Encodes every rendition concurrently, applies job.policy to failures,
    // then writes media playlists and master.m3u8 for what succeeded.
    // Never throws for per-rendition failures: they are described in the report.
    JobReport run(const Job& job);

private:
    std::shared_ptr<IEncoder> encoder_;
    std::shared_ptr<IPlaylistWriter> playlists_;
};

}  // namespace hls
