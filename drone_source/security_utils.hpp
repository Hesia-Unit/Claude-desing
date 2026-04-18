// security_utils.hpp - FICHIER HEADER
#ifndef SECURITY_UTILS_HPP
#define SECURITY_UTILS_HPP

#include <vector>
#include <cstdint>
#include <cstring>
#include <string>
#include <memory>
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include "logger.hpp"
#include <atomic>
#include <mutex>

namespace hesia {

// ===== CLASSES DE SÉCURITÉ FONDAMENTALES =====

// 1. Zeroization sécurisée de la mémoire
class SecureMemory {
public:
    static void zeroize(void* ptr, size_t len);
    // Wipe without changing the vector size/capacity.
    static void wipe(std::vector<uint8_t>& data);
    // Wipe then clear() the vector (size becomes 0). Prefer wipe() when the
    // caller still needs the buffer size.
    static void zeroize(std::vector<uint8_t>& data);

    // Copy helper (kept here because several components use SecureMemory::*).
    static void copy(void* dst, const void* src, size_t len);
    
    // Allocation sécurisée
    static std::vector<uint8_t> secure_alloc(size_t size);
    static void secure_free(void* ptr, size_t size);
};

// 2. Comparaison constant-time
class ConstantTime {
public:
    // Comparaison sécurisée qui ne fuit pas d'informations temporelles
    static bool equals(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b);
    static bool equals(const uint8_t* a, const uint8_t* b, size_t len);
    
    // Sélection constant-time
    static uint8_t select(uint8_t mask, uint8_t a, uint8_t b);
};

// 3. Protection Cache Timing (Propriété 2)
class CacheProtection {
public:
    // Isolation cache L1/L2 pour éviter les cache-timing attacks
    static void flush_cache_lines();
    static void isolate_cache_access();
    static bool detect_cache_timing();
    static void enable_cache_timing_protection();
    
    // Protection AES-NI contre les attaques par timing
    static void enable_aes_ni_optimization();
    static bool is_aes_ni_available();
};

// 4. Power Masking (Propriété 2)
class PowerMasking {
public:
    // Masquage consommation électrique pour contrer DPA/CPA
    static void add_consumption_noise();
    static void randomize_execution_timing();
    static void balance_power_consumption();
    static void enable_power_masking();
    
    // Blindage algorithmique
    static void apply_algorithmic_blinding();
    static bool detect_power_analysis();
};

// HKDF implementation for secure key derivation
class HKDF {
public:
    static std::vector<uint8_t> extract(const std::vector<uint8_t>& salt, const std::vector<uint8_t>& ikm);
    static std::vector<uint8_t> expand(const std::vector<uint8_t>& prk, const std::vector<uint8_t>& info, size_t len);
    static std::vector<uint8_t> derive(const std::vector<uint8_t>& salt, const std::vector<uint8_t>& ikm, 
                                      const std::vector<uint8_t>& info, size_t len);
    
private:
    static const size_t HASH_SIZE = 64; // SHA3-512
};

// 3. Générateur CSPRNG avancé
class SecureRNG {
public:
    static bool generate_bytes(std::vector<uint8_t>& output);
    static bool generate_bytes(uint8_t* output, size_t len);
    
    // Génération avec entropy monitoring
    static bool secure_generate(std::vector<uint8_t>& output);
    
private:
    static bool check_entropy();
};

// 4. Protection contre les fautes
class FaultDetection {
public:
    // Calcul redondant avec vérification
    static bool verify_computation(const std::vector<uint8_t>& input, 
                                 const std::vector<uint8_t>& output);
    
    // Double calcul avec comparaison
    static std::vector<uint8_t> redundant_encrypt(const std::vector<uint8_t>& plaintext,
                                               const std::vector<uint8_t>& key,
                                               const std::vector<uint8_t>& iv);
};

// 5. Monitoring de la mémoire GPU (si disponible)
class MemoryMonitor {
public:
    static bool check_gpu_memory_leak();
    static void secure_gpu_cleanup();
};

// 6. Protection contre le cold boot
class ColdBootProtection {
public:
    // Effacement sécurisé au démarrage/arrêt
    static void secure_startup();
    static void secure_shutdown();
    // Chiffrement de la mémoire sensible
    static void encrypt_sensitive_memory();
};

// ===== ADVANCED RUNTIME PROTECTION =====
class RuntimeProtection {
public:
    // Initialisation
    static void setup_protection();
    
    // Anti-debugging
    static bool detect_debugger();
    static bool detect_debugger_windows();
    static bool detect_debugger_linux();
    static void enable_anti_debugging();
    static bool is_wsl_environment();
    
    // Monitoring système
    static void monitor_system_anomalies();
    static bool self_healing_check();
    static void emergency_shutdown();
    static void setup_honeypots();
    static bool check_honeypot_triggers();
    
    // Réparation automatique
    static bool attempt_function_repair(const void* func_ptr);
    static bool attempt_library_cleanup();
    static bool attempt_memory_protection();
    
    // Remote attestation
    static std::vector<uint8_t> generate_attestation_report();
    static bool validate_remote_attestation(const std::vector<uint8_t>& report);
    
    // Intégrité du code
    static bool verify_function_integrity(const void* func_ptr, size_t func_size);
    static void calculate_function_hash(const void* func_ptr, size_t func_size, std::vector<uint8_t>& hash);
    static bool check_loaded_libraries();
    static bool verify_library_integrity(const std::string& lib_name);
    static bool monitor_memory_access();
    static void detect_memory_dumping();
    static void paranoid_mode();
    static void enable_continuous_monitoring();
    
    // Fault Injection Awareness
    static bool detect_fault_injection();
    static void monitor_system_integrity();
    static void validate_critical_computation(const std::vector<uint8_t>& input, const std::vector<uint8_t>& output);
    
    // Randomisation d'Exécution
    static void randomize_execution();
    static void add_dummy_operations();
    static void shuffle_memory_access(void* ptr, size_t size);
    
    // Re-keying / Session Éphémère
    static bool should_rotate_key(uint64_t data_transferred);
    static std::vector<uint8_t> derive_child_key(const std::vector<uint8_t>& parent_key, const std::string& context);
    static void secure_cleanup_old_key(std::vector<uint8_t>& old_key); // ✅ P1: non-const pour éviter UB
    
private:
    // Fonctions utilitaires pour fault injection
    static bool verify_system_checksums();
    static bool detect_system_anomalies();
    static bool detect_power_anomalies();
    static std::vector<uint8_t> perform_critical_operation(const std::vector<uint8_t>& input);
    static std::vector<uint8_t> perform_alternative_critical_operation(const std::vector<uint8_t>& input);
    static void execute_operation(int operation_id);
    
    // Journalisation
    static std::shared_ptr<Logger> setup_logger(
        const std::string& name,
        const std::string& log_dir
    );
    
private:
    static std::vector<std::pair<const void*, std::vector<uint8_t>>> function_hashes;
    static std::mutex function_hashes_mutex;
    static std::atomic<bool> protection_enabled;
};

} // namespace hesia

// Fonctions utilitaires
namespace hesia {
    uint8_t safe_compare(const uint8_t* a, const uint8_t* b, size_t len);
    void* secure_alloc(size_t size);
    void secure_free(void* ptr, size_t size);
    void sensitive_operation(const uint8_t* secret);
    bool is_aesni_supported();
    std::vector<uint8_t> hash_data(const std::vector<uint8_t>& data);
}

#endif // SECURITY_UTILS_HPP