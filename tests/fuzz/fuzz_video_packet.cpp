#include "video_packet_parser.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

using namespace hesia;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    std::vector<std::uint8_t> input(data, data + size);
    try {
        (void)parse_video_packet_fields(input);
    } catch (...) {
    }
    return 0;
}
