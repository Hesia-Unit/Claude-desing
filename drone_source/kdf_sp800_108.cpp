#include "kdf_sp800_108.hpp"

#include <openssl/hmac.h>
#include <stdexcept>
#include <algorithm>

namespace hesia {

static void write_u32_be(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(v & 0xFF));
}

std::vector<uint8_t> kdf_sp800_108_hmac_sha256(const std::vector<uint8_t>& key,
                                               const std::string& label,
                                               const std::vector<uint8_t>& context,
                                               size_t out_len) {
    std::vector<uint8_t> out;
    if (out_len == 0) return out;

    const uint32_t l_bits = static_cast<uint32_t>(out_len * 8);
    uint32_t counter = 1;
    out.reserve(out_len);

    while (out.size() < out_len) {
        std::vector<uint8_t> data;
        data.reserve(4 + label.size() + 1 + context.size() + 4);
        write_u32_be(data, counter);
        data.insert(data.end(), label.begin(), label.end());
        data.push_back(0x00);
        data.insert(data.end(), context.begin(), context.end());
        write_u32_be(data, l_bits);

        unsigned int mac_len = 0;
        unsigned char mac[EVP_MAX_MD_SIZE];
        if (!HMAC(EVP_sha256(),
                  key.data(), static_cast<int>(key.size()),
                  data.data(), data.size(),
                  mac, &mac_len)) {
            throw std::runtime_error("HMAC failed in SP800-108 KDF");
        }

        size_t to_copy = std::min(static_cast<size_t>(mac_len), out_len - out.size());
        out.insert(out.end(), mac, mac + to_copy);
        counter++;
    }

    return out;
}

} // namespace hesia
