#ifndef FAULT_INJECTION_PROTECTION_HPP
#define FAULT_INJECTION_PROTECTION_HPP

#include <vector>
#include <cstdint>
#include <chrono>
#include <atomic>
#include <thread>
#include <memory>
#include <fstream>
#include <string>
#include <mutex> // Ajout de l'include <mutex>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#endif

namespace hesia {

// ===== PROTECTION CONTRE LES ATTAQUES PAR INJECTION DE FAUTES =====

class FaultInjectionProtection {
private:
    struct FaultSample {
        uint32_t checksum;
        uint64_t timestamp;
        uint32_t operation_id;
        uint8_t fault_type;
        bool redundancy_passed;
    };
    
    static std::atomic<bool> fault_monitor_active;
    static std::vector<FaultSample> fault_history;
    static std::mutex fault_history_mutex; // ✅ P2: Protéger l'historique contre les data races
    static std::thread monitoring_thread;
    static std::atomic<uint32_t> fault_count;
    static std::atomic<uint32_t> redundancy_failures;
    static std::atomic<uint32_t> checksum_errors;
    
    // Configuration de la protection
    static std::atomic<bool> enable_spatial_redundancy;
    static std::atomic<bool> enable_temporal_redundancy;
    static std::atomic<uint32_t> redundancy_level; // 2=double, 3=triple
    static std::atomic<uint32_t> fault_threshold;
    static std::atomic<bool> hardware_assisted;
    
    // Fichiers de monitoring
    static std::string integrity_log_path;
    static std::string fault_detection_path;
    
    // Fonctions de monitoring
    static void fault_monitoring_loop();
    static FaultSample perform_redundant_check(uint32_t operation_id, const void* data, size_t size);
    static uint32_t calculate_checksum(const void* data, size_t size);
    static bool verify_spatial_redundancy(const void* data, size_t size);
    static bool verify_temporal_redundancy(const void* data, size_t size);
    
    // Détection de fautes
    static bool detect_fault_injection(const FaultSample& sample);
    static bool detect_laser_fault(const FaultSample& sample);
    static bool detect_voltage_fault(const FaultSample& sample);
    static bool detect_em_fault(const FaultSample& sample);
    static bool detect_rowhammer_fault(const FaultSample& sample);
    
    // Contre-mesures
    static void implement_fault_correction();
    static void trigger_fault_response();
    static void isolate_compromised_memory();
    static void perform_secure_recomputation();
    
public:
    // Initialisation de la protection contre injection de fautes
    static bool initialize(uint32_t redundancy_lvl = 2);
    
    // Démarrer le monitoring des fautes
    static bool start_fault_monitoring();
    
    // Arrêter le monitoring
    static void stop_fault_monitoring();
    
    // Vérifier si le monitoring est actif
    static bool is_monitoring_active();
    static bool is_hardware_assisted();
    
    // Types pour les statistiques de fautes
    struct FaultStats {
        uint32_t total_operations;
        uint32_t fault_count;
        uint32_t redundancy_failures;
        uint32_t checksum_errors;
        double fault_rate;
        uint32_t laser_faults;
        uint32_t voltage_faults;
        uint32_t em_faults;
        uint32_t rowhammer_faults;
    };
    
    enum FaultType {
        FAULT_NONE = 0,
        FAULT_LASER = 1,
        FAULT_VOLTAGE = 2,
        FAULT_EM = 3,
        FAULT_ROWHAMMER = 4,
        FAULT_UNKNOWN = 5
    };
    
    enum FaultHealth {
        FAULT_HEALTH_GOOD = 0,
        FAULT_HEALTH_WARNING = 1,
        FAULT_HEALTH_CRITICAL = 2,
        FAULT_HEALTH_EMERGENCY = 3
    };
    
    // Obtenir les statistiques de fautes
    static FaultStats get_fault_statistics();
    
    // Configuration de la protection
    static void set_redundancy_level(uint32_t level);
    static void enable_redundant_computation();
    static void implement_spatial_separation();
    static void setup_fault_detection_circuits();
    
    // Détection d'attaques spécifiques
    static bool detect_fault_injection();
    static bool detect_laser_fault();
    static bool detect_voltage_fault();
    static bool detect_em_fault();
    static bool detect_rowhammer_fault();
    
    // Vérification d'intégrité
    static bool verify_operation_integrity(uint32_t operation_id, const void* data, size_t size);
    static bool verify_memory_integrity(const void* ptr, size_t size);
    static bool verify_computation_integrity(uint32_t operation_id, uint32_t result);
    
    // Test de la protection
    static bool test_fault_protection();
    
    // Réinitialiser les statistiques
    static void reset_fault_statistics();
    
    // Obtenir l'état de santé des fautes
    static FaultHealth get_fault_health();
    
    // Configuration des chemins
    static void set_integrity_log_path(const std::string& path);
    static void set_fault_detection_path(const std::string& path);
    
    // Nettoyage et arrêt
    static void cleanup();
    
    // Fonctions utilitaires pour la redondance
    static uint32_t compute_with_redundancy(uint32_t (*func)(uint32_t), uint32_t input);
    static std::vector<uint8_t> encrypt_with_redundancy(const std::vector<uint8_t>& data);
    static bool compare_with_redundancy(const void* data1, const void* data2, size_t size);
};

} // namespace hesia

#endif // FAULT_INJECTION_PROTECTION_HPP
