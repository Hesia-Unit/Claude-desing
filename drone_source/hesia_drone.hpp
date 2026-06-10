#ifndef HESIA_DRONE_HPP
#define HESIA_DRONE_HPP

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <cstdint>
#include "state_drone.hpp"
#include "protocole.hpp"
#include "secure_channel.hpp"
#include "video_channel.hpp"
#include "crypto_real.hpp"
#include "exceptions.hpp"
#include "policy.hpp"
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <filesystem>

namespace hesia {

class HesiaDrone {
private:
    DroneState state;
    std::string drone_id;
    std::vector<uint8_t> last_block_hash;
    std::vector<uint8_t> session_key;
    std::vector<uint8_t> session_id;
    std::vector<uint8_t> key_exchange_transcript_hash;

    // Capabilities negotiated during HELLO/HELLO_ACK
    uint32_t server_capabilities = 0;
    bool use_tls_exporter_binding = false;

    // TLS 1.3 bindings (set by transport layer after handshake)
    std::vector<uint8_t> tls_exporter_secret;
    std::vector<uint8_t> tls_peer_cert_sha256;
    
    std::pair<std::vector<uint8_t>, std::vector<uint8_t>> drone_keypair; // pk, sk
    std::vector<uint8_t> puf_secret;
    std::vector<uint8_t> hello_random;
    SecurityPolicy policy_;
    
    // ✅ Clé publique du serveur (intégrée) pour vérifier les signatures du serveur
    std::vector<uint8_t> server_pubkey;
    
    std::unique_ptr<SecureChannel> secure_channel;
    std::unique_ptr<VideoChannel> video_channel;
    std::vector<uint8_t> encrypted_msg;
    uint64_t seq;
    // Protège l'état de chaînage des messages sécurisés (last_block_hash, seq)
    // contre les émissions concurrentes (ping / télémétrie / données de vol).
    std::mutex secure_message_mutex_;
    
    std::vector<uint8_t> derive_video_key(const std::vector<uint8_t>& session_key);
    std::vector<uint8_t> compute_firmware_sha3_512();
    std::vector<uint8_t> sign_with_drone_identity(const std::vector<uint8_t>& payload);
    std::filesystem::path sealed_dilithium_path_;
    bool tee_anchored_dilithium_ = false;
    
public:
    HesiaDrone(const std::string& did = "DRONE_001");
    ~HesiaDrone();
    
    // HELLO
    Hello build_hello();
    void handle_hello_ack(const HelloAck& ack);
    
    // KYBER
    KeyResp handle_key_init(const KeyInit& init);
    // KEY_CONFIRM (preuve serveur + binding transcript)
    void handle_key_confirm(const KeyConfirm& kc, const std::vector<uint8_t>& expected_transcript_hash);
    
    // DRONE AUTH
    BlockDroneAuth build_drone_auth();
    
    // SERVER AUTH
    std::vector<uint8_t> handle_server_auth(const BlockServerAuth& block);
    
    // CONFIRMATION
    std::vector<uint8_t> build_confirm();
    void finalize_confirm_ok(const std::vector<uint8_t>& response);
    
    // Messages sécurisés
    std::vector<uint8_t> send_secure_message(const std::string& msg_type, const std::string& json_data);
    
    // Getters
    DroneState get_state() const { return state; }
    std::string get_drone_id() const { return drone_id; }
    SecureChannel* get_secure_channel() const { return secure_channel.get(); }
    VideoChannel* get_video_channel() const { return video_channel.get(); }

    // TLS bindings (set by network layer after TLS handshake)
    void set_tls_exporter_secret(const std::vector<uint8_t>& secret);
    void set_tls_peer_cert_sha256(const std::vector<uint8_t>& digest);
};

} // namespace hesia

#endif // HESIA_DRONE_HPP
