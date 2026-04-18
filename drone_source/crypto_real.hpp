#ifndef CRYPTO_REAL_HPP
#define CRYPTO_REAL_HPP

#include <vector>
#include <string>
#include <cstdint>
#include <utility>
#include <stdexcept>

#if defined(HAVE_LIBOQS) && !defined(HESIA_FIPS_BUILD)
#include <oqs/oqs.h>
#endif

namespace hesia {

// Hash SHA3-512
std::vector<uint8_t> hash_data(const std::vector<uint8_t>& data);

// --- Post-quantum algorithms ---
//
// IMPORTANT (FIPS 140-3): PQC algorithms such as ML-DSA/ML-KEM are not part of
// the traditional CMVP FIPS-approved set for most validated software modules.
// To avoid accidental inclusion inside an "Approved mode" boundary, these
// APIs are automatically disabled when HESIA_FIPS_BUILD is defined.

#if !defined(HESIA_FIPS_BUILD)

class Dilithium {
public:
    static constexpr const char* ALG = "ML-DSA-87";  // ML-DSA-87 est équivalent à Dilithium5
    static std::pair<std::vector<uint8_t>, std::vector<uint8_t>> generate_keypair();
    static std::vector<uint8_t> sign(const std::vector<uint8_t>& sk, const std::vector<uint8_t>& data);
    static bool verify(const std::vector<uint8_t>& pk,
                       const std::vector<uint8_t>& data,
                       const std::vector<uint8_t>& signature);
};

class Kyber {
public:
    static constexpr const char* ALG = "ML-KEM-1024";  // ML-KEM-1024 est équivalent à Kyber1024
    static std::pair<std::vector<uint8_t>, std::vector<uint8_t>> generate_keypair();
    static std::pair<std::vector<uint8_t>, std::vector<uint8_t>> encaps(const std::vector<uint8_t>& pk);
    static std::vector<uint8_t> decaps(const std::vector<uint8_t>& sk, const std::vector<uint8_t>& ct);
};

#else

// FIPS build: hard-disable non-approved algorithms.
class Dilithium {
public:
    static constexpr const char* ALG = "ML-DSA-87";
    static std::pair<std::vector<uint8_t>, std::vector<uint8_t>> generate_keypair() {
        throw std::runtime_error("Dilithium/ML-DSA désactivé en build FIPS (HESIA_FIPS_BUILD)");
    }
    static std::vector<uint8_t> sign(const std::vector<uint8_t>&, const std::vector<uint8_t>&) {
        throw std::runtime_error("Dilithium/ML-DSA désactivé en build FIPS (HESIA_FIPS_BUILD)");
    }
    static bool verify(const std::vector<uint8_t>&, const std::vector<uint8_t>&, const std::vector<uint8_t>&) {
        throw std::runtime_error("Dilithium/ML-DSA désactivé en build FIPS (HESIA_FIPS_BUILD)");
    }
};

class Kyber {
public:
    static constexpr const char* ALG = "ML-KEM-1024";
    static std::pair<std::vector<uint8_t>, std::vector<uint8_t>> generate_keypair() {
        throw std::runtime_error("Kyber/ML-KEM désactivé en build FIPS (HESIA_FIPS_BUILD)");
    }
    static std::pair<std::vector<uint8_t>, std::vector<uint8_t>> encaps(const std::vector<uint8_t>&) {
        throw std::runtime_error("Kyber/ML-KEM désactivé en build FIPS (HESIA_FIPS_BUILD)");
    }
    static std::vector<uint8_t> decaps(const std::vector<uint8_t>&, const std::vector<uint8_t>&) {
        throw std::runtime_error("Kyber/ML-KEM désactivé en build FIPS (HESIA_FIPS_BUILD)");
    }
};

#endif

} // namespace hesia

#endif // CRYPTO_REAL_HPP

