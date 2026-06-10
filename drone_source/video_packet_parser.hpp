#pragma once

#include <cstdint>
#include <vector>

namespace hesia {

struct ParsedVideoPacketFields {
    std::uint32_t stream_id = 0;
    std::uint64_t frame_id = 0;
    std::vector<std::uint8_t> iv;
    std::vector<std::uint8_t> payload;
};

ParsedVideoPacketFields parse_video_packet_fields(const std::vector<std::uint8_t>& data);

} // namespace hesia
