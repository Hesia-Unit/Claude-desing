// fips_selftest.cpp

#include "fips_selftest.hpp"

#include <array>
#include <cstdint>
#include <vector>

#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/core_names.h>
#include <openssl/params.h>
#include <openssl/crypto.h>
#include <openssl/err.h>

namespace hesia::fips::selftest {

namespace {

std::string last_openssl_error() {
    unsigned long err = ERR_get_error();
    if (err == 0) return {};
    char buf[256];
    ERR_error_string_n(err, buf, sizeof(buf));
    return std::string(buf);
}

bool sha256_kat(const char* propq, std::string& err) {
    static constexpr uint8_t kMsg[] = {'a','b','c'};
    static constexpr uint8_t kExpected[32] = {
        0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad
    };

    EVP_MD* md = EVP_MD_fetch(nullptr, "SHA256", propq);
    if (!md) {
        err = "SHA256 fetch failed: " + last_openssl_error();
        return false;
    }
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        EVP_MD_free(md);
        err = "EVP_MD_CTX_new failed";
        return false;
    }

    std::array<uint8_t, 32> out{};
    unsigned int out_len = 0;

    bool ok = (EVP_DigestInit_ex(ctx, md, nullptr) == 1) &&
              (EVP_DigestUpdate(ctx, kMsg, sizeof(kMsg)) == 1) &&
              (EVP_DigestFinal_ex(ctx, out.data(), &out_len) == 1) &&
              (out_len == out.size());

    EVP_MD_CTX_free(ctx);
    EVP_MD_free(md);

    if (!ok) {
        err = "SHA256 KAT failed: " + last_openssl_error();
        return false;
    }

    if (CRYPTO_memcmp(out.data(), kExpected, out.size()) != 0) {
        err = "SHA256 KAT mismatch";
        return false;
    }
    return true;
}

bool hmac_sha256_kat(const char* propq, std::string& err) {
    // RFC 4231 test case 1
    static constexpr uint8_t kKey[20] = {
        0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,
        0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b
    };
    static constexpr uint8_t kMsg[] = {'H','i',' ','T','h','e','r','e'};
    static constexpr uint8_t kExpected[32] = {
        0xb0,0x34,0x4c,0x61,0xd8,0xdb,0x38,0x53,0x5c,0xa8,0xaf,0xce,0xaf,0x0b,0xf1,0x2b,
        0x88,0x1d,0xc2,0x00,0xc9,0x83,0x3d,0xa7,0x26,0xe9,0x37,0x6c,0x2e,0x32,0xcf,0xf7
    };

    EVP_MAC* mac = EVP_MAC_fetch(nullptr, "HMAC", propq);
    if (!mac) {
        err = "HMAC fetch failed: " + last_openssl_error();
        return false;
    }
    EVP_MAC_CTX* mctx = EVP_MAC_CTX_new(mac);
    if (!mctx) {
        EVP_MAC_free(mac);
        err = "EVP_MAC_CTX_new failed";
        return false;
    }

    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST, const_cast<char*>("SHA256"), 0),
        OSSL_PARAM_construct_end()
    };

    std::array<uint8_t, 32> out{};
    size_t out_len = out.size();

    bool ok = (EVP_MAC_init(mctx, kKey, sizeof(kKey), params) == 1) &&
              (EVP_MAC_update(mctx, kMsg, sizeof(kMsg)) == 1) &&
              (EVP_MAC_final(mctx, out.data(), &out_len, out.size()) == 1) &&
              (out_len == out.size());

    EVP_MAC_CTX_free(mctx);
    EVP_MAC_free(mac);

    if (!ok) {
        err = "HMAC-SHA256 KAT failed: " + last_openssl_error();
        return false;
    }
    if (CRYPTO_memcmp(out.data(), kExpected, out.size()) != 0) {
        err = "HMAC-SHA256 KAT mismatch";
        return false;
    }
    return true;
}

bool hkdf_sha256_kat(const char* propq, std::string& err) {
    // RFC 5869 test case 1
    static constexpr uint8_t kIkm[22] = {
        0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,
        0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b
    };
    static constexpr uint8_t kSalt[13] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c
    };
    static constexpr uint8_t kInfo[10] = {
        0xf0,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9
    };
    static constexpr uint8_t kExpected[42] = {
        0x3c,0xb2,0x5f,0x25,0xfa,0xac,0xd5,0x7a,0x90,0x43,0x4f,0x64,0xd0,0x36,0x2f,0x2a,
        0x2d,0x2d,0x0a,0x90,0xcf,0x1a,0x5a,0x4c,0x5d,0xb0,0x2d,0x56,0xec,0xc4,0xc5,0xbf,
        0x34,0x00,0x72,0x08,0xd5,0xb8,0x87,0x18,0x58,0x65
    };

    EVP_KDF* kdf = EVP_KDF_fetch(nullptr, "HKDF", propq);
    if (!kdf) {
        err = "HKDF fetch failed: " + last_openssl_error();
        return false;
    }
    EVP_KDF_CTX* kctx = EVP_KDF_CTX_new(kdf);
    if (!kctx) {
        EVP_KDF_free(kdf);
        err = "EVP_KDF_CTX_new failed";
        return false;
    }

    std::array<uint8_t, 42> out{};

    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, const_cast<char*>("SHA256"), 0),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT, const_cast<uint8_t*>(kSalt), sizeof(kSalt)),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY, const_cast<uint8_t*>(kIkm), sizeof(kIkm)),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO, const_cast<uint8_t*>(kInfo), sizeof(kInfo)),
        OSSL_PARAM_construct_end()
    };

    bool ok = (EVP_KDF_derive(kctx, out.data(), out.size(), params) == 1);

    EVP_KDF_CTX_free(kctx);
    EVP_KDF_free(kdf);

    if (!ok) {
        err = "HKDF-SHA256 KAT failed: " + last_openssl_error();
        return false;
    }

    if (CRYPTO_memcmp(out.data(), kExpected, out.size()) != 0) {
        err = "HKDF-SHA256 KAT mismatch";
        return false;
    }
    return true;
}

bool aes256_gcm_encrypt_kat(const char* propq, std::string& err) {
    // Deterministic vector (computed against OpenSSL AES-256-GCM):
    // key: 00..1f
    // iv : 00..0b (12 bytes)
    // aad: 00..0f (16 bytes)
    // pt : 00..0f (16 bytes)
    // ct : 4703d418c1e0c41c85489d80bde47662
    // tag: 05bfa864f3eb794554c9f61fe630b676

    std::array<uint8_t, 32> key{};
    for (size_t i = 0; i < key.size(); i++) key[i] = static_cast<uint8_t>(i);
    std::array<uint8_t, 12> iv{};
    for (size_t i = 0; i < iv.size(); i++) iv[i] = static_cast<uint8_t>(i);
    std::array<uint8_t, 16> aad{};
    for (size_t i = 0; i < aad.size(); i++) aad[i] = static_cast<uint8_t>(i);
    std::array<uint8_t, 16> pt{};
    for (size_t i = 0; i < pt.size(); i++) pt[i] = static_cast<uint8_t>(i);

    static constexpr uint8_t kExpectedCt[16] = {
        0x47,0x03,0xd4,0x18,0xc1,0xe0,0xc4,0x1c,0x85,0x48,0x9d,0x80,0xbd,0xe4,0x76,0x62
    };
    static constexpr uint8_t kExpectedTag[16] = {
        0x05,0xbf,0xa8,0x64,0xf3,0xeb,0x79,0x45,0x54,0xc9,0xf6,0x1f,0xe6,0x30,0xb6,0x76
    };

    EVP_CIPHER* cipher = EVP_CIPHER_fetch(nullptr, "AES-256-GCM", propq);
    if (!cipher) {
        err = "AES-256-GCM fetch failed: " + last_openssl_error();
        return false;
    }
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        EVP_CIPHER_free(cipher);
        err = "EVP_CIPHER_CTX_new failed";
        return false;
    }

    std::array<uint8_t, 16> ct{};
    std::array<uint8_t, 16> tag{};
    int outl = 0;

    bool ok = (EVP_EncryptInit_ex(ctx, cipher, nullptr, nullptr, nullptr) == 1) &&
              (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, (int)iv.size(), nullptr) == 1) &&
              (EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), iv.data()) == 1);

    if (ok) {
        ok = (EVP_EncryptUpdate(ctx, nullptr, &outl, aad.data(), (int)aad.size()) == 1) &&
             (EVP_EncryptUpdate(ctx, ct.data(), &outl, pt.data(), (int)pt.size()) == 1);
    }
    if (ok) {
        ok = (EVP_EncryptFinal_ex(ctx, nullptr, &outl) == 1) &&
             (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, (int)tag.size(), tag.data()) == 1);
    }

    EVP_CIPHER_CTX_free(ctx);
    EVP_CIPHER_free(cipher);

    if (!ok) {
        err = "AES-256-GCM KAT failed: " + last_openssl_error();
        return false;
    }
    if (CRYPTO_memcmp(ct.data(), kExpectedCt, ct.size()) != 0) {
        err = "AES-256-GCM KAT ciphertext mismatch";
        return false;
    }
    if (CRYPTO_memcmp(tag.data(), kExpectedTag, tag.size()) != 0) {
        err = "AES-256-GCM KAT tag mismatch";
        return false;
    }
    return true;
}

} // namespace

bool RunAll(const char* propq, std::string& err) {
    // Clear any prior errors to avoid confusing diagnostics.
    ERR_clear_error();

    if (!sha256_kat(propq, err)) return false;
    if (!hmac_sha256_kat(propq, err)) return false;
    if (!hkdf_sha256_kat(propq, err)) return false;
    if (!aes256_gcm_encrypt_kat(propq, err)) return false;

    return true;
}

} // namespace hesia::fips::selftest
