#include "serialization.hpp"

#include <cstring>
#include <stdexcept>

namespace hesia {

namespace {

// Limites raisonnables pour éviter les OOM/DoS par messages malformés.
constexpr uint32_t kMaxStringLen = 8 * 1024;        // 8 KiB
constexpr uint32_t kMaxBlobLen = 256 * 1024;        // 256 KiB
constexpr uint32_t kMaxKeyBlobLen = 16 * 1024;      // 16 KiB
constexpr uint32_t kMaxSignatureLen = 64 * 1024;    // 64 KiB

inline void require_size_eq(const std::vector<uint8_t>& v, size_t expected, const char* field) {
    if (v.size() != expected) {
        throw std::runtime_error(std::string(field) + " taille invalide: " + std::to_string(v.size()) +
                                 " != " + std::to_string(expected));
    }
}

inline void require_size_range(const std::vector<uint8_t>& v,
                               size_t min_v,
                               size_t max_v,
                               const char* field) {
    if (v.size() < min_v || v.size() > max_v) {
        throw std::runtime_error(std::string(field) + " taille invalide: " + std::to_string(v.size()) +
                                 " (attendu " + std::to_string(min_v) + "-" + std::to_string(max_v) + ")");
    }
}

inline void require_string_len(const std::string& s, size_t max_v, const char* field) {
    if (s.size() > max_v) {
        throw std::runtime_error(std::string(field) + " trop long: " + std::to_string(s.size()));
    }
}

} // namespace

void Serializer::write_uint32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

uint32_t Serializer::read_uint32(const std::vector<uint8_t>& data, size_t& offset) {
    if (offset + 4 > data.size()) {
        throw std::runtime_error("Not enough data for uint32");
    }
    uint32_t value = (static_cast<uint32_t>(data[offset]) << 24) |
                     (static_cast<uint32_t>(data[offset + 1]) << 16) |
                     (static_cast<uint32_t>(data[offset + 2]) << 8) |
                     static_cast<uint32_t>(data[offset + 3]);
    offset += 4;
    return value;
}

void Serializer::write_uint64(std::vector<uint8_t>& out, uint64_t value) {
    out.push_back(static_cast<uint8_t>((value >> 56) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 48) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 40) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 32) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

uint64_t Serializer::read_uint64(const std::vector<uint8_t>& data, size_t& offset) {
    if (offset + 8 > data.size()) {
        throw std::runtime_error("Not enough data for uint64");
    }
    uint64_t value = (static_cast<uint64_t>(data[offset]) << 56) |
                     (static_cast<uint64_t>(data[offset + 1]) << 48) |
                     (static_cast<uint64_t>(data[offset + 2]) << 40) |
                     (static_cast<uint64_t>(data[offset + 3]) << 32) |
                     (static_cast<uint64_t>(data[offset + 4]) << 24) |
                     (static_cast<uint64_t>(data[offset + 5]) << 16) |
                     (static_cast<uint64_t>(data[offset + 6]) << 8) |
                     static_cast<uint64_t>(data[offset + 7]);
    offset += 8;
    return value;
}

void Serializer::write_string(std::vector<uint8_t>& out, const std::string& str) {
    if (str.size() > kMaxStringLen) {
        throw std::runtime_error("String too large");
    }
    write_uint32(out, static_cast<uint32_t>(str.size()));
    out.insert(out.end(), str.begin(), str.end());
}

std::string Serializer::read_string(const std::vector<uint8_t>& data, size_t& offset) {
    uint32_t len = read_uint32(data, offset);
    if (len > kMaxStringLen) {
        throw std::runtime_error("String length too large");
    }
    if (offset + len > data.size()) {
        throw std::runtime_error("Not enough data for string");
    }
    std::string str(reinterpret_cast<const char*>(&data[offset]), len);
    offset += len;
    return str;
}

void Serializer::write_bytes(std::vector<uint8_t>& out, const std::vector<uint8_t>& bytes) {
    if (bytes.size() > kMaxBlobLen) {
        throw std::runtime_error("Byte blob too large");
    }
    write_uint32(out, static_cast<uint32_t>(bytes.size()));
    out.insert(out.end(), bytes.begin(), bytes.end());
}

std::vector<uint8_t> Serializer::read_bytes(const std::vector<uint8_t>& data, size_t& offset) {
    uint32_t len = read_uint32(data, offset);
    if (len > kMaxBlobLen) {
        throw std::runtime_error("Byte blob too large");
    }
    if (offset + len > data.size()) {
        throw std::runtime_error("Not enough data for bytes");
    }
    std::vector<uint8_t> bytes(data.begin() + static_cast<std::ptrdiff_t>(offset),
                               data.begin() + static_cast<std::ptrdiff_t>(offset + len));
    offset += len;
    return bytes;
}

std::vector<uint8_t> Serializer::serialize_hello(const Hello& hello) {
    std::vector<uint8_t> result;
    write_bytes(result, hello.random_64);
    write_uint32(result, hello.proto_version);
    write_uint32(result, hello.features);
    return result;
}

Hello Serializer::deserialize_hello(const std::vector<uint8_t>& data) {
    size_t offset = 0;
    Hello hello;
    hello.random_64 = read_bytes(data, offset);
    require_size_eq(hello.random_64, 64, "Hello.random_64");
    hello.proto_version = read_uint32(data, offset);
    hello.features = read_uint32(data, offset);
    if (offset != data.size()) {
        throw std::runtime_error("HELLO: trailing bytes not allowed");
    }
    return hello;
}

std::vector<uint8_t> Serializer::serialize_hello_ack(const HelloAck& ack) {
    std::vector<uint8_t> result;
    write_bytes(result, ack.response_hash);
    write_uint32(result, ack.capabilities);
    return result;
}

HelloAck Serializer::deserialize_hello_ack(const std::vector<uint8_t>& data) {
    size_t offset = 0;
    HelloAck ack = deserialize_hello_ack(data, offset);
    if (offset != data.size()) {
        throw std::runtime_error("HELLO_ACK: trailing bytes not allowed");
    }
    return ack;
}

HelloAck Serializer::deserialize_hello_ack(const std::vector<uint8_t>& data, size_t& offset) {
    HelloAck ack;
    ack.response_hash = read_bytes(data, offset);
    // hash_data() renvoie 64 octets (SHA3-512 / SHA-512)
    require_size_eq(ack.response_hash, 64, "HelloAck.response_hash");
    ack.capabilities = read_uint32(data, offset);
    return ack;
}

std::vector<uint8_t> Serializer::serialize_key_init(const KeyInit& init) {
    std::vector<uint8_t> result;
    write_bytes(result, init.kyber_pubkey);
    write_bytes(result, init.nonce_s);
    write_bytes(result, init.session_id);
    write_uint64(result, init.timestamp);
    write_bytes(result, init.context_hash);
    return result;
}

KeyInit Serializer::deserialize_key_init(const std::vector<uint8_t>& data) {
    size_t offset = 0;
    KeyInit init = deserialize_key_init(data, offset);
    if (offset != data.size()) {
        throw std::runtime_error("KEY_INIT: trailing bytes not allowed");
    }
    return init;
}

KeyInit Serializer::deserialize_key_init(const std::vector<uint8_t>& data, size_t& offset) {
    KeyInit init;
    init.kyber_pubkey = read_bytes(data, offset);
    init.nonce_s = read_bytes(data, offset);
    init.session_id = read_bytes(data, offset);
    init.timestamp = read_uint64(data, offset);
    init.context_hash = read_bytes(data, offset);

    require_size_range(init.kyber_pubkey, 1, kMaxKeyBlobLen, "KeyInit.kyber_pubkey");
    require_size_range(init.nonce_s, 16, 64, "KeyInit.nonce_s");
    require_size_range(init.session_id, 16, 128, "KeyInit.session_id");
    require_size_eq(init.context_hash, 64, "KeyInit.context_hash");

    return init;
}


std::vector<uint8_t> Serializer::serialize_key_resp(const KeyResp& resp) {
    std::vector<uint8_t> result;
    write_bytes(result, resp.kyber_ciphertext);
    write_bytes(result, resp.nonce_d);
    write_bytes(result, resp.session_id);
    write_bytes(result, resp.response_hash);
    return result;
}

KeyResp Serializer::deserialize_key_resp(const std::vector<uint8_t>& data) {
    size_t offset = 0;
    KeyResp resp;
    resp.kyber_ciphertext = read_bytes(data, offset);
    resp.nonce_d = read_bytes(data, offset);
    resp.session_id = read_bytes(data, offset);
    resp.response_hash = read_bytes(data, offset);

    require_size_range(resp.kyber_ciphertext, 1, kMaxKeyBlobLen, "KeyResp.kyber_ciphertext");
    require_size_range(resp.nonce_d, 16, 64, "KeyResp.nonce_d");
    require_size_range(resp.session_id, 16, 128, "KeyResp.session_id");
    require_size_eq(resp.response_hash, 64, "KeyResp.response_hash");

    if (offset != data.size()) {
        throw std::runtime_error("KEY_RESP: trailing bytes not allowed");
    }
    return resp;
}

std::vector<uint8_t> Serializer::serialize_key_confirm(const KeyConfirm& kc) {
    std::vector<uint8_t> result;
    write_bytes(result, kc.session_id);
    write_bytes(result, kc.transcript_hash);
    write_uint64(result, kc.timestamp);
    write_bytes(result, kc.signature);
    return result;
}

KeyConfirm Serializer::deserialize_key_confirm(const std::vector<uint8_t>& data) {
    size_t offset = 0;
    KeyConfirm kc;
    kc.session_id = read_bytes(data, offset);
    kc.transcript_hash = read_bytes(data, offset);
    kc.timestamp = read_uint64(data, offset);
    kc.signature = read_bytes(data, offset);

    require_size_range(kc.session_id, 16, 128, "KeyConfirm.session_id");
    require_size_eq(kc.transcript_hash, 64, "KeyConfirm.transcript_hash");
    require_size_range(kc.signature, 1, kMaxSignatureLen, "KeyConfirm.signature");
    if (offset != data.size()) {
        throw std::runtime_error("KEY_CONFIRM: trailing bytes not allowed");
    }
    return kc;
}

std::vector<uint8_t> Serializer::serialize_drone_auth(const BlockDroneAuth& auth) {
    std::vector<uint8_t> result;
    write_string(result, auth.drone_id);
    write_bytes(result, auth.drone_pubkey);
    write_bytes(result, auth.firmware_hash);
    write_bytes(result, auth.puf_response);
    write_bytes(result, auth.last_block_hash);
    write_bytes(result, auth.signature);

    // Extensions PoP (optionnelles)
    write_bytes(result, auth.session_id);
    write_bytes(result, auth.transcript_hash);
    write_bytes(result, auth.server_cert_sha256);

    return result;
}

BlockDroneAuth Serializer::deserialize_drone_auth(const std::vector<uint8_t>& data) {
    size_t offset = 0;
    BlockDroneAuth auth;
    auth.drone_id = read_string(data, offset);
    auth.drone_pubkey = read_bytes(data, offset);
    auth.firmware_hash = read_bytes(data, offset);
    auth.puf_response = read_bytes(data, offset);
    auth.last_block_hash = read_bytes(data, offset);
    auth.signature = read_bytes(data, offset);

    // Extensions PoP (strictes)
    auth.session_id = read_bytes(data, offset);
    auth.transcript_hash = read_bytes(data, offset);
    auth.server_cert_sha256 = read_bytes(data, offset);

    require_string_len(auth.drone_id, 128, "BlockDroneAuth.drone_id");
    require_size_range(auth.drone_pubkey, 1, kMaxKeyBlobLen, "BlockDroneAuth.drone_pubkey");
    // firmware_hash: hash_data() => 64
    require_size_eq(auth.firmware_hash, 64, "BlockDroneAuth.firmware_hash");
    require_size_range(auth.puf_response, 16, kMaxBlobLen, "BlockDroneAuth.puf_response");
    require_size_eq(auth.last_block_hash, 64, "BlockDroneAuth.last_block_hash");
    require_size_range(auth.signature, 1, kMaxSignatureLen, "BlockDroneAuth.signature");
    require_size_range(auth.session_id, 16, 128, "BlockDroneAuth.session_id");
    require_size_eq(auth.transcript_hash, 64, "BlockDroneAuth.transcript_hash");
    require_size_eq(auth.server_cert_sha256, 32, "BlockDroneAuth.server_cert_sha256");

    if (offset != data.size()) {
        throw std::runtime_error("DRONE_AUTH: trailing bytes not allowed");
    }

    return auth;
}

std::vector<uint8_t> Serializer::serialize_server_auth(const BlockServerAuth& auth) {
    std::vector<uint8_t> result;
    write_string(result, auth.server_id);
    write_bytes(result, auth.server_pubkey);
    write_string(result, auth.mission_id);
    write_bytes(result, auth.policy_hash);
    write_bytes(result, auth.last_block_hash);
    write_bytes(result, auth.signature);
    return result;
}

BlockServerAuth Serializer::deserialize_server_auth(const std::vector<uint8_t>& data) {
    size_t offset = 0;
    BlockServerAuth auth;
    auth.server_id = read_string(data, offset);
    auth.server_pubkey = read_bytes(data, offset);
    auth.mission_id = read_string(data, offset);
    auth.policy_hash = read_bytes(data, offset);
    auth.last_block_hash = read_bytes(data, offset);
    auth.signature = read_bytes(data, offset);

    require_string_len(auth.server_id, 128, "BlockServerAuth.server_id");
    require_size_range(auth.server_pubkey, 0, kMaxKeyBlobLen, "BlockServerAuth.server_pubkey");  // ✅ Autoriser clé vide
    require_string_len(auth.mission_id, 256, "BlockServerAuth.mission_id");
    require_size_eq(auth.policy_hash, 64, "BlockServerAuth.policy_hash");
    require_size_eq(auth.last_block_hash, 64, "BlockServerAuth.last_block_hash");
    require_size_range(auth.signature, 1, kMaxSignatureLen, "BlockServerAuth.signature");

    if (offset != data.size()) {
        throw std::runtime_error("SERVER_AUTH: trailing bytes not allowed");
    }
    return auth;
}

} // namespace hesia
