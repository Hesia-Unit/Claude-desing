#ifndef PROTOCOLE_HPP
#define PROTOCOLE_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace hesia {

// Structures de protocole attendues par serialization.hpp / serialization.cpp

struct Hello {
    std::vector<uint8_t> random_64;  // 64 octets
    uint32_t proto_version = 0;
    uint32_t features = 0;
};

struct HelloAck {
    std::vector<uint8_t> response_hash; // 64 octets (ex: SHA3-512)
    uint32_t capabilities = 0;
};

struct KeyInit {
    std::vector<uint8_t> kyber_pubkey;
    std::vector<uint8_t> nonce_s;
    std::vector<uint8_t> session_id;
    uint64_t timestamp = 0;
    std::vector<uint8_t> context_hash;
};

struct KeyResp {
    std::vector<uint8_t> kyber_ciphertext;
    std::vector<uint8_t> nonce_d;
    std::vector<uint8_t> session_id;
    std::vector<uint8_t> response_hash;
};



struct KeyConfirm {
    std::vector<uint8_t> session_id;
    std::vector<uint8_t> transcript_hash; // 64 octets (SHA3-512)
    std::vector<uint8_t> signature;       // Signature serveur (ML-DSA-87 / Dilithium5)
    uint64_t timestamp = 0;               // optionnel (ms)
};
struct BlockDroneAuth {
    std::string drone_id;
    std::vector<uint8_t> drone_pubkey;
    std::vector<uint8_t> firmware_hash;
    std::vector<uint8_t> puf_response;
    std::vector<uint8_t> last_block_hash;
    std::vector<uint8_t> signature;

    // ✅ Proof-of-Possession bindings (appendés après signature pour compatibilité)
    std::vector<uint8_t> session_id;          // 16 octets
    std::vector<uint8_t> transcript_hash;     // 64 octets (SHA3-512)
    std::vector<uint8_t> server_cert_sha256;  // 32 octets (SHA-256 du cert serveur DER vu côté drone)
    std::vector<uint8_t> boot_measure_digest;       // 64 octets (SHA3-512 du rapport de measured boot)
    // Attestation TEE: ML-DSA-87 prefere, P-256 legacy encore acceptee cote serveur.
    std::vector<uint8_t> tee_attestation_pubkey;
    std::vector<uint8_t> tee_attestation_signature;
};

struct BlockServerAuth {
    std::string server_id;
    std::vector<uint8_t> server_pubkey;
    std::string mission_id;
    std::vector<uint8_t> policy_hash;
    std::vector<uint8_t> last_block_hash;
    std::vector<uint8_t> signature;
};

} // namespace hesia

#endif // PROTOCOLE_HPP
