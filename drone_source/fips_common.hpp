// fips_common.hpp
// Minimal utilities used by the FIPS-oriented wrapper layer.
//
// NOTE:
// - This code does NOT by itself grant FIPS 140-3 validation.
// - It is meant to help structure the code as a "cryptographic module" wrapper
//   that can be bound/embedded to an already validated cryptographic module
//   (e.g., OpenSSL FIPS Provider) and to enforce an "Approved mode" at runtime.

#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>

#include <openssl/crypto.h>

namespace hesia::fips {

inline void secure_zero(void* ptr, size_t len) noexcept {
    if (!ptr || len == 0) return;
    OPENSSL_cleanse(ptr, len);
    std::atomic_thread_fence(std::memory_order_seq_cst);
}

inline bool ct_equal(const void* a, const void* b, size_t len) noexcept {
    if (a == nullptr || b == nullptr) return false;
    // CRYPTO_memcmp is constant-time with respect to the contents.
    return CRYPTO_memcmp(a, b, len) == 0;
}

} // namespace hesia::fips
