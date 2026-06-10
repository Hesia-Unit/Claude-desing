#include "../../drone_source/config.hpp"
#include "../../drone_source/logger.hpp"
#include "../../drone_source/security_utils.hpp"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace hesia {

std::string Config::LOG_DIR = ".";
std::filesystem::path Config::LOG_PATH = ".";

std::atomic<bool> Logger::debug_enabled{false};
std::atomic<bool> Logger::file_output_enabled{false};

Logger::Logger(const std::string& logger_name, const fs::path&) : name(logger_name), console_enabled(false), file_enabled(false) {}
Logger::~Logger() = default;
void Logger::set_debug_enabled(bool enabled) { debug_enabled.store(enabled); }
void Logger::set_file_output_enabled(bool enabled) { file_output_enabled.store(enabled); }
void Logger::disable_file_output() { file_enabled = false; }
void Logger::debug(const std::string&) {}
void Logger::info(const std::string&) {}
void Logger::warning(const std::string&) {}
void Logger::error(const std::string&) {}
void Logger::set_level(const std::string&) {}
void Logger::write_log(const std::string&, const std::string&) {}

std::shared_ptr<Logger> setup_logger(const std::string& name, const fs::path& log_dir) {
    return std::make_shared<Logger>(name, log_dir);
}

void SecureMemory::zeroize(void* ptr, size_t len) {
    if (ptr && len) {
        OPENSSL_cleanse(ptr, len);
    }
}

void SecureMemory::wipe(std::vector<uint8_t>& data) {
    zeroize(data.data(), data.size());
}

void SecureMemory::zeroize(std::string& data) {
    zeroize(data.data(), data.size());
    data.clear();
}

void SecureMemory::zeroize(std::vector<uint8_t>& data) {
    zeroize(data.data(), data.size());
    data.clear();
}

void SecureMemory::copy(void* dst, const void* src, size_t len) {
    if (len) {
        std::memcpy(dst, src, len);
    }
}

std::vector<uint8_t> SecureMemory::secure_alloc(size_t size) {
    return std::vector<uint8_t>(size, 0);
}

void SecureMemory::secure_free(void* ptr, size_t size) {
    zeroize(ptr, size);
}

bool ConstantTime::equals(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    return equals(a.data(), b.data(), a.size());
}

bool ConstantTime::equals(const uint8_t* a, const uint8_t* b, size_t len) {
    return CRYPTO_memcmp(a, b, len) == 0;
}

uint8_t ConstantTime::select(uint8_t mask, uint8_t a, uint8_t b) {
    return static_cast<uint8_t>((mask & a) | (~mask & b));
}

std::vector<uint8_t> HKDF::extract(const std::vector<uint8_t>&, const std::vector<uint8_t>&) {
    throw std::runtime_error("HKDF::extract not implemented in unit stub");
}

std::vector<uint8_t> HKDF::expand(const std::vector<uint8_t>&, const std::vector<uint8_t>&, size_t) {
    throw std::runtime_error("HKDF::expand not implemented in unit stub");
}

std::vector<uint8_t> HKDF::derive(const std::vector<uint8_t>& salt,
                                  const std::vector<uint8_t>& ikm,
                                  const std::vector<uint8_t>& info,
                                  size_t len) {
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
    if (!ctx) {
        throw std::runtime_error("EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF) failed");
    }
    if (EVP_PKEY_derive_init(ctx) <= 0 ||
        EVP_PKEY_CTX_set_hkdf_md(ctx, EVP_sha3_512()) <= 0 ||
        EVP_PKEY_CTX_set1_hkdf_salt(ctx, salt.data(), static_cast<int>(salt.size())) <= 0 ||
        EVP_PKEY_CTX_set1_hkdf_key(ctx, ikm.data(), static_cast<int>(ikm.size())) <= 0 ||
        EVP_PKEY_CTX_add1_hkdf_info(ctx, info.data(), static_cast<int>(info.size())) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        throw std::runtime_error("HKDF setup failed in unit stub");
    }
    std::vector<uint8_t> out(len);
    size_t out_len = out.size();
    if (EVP_PKEY_derive(ctx, out.data(), &out_len) <= 0 || out_len != out.size()) {
        EVP_PKEY_CTX_free(ctx);
        throw std::runtime_error("HKDF derive failed in unit stub");
    }
    EVP_PKEY_CTX_free(ctx);
    return out;
}

bool SecureRNG::generate_bytes(std::vector<uint8_t>& output) {
    if (output.empty()) {
        return true;
    }
    return generate_bytes(output.data(), output.size());
}

bool SecureRNG::generate_bytes(uint8_t* output, size_t len) {
    return output != nullptr && (!len || RAND_bytes(output, static_cast<int>(len)) == 1);
}

bool SecureRNG::secure_generate(std::vector<uint8_t>& output) {
    return generate_bytes(output);
}

bool SecureRNG::check_entropy() {
    return true;
}

bool FaultDetection::verify_computation(const std::vector<uint8_t>&, const std::vector<uint8_t>&) {
    return true;
}

std::vector<uint8_t> FaultDetection::redundant_encrypt(const std::vector<uint8_t>&,
                                                       const std::vector<uint8_t>&,
                                                       const std::vector<uint8_t>&) {
    return {};
}

bool MemoryMonitor::check_gpu_memory_leak() { return false; }
void MemoryMonitor::secure_gpu_cleanup() {}
void ColdBootProtection::secure_startup() {}
void ColdBootProtection::secure_shutdown() {}
void ColdBootProtection::encrypt_sensitive_memory() {}
void RuntimeProtection::monitor_system_integrity() {}
void RuntimeProtection::randomize_execution() {}
void RuntimeProtection::add_dummy_operations() {}
void RuntimeProtection::shuffle_memory_access(void*, size_t) {}
bool RuntimeProtection::should_rotate_key(uint64_t) { return false; }
std::vector<uint8_t> RuntimeProtection::derive_child_key(const std::vector<uint8_t>&, const std::string&) { return {}; }
void RuntimeProtection::secure_cleanup_old_key(std::vector<uint8_t>&) {}
void CacheProtection::flush_cache_lines() {}
void CacheProtection::isolate_cache_access() {}
bool CacheProtection::detect_cache_timing() { return false; }
void CacheProtection::enable_cache_timing_protection() {}
void CacheProtection::enable_aes_ni_optimization() {}
bool CacheProtection::is_aes_ni_available() { return false; }
void PowerMasking::add_consumption_noise() {}
void PowerMasking::randomize_execution_timing() {}
void PowerMasking::balance_power_consumption() {}
void PowerMasking::enable_power_masking() {}
void PowerMasking::apply_algorithmic_blinding() {}
bool PowerMasking::detect_power_analysis() { return false; }

} // namespace hesia
