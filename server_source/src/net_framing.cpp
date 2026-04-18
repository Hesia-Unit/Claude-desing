#include "net_framing.hpp"
#include <cstring>
#include <stdexcept>

namespace hesia {

bool ssl_read_all(SSL* ssl, uint8_t* dst, size_t n) {
    size_t off = 0;
    while (off < n) {
        int r = SSL_read(ssl, dst + off, static_cast<int>(n - off));
        if (r <= 0) {
            return false;
        }
        off += static_cast<size_t>(r);
    }
    return true;
}

bool ssl_write_all(SSL* ssl, const uint8_t* src, size_t n) {
    size_t off = 0;
    while (off < n) {
        int w = SSL_write(ssl, src + off, static_cast<int>(n - off));
        if (w <= 0) {
            return false;
        }
        off += static_cast<size_t>(w);
    }
    return true;
}

bool recv_frame(SSL* ssl, std::vector<uint8_t>& out_payload, size_t max_size_bytes) {
    uint8_t hdr[4];
    if (!ssl_read_all(ssl, hdr, sizeof(hdr))) return false;

    uint32_t len = (static_cast<uint32_t>(hdr[0]) << 24) |
                   (static_cast<uint32_t>(hdr[1]) << 16) |
                   (static_cast<uint32_t>(hdr[2]) << 8)  |
                   (static_cast<uint32_t>(hdr[3]));
    if (len > max_size_bytes) {
        return false;
    }
    out_payload.assign(len, 0);
    if (len == 0) return true;
    return ssl_read_all(ssl, out_payload.data(), len);
}

bool send_frame(SSL* ssl, const std::vector<uint8_t>& payload) {
    const uint32_t len = static_cast<uint32_t>(payload.size());
    uint8_t hdr[4];
    hdr[0] = (len >> 24) & 0xFF;
    hdr[1] = (len >> 16) & 0xFF;
    hdr[2] = (len >> 8) & 0xFF;
    hdr[3] = (len) & 0xFF;
    if (!ssl_write_all(ssl, hdr, sizeof(hdr))) return false;
    if (len == 0) return true;
    return ssl_write_all(ssl, payload.data(), payload.size());
}

} // namespace hesia
