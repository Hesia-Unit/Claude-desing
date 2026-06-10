#include "video_packet_parser.hpp"

#include <stdexcept>

namespace hesia {

ParsedVideoPacketFields parse_video_packet_fields(const std::vector<std::uint8_t>& data) {
    if (data.size() < 24) {
        throw std::runtime_error("Paquet trop court");
    }

    ParsedVideoPacketFields parsed;
    parsed.stream_id = (static_cast<std::uint32_t>(data[0]) << 24) |
                       (static_cast<std::uint32_t>(data[1]) << 16) |
                       (static_cast<std::uint32_t>(data[2]) << 8) |
                       static_cast<std::uint32_t>(data[3]);

    for (int i = 0; i < 8; i++) {
        parsed.frame_id = (parsed.frame_id << 8) | static_cast<std::uint64_t>(data[4 + i]);
    }

    parsed.iv.assign(data.begin() + 12, data.begin() + 24);
    parsed.payload.assign(data.begin() + 24, data.end());
    return parsed;
}

} // namespace hesia
