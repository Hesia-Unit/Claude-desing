#ifndef VOLTAGE_GLITCH_PROTECTION_HPP
#define VOLTAGE_GLITCH_PROTECTION_HPP

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

// ===== PROTECTION CONTRE LES ATTAQUES PAR VOLTAGE GLITCH =====

class VoltageGlitchProtection {
private:
    struct VoltageGlitchSample {
        double voltage_v;
        uint64_t timestamp;
        uint32_t duration_us;
        uint8_t glitch_type;
        mutable bool glitch_detected; // mutable pour permettre la modification
    };
    
    static std::atomic<bool> glitch_monitor_active;
    static std::vector<VoltageGlitchSample> glitch_history;
    static std::mutex glitch_history_mutex; // ✅ P2: Protéger l'historique contre les data races
    static std::thread monitoring_thread;
    static std::atomic<uint32_t> glitch_count;
    static std::atomic<uint32_t> clamping_events;
    static std::atomic<bool> voltage_sensor_available;
    
    // Configuration de la protection voltage
    static std::atomic<double> nominal_voltage;
    static std::atomic<double> glitch_threshold_percent;
    static std::atomic<uint32_t> clamping_threshold_mv;
    static std::atomic<bool> voltage_clamping_active;
    static std::atomic<bool> watchdog_active;
    
    // Fichiers de monitoring voltage
    static std::string voltage_sensor_path;
    static std::string clamping_status_path;
    static std::string watchdog_status_path;
    static std::string voltage_log_path;
    
    // Fonctions de monitoring
    static void voltage_glitch_monitoring_loop();
    static VoltageGlitchSample read_voltage_sample();
    static double read_voltage_level();
    static uint32_t read_voltage_duration();
    static bool detect_voltage_glitch(const VoltageGlitchSample& current);
    
    // Détection de glitches spécifiques
    static bool detect_under_voltage_glitch(const VoltageGlitchSample& sample);
    static bool detect_over_voltage_glitch(const VoltageGlitchSample& sample);
    static bool detect_voltage_spike(const VoltageGlitchSample& sample);
    static bool detect_voltage_dip(const VoltageGlitchSample& sample);
    
    // Contre-mesures voltage
    static void implement_voltage_clamping_internal(); // Renommé pour éviter conflit
    static void setup_voltage_watchdog_internal(); // Renommé pour éviter conflit
    static void trigger_voltage_clamping();
    static void trigger_watchdog_reset();
    static void adjust_voltage_protection();
    // Simulation de capteurs voltage (retir?e)
    
public:
    // Initialisation de la protection voltage glitch
    static bool initialize(double nominal_voltage_v = 3.3); // 3.3V typique
    
    // Démarrer le monitoring voltage glitch
    static bool start_voltage_glitch_monitoring();
    
    // Arrêter le monitoring
    static void stop_voltage_glitch_monitoring();
    
    // Vérifier si le monitoring est actif
    static bool is_monitoring_active();
    static bool is_voltage_sensor_available();
    
    // Types pour les statistiques de glitches
    struct VoltageGlitchStats {
        uint32_t total_samples;
        uint32_t glitch_count;
        uint32_t clamping_events;
        double avg_voltage;
        double min_voltage;
        double max_voltage;
        uint32_t under_voltage_glitches;
        uint32_t over_voltage_glitches;
        uint32_t voltage_spikes;
        uint32_t voltage_dips;
    };
    
    enum VoltageGlitchType {
        GLITCH_NONE = 0,
        GLITCH_UNDER_VOLTAGE = 1,
        GLITCH_OVER_VOLTAGE = 2,
        GLITCH_VOLTAGE_SPIKE = 3,
        GLITCH_VOLTAGE_DIP = 4,
        GLITCH_UNKNOWN = 5
    };
    
    enum VoltageHealth {
        VOLTAGE_HEALTH_GOOD = 0,
        VOLTAGE_HEALTH_WARNING = 1,
        VOLTAGE_HEALTH_CRITICAL = 2,
        VOLTAGE_HEALTH_EMERGENCY = 3
    };
    
    // Obtenir les statistiques de glitches
    static VoltageGlitchStats get_voltage_glitch_statistics();
    
    // Configuration de la protection voltage
    static void set_glitch_threshold(double threshold_percent);
    static void set_clamping_threshold(uint32_t threshold_mv);
    static void set_nominal_voltage(double nominal_voltage_v);
    
    // Détection d'attaques spécifiques
    static bool detect_voltage_glitch();
    static bool detect_under_voltage_glitch();
    static bool detect_over_voltage_glitch();
    static bool detect_voltage_spike();
    static bool detect_voltage_dip();
    
    // Activer les protections voltage
    static void enable_voltage_monitoring();
    static void implement_voltage_clamping();
    static void setup_voltage_watchdog();
    
    // Contrôle du clamping
    static bool is_clamping_active();
    static void activate_clamping();
    static void deactivate_clamping();
    
    // Contrôle du watchdog
    static bool is_watchdog_active();
    static void activate_watchdog();
    static void deactivate_watchdog();
    
    // Test de la protection voltage
    static bool test_voltage_glitch_protection();
    
    // Réinitialiser les statistiques
    static void reset_voltage_glitch_statistics();
    
    // Obtenir l'état de santé voltage
    static VoltageHealth get_voltage_health();
    
    // Configuration des chemins
    static void set_voltage_sensor_path(const std::string& path);
    static void set_clamping_status_path(const std::string& path);
    static void set_watchdog_status_path(const std::string& path);
    
    // Nettoyage et arrêt
    static void cleanup();
    
    // Fonctions utilitaires pour la protection voltage
    static double calculate_voltage_risk(const VoltageGlitchSample& sample);
    static bool is_voltage_anomalous(double voltage);
    static void log_voltage_event(const std::string& event, const VoltageGlitchSample& sample);
};

} // namespace hesia

#endif // VOLTAGE_GLITCH_PROTECTION_HPP
