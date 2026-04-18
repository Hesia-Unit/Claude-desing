#ifndef HESIA_TLS_UTILS_HPP
#define HESIA_TLS_UTILS_HPP

#include <vector>
#include <string>
#include <openssl/ssl.h>

namespace hesia {

// Export 32-byte keying material using the same label as the drone.
// Throws std::runtime_error on error.
std::vector<uint8_t> tls_exporter_32(SSL* ssl);

// SHA-256 digest of the server certificate in DER form (what the peer effectively sees).
// Throws std::runtime_error on error.
std::vector<uint8_t> tls_server_cert_sha256_der(SSL* ssl);

// Utility: OpenSSL error stack as string (best-effort).
std::string openssl_error_stack();

} // namespace hesia

#endif
