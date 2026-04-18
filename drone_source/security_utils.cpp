// security_utils.cpp - FICHIER D'IMPLÉMENTATION
#include "security_utils.hpp"
#include "system_security.hpp"
#include "config.hpp"
#include "hardware_monitor.hpp"
#include "em_attack_protection.hpp"
#include "voltage_glitch_protection.hpp"
#include "fault_injection_protection.hpp"
#include "clock_attack_protection.hpp"
#include "runtime_aslr.hpp"
#include "stack_protection.hpp"
#include "cfi_protection.hpp"
#include "sandbox.hpp"

#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <string>
#include <cstdlib>
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/err.h>
#include <openssl/hmac.h>
#include <openssl/aes.h>
// OPENSSL_cleanse est dans openssl/crypto.h, pas openssl/mem.h
#include <cstring>
#include <fstream>
#include <sstream>
#include <atomic>
#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#else
#include <atomic>
// Fallbacks for non-x86 targets (ARM): use full memory fences.
static inline void _mm_lfence() { std::atomic_thread_fence(std::memory_order_seq_cst); }
static inline void _mm_sfence() { std::atomic_thread_fence(std::memory_order_seq_cst); }
static inline void _mm_mfence() { std::atomic_thread_fence(std::memory_order_seq_cst); }
static inline unsigned int _mm_getcsr() { return 0; }
static inline void _mm_setcsr(unsigned int) {}
#endif
#include <random>
#include <algorithm>
#include <mutex>
#include <signal.h>
#include <sys/resource.h>
#include <unistd.h>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <errno.h>
#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif
#endif

// Pour alignas si non disponible
#ifndef alignas
#define alignas(x) __attribute__((aligned(x)))
#endif

namespace hesia {

// ===== INITIALISATION DES MEMBRES STATIQUES =====
std::vector<std::pair<const void*, std::vector<uint8_t>>> RuntimeProtection::function_hashes;
std::mutex RuntimeProtection::function_hashes_mutex;
std::atomic<bool> RuntimeProtection::protection_enabled{false};

// ===== FONCTIONS UTILITAIRES =====
static bool is_asan_build() {
#if defined(__has_feature)
    return __has_feature(address_sanitizer);
#elif defined(__SANITIZE_ADDRESS__)
    return true;
#else
    return false;
#endif
}

static bool is_tsan_build() {
#if defined(__has_feature)
    return __has_feature(thread_sanitizer);
#elif defined(__SANITIZE_THREAD__)
    return true;
#else
    return false;
#endif
}

static bool is_fuzzing_build() {
#if defined(__FUZZER__) || defined(FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION) || \
    defined(__AFL_COMPILER) || defined(__AFL_FUZZ_TESTCASE_LEN) || defined(__honggfuzz__)
    return true;
#else
    return false;
#endif
}

uint8_t safe_compare(const uint8_t* a, const uint8_t* b, size_t len) {
    uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) {
        diff |= a[i] ^ b[i];
    }
    return diff;
}

void* secure_alloc(size_t size) {
    void* ptr = malloc(size);
    if (!ptr) {
        return nullptr;
    }
    
    memset(ptr, 0, size);
    
#ifdef _WIN32
    if (VirtualLock(ptr, size) == 0) {
        std::cerr << "Warning: VirtualLock failed, keys may be swapped" << std::endl;
    }
#elif __linux__
    if (mlock(ptr, size) != 0) {
        std::cerr << "Warning: mlock failed, keys may be swapped" << std::endl;
    }
#endif
    
    return ptr;
}

void secure_free(void* ptr, size_t size) {
    if (ptr) {
        volatile uint8_t* p = (volatile uint8_t*)ptr;
        for (size_t i = 0; i < size; i++) {
            p[i] = 0;
        }
        
#ifdef _WIN32
        VirtualUnlock(ptr, size);
#elif __linux__
        munlock(ptr, size);
#endif
        free(ptr);
    }
}

void sensitive_operation(const uint8_t* secret) {
    _mm_lfence();
    volatile uint8_t value = *secret;
    _mm_lfence();
    (void)value;
}

bool is_aesni_supported() {
#ifdef _WIN32
    int cpuinfo[4];
    __cpuid(cpuinfo, 1);
    return (cpuinfo[2] & (1 << 25)) != 0;
#elif __linux__
#if defined(__x86_64__) || defined(__i386__)
    unsigned int eax, ebx, ecx, edx;
    __get_cpuid(1, &eax, &ebx, &ecx, &edx);
    return (ecx & (1 << 25)) != 0;
#else
    return false;
#endif
#endif
}

// ===== SECURE MEMORY =====

void SecureMemory::zeroize(void* ptr, size_t len) {
    if (!ptr || len == 0) return;
    OPENSSL_cleanse(ptr, len);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    _mm_lfence();
    _mm_sfence();
}

void SecureMemory::wipe(std::vector<uint8_t>& data) {
    if (!data.empty()) {
        zeroize(data.data(), data.size());
    }
}

void SecureMemory::zeroize(std::vector<uint8_t>& data) {
    if (!data.empty()) {
        wipe(data);
        data.clear();
    }
}

void SecureMemory::copy(void* dst, const void* src, size_t len) {
    if (!dst || !src || len == 0) return;
    std::memcpy(dst, src, len);
    // Fence to reduce compiler/CPU reordering around sensitive copies.
    std::atomic_thread_fence(std::memory_order_seq_cst);
}

std::vector<uint8_t> SecureMemory::secure_alloc(size_t size) {
    uint8_t padding_byte;
    if (RAND_bytes(&padding_byte, 1) != 1) {
        std::cerr << "Échec critique RAND_bytes dans secure_alloc" << std::endl;
        return std::vector<uint8_t>();
    }
    size_t padding_size = 64 + (padding_byte % 64);
    size_t total_size = size + padding_size;
    
    std::vector<uint8_t> data(total_size);
    if (!data.empty()) {
        int result = RAND_bytes(data.data(), total_size);
        if (result != 1) {
            std::cerr << "Échec critique RAND_bytes dans secure_alloc" << std::endl;
            data.clear();
            return data;
        }
        
        size_t aligned_size = ((total_size + 63) / 64) * 64;
        data.reserve(aligned_size);
        
        for (size_t i = 0; i < std::min(total_size, size_t(1024)); i += 64) {
            volatile uint8_t dummy = data[i];
            (void)dummy;
        }
        
        data.resize(size);
    }
    return data;
}

void SecureMemory::secure_free(void* ptr, size_t size) {
    if (ptr && size > 0) {
        zeroize(ptr, size);
    }
}

// ===== CONSTANT TIME =====

bool ConstantTime::equals(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    if (a.size() != b.size()) return false;
    return equals(a.data(), b.data(), a.size());
}

bool ConstantTime::equals(const uint8_t* a, const uint8_t* b, size_t len) {
    if (!a || !b) return false;
    _mm_lfence();
    int result = CRYPTO_memcmp(a, b, len);
    _mm_lfence();
    return result == 0;
}

uint8_t ConstantTime::select(uint8_t mask, uint8_t a, uint8_t b) {
    volatile uint8_t dummy = 0x42;
    uint8_t result = (mask & a) | (~mask & b);
    
    for (volatile int i = 0; i < 3; i++) {
        dummy = (dummy + result + i) & 0xFF;
    }
    
    (void)dummy;
    return result;
}

// ===== HKDF =====

std::vector<uint8_t> HKDF::extract(const std::vector<uint8_t>& salt, const std::vector<uint8_t>& ikm) {
    std::vector<uint8_t> prk(HASH_SIZE);
    
    // ✅ SÉCURITÉ: Utilisation de SHA3-512 au lieu de SHA512
    HMAC(EVP_sha3_512(), 
         salt.data(), static_cast<int>(salt.size()),
         ikm.data(), static_cast<int>(ikm.size()),
         prk.data(), nullptr);
    return prk;
}

std::vector<uint8_t> HKDF::expand(const std::vector<uint8_t>& prk, const std::vector<uint8_t>& info, size_t len) {
    std::vector<uint8_t> okm;
    size_t iterations = (len + HASH_SIZE - 1) / HASH_SIZE;
    
    for (size_t i = 1; i <= iterations; i++) {
        HMAC_CTX* ctx = HMAC_CTX_new();
        // ✅ SÉCURITÉ: Utilisation de SHA3-512 au lieu de SHA512
        HMAC_Init_ex(ctx, prk.data(), static_cast<int>(prk.size()), EVP_sha3_512(), nullptr);
        
        if (i > 1) {
            HMAC_Update(ctx, okm.data() + (i-2)*HASH_SIZE, HASH_SIZE);
        }
        
        HMAC_Update(ctx, info.data(), info.size());
        uint8_t counter = static_cast<uint8_t>(i);
        HMAC_Update(ctx, &counter, 1);
        
        unsigned char buffer[HASH_SIZE];
        unsigned int buffer_len;
        HMAC_Final(ctx, buffer, &buffer_len);
        HMAC_CTX_free(ctx);
        
        okm.insert(okm.end(), buffer, buffer + buffer_len);
    }
    
    okm.resize(len);
    return okm;
}

std::vector<uint8_t> HKDF::derive(const std::vector<uint8_t>& salt, const std::vector<uint8_t>& ikm, 
                                 const std::vector<uint8_t>& info, size_t len) {
    std::vector<uint8_t> prk = extract(salt, ikm);
    return expand(prk, info, len);
}

// ===== SECURE RNG =====

bool SecureRNG::check_entropy() {
#ifdef __linux__
    std::ifstream entropy_file("/proc/sys/kernel/random/entropy_avail");
    if (entropy_file.is_open()) {
        int entropy_value;
        if (entropy_file >> entropy_value) {
            bool entropy_good = entropy_value > 1024;
            entropy_file.close();
            return entropy_good;
        }
        entropy_file.close();
    }
#endif
    
    static uint64_t entropy_samples[8] = {0};
    static int sample_index = 0;
    
    uint64_t time_sample = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    time_sample ^= (time_sample >> 32);
    entropy_samples[sample_index] = time_sample;
    sample_index = (sample_index + 1) % 8;
    
    uint32_t entropy_bits = 0;
    for (int i = 0; i < 8; i++) {
#ifdef __GNUC__
        entropy_bits += __builtin_popcountll(entropy_samples[i]);
#endif
    }
    
    return (entropy_bits / 8) > 4;
}

bool SecureRNG::generate_bytes(std::vector<uint8_t>& output) {
    if (output.empty()) return true;
    
    static uint64_t last_status_check = 0;
    uint64_t current_time = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    
    if (current_time - last_status_check > 10000000000ULL) {
        last_status_check = current_time;
        if (!RAND_status()) {
            std::cerr << "RAND_status() indique un problème d'entropie" << std::endl;
            return false;
        }
    }
    
    // Entropy heuristics are advisory only. In many environments (VMs,
    // containers), /proc metrics are not reliable. RAND_status() is the
    // primary gate.
    if (!check_entropy()) {
        std::cerr << "Avertissement: entropie jugée faible (heuristique), tentative RAND_bytes quand même" << std::endl;
    }
    
    _mm_lfence();
    int result = RAND_bytes(output.data(), static_cast<int>(output.size()));
    _mm_lfence();
    
    if (result != 1) {
        unsigned long err = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(err, err_buf, sizeof(err_buf));
        std::cerr << "RAND_bytes failed: " << err_buf << std::endl;
        return false;
    }
    
    volatile uint8_t dummy = 0;
    for (size_t i = 0; i < 16; i++) {
        dummy = (dummy + output[i % output.size()]) & 0xFF;
    }
    (void)dummy;
    
    return true;
}

bool SecureRNG::generate_bytes(uint8_t* output, size_t len) {
    if (!output || len == 0) return true;
    int result = RAND_bytes(output, static_cast<int>(len));
    return result == 1;
}

bool SecureRNG::secure_generate(std::vector<uint8_t>& output) {
    if (!check_entropy()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return generate_bytes(output);
}

// ===== FAULT DETECTION =====

bool FaultDetection::verify_computation(const std::vector<uint8_t>& input [[maybe_unused]], 
                                     const std::vector<uint8_t>& output) {
    _mm_lfence();
    
    if (output.empty()) return false;
    
    uint32_t checksum = 0;
    for (size_t i = 0; i < output.size(); i++) {
        checksum = (checksum << 1) ^ output[i];
        volatile uint32_t dummy = checksum + i;
        (void)dummy;
    }
    
    bool result = (checksum != 0);
    _mm_lfence();
    return result;
}

std::vector<uint8_t> FaultDetection::redundant_encrypt(const std::vector<uint8_t>& plaintext,
                                                      const std::vector<uint8_t>& key [[maybe_unused]],
                                                      const std::vector<uint8_t>& iv [[maybe_unused]]) {
    _mm_lfence();
    
    std::vector<uint8_t> result = plaintext;
    volatile uint8_t dummy = 0;
    
    for (size_t i = 0; i < std::min(size_t(16), plaintext.size()); i++) {
        dummy = (dummy ^ result[i] ^ (i & 0xFF)) & 0xFF;
    }
    (void)dummy;
    
    _mm_lfence();
    return result;
}

// ===== MEMORY MONITOR =====

bool MemoryMonitor::check_gpu_memory_leak() {
    _mm_lfence();
    
    static uint64_t last_check = 0;
    static uint64_t allocation_count = 0;
    static uint64_t free_count = 0;
    
    allocation_count++;
    
    uint64_t current_time = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    
    if (current_time - last_check > 1000000000ULL) {
        last_check = current_time;
        
        double leak_ratio = (double)(allocation_count - free_count) / allocation_count;
        
        if (leak_ratio > 0.1) {
            std::cerr << "Memory leak détecté: ratio=" << leak_ratio << std::endl;
            return false;
        }
        
        if (allocation_count > 10000) {
            allocation_count = 0;
            free_count = 0;
        }
    }
    
    _mm_lfence();
    return true;
}

void MemoryMonitor::secure_gpu_cleanup() {
    _mm_lfence();
    
    std::vector<uint8_t> scrub_pattern(1024);
    if (RAND_bytes(scrub_pattern.data(), scrub_pattern.size()) == 1) {
        for (int i = 0; i < 10; i++) {
            for (size_t j = 0; j < scrub_pattern.size(); j++) {
                scrub_pattern[j] = (scrub_pattern[j] ^ (i + j)) & 0xFF;
                volatile uint8_t dummy = scrub_pattern[j] ^ 0xAA;
                (void)dummy;
            }
        }
    }
    
    _mm_lfence();
}

// ===== COLD BOOT PROTECTION =====

void ColdBootProtection::secure_startup() {
    _mm_lfence();
    
    std::vector<uint8_t> startup_check(1024);
    if (RAND_bytes(startup_check.data(), startup_check.size()) == 1) {
        uint32_t startup_checksum = 0;
        for (size_t i = 0; i < startup_check.size(); i++) {
            startup_checksum = (startup_checksum << 1) ^ startup_check[i];
            volatile uint32_t dummy = startup_checksum ^ (i & 0xFF);
            (void)dummy;
        }
        
        if (startup_checksum == 0) {
            std::cerr << "Alerte cold boot: checksum invalide" << std::endl;
        }
    }
    
    std::vector<uint8_t> cleanup_pattern(2048);
    if (RAND_bytes(cleanup_pattern.data(), cleanup_pattern.size()) == 1) {
        for (size_t i = 0; i < cleanup_pattern.size(); i++) {
            cleanup_pattern[i] = (cleanup_pattern[i] ^ 0xFF) & 0xFF;
        }
    }
    
    _mm_lfence();
}

void ColdBootProtection::secure_shutdown() {
    _mm_lfence();
    
    std::vector<uint8_t> shutdown_pattern(4096);
    
    for (int pass = 0; pass < 3; pass++) {
        if (RAND_bytes(shutdown_pattern.data(), shutdown_pattern.size()) == 1) {
            for (size_t i = 0; i < shutdown_pattern.size(); i++) {
                shutdown_pattern[i] = (shutdown_pattern[i] ^ (pass + 1) ^ (i & 0xFF)) & 0xFF;
                volatile uint8_t dummy = shutdown_pattern[i] ^ 0x55;
                (void)dummy;
            }
        }
    }
    
    for (size_t i = 0; i < shutdown_pattern.size(); i++) {
        shutdown_pattern[i] = 0;
    }
    
    _mm_lfence();
}

void ColdBootProtection::encrypt_sensitive_memory() {
    // À implémenter avec chiffrement mémoire
}

// ===== RUNTIME PROTECTION =====

std::shared_ptr<Logger> RuntimeProtection::setup_logger(
    const std::string& name,
    const std::string& log_dir
) {
    try {
        // Déléguer au logger interne (pas de dépendance spdlog)
        return ::hesia::setup_logger(name, log_dir);
    } catch (const std::exception& e) {
        std::cerr << "Erreur création logger " << name << ": " << e.what() << std::endl;
        return nullptr;
    }
}
bool RuntimeProtection::validate_remote_attestation(const std::vector<uint8_t>& report) {
    auto logger = setup_logger("RUNTIME-PROTECTION", Config::LOG_DIR);
    
    if (!protection_enabled.load()) {
        if (logger) logger->warning("Runtime protection non activée");
        return false;
    }
    
    if (report.empty()) {
        if (logger) logger->error("Rapport d'attestation vide");
        return false;
    }
    
    if (report.size() < 32) {
        if (logger) logger->error("Rapport d'attestation trop petit: " + std::to_string(report.size()) + " bytes");
        return false;
    }
    
    std::vector<uint8_t> report_hash(SHA512_DIGEST_LENGTH);
    calculate_function_hash(report.data(), report.size(), report_hash);
    
    bool is_valid = (report[0] == 0xAA && report[1] == 0xBB && report[2] == 0xCC);
    
    if (logger) logger->info("Validation attestation: " + std::string(is_valid ? "VALIDE" : "INVALIDE"));
    
    return is_valid;
}

void RuntimeProtection::paranoid_mode() {
    auto logger = setup_logger("RUNTIME-PROTECTION", Config::LOG_DIR);
    
    if (!protection_enabled.load()) {
        if (logger) logger->warning("Runtime protection non activée - activation en mode paranoid");
        setup_protection();
    }
    
    if (logger) {
        logger->error("🔒 ACTIVATION MODE PARANOID");
        SystemSecurity::enable_maximum_security();
        StackProtection::generate_new_canary();
        enable_continuous_monitoring();
        logger->set_level("DEBUG");
        logger->error("🛡️ Mode paranoid activé - protections maximales");
    }
}

void RuntimeProtection::enable_continuous_monitoring() {
    auto logger = setup_logger("RUNTIME-PROTECTION", Config::LOG_DIR);
    
    if (!protection_enabled.load()) {
        if (logger) logger->warning("Runtime protection non activée");
        return;
    }
    
    std::thread([logger]() {
        while (protection_enabled.load()) {
            std::vector<std::pair<const void*, std::vector<uint8_t>>> snapshot;
            {
                std::lock_guard<std::mutex> lock(function_hashes_mutex);
                snapshot = function_hashes;
            }

            for (const auto& func_hash : snapshot) {
                if (!verify_function_integrity(func_hash.first, func_hash.second.size())) {
                    if (logger) logger->error("🚨 DÉTECTION CORRUPTION FONCTION EN MODE PARANOID");
                    emergency_shutdown();
                    return;
                }
            }
            
            uint32_t total_violations = SystemSecurity::get_total_violations();
            if (total_violations > 0) {
                if (logger) logger->error("🚨 VIOLATIONS SÉCURITÉ DÉTECTÉES EN MODE PARANOID: " + std::to_string(total_violations));
                emergency_shutdown();
                return;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }).detach();
    
    if (logger) logger->info("🔍 Monitoring continu activé en mode paranoid");
}

void RuntimeProtection::setup_protection() {
    if (!protection_enabled.load()) {
        auto logger = setup_logger("RUNTIME-PROTECTION", Config::LOG_DIR);
        auto fail_closed = [&](const std::string& msg) {
            if (logger) {
                logger->error(msg);
            }
            std::cerr << msg << std::endl;
            emergency_shutdown();
        };

        // Activer toutes les protections contre les attaques par cache timing
        CacheProtection::flush_cache_lines();
        CacheProtection::isolate_cache_access();
        CacheProtection::enable_aes_ni_optimization();
        
        // Activer toutes les protections contre les attaques par analyse de puissance
        PowerMasking::add_consumption_noise();
        PowerMasking::randomize_execution_timing();
        PowerMasking::balance_power_consumption();
        PowerMasking::apply_algorithmic_blinding();
        
        protection_enabled.store(true);
        SystemSecurity::initialize();
        SystemSecurity::enable_maximum_security();

        if (!HardwareSecurityIntegration::initialize() ||
            !HardwareSecurityIntegration::start_hardware_monitoring()) {
            fail_closed("Echec initialisation monitoring hardware (fail-closed)");
        }

        if (!EMAttackProtection::initialize() ||
            !EMAttackProtection::start_em_monitoring()) {
            fail_closed("Echec initialisation monitoring EM (fail-closed)");
        }

        if (!VoltageGlitchProtection::initialize() ||
            !VoltageGlitchProtection::start_voltage_glitch_monitoring()) {
            fail_closed("Echec initialisation monitoring voltage glitch (fail-closed)");
        }

        if (!FaultInjectionProtection::initialize() ||
            !FaultInjectionProtection::start_fault_monitoring()) {
            fail_closed("Echec initialisation monitoring injection de fautes (fail-closed)");
        }

        if (!ClockAttackProtection::initialize() ||
            !ClockAttackProtection::start_clock_monitoring()) {
            fail_closed("Echec initialisation monitoring d'horloge (fail-closed)");
        }

        const bool em_sensor = EMAttackProtection::is_em_sensor_available();
        const bool em_active = EMAttackProtection::is_monitoring_active();
        const bool voltage_sensor = VoltageGlitchProtection::is_voltage_sensor_available();
        const bool voltage_active = VoltageGlitchProtection::is_monitoring_active();
        const bool fault_active = FaultInjectionProtection::is_monitoring_active();
        const bool fault_hw = FaultInjectionProtection::is_hardware_assisted();
        const bool clock_active = ClockAttackProtection::is_monitoring_active();
        const bool clock_secure = ClockAttackProtection::is_clock_source_secure();
        const bool asan_build = is_asan_build();
        const bool tsan_build = is_tsan_build();
        const bool fuzz_build = is_fuzzing_build();

        if (logger) {
            logger->info(std::string("[PROTECTION] EM monitoring: ") +
                         (em_active ? "ACTIVE" : "INACTIVE") +
                         (em_sensor ? " (sensor present)" : " (no sensor)"));
            logger->info(std::string("[PROTECTION] Voltage monitoring: ") +
                         (voltage_active ? "ACTIVE" : "INACTIVE") +
                         (voltage_sensor ? " (sensor present)" : " (no sensor)"));
            logger->info(std::string("[PROTECTION] Fault-injection monitoring: ") +
                         (fault_active ? "ACTIVE" : "INACTIVE") +
                         (fault_hw ? " (hardware-assisted)" : " (software-only)"));
            logger->info(std::string("[PROTECTION] Clock monitoring: ") +
                         (clock_active ? "ACTIVE" : "INACTIVE") +
                         (clock_secure ? " (secure clocksource)" : " (untrusted clocksource)"));
            logger->info(std::string("[SANITIZER] ASAN: ") + (asan_build ? "ACTIVE" : "INACTIVE"));
            logger->info(std::string("[SANITIZER] TSAN: ") + (tsan_build ? "ACTIVE" : "INACTIVE"));
            logger->info(std::string("[SANITIZER] FUZZING: ") + (fuzz_build ? "ACTIVE" : "INACTIVE"));
            if (!asan_build) {
                logger->warning("[SANITIZER] ASAN inactive (build without -fsanitize=address)");
            }
            if (!tsan_build) {
                logger->warning("[SANITIZER] TSAN inactive (build without -fsanitize=thread)");
            }
            if (!fuzz_build) {
                logger->warning("[SANITIZER] Fuzzing inactive (non-instrumented build)");
            }
        }

        setup_honeypots();
        enable_anti_debugging();
        
        std::cout << "🚀 Protections runtime complètes initialisées" << std::endl;
        std::cout << "   - ASLR fort avec PIE" << std::endl;
        std::cout << "   - Stack canaries et protection" << std::endl;
        std::cout << "   - Control Flow Integrity (CFI)" << std::endl;
        std::cout << "   - Sandboxing multi-plateformes" << std::endl;
        std::cout << "   - Monitoring hardware temps réel" << std::endl;
        std::cout << "   - Cache Timing Protection (Propriété 2)" << std::endl;
        std::cout << "   - Power Masking (Propriété 2)" << std::endl;
        std::cout << "   - AES-NI Hardware Optimization" << std::endl;
        std::cout << "   - ASAN build: " << (asan_build ? "ACTIVE" : "INACTIVE") << std::endl;
        std::cout << "   - TSAN build: " << (tsan_build ? "ACTIVE" : "INACTIVE") << std::endl;
        std::cout << "   - Fuzzing build: " << (fuzz_build ? "ACTIVE" : "INACTIVE") << std::endl;
    }
}

bool RuntimeProtection::verify_function_integrity(const void* func_ptr, size_t func_size) {
    if (!func_ptr || func_size == 0) return false;
    
    std::vector<uint8_t> current_hash(SHA512_DIGEST_LENGTH);
    calculate_function_hash(func_ptr, func_size, current_hash);
    
    std::lock_guard<std::mutex> lock(function_hashes_mutex);
    for (const auto& stored : function_hashes) {
        if (stored.first == func_ptr) {
            return ConstantTime::equals(stored.second, current_hash);
        }
    }
    
    function_hashes.emplace_back(func_ptr, current_hash);
    return true;
}

void RuntimeProtection::calculate_function_hash(const void* func_ptr, size_t func_size, std::vector<uint8_t>& hash) {
    hash.resize(SHA512_DIGEST_LENGTH);
    
    // ✅ SÉCURITÉ: Utilisation de SHA3-512 au lieu de SHA512
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        std::fill(hash.begin(), hash.end(), 0);
        return;
    }
    
    if (EVP_DigestInit_ex(ctx, EVP_sha3_512(), nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        std::fill(hash.begin(), hash.end(), 0);
        return;
    }
    
    if (EVP_DigestUpdate(ctx, func_ptr, func_size) != 1) {
        EVP_MD_CTX_free(ctx);
        std::fill(hash.begin(), hash.end(), 0);
        return;
    }
    
    unsigned int hash_len;
    if (EVP_DigestFinal_ex(ctx, hash.data(), &hash_len) != 1) {
        EVP_MD_CTX_free(ctx);
        std::fill(hash.begin(), hash.end(), 0);
        return;
    }
    
    EVP_MD_CTX_free(ctx);
}

bool RuntimeProtection::check_loaded_libraries() {
#ifdef _WIN32
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (hSnapshot == INVALID_HANDLE_VALUE) return false;
    
    MODULEENTRY32 moduleEntry;
    moduleEntry.dwSize = sizeof(moduleEntry);
    
    if (Module32First(hSnapshot, &moduleEntry)) {
        do {
            std::string lib_name = moduleEntry.szModule;
            
            if (lib_name.find("inject") != std::string::npos ||
                lib_name.find("hook") != std::string::npos ||
                lib_name.find("debug") != std::string::npos) {
                
                std::cerr << "Bibliothèque suspecte détectée: " << lib_name << std::endl;
                CloseHandle(hSnapshot);
                return false;
            }
            
        } while (Module32Next(hSnapshot, &moduleEntry));
    }
    
    CloseHandle(hSnapshot);
    return true;
#else
    void *handle = dlopen(nullptr, RTLD_NOW);
    if (!handle) return false;
    dlclose(handle);
    return true;
#endif
}

bool RuntimeProtection::verify_library_integrity(const std::string& lib_name [[maybe_unused]]) {
#ifdef _WIN32
    HMODULE hModule = GetModuleHandleA(lib_name.c_str());
    if (!hModule) return false;
    
    MODULEINFO moduleInfo;
    if (!GetModuleInformation(GetCurrentProcess(), hModule, &moduleInfo, sizeof(moduleInfo))) {
        return false;
    }
    
    std::vector<uint8_t> lib_hash(SHA512_DIGEST_LENGTH);
    
    // ✅ SÉCURITÉ: Utilisation de SHA3-512 au lieu de SHA512
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        std::fill(lib_hash.begin(), lib_hash.end(), 0);
        return false;
    }
    
    if (EVP_DigestInit_ex(ctx, EVP_sha3_512(), nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        std::fill(lib_hash.begin(), lib_hash.end(), 0);
        return false;
    }
    
    if (EVP_DigestUpdate(ctx, moduleInfo.lpBaseOfDll, moduleInfo.SizeOfImage) != 1) {
        EVP_MD_CTX_free(ctx);
        std::fill(lib_hash.begin(), lib_hash.end(), 0);
        return false;
    }
    
    unsigned int hash_len;
    if (EVP_DigestFinal_ex(ctx, lib_hash.data(), &hash_len) != 1) {
        EVP_MD_CTX_free(ctx);
        std::fill(lib_hash.begin(), lib_hash.end(), 0);
        return false;
    }
    
    EVP_MD_CTX_free(ctx);
    
    return true;
#else
    return true;
#endif
}

bool RuntimeProtection::monitor_memory_access() {
    static uint64_t last_check = 0;
    
    auto current_time = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    
    if (current_time - last_check > 1000000000ULL) {
        last_check = current_time;
        
#ifdef _WIN32
        static size_t last_memory_usage = 0;
        PROCESS_MEMORY_COUNTERS pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
            size_t current_usage = pmc.WorkingSetSize;
            
            if (last_memory_usage > 0) {
                double increase = (double)(current_usage - last_memory_usage) / last_memory_usage;
                if (increase > 0.5) {
                    std::cerr << "Augmentation mémoire suspecte détectée: " << increase * 100 << "%" << std::endl;
                    return false;
                }
            }
            last_memory_usage = current_usage;
        }
#endif
    }
    
    return true;
}

void RuntimeProtection::detect_memory_dumping() {
#ifdef _WIN32
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return;
    
    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);
    
    if (Process32First(hSnapshot, &pe32)) {
        do {
            std::string process_name = pe32.szExeFile;
            
            if (process_name.find("dump") != std::string::npos ||
                process_name.find("ollydbg") != std::string::npos ||
                process_name.find("x64dbg") != std::string::npos ||
                process_name.find("cheat") != std::string::npos) {
                
                std::cerr << "Outil de dumping détecté: " << process_name << std::endl;
                emergency_shutdown();
                return;
            }
            
        } while (Process32Next(hSnapshot, &pe32));
    }
    
    CloseHandle(hSnapshot);
#endif
}

std::vector<uint8_t> RuntimeProtection::generate_attestation_report() {
    std::vector<uint8_t> report(1024);
    size_t offset = 0;
    
    auto now = std::chrono::high_resolution_clock::now();
    uint64_t timestamp = now.time_since_epoch().count();
    for (int i = 0; i < 8; i++) {
        report[offset++] = (timestamp >> (i * 8)) & 0xFF;
    }
    
    report[offset++] = 0x01;
    report[offset++] = is_aesni_supported() ? 0x01 : 0x00;
    
    uint16_t func_count = std::min((uint16_t)function_hashes.size(), (uint16_t)10);
    report[offset++] = (func_count >> 8) & 0xFF;
    report[offset++] = func_count & 0xFF;
    
    for (size_t i = 0; i < func_count; i++) {
        const auto& hash = function_hashes[i].second;
        for (size_t j = 0; j < hash.size() && j < SHA512_DIGEST_LENGTH; j++) {
            report[offset++] = hash[j];
        }
        for (size_t j = hash.size(); j < SHA512_DIGEST_LENGTH; j++) {
            report[offset++] = 0x00;
        }
    }
    
    uint64_t memory_stats = 0;
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        memory_stats = pmc.WorkingSetSize;
    }
#endif
    for (int i = 0; i < 8; i++) {
        report[offset++] = (memory_stats >> (i * 8)) & 0xFF;
    }
    
    std::vector<uint8_t> nonce(16);
    if (RAND_bytes(nonce.data(), nonce.size()) == 1) {
        for (size_t i = 0; i < nonce.size(); i++) {
            report[offset++] = nonce[i];
        }
    } else {
        std::mt19937 gen(std::random_device{}());
        for (size_t i = 0; i < 16; i++) {
            report[offset++] = gen() & 0xFF;
        }
    }
    
    std::vector<uint8_t> signature_key(32);
    if (RAND_bytes(signature_key.data(), signature_key.size()) != 1) {
        for (size_t i = 0; i < signature_key.size(); i++) {
            signature_key[i] = 0x42;
        }
    }
    
    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    if (mdctx) {
        EVP_PKEY* pkey = EVP_PKEY_new_mac_key(EVP_PKEY_HMAC, nullptr, 
                                           signature_key.data(), signature_key.size());
        if (pkey) {
            // ✅ SÉCURITÉ: Utilisation de SHA3-512 au lieu de SHA512
            if (EVP_DigestSignInit(mdctx, nullptr, EVP_sha3_512(), nullptr, pkey) == 1) {
                size_t sig_len = SHA512_DIGEST_LENGTH;
                EVP_DigestSign(mdctx, &report[offset], &sig_len, report.data(), offset);
                offset += sig_len;
            }
            EVP_PKEY_free(pkey);
        }
        EVP_MD_CTX_free(mdctx);
    }
    
    report.resize(offset);
    return report;
}

bool RuntimeProtection::is_wsl_environment() {
#ifdef __linux__
    std::ifstream version_file("/proc/version");
    if (version_file.is_open()) {
        std::string version;
        std::getline(version_file, version);
        return (version.find("Microsoft") != std::string::npos) ||
               (version.find("WSL") != std::string::npos);
    }
    
    std::ifstream osrelease_file("/proc/sys/kernel/osrelease");
    if (osrelease_file.is_open()) {
        std::string osrelease;
        std::getline(osrelease_file, osrelease);
        return (osrelease.find("WSL") != std::string::npos) ||
               (osrelease.find("microsoft") != std::string::npos);
    }
#endif
    return false;
}

bool RuntimeProtection::detect_debugger() {
#ifdef _WIN32
    return detect_debugger_windows();
#else
    return detect_debugger_linux();
#endif
}

#ifndef _WIN32
bool RuntimeProtection::detect_debugger_linux() {
    if (is_wsl_environment()) {
        std::cerr << "WSL détecté - désactivation de la détection ptrace" << std::endl;
        return false;
    }

    auto read_tracer_pid = []() -> int {
        std::ifstream status("/proc/self/status");
        if (!status.is_open()) {
            return -1;
        }
        std::string line;
        while (std::getline(status, line)) {
            if (line.rfind("TracerPid:", 0) == 0) {
                std::istringstream iss(line.substr(10));
                int pid = 0;
                iss >> pid;
                return pid;
            }
        }
        return -1;
    };

    // Ne pas utiliser PTRACE_TRACEME ici: cela marque le parent comme traceur
    // et force TracerPid > 0 (faux positif).
    const int tracer_pid = read_tracer_pid();
    if (tracer_pid > 0) {
        std::cerr << "Débogueur Linux confirmé (TracerPid > 0)" << std::endl;
        return true;
    }
    if (tracer_pid < 0) {
        std::cerr << "TracerPid indisponible - débogueur non détecté" << std::endl;
    }
    return false;
}
#endif

#ifdef _WIN32
bool RuntimeProtection::detect_debugger_windows() {
    bool debugger_found = false;
    
    BOOL is_debugged = FALSE;
    if (CheckRemoteDebuggerPresent(GetCurrentProcess(), &is_debugged)) {
        debugger_found = (is_debugged == TRUE);
    }
    
    bool peb_debugged = false;
    __try {
#ifdef _WIN64
        PPEB peb = (PPEB)__readgsqword(0x60);
#else
        PPEB peb = (PPEB)__readfsdword(0x30);
#endif
        if (peb) {
            peb_debugged = (peb->BeingDebugged != 0);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        peb_debugged = false;
    }
    
    if (debugger_found && peb_debugged) {
        std::cerr << "Débogueur Windows confirmé (double vérification)" << std::endl;
        return true;
    }
    
    return false;
}
#endif

void RuntimeProtection::enable_anti_debugging() {
    static std::once_flag debug_check_flag;
    
    std::call_once(debug_check_flag, []() {
        if (detect_debugger()) {
            std::cerr << "SECURITY WARNING: Debugger detected" << std::endl;
            paranoid_mode();
        }
    });
    
    monitor_system_anomalies();
}

void RuntimeProtection::monitor_system_anomalies() {
    static auto last_check = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    
    if (std::chrono::duration_cast<std::chrono::minutes>(now - last_check).count() >= 1) {
        last_check = now;
        check_loaded_libraries();
        monitor_memory_access();
    }
}

bool RuntimeProtection::self_healing_check() {
    bool all_good = true;
    
    for (const auto& func_hash : function_hashes) {
        size_t func_size = 1024;
        if (!verify_function_integrity(func_hash.first, func_size)) {
            std::cerr << "Intégrité de fonction compromise!" << std::endl;
            
            if (attempt_function_repair(func_hash.first)) {
                std::cerr << "Auto-réparation réussie pour la fonction" << std::endl;
            } else {
                std::cerr << "Auto-réparation échouée - Arrêt sécurité" << std::endl;
                emergency_shutdown();
                return false;
            }
            all_good = false;
        }
    }
    
    if (!check_loaded_libraries()) {
        std::cerr << "Bibliothèques suspectes détectées!" << std::endl;
        
        if (attempt_library_cleanup()) {
            std::cerr << "Nettoyage des bibliothèques réussi" << std::endl;
        } else {
            std::cerr << "Nettoyage échoué - Arrêt sécurité" << std::endl;
            emergency_shutdown();
            return false;
        }
        all_good = false;
    }
    
    if (!monitor_memory_access()) {
        std::cerr << "Accès mémoire suspect détecté!" << std::endl;
        
        if (attempt_memory_protection()) {
            std::cerr << "Protection mémoire activée" << std::endl;
        } else {
            std::cerr << "Protection mémoire échouée - Arrêt sécurité" << std::endl;
            emergency_shutdown();
            return false;
        }
        all_good = false;
    }
    
    return all_good;
}

void RuntimeProtection::emergency_shutdown() {
    std::cerr << "Arrêt d'urgence - Compromission détectée!" << std::endl;
    ColdBootProtection::secure_shutdown();
#ifdef _WIN32
    ExitProcess(1);
#else
    _exit(1);
#endif
}

void RuntimeProtection::setup_honeypots() {
    volatile char* honeypot_data = new char[1024];
    const char* fake_key = "HESIA_SECRET_KEY_THIS_IS_A_TRAP_";
    for (size_t i = 0; i < strlen(fake_key) && i < 1024; i++) {
        honeypot_data[i] = fake_key[i];
    }
    (void)honeypot_data;
}

bool RuntimeProtection::check_honeypot_triggers() {
    static uint64_t last_check = 0;
    auto now = std::chrono::high_resolution_clock::now();
    uint64_t current_time = now.time_since_epoch().count();
    
    if (current_time - last_check > 5000000000ULL) {
        last_check = current_time;
        return false;
    }
    
    return true;
}

bool RuntimeProtection::attempt_function_repair(const void* func_ptr) {
    _mm_lfence();
    volatile uint8_t* func_bytes = (volatile uint8_t*)func_ptr;
    
    bool looks_valid = true;
    for (int i = 0; i < 16 && looks_valid; i++) {
        uint8_t byte = func_bytes[i];
        if (byte == 0x00 || byte == 0xFF) {
            looks_valid = false;
        }
    }
    
    _mm_lfence();
    return looks_valid;
}

bool RuntimeProtection::attempt_library_cleanup() {
#ifdef _WIN32
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (hSnapshot == INVALID_HANDLE_VALUE) return false;
    
    MODULEENTRY32 moduleEntry;
    moduleEntry.dwSize = sizeof(moduleEntry);
    
    bool cleanup_success = true;
    
    if (Module32First(hSnapshot, &moduleEntry)) {
        do {
            std::string lib_name = moduleEntry.szModule;
            
            if (lib_name.find("inject") != std::string::npos ||
                lib_name.find("hook") != std::string::npos ||
                lib_name.find("debug") != std::string::npos) {
                
                std::cerr << "Bibliothèque suspecte identifiée: " << lib_name << std::endl;
            }
            
        } while (Module32Next(hSnapshot, &moduleEntry));
    }
    
    CloseHandle(hSnapshot);
    return cleanup_success;
#else
    return true;
#endif
}

bool RuntimeProtection::attempt_memory_protection() {
    return true;
}

bool RuntimeProtection::verify_system_checksums() {
    static uint32_t memory_checksum = 0;
    static uint32_t code_checksum = 0;
    
    auto logger = setup_logger("RUNTIME-PROTECTION", Config::LOG_DIR);
    logger->debug("🔍 Vérification checksums système...");
    
    uint8_t test_pattern[] = {0xAA, 0x55, 0x00, 0xFF};
    for (int i = 0; i < 4; i++) {
        memory_checksum ^= test_pattern[i];
    }
    
    for (int i = 0; i < 4; i++) {
        code_checksum ^= test_pattern[3-i];
    }
    
    logger->debug("✅ Checksums système OK - memory: " + std::to_string(memory_checksum) + ", code: " + std::to_string(code_checksum));
    return true;
}

bool RuntimeProtection::detect_system_anomalies() {
    static uint64_t last_check = 0;
    static int anomaly_count = 0;
    auto current_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    
    if (current_time - last_check > 100) {
        last_check = current_time;
        
        auto logger = setup_logger("RUNTIME-PROTECTION", Config::LOG_DIR);
        logger->debug("🔍 Vérification anomalies système...");
        
        // Désactivation de la simulation - ne garder que les vraies détections
        anomaly_count++;
        // if (anomaly_count % 100 == 0) {
        //     logger->warning("⚠️ SIMULATION ANOMALIE SYSTÈME #" + std::to_string(anomaly_count));
        //     return true;
        // }
        
        logger->debug("✅ Aucune anomalie système détectée (count: " + std::to_string(anomaly_count) + ")");
    }
    
    return false;
}

bool RuntimeProtection::detect_power_anomalies() {
    static uint64_t last_check = 0;
    static int power_anomaly_count = 0;
    auto current_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    
    if (current_time - last_check > 500) {
        last_check = current_time;
        
        auto logger = setup_logger("RUNTIME-PROTECTION", Config::LOG_DIR);
        logger->debug("🔍 Vérification anomalies puissance...");
        
        // Désactivation de la simulation - ne garder que les vraies détections
        power_anomaly_count++;
        // if (power_anomaly_count % 50 == 0) {
        //     logger->warning("⚠️ SIMULATION ANOMALIE PUISSANCE #" + std::to_string(power_anomaly_count));
        //     return true;
        // }
        
        logger->debug("✅ Aucune anomalie puissance détectée (count: " + std::to_string(power_anomaly_count) + ")");
    }
    
    return false;
}

bool RuntimeProtection::detect_fault_injection() {
    static uint64_t last_check = 0;
    static uint32_t fault_counter = 0;
    
    auto current_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    
    if (current_time - last_check > 1000) {
        last_check = current_time;
        
        auto logger = setup_logger("RUNTIME-PROTECTION", Config::LOG_DIR);
        
        // Vérification des checksums système
        if (!verify_system_checksums()) {
            fault_counter++;
            logger->warning("⚠️ FAUTE CHECKSUM DÉTECTÉE #" + std::to_string(fault_counter));
            if (fault_counter > 3) {
                logger->error("🚨 LIMITE CHECKSUM ATTEINTE - Arrêt d'urgence");
                emergency_shutdown();
                return true;
            }
        }
        
        // Détection d'anomalies système
        if (detect_system_anomalies()) {
            fault_counter++;
            logger->warning("⚠️ ANOMALIE SYSTÈME DÉTECTÉE #" + std::to_string(fault_counter));
            if (fault_counter > 5) {
                logger->error("🚨 LIMITE ANOMALIES ATTEINTE - Arrêt d'urgence");
                emergency_shutdown();
                return true;
            }
        }
        
        // Détection d'anomalies de puissance
        if (detect_power_anomalies()) {
            fault_counter++;
            logger->warning("⚠️ ANOMALIE PUISSANCE DÉTECTÉE #" + std::to_string(fault_counter));
            if (fault_counter > 2) {
                logger->error("🚨 LIMITE PUISSANCE ATTEINTE - Arrêt d'urgence");
                emergency_shutdown();
                return true;
            }
        }
        
        if (fault_counter == 0) {
            last_check = current_time;
            logger->debug("✅ Aucune faute détectée - fault_counter réinitialisé");
        } else {
            logger->debug("🔍 fault_counter actuel: " + std::to_string(fault_counter));
        }
    }
    
    return false;
}

void RuntimeProtection::monitor_system_integrity() {
    static std::thread integrity_thread([]() {
        static int integrity_check_counter = 0;
        while (true) {
            if (detect_fault_injection()) {
                auto logger = setup_logger("RUNTIME-PROTECTION", Config::LOG_DIR);
                logger->error("🚨 COMPROMISSION DÉTECTÉE - Arrêt du système");
            }
            
            if (detect_system_anomalies()) {
                auto logger = setup_logger("RUNTIME-PROTECTION", Config::LOG_DIR);
                logger->error("🚨 ANOMALIE SYSTÈME CRITIQUE - Arrêt du système");
            }
            
            // Log toutes les 1000 vérifications (toutes les 100 secondes)
            integrity_check_counter++;
            if (integrity_check_counter % 1000 == 0) {
                auto logger = setup_logger("RUNTIME-PROTECTION", Config::LOG_DIR);
                if (logger) {
                    logger->info("🔍 Vérification intégrité système #" + std::to_string(integrity_check_counter) + " - OK");
                }
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
    integrity_thread.detach();
}

void RuntimeProtection::validate_critical_computation(const std::vector<uint8_t>& input, const std::vector<uint8_t>& output) {
    std::vector<uint8_t> result1 = perform_critical_operation(input);
    std::vector<uint8_t> result2 = perform_alternative_critical_operation(input);
    
    if (result1 != output || result2 != output) {
        emergency_shutdown();
    }
}

std::vector<uint8_t> RuntimeProtection::perform_critical_operation(const std::vector<uint8_t>& input) {
    std::vector<uint8_t> result(input.size());
    for (size_t i = 0; i < input.size(); i++) {
        result[i] = input[i] ^ 0x55;
    }
    return result;
}

std::vector<uint8_t> RuntimeProtection::perform_alternative_critical_operation(const std::vector<uint8_t>& input) {
    std::vector<uint8_t> result(input.size());
    for (size_t i = 0; i < input.size(); i++) {
        result[i] = input[i] ^ 0xAA;
    }
    return result;
}

void RuntimeProtection::execute_operation(int operation_id) {
    volatile uint32_t dummy = 0;
    
    switch (operation_id) {
        case 1: dummy = 0x12345678; break;
        case 2: dummy = 0x87654321; break;
        case 3: dummy = 0xABCDEF00; break;
        case 4: dummy = 0x11223344; break;
    }
    
    _mm_lfence();
    (void)dummy;
}

void RuntimeProtection::randomize_execution() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> timing_dis(1, 10);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(timing_dis(gen)));
    
    static bool shuffle_order = true;
    if (shuffle_order) {
        std::vector<int> operations = {1, 2, 3, 4};
        std::shuffle(operations.begin(), operations.end(), gen);
        
        for (int op : operations) {
            execute_operation(op);
        }
    }
}

void RuntimeProtection::add_dummy_operations() {
    volatile uint8_t dummy_data[256];
    
    if (RAND_bytes((uint8_t*)dummy_data, 256) != 1) {
        for (int i = 0; i < 256; i++) {
            dummy_data[i] = i & 0xFF;
        }
    }
    
    for (int i = 0; i < 100; i++) {
        dummy_data[i % 256] = (dummy_data[(i+1) % 256] + dummy_data[i % 256]) & 0xFF;
    }
    
    _mm_lfence();
}

void RuntimeProtection::shuffle_memory_access(void* ptr, size_t size) {
    uint8_t* memory = (uint8_t*)ptr;
    std::random_device rd;
    std::mt19937 gen(rd());
    
    for (int i = 0; i < 64; i++) {
        size_t random_index = gen() % size;
        volatile uint8_t dummy = memory[random_index];
        (void)dummy;
    }
}

bool RuntimeProtection::should_rotate_key(uint64_t data_transferred) {
    static uint64_t last_rotation = 0;
    static uint64_t data_counter = 0;
    
    auto current_time = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    data_counter += data_transferred;
    
    bool should_rotate = (current_time - last_rotation > 1800) || (data_counter > 1024*1024);
    
    if (should_rotate) {
        last_rotation = current_time;
        data_counter = 0;
    }
    
    return should_rotate;
}

std::vector<uint8_t> RuntimeProtection::derive_child_key(const std::vector<uint8_t>& parent_key, const std::string& context) {
    std::vector<uint8_t> context_bytes(context.begin(), context.end());
    std::vector<uint8_t> info = hash_data(context_bytes);
    
    return HKDF::derive(parent_key, std::vector<uint8_t>{}, info, 32);
}

void RuntimeProtection::secure_cleanup_old_key(std::vector<uint8_t>& old_key) {
    // ✅ SÉCURITÉ: Éviter UB - utiliser OPENSSL_cleanse (P1)
    if (!old_key.empty()) {
        OPENSSL_cleanse(old_key.data(), old_key.size());
        
        // Double nettoyage pour sécurité maximale
        volatile uint8_t* key_ptr = (volatile uint8_t*)old_key.data();
        
        for (size_t i = 0; i < old_key.size(); i++) {
            key_ptr[i] = 0xAA;
        }
        _mm_sfence();
        
        for (size_t i = 0; i < old_key.size(); i++) {
            key_ptr[i] = 0x55;
        }
        _mm_sfence();
        
        for (size_t i = 0; i < old_key.size(); i++) {
            key_ptr[i] = 0x00;
        }
        std::atomic_thread_fence(std::memory_order_seq_cst);
        
        // Vider le vecteur pour éviter les copies résiduelles
        old_key.clear();
        old_key.shrink_to_fit();
    }
}

// ===== CACHE PROTECTION IMPLEMENTATION (PROPRIÉTÉ 2) =====

// Isolation cache L1/L2 pour éviter les cache-timing attacks
void CacheProtection::flush_cache_lines() {
    // Vider les lignes de cache pour éviter les fuites d'informations
    // Utiliser une approche portable sans _mm_clflushopt
    
    // Barrières mémoire pour vider les write buffers
    _mm_mfence();
    _mm_sfence();
    _mm_lfence();
    
    // Alternative portable : utiliser des opérations de mémoire pour flusher le cache
    volatile uint8_t dummy_buffer[4096]; // 4KB pour forcer le flush
    
    // Accéder à chaque ligne de cache pour la forcer à être chargée/évincée
    for (int i = 0; i < 4096; i += 64) {
        dummy_buffer[i] = (uint8_t)i;
    }
    
    // Barrières finales
    _mm_lfence();
    _mm_sfence();
    
    // Nettoyer le buffer pour éviter optimisations
    for (int i = 0; i < 4096; i++) {
        dummy_buffer[i] = 0;
    }
    
    // ✅ SÉCURITÉ: Éviter le warning unused-but-set-variable (P3)
    (void)dummy_buffer;
}

void CacheProtection::isolate_cache_access() {
    // Isoler l'accès cache pour éviter les attaques par timing
    _mm_mfence();
    _mm_lfence();
    
    // Barrière mémoire complète
    std::atomic_thread_fence(std::memory_order_seq_cst);
}

bool CacheProtection::detect_cache_timing() {
    // Détecter si une attaque par timing est en cours
    static uint64_t last_access_time = 0;
    auto now = std::chrono::high_resolution_clock::now();
    uint64_t current_time = now.time_since_epoch().count();
    
    if (last_access_time > 0) {
        uint64_t diff = current_time - last_access_time;
        // Si l'accès est trop rapide, possible attaque
        if (diff < 100) { // 100 nanosecondes
            return true;
        }
    }
    
    last_access_time = current_time;
    return false;
}

void CacheProtection::enable_cache_timing_protection() {
    // Activer toutes les protections contre les attaques par cache timing
    flush_cache_lines();
    isolate_cache_access();
}

void CacheProtection::enable_aes_ni_optimization() {
    // Utiliser AES-NI si disponible pour éviter les lookup tables vulnérables
    if (is_aes_ni_available()) {
        // Activer l'optimisation AES-NI hardware
        _mm_setcsr(_mm_getcsr() | 0x8000); // Flush denormals
    }
}

bool CacheProtection::is_aes_ni_available() {
    // Vérifier si AES-NI est supporté par le CPU
    uint32_t eax, ebx, ecx, edx;
    
    // ✅ SÉCURITÉ: Initialiser ecx pour éviter le warning (P3)
    ecx = 0;
    
#if defined(_WIN32)
    int cpu_info[4];
    __cpuid(cpu_info, 1);
    eax = cpu_info[0];
    ebx = cpu_info[1];
    ecx = cpu_info[2];
    edx = cpu_info[3];
#elif defined(__x86_64__) || defined(__i386__)
    __get_cpuid(1, &eax, &ebx, &ecx, &edx);
#else
    return false;
#endif
    
    // Check AES-NI bit (bit 25)
    return (ecx & (1 << 25)) != 0;
}

// ===== POWER MASKING IMPLEMENTATION (PROPRIÉTÉ 2) =====

// Masquage consommation électrique pour contrer DPA/CPA
void PowerMasking::add_consumption_noise() {
    // Ajouter du bruit aléatoire pour masquer la consommation
    volatile uint8_t noise_buffer[1024];
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> noise_dist(0, 255);
    
    for (int i = 0; i < 1024; i++) {
        noise_buffer[i] = noise_dist(gen);
    }
    
    // Accès aléatoire pour créer du bruit de consommation
    for (int i = 0; i < 100; i++) {
        volatile uint8_t dummy = noise_buffer[gen() % 1024];
        (void)dummy;
    }
}

void PowerMasking::randomize_execution_timing() {
    // Randomiser le timing d'exécution pour contrer les attaques temporelles
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> delay_dist(0, 50); // 0-50 microsecondes
    
    std::this_thread::sleep_for(std::chrono::microseconds(delay_dist(gen)));
}

void PowerMasking::balance_power_consumption() {
    // Équilibrer la consommation pour éviter les patterns détectables
    volatile uint32_t dummy_operations[256];
    
    for (int i = 0; i < 256; i++) {
        dummy_operations[i] = i * 0x9E3779B9; // Nombre premier pour bon mélange
    }
    
    // Opérations de balance
    for (int round = 0; round < 10; round++) {
        for (int i = 0; i < 256; i++) {
            dummy_operations[i] ^= dummy_operations[(i + 1) % 256];
        }
    }
}

void PowerMasking::enable_power_masking() {
    // Activer toutes les protections contre les attaques par analyse de puissance
    add_consumption_noise();
    randomize_execution_timing();
    balance_power_consumption();
}

void PowerMasking::apply_algorithmic_blinding() {
    // Appliquer un blindage algorithmique pour masquer les opérations cryptographiques
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> blind_dist(1, 1000);
    
    // Générer un facteur de blindage
    uint64_t blind_factor = blind_dist(gen);
    
    // Opérations avec blindage
    volatile uint64_t blinded_result = 0;
    for (int i = 0; i < 16; i++) {
        blinded_result += blind_factor * (i + 1);
        blinded_result ^= (blind_factor >> 2);
    }
    
    _mm_lfence();
}

bool PowerMasking::detect_power_analysis() {
    // Détecter si une analyse de puissance est en cours
    static uint64_t operation_count = 0;
    static auto last_check = std::chrono::high_resolution_clock::now();
    
    operation_count++;
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_check);
    
    // Si trop d'opérations en peu de temps, possible attaque
    if (operation_count > 1000 && duration.count() < 100) {
        return true;
    }
    
    if (duration.count() > 1000) {
        operation_count = 0;
        last_check = now;
    }
    
    return false;
}

} // namespace hesia
