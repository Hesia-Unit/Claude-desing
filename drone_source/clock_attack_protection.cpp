#include "clock_attack_protection.hpp"
#include "hardware_monitor.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <random>
#include <chrono>

#ifdef _WIN32
#include <intrin.h>
#else
#if defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#include <cpuid.h>
#else
#include <ctime>
#include <atomic>
// Fallback for non-x86 targets: use monotonic clock as a pseudo-TSC.
static inline uint64_t __rdtsc() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL +
           static_cast<uint64_t>(ts.tv_nsec);
}
#endif
#endif

namespace hesia {

// ===== INITIALISATION DES MEMBRES STATIQUES =====

std::atomic<bool> ClockAttackProtection::clock_monitor_active{false};
std::vector<ClockAttackProtection::ClockSample> ClockAttackProtection::clock_history;
std::thread ClockAttackProtection::monitoring_thread;
std::atomic<double> ClockAttackProtection::nominal_frequency{2400.0};
std::atomic<double> ClockAttackProtection::frequency_tolerance_percent{5.0};
std::atomic<uint64_t> ClockAttackProtection::jitter_threshold_ns{1000};
std::atomic<uint32_t> ClockAttackProtection::anomaly_count{0};
std::atomic<bool> ClockAttackProtection::clock_source_secure{false};

std::string ClockAttackProtection::cpuinfo_path = "/proc/cpuinfo";
std::string ClockAttackProtection::time_path = "/proc/uptime";
std::string ClockAttackProtection::clock_source_path = "/sys/devices/system/clocksource/clocksource0/current_clocksource";

// ===== FONCTIONS DE MONITORING =====

bool ClockAttackProtection::initialize(double nominal_freq_mhz) {
    std::cout << "🕐 Initialisation ClockAttackProtection..." << std::endl;
    
    nominal_frequency = nominal_freq_mhz;
    anomaly_count = 0;
    clock_history.clear();
    
    // Calibration de la fréquence nominale
    if (!calibrate_nominal_frequency()) {
        std::cerr << "❌ Échec calibration fréquence nominale" << std::endl;
        return false;
    }
    
    // Vérification de l'intégrité de la source clock
    if (!verify_clock_source_integrity()) {
        std::cerr << "⚠️ Source clock non sécurisée détectée" << std::endl;
    }
    
    std::cout << "✅ ClockAttackProtection initialisé (freq nominale: " 
              << nominal_frequency << " MHz)" << std::endl;
    return true;
}

bool ClockAttackProtection::start_clock_monitoring() {
    if (clock_monitor_active.load()) {
        std::cout << "⚠️ Monitoring clock déjà actif" << std::endl;
        return true;
    }
    
    clock_monitor_active = true;
    monitoring_thread = std::thread(clock_monitoring_loop);
    
    std::cout << "🕐 Monitoring clock démarré" << std::endl;
    return true;
}

void ClockAttackProtection::stop_clock_monitoring() {
    if (!clock_monitor_active.load()) {
        return;
    }
    
    clock_monitor_active = false;
    if (monitoring_thread.joinable()) {
        monitoring_thread.join();
    }
    
    std::cout << "🕐 Monitoring clock arrêté" << std::endl;
}

bool ClockAttackProtection::is_monitoring_active() {
    return clock_monitor_active.load();
}

bool ClockAttackProtection::is_clock_source_secure() {
    return clock_source_secure.load();
}

void ClockAttackProtection::clock_monitoring_loop() {
    std::cout << "🔄 Démarrage boucle monitoring clock..." << std::endl;
    
    while (clock_monitor_active.load()) {
        try {
            ClockSample sample = read_clock_sample();
            
            // Ajouter à l'historique
            clock_history.push_back(sample);
            if (clock_history.size() > 1000) {
                clock_history.erase(clock_history.begin());
            }
            
            // Détection d'anomalies
            if (detect_frequency_manipulation(sample)) {
                anomaly_count++;
                trigger_clock_attack_response();
            }
            
            if (detect_clock_jamming(sample)) {
                anomaly_count++;
                implement_clock_stabilization();
            }
            
            if (detect_clock_glitch(sample)) {
                anomaly_count++;
                enable_secure_clock_source();
            }
            
        } catch (const std::exception& e) {
            std::cerr << "❌ Erreur monitoring clock: " << e.what() << std::endl;
        }

        if (ExternalWatchdog::is_active()) {
            ExternalWatchdog::pet_watchdog();
        }
        
        // Pause de 100ms entre les échantillons
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "🔄 Boucle monitoring clock terminée" << std::endl;
}

ClockAttackProtection::ClockSample ClockAttackProtection::read_clock_sample() {
    ClockSample sample;
    
    // Lire la fréquence CPU
    sample.frequency_mhz = read_cpu_frequency();
    
    // Lire le timestamp
    sample.timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    
    // Lire la valeur TSC
    sample.tsc_value = read_tsc_value();
    
    // Calculer le jitter
    sample.jitter_ns = calculate_jitter(sample);
    
    return sample;
}

double ClockAttackProtection::read_cpu_frequency() {
#ifdef _WIN32
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    return freq.QuadPart / 1000000.0; // Convertir en MHz
#else
    // Lire depuis /proc/cpuinfo
    std::ifstream cpuinfo(cpuinfo_path);
    std::string line;
    double freq = 0.0;
    
    while (std::getline(cpuinfo, line)) {
        if (line.find("cpu MHz") != std::string::npos) {
            size_t colon = line.find(':');
            if (colon != std::string::npos) {
                freq = std::stod(line.substr(colon + 1));
                break;
            }
        }
    }
    
    if (freq == 0.0) {
        // Alternative: utiliser TSC
        uint64_t tsc_start = __rdtsc();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        uint64_t tsc_end = __rdtsc();
        
        freq = ((tsc_end - tsc_start) / 100000.0); // MHz
    }
    
    return freq;
#endif
}

uint64_t ClockAttackProtection::read_tsc_value() {
    return __rdtsc();
}

double ClockAttackProtection::calculate_jitter(const ClockSample& current) {
    if (clock_history.empty()) {
        return 0.0;
    }
    
    const ClockSample& previous = clock_history.back();
    
    // Calculer le jitter basé sur la différence de fréquence
    double freq_diff = std::abs(current.frequency_mhz - previous.frequency_mhz);
    double time_diff = (current.timestamp - previous.timestamp) / 1000000000.0; // secondes
    
    return (freq_diff / time_diff) * 1000.0; // nanosecondes
}

// ===== DÉTECTION D'ATTAQUES =====

bool ClockAttackProtection::detect_frequency_manipulation(const ClockSample& current) {
    double nominal = nominal_frequency.load();
    double tolerance = nominal * (frequency_tolerance_percent.load() / 100.0);
    
    double deviation = std::abs(current.frequency_mhz - nominal);
    
    if (deviation > tolerance) {
        std::cout << "🚨 Manipulation fréquence détectée: " 
                  << current.frequency_mhz << " MHz (deviation: " 
                  << deviation << " MHz)" << std::endl;
        return true;
    }
    
    return false;
}

bool ClockAttackProtection::detect_clock_jamming(const ClockSample& current) {
    if (clock_history.size() < 10) {
        return false;
    }
    
    // Analyser les 10 derniers échantillons
    double avg_jitter = 0.0;
    for (size_t i = clock_history.size() - 10; i < clock_history.size(); i++) {
        avg_jitter += clock_history[i].jitter_ns;
    }
    avg_jitter /= 10.0;
    
    if (avg_jitter > jitter_threshold_ns.load()) {
        std::cout << "🚨 Clock jamming détecté (jitter moyen: " 
                  << avg_jitter << " ns)" << std::endl;
        return true;
    }
    
    return false;
}

bool ClockAttackProtection::detect_clock_glitch(const ClockSample& current) {
    if (clock_history.size() < 2) {
        return false;
    }
    
    const ClockSample& previous = clock_history[clock_history.size() - 2];
    
    // Détection de saut soudain de fréquence
    double freq_change = std::abs(current.frequency_mhz - previous.frequency_mhz);
    double time_diff = (current.timestamp - previous.timestamp) / 1000000.0; // ms
    
    if (time_diff > 0 && freq_change / time_diff > 100.0) { // 100 MHz/ms
        std::cout << "🚨 Clock glitch détecté (changement: " 
                  << freq_change << " MHz en " << time_diff << " ms)" << std::endl;
        return true;
    }
    
    return false;
}

bool ClockAttackProtection::detect_underclock_attack(const ClockSample& current) {
    double nominal = nominal_frequency.load();
    double threshold = nominal * 0.8; // 80% de la nominale
    
    if (current.frequency_mhz < threshold) {
        std::cout << "🚨 Underclock attack détectée: " 
                  << current.frequency_mhz << " MHz" << std::endl;
        return true;
    }
    
    return false;
}

bool ClockAttackProtection::detect_overclock_attack(const ClockSample& current) {
    double nominal = nominal_frequency.load();
    double threshold = nominal * 1.2; // 120% de la nominale
    
    if (current.frequency_mhz > threshold) {
        std::cout << "🚨 Overclock attack détectée: " 
                  << current.frequency_mhz << " MHz" << std::endl;
        return true;
    }
    
    return false;
}

// ===== CONTRE-MESURES =====

void ClockAttackProtection::implement_clock_stabilization() {
    std::cout << "🛡️ Implémentation stabilisation clock..." << std::endl;
    
    // Activer les contre-mesures de stabilisation
#ifdef _WIN32
    // Windows: ajuster les paramètres de performance
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
#else
    // Linux: ajuster la politique du scheduler
    struct sched_param param;
    param.sched_priority = 99;
    sched_setscheduler(0, SCHED_FIFO, &param);
#endif
    
    // Ajouter du bruit pour contrer les attaques
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> delay_dist(1, 10);
    
    for (int i = 0; i < 10; i++) {
        std::this_thread::sleep_for(std::chrono::microseconds(delay_dist(gen)));
    }
}

void ClockAttackProtection::enable_secure_clock_source() {
    std::cout << "🔒 Activation source clock sécurisée..." << std::endl;
    
    // Essayer de basculer vers une source clock sécurisée
#ifdef _WIN32
    // Windows: utiliser QueryPerformanceCounter
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    if (freq.QuadPart > 0) {
        std::cout << "✅ Source clock haute résolution activée" << std::endl;
    }
#else
    // Linux: vérifier et utiliser clocksource sécurisée
    std::ifstream clocksource(clock_source_path);
    std::string source;
    if (clocksource >> source) {
        if (source == "tsc" || source == "hpet" || source == "arch_sys_counter" || source == "arm_arch_timer") {
            std::cout << "✅ Source clock sécurisée: " << source << std::endl;
        } else {
            std::cout << "⚠️ Source clock non sécurisée: " << source << std::endl;
        }
    }
#endif
}

void ClockAttackProtection::trigger_clock_attack_response() {
    std::cout << "[CLOCK] Attack response triggered." << std::endl;
    implement_clock_stabilization();
    enable_secure_clock_source();
    std::cout << "[SENTINEL][CLOCK] Local response only (no external hardware integration)." << std::endl;
}


// ===== FONCTIONS PUBLIQUES =====

bool ClockAttackProtection::calibrate_nominal_frequency() {
    std::cout << "⚙️ Calibration fréquence nominale..." << std::endl;
    
    // Prendre 10 échantillons pour calibrer
    double total_freq = 0.0;
    int valid_samples = 0;
    
    for (int i = 0; i < 10; i++) {
        double freq = read_cpu_frequency();
        if (freq > 0.0) {
            total_freq += freq;
            valid_samples++;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    if (valid_samples > 0) {
        double avg_freq = total_freq / valid_samples;
        nominal_frequency = avg_freq;
        std::cout << "✅ Fréquence nominale calibrée: " << avg_freq << " MHz" << std::endl;
        return true;
    }
    
    return false;
}

bool ClockAttackProtection::verify_clock_source_integrity() {
    std::cout << "[CLOCK] Checking clock source integrity..." << std::endl;

#ifdef _WIN32
    LARGE_INTEGER freq;
    bool qpc_available = QueryPerformanceFrequency(&freq) != 0;
    std::cout << "Source clock: " << (qpc_available ? "QPC" : "System") << std::endl;
    clock_source_secure = qpc_available;
    return qpc_available;
#else
    std::ifstream clocksource(clock_source_path);
    std::string source;
    bool secure = false;
    if (clocksource >> source) {
        std::cout << "Source clock actuelle: " << source << std::endl;
        if (source == "tsc" || source == "hpet" || source == "arch_sys_counter" || source == "arm_arch_timer") {
            secure = true;
        }
    }
    clock_source_secure = secure;
    return secure;
#endif
}


bool ClockAttackProtection::detect_clock_manipulation() {
    if (clock_history.empty()) {
        return false;
    }
    
    const ClockSample& current = clock_history.back();
    return detect_frequency_manipulation(current);
}

bool ClockAttackProtection::detect_clock_jamming() {
    if (clock_history.empty()) {
        return false;
    }
    
    const ClockSample& current = clock_history.back();
    return detect_clock_jamming(current);
}

bool ClockAttackProtection::detect_clock_glitch() {
    if (clock_history.empty()) {
        return false;
    }
    
    const ClockSample& current = clock_history.back();
    return detect_clock_glitch(current);
}

void ClockAttackProtection::enable_clock_monitoring() {
    start_clock_monitoring();
}

void ClockAttackProtection::implement_clock_jamming_detection() {
    jitter_threshold_ns = 500; // 500 nanosecondes
    frequency_tolerance_percent = 2.0; // 2% de tolérance
    std::cout << "🛡️ Détection clock jamming configurée" << std::endl;
}

void ClockAttackProtection::setup_secure_clock_source() {
    enable_secure_clock_source();
}

bool ClockAttackProtection::test_clock_protection() {
    std::cout << "🧪 Test protection clock..." << std::endl;
    
    // Test de détection de manipulation
    double original_freq = nominal_frequency.load();
    nominal_frequency = 1000.0; // Simuler manipulation
    
    ClockSample test_sample;
    test_sample.frequency_mhz = 500.0; // Fréquence anormale
    test_sample.timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    
    bool detection_works = detect_frequency_manipulation(test_sample);
    
    nominal_frequency = original_freq; // Restaurer
    
    std::cout << "Test clock protection: " << (detection_works ? "✅ PASS" : "❌ FAIL") << std::endl;
    return detection_works;
}

ClockAttackProtection::ClockStats ClockAttackProtection::get_clock_statistics() {
    ClockStats stats{};
    
    if (clock_history.empty()) {
        return stats;
    }
    
    stats.total_samples = clock_history.size();
    stats.anomaly_count = anomaly_count.load();
    
    double total_freq = 0.0;
    double total_jitter = 0.0;
    stats.min_frequency = clock_history[0].frequency_mhz;
    stats.max_frequency = clock_history[0].frequency_mhz;
    stats.max_jitter = 0;
    
    for (const auto& sample : clock_history) {
        total_freq += sample.frequency_mhz;
        total_jitter += sample.jitter_ns;
        
        if (sample.frequency_mhz < stats.min_frequency) {
            stats.min_frequency = sample.frequency_mhz;
        }
        if (sample.frequency_mhz > stats.max_frequency) {
            stats.max_frequency = sample.frequency_mhz;
        }
        if (sample.jitter_ns > stats.max_jitter) {
            stats.max_jitter = sample.jitter_ns;
        }
    }
    
    stats.avg_frequency = total_freq / clock_history.size();
    stats.avg_jitter = total_jitter / clock_history.size();
    
    return stats;
}

void ClockAttackProtection::set_frequency_tolerance(double tolerance_percent) {
    frequency_tolerance_percent = tolerance_percent;
}

void ClockAttackProtection::set_jitter_threshold(uint64_t threshold_ns) {
    jitter_threshold_ns = threshold_ns;
}

void ClockAttackProtection::set_nominal_frequency(double freq_mhz) {
    nominal_frequency = freq_mhz;
}

void ClockAttackProtection::reset_clock_statistics() {
    anomaly_count = 0;
    clock_history.clear();
}

ClockAttackProtection::ClockHealth ClockAttackProtection::get_clock_health() {
    if (clock_history.empty()) {
        return CLOCK_HEALTH_GOOD;
    }
    
    uint32_t recent_anomalies = 0;
    size_t check_samples = std::min(clock_history.size(), static_cast<size_t>(50));
    
    for (size_t i = clock_history.size() - check_samples; i < clock_history.size(); i++) {
        // Simuler la détection d'anomalies (à implémenter correctement)
        if (detect_frequency_manipulation(clock_history[i])) {
            recent_anomalies++;
        }
    }
    
    if (recent_anomalies > 10) {
        return CLOCK_HEALTH_EMERGENCY;
    } else if (recent_anomalies > 5) {
        return CLOCK_HEALTH_CRITICAL;
    } else if (recent_anomalies > 1) {
        return CLOCK_HEALTH_WARNING;
    }
    
    return CLOCK_HEALTH_GOOD;
}

uint32_t ClockAttackProtection::get_anomaly_count() {
    return anomaly_count.load();
}

bool ClockAttackProtection::verify_clock_integrity() {
    return verify_clock_source_integrity();
}

void ClockAttackProtection::set_cpuinfo_path(const std::string& path) {
    cpuinfo_path = path;
}

void ClockAttackProtection::set_time_path(const std::string& path) {
    time_path = path;
}

void ClockAttackProtection::set_clock_source_path(const std::string& path) {
    clock_source_path = path;
}

void ClockAttackProtection::cleanup() {
    stop_clock_monitoring();
    clock_history.clear();
    anomaly_count = 0;
}

} // namespace hesia
