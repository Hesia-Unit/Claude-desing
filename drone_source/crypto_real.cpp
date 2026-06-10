#include "crypto_real.hpp"
#include "security_utils.hpp"
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <stdexcept>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

#if defined(HAVE_LIBOQS) && !defined(HESIA_FIPS_BUILD)
#include <oqs/oqs.h>
#endif

// Note: Pour Dilithium et Kyber, on utilise liboqs

namespace hesia {

namespace {

#ifndef _WIN32
struct PageAlignedRegion {
    void* base = nullptr;
    size_t len = 0;
};

static PageAlignedRegion page_align_region(void* ptr, size_t len) {
    PageAlignedRegion region;
    if (!ptr || len == 0) {
        return region;
    }
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        return region;
    }
    const uintptr_t start = reinterpret_cast<uintptr_t>(ptr);
    const uintptr_t mask = static_cast<uintptr_t>(page_size - 1);
    const uintptr_t base = start & ~mask;
    const uintptr_t end = (start + len + mask) & ~mask;
    region.base = reinterpret_cast<void*>(base);
    region.len = static_cast<size_t>(end - base);
    return region;
}
#endif

class ScopedSecretMemoryLock {
public:
    ScopedSecretMemoryLock(const uint8_t* ptr, size_t len)
        : ptr_(const_cast<uint8_t*>(ptr)), len_(len) {
        if (!ptr_ || len_ == 0) {
            return;
        }
#ifdef _WIN32
        locked_ = (VirtualLock(ptr_, len_) != 0);
#else
        locked_ = (mlock(ptr_, len_) == 0);
#ifdef MADV_DONTDUMP
        dontdump_region_ = page_align_region(ptr_, len_);
        if (dontdump_region_.base && dontdump_region_.len != 0) {
            (void)madvise(dontdump_region_.base, dontdump_region_.len, MADV_DONTDUMP);
        }
#endif
#endif
    }

    ~ScopedSecretMemoryLock() {
        if (!locked_ || !ptr_ || len_ == 0) {
            return;
        }
#ifdef _WIN32
        VirtualUnlock(ptr_, len_);
#else
#ifdef MADV_DODUMP
        if (dontdump_region_.base && dontdump_region_.len != 0) {
            (void)madvise(dontdump_region_.base, dontdump_region_.len, MADV_DODUMP);
        }
#endif
        munlock(ptr_, len_);
#endif
    }

private:
    uint8_t* ptr_ = nullptr;
    size_t len_ = 0;
    bool locked_ = false;
#ifndef _WIN32
    PageAlignedRegion dontdump_region_{};
#endif
};

} // namespace

std::vector<uint8_t> hash_data(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> hash(SHA512_DIGEST_LENGTH);
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    
    if (!ctx) {
        throw std::runtime_error("EVP_MD_CTX_new failed");
    }
    
    // ✅ SÉCURITÉ: Utilisation systématique de SHA3-512
    const EVP_MD* md = EVP_sha3_512();
    if (EVP_DigestInit_ex(ctx, md, nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("EVP_DigestInit_ex failed");
    }
    
    if (EVP_DigestUpdate(ctx, data.data(), data.size()) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("EVP_DigestUpdate failed");
    }
    
    unsigned int hash_len;
    if (EVP_DigestFinal_ex(ctx, hash.data(), &hash_len) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("EVP_DigestFinal_ex failed");
    }
    
    EVP_MD_CTX_free(ctx);
    return hash;
}

#if !defined(HESIA_FIPS_BUILD)

// Dilithium - Implémentation avec liboqs et validations robustes
std::pair<std::vector<uint8_t>, std::vector<uint8_t>> Dilithium::generate_keypair() {
#ifdef HAVE_LIBOQS
    OQS_SIG* sig = OQS_SIG_new(Dilithium::ALG);
    if (!sig) {
        throw std::runtime_error("OQS_SIG_new failed for Dilithium");
    }
    
    // ✅ SÉCURITÉ: Validation des tailles avant allocation
    size_t public_key_len = sig->length_public_key;
    size_t secret_key_len = sig->length_secret_key;
    
    if (public_key_len == 0 || public_key_len > 10000 ||
        secret_key_len == 0 || secret_key_len > 10000) {
        OQS_SIG_free(sig);
        throw std::runtime_error("Tailles de clés Dilithium invalides");
    }
    
    std::vector<uint8_t> public_key(public_key_len);
    std::vector<uint8_t> secret_key(secret_key_len);
    (void)SecureMemory::protect(secret_key);
    
    if (OQS_SIG_keypair(sig, public_key.data(), secret_key.data()) != OQS_SUCCESS) {
        OQS_SIG_free(sig);
        throw std::runtime_error("OQS_SIG_keypair failed");
    }
    
    OQS_SIG_free(sig);
    return {public_key, secret_key};
#else
    throw std::runtime_error("Dilithium::generate_keypair requires liboqs - compile with -DHAVE_LIBOQS");
#endif
}

std::vector<uint8_t> Dilithium::sign(const std::vector<uint8_t>& sk, const std::vector<uint8_t>& data) {
#ifdef HAVE_LIBOQS
#ifndef HESIA_ALLOW_SOFT_SIGN
    (void)sk;
    (void)data;
    throw std::runtime_error("Dilithium::sign disabled in REE build; provision OP-TEE ML-DSA signing or compile with -DHESIA_ALLOW_SOFT_SIGN=ON for non-production recovery only");
#else
    // ✅ SÉCURITÉ: Validation des entrées
    if (sk.empty()) {
        throw std::invalid_argument("Clé secrète Dilithium vide");
    }
    if (data.empty()) {
        throw std::invalid_argument("Données à signer vides");
    }
    if (data.size() > 1024 * 1024) { // Max 1MB
        throw std::invalid_argument("Données à signer trop grandes");
    }
    
    OQS_SIG* sig = OQS_SIG_new(Dilithium::ALG);
    if (!sig) {
        throw std::runtime_error("OQS_SIG_new failed for Dilithium");
    }
    
    // ✅ SÉCURITÉ: Validation taille clé secrète
    if (sk.size() != sig->length_secret_key) {
        OQS_SIG_free(sig);
        throw std::invalid_argument("Taille clé secrète Dilithium invalide");
    }
    
    size_t signature_len = sig->length_signature;
    if (signature_len == 0 || signature_len > 10000) {
        OQS_SIG_free(sig);
        throw std::runtime_error("Taille signature Dilithium invalide");
    }
    
    std::vector<uint8_t> signature(signature_len);
    size_t signature_len_out;
    ScopedSecretMemoryLock secret_lock(sk.data(), sk.size());
    
    if (OQS_SIG_sign(sig, signature.data(), &signature_len_out, data.data(), data.size(), sk.data()) != OQS_SUCCESS) {
        OQS_SIG_free(sig);
        throw std::runtime_error("OQS_SIG_sign failed");
    }
    
    OQS_SIG_free(sig);
    signature.resize(signature_len_out);
    return signature;
#endif
#else
    throw std::runtime_error("Dilithium::sign requires liboqs - compile with -DHAVE_LIBOQS");
#endif
}

bool Dilithium::verify(const std::vector<uint8_t>& pk, const std::vector<uint8_t>& data, const std::vector<uint8_t>& signature) {
#ifdef HAVE_LIBOQS
    OQS_SIG* sig = OQS_SIG_new(Dilithium::ALG);
    if (!sig) {
        throw std::runtime_error("OQS_SIG_new failed for Dilithium");
    }
    
    // ✅ SÉCURITÉ: Validation stricte des tailles (P0)
    if (pk.size() != sig->length_public_key) {
        OQS_SIG_free(sig);
        throw std::invalid_argument("Taille clé publique Dilithium invalide: " + 
                                 std::to_string(pk.size()) + " != " + 
                                 std::to_string(sig->length_public_key));
    }
    
    if (signature.size() != sig->length_signature) {
        OQS_SIG_free(sig);
        throw std::invalid_argument("Taille signature Dilithium invalide: " + 
                                 std::to_string(signature.size()) + " != " + 
                                 std::to_string(sig->length_signature));
    }
    
    if (data.size() > 1024 * 1024) { // Max 1MB
        OQS_SIG_free(sig);
        throw std::invalid_argument("Données à vérifier trop grandes");
    }
    
    OQS_STATUS status = OQS_SIG_verify(sig, data.data(), data.size(), signature.data(), signature.size(), pk.data());
    OQS_SIG_free(sig);
    
    return status == OQS_SUCCESS;
#else
    throw std::runtime_error("Dilithium::verify requires liboqs - compile with -DHAVE_LIBOQS");
#endif
}

// Kyber - Implémentation avec liboqs
std::pair<std::vector<uint8_t>, std::vector<uint8_t>> Kyber::generate_keypair() {
#ifdef HAVE_LIBOQS
    OQS_KEM* kem = OQS_KEM_new(Kyber::ALG);
    if (!kem) {
        throw std::runtime_error("OQS_KEM_new failed for Kyber");
    }
    
    size_t public_key_len = kem->length_public_key;
    size_t secret_key_len = kem->length_secret_key;
    
    std::vector<uint8_t> public_key(public_key_len);
    std::vector<uint8_t> secret_key(secret_key_len);
    (void)SecureMemory::protect(secret_key);
    
    if (OQS_KEM_keypair(kem, public_key.data(), secret_key.data()) != OQS_SUCCESS) {
        OQS_KEM_free(kem);
        throw std::runtime_error("OQS_KEM_keypair failed");
    }
    
    OQS_KEM_free(kem);
    return {public_key, secret_key};
#else
    throw std::runtime_error("Kyber::generate_keypair requires liboqs - compile with -DHAVE_LIBOQS");
#endif
}

std::pair<std::vector<uint8_t>, std::vector<uint8_t>> Kyber::encaps(const std::vector<uint8_t>& pk) {
#ifdef HAVE_LIBOQS
    OQS_KEM* kem = OQS_KEM_new(Kyber::ALG);
    if (!kem) {
        throw std::runtime_error("OQS_KEM_new failed for Kyber");
    }
    
    if (pk.size() != kem->length_public_key) {
        OQS_KEM_free(kem);
        throw std::runtime_error("Invalid public key size for Kyber");
    }
    
    size_t ciphertext_len = kem->length_ciphertext;
    size_t shared_secret_len = kem->length_shared_secret;
    
    std::vector<uint8_t> ciphertext(ciphertext_len);
    std::vector<uint8_t> shared_secret(shared_secret_len);
    (void)SecureMemory::protect(shared_secret);
    
    if (OQS_KEM_encaps(kem, ciphertext.data(), shared_secret.data(), pk.data()) != OQS_SUCCESS) {
        OQS_KEM_free(kem);
        throw std::runtime_error("OQS_KEM_encaps failed");
    }
    
    OQS_KEM_free(kem);
    return {ciphertext, shared_secret};
#else
    throw std::runtime_error("Kyber::encaps requires liboqs - compile with -DHAVE_LIBOQS");
#endif
}

std::vector<uint8_t> Kyber::decaps(const std::vector<uint8_t>& sk, const std::vector<uint8_t>& ct) {
#ifdef HAVE_LIBOQS
    OQS_KEM* kem = OQS_KEM_new(Kyber::ALG);
    if (!kem) {
        throw std::runtime_error("OQS_KEM_new failed for Kyber");
    }
    
    // ✅ SÉCURITÉ: Validation stricte des tailles (P0)
    if (sk.size() != kem->length_secret_key) {
        OQS_KEM_free(kem);
        throw std::invalid_argument("Taille clé secrète Kyber invalide: " + 
                                 std::to_string(sk.size()) + " != " + 
                                 std::to_string(kem->length_secret_key));
    }
    
    if (ct.size() != kem->length_ciphertext) {
        OQS_KEM_free(kem);
        throw std::invalid_argument("Taille ciphertext Kyber invalide: " + 
                                 std::to_string(ct.size()) + " != " + 
                                 std::to_string(kem->length_ciphertext));
    }
    
    size_t shared_secret_len = kem->length_shared_secret;
    std::vector<uint8_t> shared_secret(shared_secret_len);
    (void)SecureMemory::protect(shared_secret);
    
    if (OQS_KEM_decaps(kem, shared_secret.data(), ct.data(), sk.data()) != OQS_SUCCESS) {
        OQS_KEM_free(kem);
        throw std::runtime_error("OQS_KEM_decaps failed");
    }
    
    OQS_KEM_free(kem);
    return shared_secret;
#else
    throw std::runtime_error("Kyber::decaps requires liboqs - compile with -DHAVE_LIBOQS");
#endif
}

#endif // !HESIA_FIPS_BUILD

} // namespace hesia
