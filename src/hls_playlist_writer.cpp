#include "hls_playlist_writer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <stdexcept>

namespace hls {
namespace {

std::ofstream openForWriting(const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot open " + path.string() + " for writing");
    return out;
}

void finish(std::ofstream& out, const std::filesystem::path& path) {
    out.flush();
    if (!out) throw std::runtime_error("failed writing " + path.string());
}

std::string formatDuration(double seconds) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.6f", seconds);
    return buf;
}

// Smallest integer that every EXTINF rounds down to or below (RFC 8216 4.3.3.1).
int targetDuration(const std::vector<Segment>& segments) {
    long maxRounded = 1;
    for (const auto& s : segments) maxRounded = std::max(maxRounded, std::lround(s.durationSec));
    return static_cast<int>(maxRounded);
}

// Peak segment bitrate in bits/s, as required for BANDWIDTH (RFC 8216 4.3.4.2).
std::uint64_t peakBandwidth(const std::vector<Segment>& segments) {
    double peak = 0;
    for (const auto& s : segments) {
        if (s.durationSec > 0) peak = std::max(peak, s.bytes * 8.0 / s.durationSec);
    }
    return static_cast<std::uint64_t>(std::ceil(peak));
}

}  // namespace

void HlsPlaylistWriter::writeMedia(const EncodeResult& result, const std::filesystem::path& path) {
    if (result.segments.empty()) {
        throw std::runtime_error("rendition '" + result.rendition.name + "' has no segments");
    }
    auto out = openForWriting(path);
    out << "#EXTM3U\n"
        << "#EXT-X-VERSION:3\n"
        << "#EXT-X-TARGETDURATION:" << targetDuration(result.segments) << "\n"
        << "#EXT-X-MEDIA-SEQUENCE:0\n"
        << "#EXT-X-PLAYLIST-TYPE:VOD\n";
    for (const auto& s : result.segments) {
        out << "#EXTINF:" << formatDuration(s.durationSec) << ",\n" << s.uri << "\n";
    }
    out << "#EXT-X-ENDLIST\n";
    finish(out, path);
}

void HlsPlaylistWriter::writeMaster(const std::vector<Variant>& variants,
                                    const std::filesystem::path& path) {
    if (variants.empty()) throw std::runtime_error("master playlist needs at least one variant");
    auto out = openForWriting(path);
    out << "#EXTM3U\n"
        << "#EXT-X-VERSION:3\n";
    for (const auto& v : variants) {
        const auto& r = v.result;
        out << "#EXT-X-STREAM-INF:BANDWIDTH=" << peakBandwidth(r.segments)
            << ",RESOLUTION=" << r.rendition.width << "x" << r.rendition.height << "\n"
            << v.mediaPlaylistUri << "\n";
    }
    finish(out, path);
}

}  // namespace hls
