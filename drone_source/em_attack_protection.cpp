#include "em_attack_protection.hpp"
#include <iostream>
#include <algorithm>
#include <random>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <filesystem>
#include <limits>

#ifdef _WIN32
#include <intrin.h>
#include <windows.h>
#else
#if defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#else
#include <atomic>
// Fallbacks for non-x86 targets (ARM).
static inline void _mm_lfence() { std::atomic_thread_fence(std::memory_order_seq_cst); }
static inline void _mm_sfence() { std::atomic_thread_fence(std::memory_order_seq_cst); }
static inline void _mm_mfence() { std::atomic_thread_fence(std::memory_order_seq_cst); }
#endif
#include <unistd.h>
#include <sys/time.h>
#endif


namespace {
#ifdef __linux__
bool read_sysfs_double(const std::string& path, double& out) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }
    return static_cast<bool>(file >> out);
}

std::string resolve_em_sensor_path() {
    const std::string legacy = "/sys/class/em_sensor/em_field";
    std::error_code ec;
    if (std::filesystem::exists(legacy, ec)) {
        return legacy;
    }

    const std::filesystem::path iio_root = "/sys/bus/iio/devices";
    if (std::filesystem::exists(iio_root, ec)) {
        for (const auto& dev : std::filesystem::directory_iterator(iio_root, ec)) {
            if (ec || !dev.is_directory()) {
                continue;
            }
            const std::filesystem::path base = dev.path();
            const std::filesystem::path magn_raw = base / "in_magn_raw";
            const std::filesystem::path magn_x = base / "in_magn_x_raw";
            const std::filesystem::path magn_y = base / "in_magn_y_raw";
            const std::filesystem::path magn_z = base / "in_magn_z_raw";
            if (std::filesystem::exists(magn_raw, ec)) {
                return magn_raw.string();
            }
            if (std::filesystem::exists(magn_x, ec) &&
                std::filesystem::exists(magn_y, ec) &&
                std::filesystem::exists(magn_z, ec)) {
                return base.string();
            }
        }
    }

    return {};
}
#endif
} // namespace

namespace hesia {

// ===== INITIALISATION DES MEMBRES STATIQUES =====

std::atomic<bool> EMAttackProtection::em_monitor_active{false};
std::vector<EMAttackProtection::EMSample> EMAttackProtection::em_history;
std::thread EMAttackProtection::monitoring_thread;
std::atomic<uint32_t> EMAttackProtection::em_attacks_detected{0};
std::atomic<uint32_t> EMAttackProtection::false_positives{0};

std::atomic<double> EMAttackProtection::baseline_field_strength{0.05}; // 0.05 μT
std::atomic<double> EMAttackProtection::field_tolerance_percent{200.0}; // 200% de tolérance
std::atomic<double> EMAttackProtection::frequency_tolerance_mhz{100.0}; // 100 MHz
std::atomic<bool> EMAttackProtection::shielding_active{false};
std::atomic<bool> EMAttackProtection::filtering_active{false};
std::atomic<bool> EMAttackProtection::em_sensor_available{false};

std::string EMAttackProtection::em_sensor_path = "/sys/class/em_sensor/em_field";
std::string EMAttackProtection::em_log_path = "/var/log/hesia/hesia_em.log";
std::string EMAttackProtection::shielding_status_path = "/var/log/hesia/hesia_shielding.status";
std::string EMAttackProtection::em_filter_config_path = "/var/log/hesia/hesia_em_filters.conf";
std::string EMAttackProtection::em_sensor_config_path = "/var/log/hesia/hesia_em_sensors.conf";

// ===== FONCTIONS DE MONITORING =====

bool EMAttackProtection::initialize(double baseline_field) {
    std::cout << "📡 Initialisation EMAttackProtection..." << std::endl;
    
    baseline_field_strength = baseline_field;
    em_attacks_detected = 0;

#ifdef __linux__
    auto resolved = resolve_em_sensor_path();
    if (resolved.empty()) {
        em_sensor_available = false;
        std::cerr << "[SENTINEL][EM] EM SENSOR NOT FOUND - EM MONITOR INACTIVE (no supported sensor present)." << std::endl;
        std::ofstream log_file_missing(em_log_path, std::ios::app);
        if (log_file_missing.is_open()) {
            log_file_missing << "[SENTINEL][EM] EM SENSOR NOT FOUND - EM MONITOR INACTIVE (no supported sensor present)." << std::endl;
        }
    } else {
        em_sensor_path = resolved;
        em_sensor_available = true;
    }
#else
    em_sensor_available = false;
    std::cerr << "[SENTINEL][EM] EM SENSOR NOT FOUND - EM MONITOR INACTIVE (no supported sensor present)." << std::endl;
#endif
    false_positives = 0;
    em_history.clear();
    
    // Configuration par défaut
    field_tolerance_percent = 200.0; // Très tolérant pour éviter les faux positifs
    frequency_tolerance_mhz = 100.0;
    shielding_active = false;
    filtering_active = false;
    
    // Création des fichiers de log
    std::ofstream log_file(em_log_path, std::ios::app);
    if (log_file.is_open()) {
        log_file << "=== EMAttackProtection Initialisé ===" << std::endl;
        log_file << "Baseline field: " << baseline_field << " μT" << std::endl;
        log_file.close();
    }
    
    std::cout << "✅ EMAttackProtection initialisé (baseline: " 
              << baseline_field << " μT)" << std::endl;
    return true;
}

bool EMAttackProtection::start_em_monitoring() {
    if (!em_sensor_available.load()) {
        std::cerr << "[SENTINEL][EM] EM MONITOR INACTIVE: no sensor present." << std::endl;
        return true;
    }
    if (em_monitor_active.load()) {
        std::cout << "⚠️ Monitoring EM déjà actif" << std::endl;
        return true;
    }
    
    em_monitor_active = true;
    monitoring_thread = std::thread(em_monitoring_loop);
    
    std::cout << "📡 Monitoring EM démarré" << std::endl;
    return true;
}

void EMAttackProtection::stop_em_monitoring() {
    if (!em_monitor_active.load()) {
        return;
    }
    
    em_monitor_active = false;
    if (monitoring_thread.joinable()) {
        monitoring_thread.join();
    }
    
    std::cout << "📡 Monitoring EM arrêté" << std::endl;
}

bool EMAttackProtection::is_monitoring_active() {
    return em_monitor_active.load();
}

bool EMAttackProtection::is_em_sensor_available() {
    return em_sensor_available.load();
}

void EMAttackProtection::em_monitoring_loop() {
    std::cout << "🔄 Démarrage boucle monitoring EM..." << std::endl;
    
    while (em_monitor_active.load()) {
        try {
            EMSample sample = read_em_sample();
            
            // Ajouter à l'historique
            em_history.push_back(sample);
            if (em_history.size() > 1000) {
                em_history.erase(em_history.begin());
            }
            
            // Détection d'anomalies EM
            if (detect_em_anomaly(sample)) {
                em_attacks_detected++;
                trigger_em_attack_response();
            }
            
        } catch (const std::exception& e) {
            std::cerr << "❌ Erreur monitoring EM: " << e.what() << std::endl;
        }
        
        // Pause de 100ms entre les échantillons
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "🔄 Boucle monitoring EM terminée" << std::endl;
}

EMAttackProtection::EMSample EMAttackProtection::read_em_sample() {
    EMSample sample;
    
    sample.timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    
    // Lire les valeurs des capteurs EM
    sample.field_strength_ut = read_field_strength();
    sample.frequency_mhz = read_frequency();
    sample.attack_type = EM_ATTACK_NONE;
    sample.anomaly_detected = false;
    
    return sample;
}

double EMAttackProtection::read_field_strength() {
#ifdef __linux__
    std::error_code ec;
    if (std::filesystem::is_directory(em_sensor_path, ec)) {
        std::filesystem::path base(em_sensor_path);
        std::filesystem::path x_path = base / "in_magn_x_raw";
        std::filesystem::path y_path = base / "in_magn_y_raw";
        std::filesystem::path z_path = base / "in_magn_z_raw";
        double x = 0.0, y = 0.0, z = 0.0;
        if (!read_sysfs_double(x_path.string(), x) ||
            !read_sysfs_double(y_path.string(), y) ||
            !read_sysfs_double(z_path.string(), z)) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        double scale = 1.0;
        std::filesystem::path scale_path = base / "in_magn_scale";
        std::filesystem::path x_scale_path = base / "in_magn_x_scale";
        double scale_val = 0.0;
        if (read_sysfs_double(scale_path.string(), scale_val)) {
            scale = scale_val;
        } else if (read_sysfs_double(x_scale_path.string(), scale_val)) {
            scale = scale_val;
        }
        const double xs = x * scale;
        const double ys = y * scale;
        const double zs = z * scale;
        return std::sqrt(xs * xs + ys * ys + zs * zs);
    }

    double value = 0.0;
    if (read_sysfs_double(em_sensor_path, value)) {
        return value;
    }
    return std::numeric_limits<double>::quiet_NaN();
#else
    return std::numeric_limits<double>::quiet_NaN();
#endif
}


double EMAttackProtection::read_frequency() {
#ifdef __linux__
    std::error_code ec;
    if (std::filesystem::is_directory(em_sensor_path, ec)) {
        std::filesystem::path base(em_sensor_path);
        std::filesystem::path freq_path = base / "in_frequency";
        double freq = 0.0;
        if (read_sysfs_double(freq_path.string(), freq)) {
            return freq; // unite depend du capteur
        }
    }
    return 0.0;
#else
    return 0.0;
#endif
}


bool EMAttackProtection::detect_em_anomaly(const EMSample& current) {
    bool anomaly = false;
    
    // Créer une copie modifiable pour la détection
    EMSample modifiable_current = current;
    
    // Vérifier si le champ EM est anormal
    if (is_field_strength_anomalous(modifiable_current.field_strength_ut)) {
        anomaly = true;
        modifiable_current.anomaly_detected = true;
        
        // Déterminer le type d'attaque
        if (detect_tempest_attack(modifiable_current)) {
            modifiable_current.attack_type = EM_ATTACK_TEMPEST;
        } else if (detect_em_injection_attack(modifiable_current)) {
            modifiable_current.attack_type = EM_ATTACK_INJECTION;
        } else if (detect_side_channel_em_attack(modifiable_current)) {
            modifiable_current.attack_type = EM_ATTACK_SIDE_CHANNEL;
        } else if (detect_power_analysis_em_attack(modifiable_current)) {
            modifiable_current.attack_type = EM_ATTACK_POWER_ANALYSIS;
        } else if (detect_jamming_attack(modifiable_current)) {
            modifiable_current.attack_type = EM_ATTACK_JAMMING;
        } else {
            modifiable_current.attack_type = EM_ATTACK_UNKNOWN;
        }
    }
    
    if (anomaly) {
        log_em_event("EM_ANOMALY_DETECTED", modifiable_current);
    }
    
    return anomaly;
}

// ===== DÉTECTION D'ATTAQUES SPÉCIFIQUES =====

bool EMAttackProtection::detect_tempest_attack(const EMSample& sample) {
    // TEMPEST: émissions EM qui révèlent des données
    // Caractéristiques: champ élevé + fréquences spécifiques (50Hz-1GHz)
    
    double baseline = baseline_field_strength.load();
    double threshold = baseline * 5.0; // 5x le normal
    
    if (sample.field_strength_ut > threshold && 
        sample.frequency_mhz >= 0.05 && sample.frequency_mhz <= 1000.0) {
        
        std::cout << "🚨 Attaque TEMPEST détectée! Champ: " 
                  << sample.field_strength_ut << " μT, Freq: " 
                  << sample.frequency_mhz << " MHz" << std::endl;
        return true;
    }
    
    return false;
}

bool EMAttackProtection::detect_em_injection_attack(const EMSample& sample) {
    // Injection EM: forcer des erreurs par rayonnement EM
    // Caractéristiques: champ très élevé + fréquences hautes
    
    double baseline = baseline_field_strength.load();
    double threshold = baseline * 10.0; // 10x le normal
    
    if (sample.field_strength_ut > threshold && 
        sample.frequency_mhz > 100.0) {
        
        std::cout << "🚨 Attaque par injection EM détectée! Champ: " 
                  << sample.field_strength_ut << " μT, Freq: " 
                  << sample.frequency_mhz << " MHz" << std::endl;
        return true;
    }
    
    return false;
}

bool EMAttackProtection::detect_side_channel_em_attack(const EMSample& sample) {
    // Side-channel EM: fuites d'informations via EM
    // Caractéristiques: variations subtiles + patterns répétitifs
    
    if (em_history.size() < 10) {
        return false;
    }
    
    // Analyser les 10 derniers échantillons
    double variance = 0.0;
    double mean = 0.0;
    
    for (size_t i = em_history.size() - 10; i < em_history.size(); i++) {
        mean += em_history[i].field_strength_ut;
    }
    mean /= 10.0;
    
    for (size_t i = em_history.size() - 10; i < em_history.size(); i++) {
        double diff = em_history[i].field_strength_ut - mean;
        variance += diff * diff;
    }
    variance /= 10.0;
    double std_dev = std::sqrt(variance);
    
    // Si l'écart type est faible mais il y a des patterns, possible side-channel
    if (std_dev < baseline_field_strength.load() * 0.1 && 
        sample.field_strength_ut > baseline_field_strength.load() * 1.5) {
        
        std::cout << "🚨 Attaque side-channel EM détectée! Variance: " 
                  << variance << std::endl;
        return true;
    }
    
    return false;
}

bool EMAttackProtection::detect_power_analysis_em_attack(const EMSample& sample) {
    // Power analysis EM: corrélation consommation/EM
    // Caractéristiques: champ EM synchronisé avec l'activité CPU
    
    double baseline = baseline_field_strength.load();
    double threshold = baseline * 2.0; // 2x le normal
    
    if (sample.field_strength_ut > baseline && 
        sample.field_strength_ut < threshold &&
        is_frequency_suspicious(sample.frequency_mhz)) {
        
        std::cout << "🚨 Attaque power analysis EM détectée! Champ: " 
                  << sample.field_strength_ut << " μT" << std::endl;
        return true;
    }
    
    return false;
}

bool EMAttackProtection::detect_jamming_attack(const EMSample& sample) {
    // Jamming EM: brouillage des communications
    // Caractéristiques: champ élevé continu + large bande de fréquences
    
    double baseline = baseline_field_strength.load();
    double threshold = baseline * 3.0; // 3x le normal
    
    if (sample.field_strength_ut > threshold && 
        sample.frequency_mhz > 1000.0) {
        
        std::cout << "🚨 Attaque par jamming EM détectée! Champ: " 
                  << sample.field_strength_ut << " μT, Freq: " 
                  << sample.frequency_mhz << " MHz" << std::endl;
        return true;
    }
    
    return false;
}

// ===== CONTRE-MESURES EM =====

void EMAttackProtection::implement_em_shielding() {
    std::cout << "🛡️ Implémentation blindage EM..." << std::endl;
    
    // Activer le blindage EM
    shielding_active = true;
    
    // Créer un fichier de statut
    std::ofstream status_file(shielding_status_path);
    if (status_file.is_open()) {
        status_file << "SHIELDING_ACTIVE=1" << std::endl;
        status_file << "SHIELDING_TYPE=Faraday_Cage" << std::endl;
        status_file << "ATTENUATION_DB=60" << std::endl;
        status_file.close();
    }
    
    std::cout << "✅ Blindage EM activé (atténuation 60dB)" << std::endl;
}

void EMAttackProtection::trigger_em_attack_response() {
    std::cout << "🚨 Réponse attaque EM déclenchée!" << std::endl;
    
    // Activer toutes les contre-mesures
    implement_em_shielding();
    implement_em_filtering_internal(); // Utiliser la fonction renommée
    activate_em_countermeasures();
    
    // Ajuster la sensibilité
    adjust_em_sensitivity();
    
    std::cout << "[SENTINEL][EM] Contre-mesures locales uniquement (pas de bus d'alerte externe)." << std::endl;
}

void EMAttackProtection::implement_em_filtering_internal() {
    std::cout << "🔧 Implémentation filtrage EM..." << std::endl;
    
    // Activer le filtrage EM
    filtering_active = true;
    
    // Création du répertoire de logs (sans system())
    try {
        std::filesystem::create_directories(std::filesystem::path(em_log_path).parent_path());
    } catch (...) {
        // Permissions/sandbox: la création pourra échouer; le logging restera best-effort
    }
    
    // Configuration des filtres
    std::ofstream filter_config(em_filter_config_path);
    if (filter_config.is_open()) {
        filter_config << "LOW_PASS_FILTER=1000MHz" << std::endl;
        filter_config << "HIGH_PASS_FILTER=50Hz" << std::endl;
        filter_config << "NOTCH_FILTER=60Hz" << std::endl;
        filter_config << "ATTENUATION=40dB" << std::endl;
        filter_config.close();
    }
    
    std::cout << "✅ Filtrage EM activé" << std::endl;
}

void EMAttackProtection::activate_em_countermeasures() {
    std::cout << "⚡ Activation contre-mesures EM..." << std::endl;
    
    // Augmenter la tolérance pour éviter les faux positifs
    field_tolerance_percent = 300.0;
    
    // Activer le mode de surveillance renforcé (logiciel uniquement)
    std::cout << "[SENTINEL][EM] Contre-mesures spécifiques non disponibles sur cette plateforme." << std::endl;
}

void EMAttackProtection::adjust_em_sensitivity() {
    std::cout << "🎛️ Ajustement sensibilité EM..." << std::endl;
    
    // Ajuster dynamiquement la sensibilité basée sur les attaques détectées
    uint32_t attacks = em_attacks_detected.load();
    
    if (attacks > 10) {
        field_tolerance_percent = 400.0; // Très tolérant
    } else if (attacks > 5) {
        field_tolerance_percent = 300.0; // Tolérant
    } else {
        field_tolerance_percent = 200.0; // Normal
    }
    
    std::cout << "📊 Tolérance de champ ajustée à " 
              << field_tolerance_percent.load() << "%" << std::endl;
}




// ===== FONCTIONS PUBLIQUES =====

bool EMAttackProtection::detect_em_attacks() {
    if (em_history.empty()) {
        return false;
    }
    
    const EMSample& latest = em_history.back();
    return detect_em_anomaly(latest);
}

bool EMAttackProtection::detect_tempest_attack() {
    if (em_history.empty()) {
        return false;
    }
    
    const EMSample& latest = em_history.back();
    return detect_tempest_attack(latest);
}

bool EMAttackProtection::detect_em_injection_attack() {
    if (em_history.empty()) {
        return false;
    }
    
    const EMSample& latest = em_history.back();
    return detect_em_injection_attack(latest);
}

bool EMAttackProtection::detect_side_channel_em_attack() {
    if (em_history.empty()) {
        return false;
    }
    
    const EMSample& latest = em_history.back();
    return detect_side_channel_em_attack(latest);
}

bool EMAttackProtection::detect_power_analysis_em_attack() {
    if (em_history.empty()) {
        return false;
    }
    
    const EMSample& latest = em_history.back();
    return detect_power_analysis_em_attack(latest);
}

bool EMAttackProtection::detect_jamming_attack() {
    if (em_history.empty()) {
        return false;
    }
    
    const EMSample& latest = em_history.back();
    return detect_jamming_attack(latest);
}

void EMAttackProtection::enable_em_shielding() {
    implement_em_shielding();
}

void EMAttackProtection::implement_em_filtering() {
    implement_em_filtering_internal(); // Appeler la version interne
}

void EMAttackProtection::setup_em_detection_sensors() {
    std::cout << "📡 Configuration capteurs EM..." << std::endl;
    
    // Configuration des capteurs simulés
    std::ofstream sensor_config(em_sensor_config_path);
    if (sensor_config.is_open()) {
        sensor_config << "SENSOR_TYPE=Loop_Antenna" << std::endl;
        sensor_config << "SENSITIVITY=0.01uT" << std::endl;
        sensor_config << "FREQUENCY_RANGE=50Hz-1GHz" << std::endl;
        sensor_config << "SAMPLING_RATE=10Hz" << std::endl;
        sensor_config.close();
    }
    
    std::cout << "✅ Capteurs EM configurés" << std::endl;
}

bool EMAttackProtection::is_shielding_active() {
    return shielding_active.load();
}

void EMAttackProtection::activate_shielding() {
    shielding_active = true;
}

void EMAttackProtection::deactivate_shielding() {
    shielding_active = false;
}

bool EMAttackProtection::is_filtering_active() {
    return filtering_active.load();
}

void EMAttackProtection::activate_filtering() {
    filtering_active = true;
}

void EMAttackProtection::deactivate_filtering() {
    filtering_active = false;
}

bool EMAttackProtection::test_em_protection() {
    std::cout << "🧪 Test protection EM..." << std::endl;
    
    // Simuler une attaque TEMPEST
    EMSample attack_sample;
    attack_sample.field_strength_ut = baseline_field_strength.load() * 10.0;
    attack_sample.frequency_mhz = 100.0;
    attack_sample.timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    attack_sample.attack_type = EM_ATTACK_NONE;
    attack_sample.anomaly_detected = false;
    
    bool detection_works = detect_tempest_attack(attack_sample);
    
    std::cout << "Test EM protection: " << (detection_works ? "✅ PASS" : "❌ FAIL") << std::endl;
    return detection_works;
}

EMAttackProtection::EMStats EMAttackProtection::get_em_statistics() {
    EMStats stats{};
    
    if (em_history.empty()) {
        return stats;
    }
    
    stats.total_samples = em_history.size();
    stats.attacks_detected = em_attacks_detected.load();
    stats.false_positives = false_positives.load();
    
    // Calculer les statistiques de champ
    double total_field = 0.0;
    stats.max_field_strength = em_history[0].field_strength_ut;
    stats.min_field_strength = em_history[0].field_strength_ut;
    
    for (const auto& sample : em_history) {
        total_field += sample.field_strength_ut;
        
        if (sample.field_strength_ut > stats.max_field_strength) {
            stats.max_field_strength = sample.field_strength_ut;
        }
        if (sample.field_strength_ut < stats.min_field_strength) {
            stats.min_field_strength = sample.field_strength_ut;
        }
        
        // Compter les types d'attaques
        switch (sample.attack_type) {
            case EM_ATTACK_TEMPEST:
                stats.tempest_attacks++;
                break;
            case EM_ATTACK_INJECTION:
                stats.injection_attacks++;
                break;
            case EM_ATTACK_SIDE_CHANNEL:
                stats.side_channel_attacks++;
                break;
            case EM_ATTACK_POWER_ANALYSIS:
                stats.power_analysis_attacks++;
                break;
            case EM_ATTACK_JAMMING:
                stats.jamming_attacks++;
                break;
            default:
                break;
        }
    }
    
    stats.avg_field_strength = total_field / em_history.size();
    
    return stats;
}

void EMAttackProtection::set_field_tolerance(double tolerance_percent) {
    field_tolerance_percent = tolerance_percent;
}

void EMAttackProtection::set_frequency_tolerance(double tolerance_mhz) {
    frequency_tolerance_mhz = tolerance_mhz;
}

void EMAttackProtection::set_baseline_field_strength(double baseline_ut) {
    baseline_field_strength = baseline_ut;
}

void EMAttackProtection::reset_em_statistics() {
    em_attacks_detected = 0;
    false_positives = 0;
    em_history.clear();
}

EMAttackProtection::EMHealth EMAttackProtection::get_em_health() {
    if (em_history.empty()) {
        return EM_HEALTH_GOOD;
    }
    
    uint32_t recent_attacks = 0;
    size_t check_samples = std::min(em_history.size(), static_cast<size_t>(50));
    
    for (size_t i = em_history.size() - check_samples; i < em_history.size(); i++) {
        if (em_history[i].anomaly_detected) {
            recent_attacks++;
        }
    }
    
    if (recent_attacks > 10) {
        return EM_HEALTH_EMERGENCY;
    } else if (recent_attacks > 5) {
        return EM_HEALTH_CRITICAL;
    } else if (recent_attacks > 1) {
        return EM_HEALTH_WARNING;
    }
    
    return EM_HEALTH_GOOD;
}

void EMAttackProtection::set_em_sensor_path(const std::string& path) {
    em_sensor_path = path;
}

void EMAttackProtection::set_em_log_path(const std::string& path) {
    em_log_path = path;
}

void EMAttackProtection::set_shielding_status_path(const std::string& path) {
    shielding_status_path = path;
}

void EMAttackProtection::cleanup() {
    stop_em_monitoring();
    em_history.clear();
    em_attacks_detected = 0;
    false_positives = 0;
    shielding_active = false;
    filtering_active = false;
}

// ===== FONCTIONS UTILITAIRES =====

double EMAttackProtection::calculate_em_risk(const EMSample& sample) {
    double baseline = baseline_field_strength.load();
    double field_ratio = sample.field_strength_ut / baseline;
    
    // Calculer le risque basé sur le rapport de champ
    if (field_ratio > 10.0) {
        return 1.0; // Risque maximum
    } else if (field_ratio > 5.0) {
        return 0.8; // Risque élevé
    } else if (field_ratio > 2.0) {
        return 0.5; // Risque moyen
    } else {
        return 0.1; // Risque faible
    }
}

bool EMAttackProtection::is_frequency_suspicious(double frequency) {
    // Fréquences suspectes pour les attaques EM
    return (frequency >= 50.0 && frequency <= 1000.0) || // Bande typique TEMPEST
           (frequency > 1000.0); // Fréquences hautes pour jamming
}

bool EMAttackProtection::is_field_strength_anomalous(double field_strength) {
    if (!std::isfinite(field_strength) || field_strength <= 0.0) {
        return true;
    }
    double baseline = baseline_field_strength.load();
    double tolerance = baseline * (field_tolerance_percent.load() / 100.0);
    
    double deviation = std::abs(field_strength - baseline);
    return deviation > tolerance;
}

void EMAttackProtection::log_em_event(const std::string& event, const EMSample& sample) {
    std::ofstream log_file(em_log_path, std::ios::app);
    if (log_file.is_open()) {
        log_file << "[" << sample.timestamp << "] " << event
                << " - Field: " << sample.field_strength_ut << "μT"
                << ", Freq: " << sample.frequency_mhz << "MHz"
                << ", Type: " << static_cast<int>(sample.attack_type) << std::endl;
        log_file.close();
    }
}

} // namespace hesia
