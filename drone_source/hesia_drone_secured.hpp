#pragma once
#include <string>
#include <vector>
#include <memory>
#include <set>
#include <chrono>
#include <openssl/kdf.h>

namespace hesia {

class HesiaDrone {
private:
    DroneState state;
    std::string drone_id;
    std::vector<uint8_t> last_block_hash;
    std::vector<uint8_t> session_key;
    
    std::pair<std::vector<uint8_t>, std::vector<uint8_t>> drone_keypair; // pk, sk
    std::vector<uint8_t> puf_secret;
    std::vector<uint8_t> hello_random;
    
    // ✅ SÉCURITÉ: Session tracking par instance (plus de static)
    std::set<std::vector<uint8_t>> used_session_ids;
    
    // ✅ SÉCURITÉ: Timestamp pour anti-rejeu
    std::chrono::milliseconds session_start_time;

public:
    // ✅ SÉCURITÉ: drone_id paramétrable (plus en dur)
    HesiaDrone(const std::string& did = "DRONE_001");
    
    // HELLO
    Hello build_hello();
    void handle_hello_ack(const HelloAck& ack);
    
    // KYBER
    KeyResp handle_key_init(const KeyInit& init);
    
    // DRONE AUTH
    BlockDroneAuth build_drone_auth();
    
    // SERVER AUTH
    std::vector<uint8_t> handle_server_auth(const BlockServerAuth& block);
    
    // CONFIRMATION
    std::vector<uint8_t> build_confirm();
    
    // Messages sécurisés
    std::vector<uint8_t> send_secure_message(const std::string& msg_type, const std::string& json_data);
    
    // Getters
    DroneState get_state() const { return state; }
    const std::string& get_drone_id() const { return drone_id; }
    
private:
    // ✅ SÉCURITÉ: Vérification certificat drone
    bool verify_drone_certificate(const std::string& received_id);
    
    // ✅ SÉCURITÉ: Vérification nonce serveur
    bool verify_server_nonce(const std::vector<uint8_t>& nonce_s);
    
    std::vector<uint8_t> derive_video_key(const std::vector<uint8_t>& session_key);
    std::string drone_state_to_string(DroneState state);
};

} // namespace hesia
