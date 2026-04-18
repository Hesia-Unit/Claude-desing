#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <openssl/ssl.h>

namespace hesia {

// TLS 1.3 wrapper (OpenSSL)
//
// - Peer verification (CA + hostname/IP SAN) required for mTLS
// - Optional SPKI pinning (SHA-256 of peer certificate SubjectPublicKeyInfo)
// - TLS exporter for PQC hybrid key schedule binding
class TLSChannel {
public:
    TLSChannel();
    ~TLSChannel();

    TLSChannel(const TLSChannel&) = delete;
    TLSChannel& operator=(const TLSChannel&) = delete;

    // Perform TLS 1.3 handshake over an already-connected socket.
    // 'host' is used for certificate verification.
    bool connect_on_socket(int socket_fd, const std::string& host, bool verify_peer);

    struct TLSPaths {
        std::string ca_path;
        std::string cert_path;
        std::string key_path;
    };

    void set_cert_paths(const TLSPaths& paths);

    bool is_connected() const { return connected_; }

    void set_timeouts(int read_timeout_ms, int write_timeout_ms);

    int write(const uint8_t* data, int len);
    int read(uint8_t* out, int maxlen);

    void close();

    // Request a TLS 1.3 KeyUpdate (rekey).
    void request_key_update();

    // Optional: pin peer certificate SPKI SHA-256.
    // If 'pin_or_bytes' is not 32 bytes, it is SHA-256 hashed to produce the 32-byte pin.
    void enable_spki_pinning(const std::vector<uint8_t>& pin_or_bytes);

    // TLS exporter (RFC 5705-style, via TLS 1.3 exporter interface).
    std::vector<uint8_t> export_keying_material(const std::string& label, size_t length) const;

    // SHA-256 digest of peer certificate DER.
    std::vector<uint8_t> peer_cert_sha256() const;

private:
    SSL_CTX* ctx_{nullptr};
    SSL* ssl_{nullptr};
    bool connected_{false};
    int read_timeout_ms_{5000};
    int write_timeout_ms_{5000};

    bool use_spki_pinning_{false};
    std::vector<uint8_t> spki_sha256_pin_;

    TLSPaths cert_paths_{};

    static bool is_ip_literal(const std::string& host);
    static std::vector<uint8_t> sha256(const uint8_t* data, size_t len);

    bool verify_peer_spki() const;
};

} // namespace hesia
