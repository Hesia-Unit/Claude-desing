#ifndef HESIA_SERVER_SESSION_HPP
#define HESIA_SERVER_SESSION_HPP

#include <vector>
#include <string>
#include <memory>
#include <cstdint>

#include <openssl/ssl.h>

#include "server_state.hpp"
#include "../../drone_source/logger.hpp"
#include "../../drone_source/protocole.hpp"
#include "../../drone_source/secure_channel.hpp"
#include "../../drone_source/video_channel.hpp"
#include "policy.hpp"

namespace hesia {

class SecurityAudit;
class RateLimiter;

// A single TLS connection/session handler.
class HesiaServerSession {
public:
    HesiaServerSession(SSL* ssl,
                       const std::string& client_label,
                       const std::string& keys_dir,
                       const std::string& secure_dir,
                       std::shared_ptr<Logger> logger,
                       std::shared_ptr<SecurityAudit> audit,
                       const SecurityPolicy& policy);
    ~HesiaServerSession();

    // Run the full handshake and then process secure session messages.
    void run();

private:
    // Handshake steps
    void step_hello();
    void step_key_exchange();
    void step_drone_auth();
    void step_confirm();
    void secure_loop();

    // Helpers
    void load_server_keys();
    void load_drone_pubkey();
    void init_kyber();
    std::vector<uint8_t> sign_with_server_identity(const std::vector<uint8_t>& payload);
    std::vector<uint8_t> hkdf_session_key(const std::vector<uint8_t>& shared,
                                         const std::vector<uint8_t>& nonce_s,
                                         const std::vector<uint8_t>& nonce_d,
                                         const std::vector<uint8_t>& session_id,
                                         const std::vector<uint8_t>& tls_exporter_32);

    std::vector<uint8_t> sha3_512_concat(const std::vector<uint8_t>& a,
                                         const std::vector<uint8_t>& b);

private:
    SSL* ssl_;
    std::string client_label_;
    std::string keys_dir_;
    std::string secure_dir_;
    std::shared_ptr<Logger> log_;
    std::shared_ptr<SecurityAudit> audit_;
    SecurityPolicy policy_;
    ServerState state_{ServerState::IDLE};

    // TLS bindings (required)
    std::vector<uint8_t> tls_exporter_secret_;     // 32 bytes
    std::vector<uint8_t> tls_server_cert_sha256_;  // 32 bytes

    // Server identity / keys (ML-DSA-87 / Dilithium5)
    std::vector<uint8_t> server_sk_;
    std::vector<uint8_t> server_pk_;
    bool server_signing_in_tee_{false};

    // Kyber (ML-KEM-1024) ephemeral per session
    std::vector<uint8_t> kyber_pk_;
    std::vector<uint8_t> kyber_sk_;

    // KeyInit context
    std::vector<uint8_t> nonce_s_;
    std::vector<uint8_t> session_id_;

    // Drone identity (from DRONE_AUTH)
    std::vector<uint8_t> drone_pubkey_;
    // Optional pinned drone public key (override/verify)
    std::vector<uint8_t> expected_drone_pubkey_;
    std::vector<uint8_t> expected_drone_tee_pubkey_;
    bool require_pinned_drone_key_{false};

    // Derived keys / channels
    std::vector<uint8_t> session_key_;   // 32 bytes (AES-256 key)
    std::vector<uint8_t> last_block_hash_; // 64 bytes

    std::unique_ptr<SecureChannel> secure_channel_;
    std::unique_ptr<VideoChannel> video_channel_;
    std::unique_ptr<RateLimiter> rate_limiter_;

    // Transcript bytes (payloads, not framed headers)
    std::vector<uint8_t> t_hello_;
    std::vector<uint8_t> t_hello_ack_;
    std::vector<uint8_t> t_key_init_;
    std::vector<uint8_t> t_key_resp_;
    std::vector<uint8_t> transcript_hash_; // 64 bytes
    std::uint64_t video_frames_ok_{0};
    std::uint64_t last_video_log_ms_{0};
    std::uint64_t last_ui_video_write_ms_{0};
};

} // namespace hesia

#endif
