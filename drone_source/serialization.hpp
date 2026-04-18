#ifndef SERIALIZATION_HPP
#define SERIALIZATION_HPP

#include <vector>
#include <string>
#include <cstdint>
#include "protocole.hpp"

namespace hesia {

// Système de sérialisation simple (remplace pickle Python)

class Serializer {
public:
    static std::vector<uint8_t> serialize_hello(const Hello& hello);
    static Hello deserialize_hello(const std::vector<uint8_t>& data);
    
    static std::vector<uint8_t> serialize_hello_ack(const HelloAck& ack);
    static HelloAck deserialize_hello_ack(const std::vector<uint8_t>& data);
    static HelloAck deserialize_hello_ack(const std::vector<uint8_t>& data, size_t& offset);
    
    static std::vector<uint8_t> serialize_key_init(const KeyInit& init);
    static KeyInit deserialize_key_init(const std::vector<uint8_t>& data);
    static KeyInit deserialize_key_init(const std::vector<uint8_t>& data, size_t& offset);
    
    static std::vector<uint8_t> serialize_key_resp(const KeyResp& resp);
    static KeyResp deserialize_key_resp(const std::vector<uint8_t>& data);

    static std::vector<uint8_t> serialize_key_confirm(const KeyConfirm& kc);
    static KeyConfirm deserialize_key_confirm(const std::vector<uint8_t>& data);
    
    static std::vector<uint8_t> serialize_drone_auth(const BlockDroneAuth& auth);
    static BlockDroneAuth deserialize_drone_auth(const std::vector<uint8_t>& data);
    
    static std::vector<uint8_t> serialize_server_auth(const BlockServerAuth& auth);
    static BlockServerAuth deserialize_server_auth(const std::vector<uint8_t>& data);
    
    // Helpers
    static void write_uint32(std::vector<uint8_t>& out, uint32_t value);
    static uint32_t read_uint32(const std::vector<uint8_t>& data, size_t& offset);
    static void write_uint64(std::vector<uint8_t>& out, uint64_t value);
    static uint64_t read_uint64(const std::vector<uint8_t>& data, size_t& offset);
    static void write_string(std::vector<uint8_t>& out, const std::string& str);
    static std::string read_string(const std::vector<uint8_t>& data, size_t& offset);
    static void write_bytes(std::vector<uint8_t>& out, const std::vector<uint8_t>& bytes);
    static std::vector<uint8_t> read_bytes(const std::vector<uint8_t>& data, size_t& offset);
};

} // namespace hesia

#endif // SERIALIZATION_HPP
