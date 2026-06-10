#include "telemetry_parser.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

using namespace hesia;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string payload(reinterpret_cast<const char*>(data), size);
    try {
        (void)parse_telemetry_json_payload(payload, 0);
    } catch (...) {
    }
    return 0;
}
