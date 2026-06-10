#include "kdf_sp800_108.hpp"

#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/params.h>
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
    if (label.find('\0') != std::string::npos) {
        throw std::invalid_argument("SP800-108 label must not contain NUL bytes");
    }
    if (label.size() > 4096 || context.size() > (1u << 20)) {
        throw std::invalid_argument("SP800-108 label/context too large");
    }

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

        EVP_MAC* mac = EVP_MAC_fetch(nullptr, "HMAC", nullptr);
        if (!mac) {
            throw std::runtime_error("EVP_MAC_fetch(HMAC) failed in SP800-108 KDF");
        }
        EVP_MAC_CTX* ctx = EVP_MAC_CTX_new(mac);
        EVP_MAC_free(mac);
        if (!ctx) {
            throw std::runtime_error("EVP_MAC_CTX_new failed in SP800-108 KDF");
        }

        OSSL_PARAM params[2];
        params[0] = OSSL_PARAM_construct_utf8_string(
            OSSL_MAC_PARAM_DIGEST,
            const_cast<char*>("SHA256"),
            0);
        params[1] = OSSL_PARAM_construct_end();

        if (EVP_MAC_init(ctx, key.data(), key.size(), params) != 1 ||
            EVP_MAC_update(ctx, data.data(), data.size()) != 1) {
            EVP_MAC_CTX_free(ctx);
            throw std::runtime_error("EVP_MAC init/update failed in SP800-108 KDF");
        }

        unsigned char mac_bytes[EVP_MAX_MD_SIZE];
        size_t mac_len = 0;
        if (EVP_MAC_final(ctx, mac_bytes, &mac_len, sizeof(mac_bytes)) != 1) {
            EVP_MAC_CTX_free(ctx);
            throw std::runtime_error("EVP_MAC final failed in SP800-108 KDF");
        }
        EVP_MAC_CTX_free(ctx);

        size_t to_copy = std::min(static_cast<size_t>(mac_len), out_len - out.size());
        out.insert(out.end(), mac_bytes, mac_bytes + to_copy);
        if (out.size() < out_len && counter == 0xFFFFFFFFu) {
            throw std::overflow_error("SP800-108 counter exhausted");
        }
        counter++;
    }

    return out;
}

} // namespace hesia
