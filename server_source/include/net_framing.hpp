#ifndef HESIA_NET_FRAMING_HPP
#define HESIA_NET_FRAMING_HPP

#include <cstdint>
#include <vector>
#include <openssl/ssl.h>

namespace hesia {

// Read exactly n bytes from SSL (blocking) into dst. Returns true on success.
bool ssl_read_all(SSL* ssl, uint8_t* dst, size_t n);

// Write exactly n bytes to SSL (blocking) from src. Returns true on success.
bool ssl_write_all(SSL* ssl, const uint8_t* src, size_t n);

// Framed protocol: 4B big-endian length header + payload.
bool recv_frame(SSL* ssl, std::vector<uint8_t>& out_payload, size_t max_size_bytes);

// Send a framed payload.
bool send_frame(SSL* ssl, const std::vector<uint8_t>& payload);

} // namespace hesia

#endif
