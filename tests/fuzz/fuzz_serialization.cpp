#include "serialization.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

using namespace hesia;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    std::vector<std::uint8_t> input(data, data + size);

    try { (void)Serializer::deserialize_hello(input); } catch (...) {}
    try { (void)Serializer::deserialize_hello_ack(input); } catch (...) {}
    try { (void)Serializer::deserialize_key_init(input); } catch (...) {}
    try { (void)Serializer::deserialize_key_resp(input); } catch (...) {}
    try { (void)Serializer::deserialize_key_confirm(input); } catch (...) {}
    try { (void)Serializer::deserialize_drone_auth(input); } catch (...) {}
    try { (void)Serializer::deserialize_server_auth(input); } catch (...) {}

    return 0;
}
