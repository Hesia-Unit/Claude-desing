#ifndef CLOCK_ATTACK_PROTECTION_HPP
#define CLOCK_ATTACK_PROTECTION_HPP

#include <vector>
#include <cstdint>
#include <chrono>
#include <atomic>
#include <thread>
#include <memory>
#include <fstream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <pdh.h>
#else
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#endif

namespace hesia {

// ===== PROTECTION CONTRE LES ATTAQUES PAR CLOCK MANIPULATION =====

class ClockAttackProtection {
private:
    struct ClockSample {
        double frequency_mhz;
        uint64_t timestamp;
        uint64_t tsc_value;
        double jitter_ns;
    };
    
    static std::atomic<bool> clock_monitor_active;
    static std::vector<ClockSample> clock_history;
    static std::thread monitoring_thread;
    static std::atomic<double> nominal_frequency;
    static std::atomic<double> frequency_tolerance_percent;
    static std::atomic<uint64_t> jitter_threshold_ns;
    static std::atomic<uint32_t> anomaly_count;
    static std::atomic<bool> clock_source_secure;
    
    // Fichiers de monitoring système
    static std::string cpuinfo_path;
    static std::string time_path;
    static std::string clock_source_path;
    
    // Fonctions de monitoring
    static void clock_monitoring_loop();
    static ClockSample read_clock_sample();
    static double read_cpu_frequency();
    static uint64_t read_tsc_value();
    static double calculate_jitter(const ClockSample& current);
    
    // Détection d'attaques
    static bool detect_frequency_manipulation(const ClockSample& current);
    static bool detect_clock_jamming(const ClockSample& current);
    static bool detect_clock_glitch(const ClockSample& current);
    static bool detect_underclock_attack(const ClockSample& current);
    static bool detect_overclock_attack(const ClockSample& current);
    
    // Calibration système
    static bool calibrate_nominal_frequency();
    static bool verify_clock_source_integrity();
    
    // Contre-mesures
    static void implement_clock_stabilization();
    static void enable_secure_clock_source();
    static void trigger_clock_attack_response();
    
public:
    // Initialisation de la protection clock
    static bool initialize(double nominal_freq_mhz = 2400.0);
    
    // Démarrer le monitoring clock
    static bool start_clock_monitoring();
    
    // Arrêter le monitoring
    static void stop_clock_monitoring();
    
    // Vérifier si le monitoring est actif
    static bool is_monitoring_active();
    static bool is_clock_source_secure();
    
    // Types pour les statistiques clock
    struct ClockStats {
        uint32_t total_samples;
        uint32_t anomaly_count;
        double avg_frequency;
        double min_frequency;
        double max_frequency;
        double avg_jitter;
        uint64_t max_jitter;
    };
    
    enum ClockHealth {
        CLOCK_HEALTH_GOOD = 0,
        CLOCK_HEALTH_WARNING = 1,
        CLOCK_HEALTH_CRITICAL = 2,
        CLOCK_HEALTH_EMERGENCY = 3
    };
    
    // Obtenir les statistiques clock actuelles
    static ClockStats get_clock_statistics();
    
    // Configuration des seuils
    static void set_frequency_tolerance(double tolerance_percent);
    static void set_jitter_threshold(uint64_t threshold_ns);
    static void set_nominal_frequency(double freq_mhz);
    
    // Détection d'attaques spécifiques
    static bool detect_clock_manipulation();
    static bool detect_clock_jamming();
    static bool detect_clock_glitch();
    
    // Activer les protections
    static void enable_clock_monitoring();
    static void implement_clock_jamming_detection();
    static void setup_secure_clock_source();
    
    // Test de la protection clock
    static bool test_clock_protection();
    
    // Réinitialiser les statistiques
    static void reset_clock_statistics();
    
    // Obtenir l'état de santé clock
    static ClockHealth get_clock_health();
    
    // Obtenir le nombre d'anomalies
    static uint32_t get_anomaly_count();
    
    // Vérifier l'intégrité de la source clock
    static bool verify_clock_integrity();
    
    // Configuration des chemins système
    static void set_cpuinfo_path(const std::string& path);
    static void set_time_path(const std::string& path);
    static void set_clock_source_path(const std::string& path);
    
    // Nettoyage et arrêt
    static void cleanup();
};

} // namespace hesia

#endif // CLOCK_ATTACK_PROTECTION_HPP
