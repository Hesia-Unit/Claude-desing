#ifndef HARDWARE_MONITOR_HPP
#define HARDWARE_MONITOR_HPP

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
#endif

namespace hesia {

// ===== WATCHDOG EXTERNE =====

class ExternalWatchdog {
private:
    static std::atomic<bool> watchdog_active;
    static std::atomic<uint64_t> last_heartbeat;
    static std::atomic<uint32_t> watchdog_timeout_ms;
    static std::thread watchdog_thread;
    static std::string watchdog_device;
    
    // Fonction de monitoring du watchdog
    static void watchdog_monitor_loop();
    
    // Communication avec le watchdog hardware
    static bool send_heartbeat_to_watchdog();
    static void reset_watchdog_hardware();
    
public:
    // Initialisation du watchdog externe
    static bool initialize(uint32_t timeout_ms = 5000);
    
    // Envoyer un signal de vie au watchdog
    static void pet_watchdog();
    
    // Vérifier si le watchdog est actif
    static bool is_active();
    
    // Arrêter le watchdog
    static void shutdown();
    
    // Obtenir le temps depuis dernier heartbeat
    static uint64_t get_time_since_last_heartbeat();
    
    // Configuration du device watchdog
    static void set_watchdog_device(const std::string& device);
    
    // Test de communication watchdog
    static bool test_watchdog_communication();

    // Méthode utilitaire pour le timestamp
    static uint64_t get_current_timestamp_ms();
};

// ===== DÉTECTION GLITCH/VOLTAGE RÉEL =====

class GlitchVoltageDetector {
private:
    struct VoltageSample {
        double voltage;
        uint64_t timestamp;
        double frequency;
        uint32_t temperature;
    };
    
    static std::atomic<bool> detector_active;
    static std::vector<VoltageSample> voltage_history;
    static std::thread monitoring_thread;
    static std::atomic<double> nominal_voltage;
    static std::atomic<double> nominal_frequency;
    static std::atomic<uint32_t> glitch_threshold_mv;
    static std::atomic<uint32_t> frequency_threshold_hz;
    static std::atomic<double> cpu_min_freq_mhz;
    static std::atomic<double> cpu_max_freq_mhz;
    static std::atomic<uint32_t> temperature_threshold_celsius;
    
    // Fichiers de monitoring système
    static std::string voltage_sysfs_path;
    static std::string frequency_sysfs_path;
    static std::string temperature_sysfs_path;
    
    // Fonctions de monitoring
    static void voltage_monitoring_loop();
    static VoltageSample read_voltage_sample();
    static double read_cpu_voltage();
    static double read_cpu_frequency();
    static uint32_t read_cpu_temperature();
    
    // Détection de glitches
    static bool detect_voltage_glitch(const VoltageSample& current);
    static bool detect_frequency_glitch(const VoltageSample& current);
    static bool detect_temperature_anomaly(const VoltageSample& current);
    
    // Calibration système
    static bool calibrate_nominal_values();
    
public:
    // Initialisation du détecteur de glitch
    static bool initialize(double nominal_voltage = 1.2, double nominal_frequency = 2400.0);
    
    // Démarrer le monitoring
    static bool start_monitoring();
    
    // Arrêter le monitoring
    static void stop_monitoring();
    
    // Vérifier si le détecteur est actif
    static bool is_active();
    
    // Types pour les statistiques et santé système
    struct GlitchStats {
        uint32_t total_samples;
        uint32_t glitch_count;
        uint32_t anomaly_count;
        double avg_voltage;
        double avg_frequency;
        uint32_t max_temperature;
    };
    
    enum SystemHealth {
        SYSTEM_HEALTH_GOOD = 0,
        SYSTEM_HEALTH_WARNING = 1,
        SYSTEM_HEALTH_CRITICAL = 2,
        SYSTEM_HEALTH_EMERGENCY = 3
    };
    
    // Obtenir les statistiques actuelles
    static GlitchStats get_statistics();
    
    // Configuration des seuils
    static void set_voltage_threshold(uint32_t threshold_mv);
    static void set_frequency_threshold(uint32_t threshold_hz);
    static void set_temperature_threshold(uint32_t threshold_celsius);
    
    // Test de détection
    static bool test_glitch_detection();
    
    // Réinitialiser les statistiques
    static void reset_statistics();
    
    // Obtenir l'état de santé système
    static SystemHealth get_system_health();
};

// ===== INTÉGRATION AVEC RUNTIME PROTECTION =====

class HardwareSecurityIntegration {
private:
    static std::atomic<bool> integration_active;
    static std::atomic<uint32_t> hardware_alerts_count;
    
public:
    // Callbacks pour RuntimeProtection
    static void on_hardware_glitch_detected();
    static void on_watchdog_timeout();
    static void on_voltage_anomaly_detected();
    
    // Initialiser l'intégration complète
    static bool initialize();
    
    // Démarrer tous les monitors hardware
    static bool start_hardware_monitoring();
    
    // Arrêter tous les monitors
    static void stop_hardware_monitoring();
    
    // Vérifier l'état de l'intégration
    static bool is_integration_active();
    
    // Obtenir le nombre d'alertes hardware
    static uint32_t get_hardware_alerts_count();
    
    // Test complet de l'intégration
    static bool test_hardware_integration();
};

} // namespace hesia

#endif // HARDWARE_MONITOR_HPP
