#ifndef EM_ATTACK_PROTECTION_HPP
#define EM_ATTACK_PROTECTION_HPP

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
#else
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#endif

namespace hesia {

// ===== PROTECTION CONTRE LES ATTAQUES ÉLECTROMAGNÉTIQUES (EM) =====

class EMAttackProtection {
private:
    struct EMSample {
        double field_strength_ut;  // Microteslas
        double frequency_mhz;     // MHz
        uint64_t timestamp;
        uint8_t attack_type;
        mutable bool anomaly_detected; // mutable pour permettre la modification
    };
    
    static std::atomic<bool> em_monitor_active;
    static std::vector<EMSample> em_history;
    static std::thread monitoring_thread;
    static std::atomic<uint32_t> em_attacks_detected;
    static std::atomic<uint32_t> false_positives;
    
    // Configuration de la protection EM
    static std::atomic<double> baseline_field_strength;
    static std::atomic<double> field_tolerance_percent;
    static std::atomic<double> frequency_tolerance_mhz;
    static std::atomic<bool> shielding_active;
    static std::atomic<bool> filtering_active;
    static std::atomic<bool> em_sensor_available;
    
    // Fichiers de monitoring EM
    static std::string em_sensor_path;
    static std::string em_log_path;
    static std::string shielding_status_path;
    static std::string em_filter_config_path;
    static std::string em_sensor_config_path;
    
    // Fonctions de monitoring
    static void em_monitoring_loop();
    static EMSample read_em_sample();
    static double read_field_strength();
    static double read_frequency();
    static bool detect_em_anomaly(const EMSample& current);
    
    // Détection d'attaques EM spécifiques
    static bool detect_tempest_attack(const EMSample& sample);
    static bool detect_em_injection_attack(const EMSample& sample);
    static bool detect_side_channel_em_attack(const EMSample& sample);
    static bool detect_power_analysis_em_attack(const EMSample& sample);
    static bool detect_jamming_attack(const EMSample& sample);
    
    // Contre-mesures EM
    static void implement_em_shielding();
    static void implement_em_filtering_internal(); // Renommé pour éviter conflit
    static void trigger_em_attack_response();
    static void activate_em_countermeasures();
    static void adjust_em_sensitivity();
    
    // Simulation de capteurs EM (retirée)
    
public:
    // Initialisation de la protection EM
    static bool initialize(double baseline_field = 0.05); // 0.05 μT typique
    
    // Démarrer le monitoring EM
    static bool start_em_monitoring();
    
    // Arrêter le monitoring
    static void stop_em_monitoring();
    
    // Vérifier si le monitoring est actif
    static bool is_monitoring_active();
    static bool is_em_sensor_available();
    
    // Types pour les statistiques EM
    struct EMStats {
        uint32_t total_samples;
        uint32_t attacks_detected;
        uint32_t false_positives;
        double avg_field_strength;
        double max_field_strength;
        double min_field_strength;
        uint32_t tempest_attacks;
        uint32_t injection_attacks;
        uint32_t side_channel_attacks;
        uint32_t power_analysis_attacks;
        uint32_t jamming_attacks;
    };
    
    enum EMAttackType {
        EM_ATTACK_NONE = 0,
        EM_ATTACK_TEMPEST = 1,
        EM_ATTACK_INJECTION = 2,
        EM_ATTACK_SIDE_CHANNEL = 3,
        EM_ATTACK_POWER_ANALYSIS = 4,
        EM_ATTACK_JAMMING = 5,
        EM_ATTACK_UNKNOWN = 6
    };
    
    enum EMHealth {
        EM_HEALTH_GOOD = 0,
        EM_HEALTH_WARNING = 1,
        EM_HEALTH_CRITICAL = 2,
        EM_HEALTH_EMERGENCY = 3
    };
    
    // Obtenir les statistiques EM
    static EMStats get_em_statistics();
    
    // Configuration de la protection EM
    static void set_field_tolerance(double tolerance_percent);
    static void set_frequency_tolerance(double tolerance_mhz);
    static void set_baseline_field_strength(double baseline_ut);
    
    // Détection d'attaques spécifiques
    static bool detect_em_attacks();
    static bool detect_tempest_attack();
    static bool detect_em_injection_attack();
    static bool detect_side_channel_em_attack();
    static bool detect_power_analysis_em_attack();
    static bool detect_jamming_attack();
    
    // Activer les protections EM
    static void enable_em_shielding();
    static void implement_em_filtering();
    static void setup_em_detection_sensors();
    
    // Contrôle du blindage
    static bool is_shielding_active();
    static void activate_shielding();
    static void deactivate_shielding();
    
    // Contrôle du filtrage
    static bool is_filtering_active();
    static void activate_filtering();
    static void deactivate_filtering();
    
    // Test de la protection EM
    static bool test_em_protection();
    
    // Réinitialiser les statistiques
    static void reset_em_statistics();
    
    // Obtenir l'état de santé EM
    static EMHealth get_em_health();
    
    // Configuration des chemins
    static void set_em_sensor_path(const std::string& path);
    static void set_em_log_path(const std::string& path);
    static void set_shielding_status_path(const std::string& path);
    
    // Nettoyage et arrêt
    static void cleanup();
    
    // Fonctions utilitaires pour la protection EM
    static double calculate_em_risk(const EMSample& sample);
    static bool is_frequency_suspicious(double frequency);
    static bool is_field_strength_anomalous(double field_strength);
    static void log_em_event(const std::string& event, const EMSample& sample);
};

} // namespace hesia

#endif // EM_ATTACK_PROTECTION_HPP
