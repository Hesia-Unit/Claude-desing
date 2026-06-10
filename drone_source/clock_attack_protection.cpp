#include "clock_attack_protection.hpp"
#include "hardware_monitor.hpp"
#include "security_utils.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <random>
#include <chrono>
#include <cstdlib>

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

namespace {

constexpr std::size_t kJammingWindowSamples = 32;
constexpr std::size_t kJammingWarmupSamples = 48;
constexpr double kMinStableFrequencyMhz = 1.0;
constexpr double kStableFrequencySpreadRatio = 0.02;
constexpr double kArmJammingThresholdNs = 50000.0;
constexpr std::uint32_t kRequiredConsecutiveJammingWindows = 3;

std::atomic<double> g_jamming_baseline_ns{0.0};
std::atomic<std::uint32_t> g_consecutive_jamming_windows{0};

uint64_t secure_seed64() {
    uint64_t seed = 0;
    if (!SecureRNG::generate_bytes(reinterpret_cast<uint8_t*>(&seed), sizeof(seed))) {
        throw std::runtime_error("SecureRNG::generate_bytes failed for clock protection seed");
    }
    return seed;
}

bool has_invariant_cycle_counter()
{
#if defined(_WIN32) || defined(__x86_64__) || defined(__i386__)
    return true;
#else
    return false;
#endif
}

void apply_clock_path_env_overrides()
{
    if (const char* path = std::getenv("HESIA_CPUINFO_PATH"); path && *path) {
        ClockAttackProtection::set_cpuinfo_path(path);
    }
    if (const char* path = std::getenv("HESIA_UPTIME_PATH"); path && *path) {
        ClockAttackProtection::set_time_path(path);
    }
    if (const char* path = std::getenv("HESIA_CLOCK_SOURCE_PATH"); path && *path) {
        ClockAttackProtection::set_clock_source_path(path);
    }
}

} // namespace

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
    std::cout << "Ã°Å¸â€¢Â Initialisation ClockAttackProtection..." << std::endl;
    
    apply_clock_path_env_overrides();
    nominal_frequency = nominal_freq_mhz;
    anomaly_count = 0;
    clock_history.clear();
    g_jamming_baseline_ns = 0.0;
    g_consecutive_jamming_windows = 0;
    
    // Calibration de la frÃƒÂ©quence nominale
    if (!calibrate_nominal_frequency()) {
        std::cerr << "Ã¢ÂÅ’ Ãƒâ€°chec calibration frÃƒÂ©quence nominale" << std::endl;
        return false;
    }
    
    // VÃƒÂ©rification de l'intÃƒÂ©gritÃƒÂ© de la source clock
    if (!verify_clock_source_integrity()) {
        std::cerr << "Ã¢Å¡Â Ã¯Â¸Â Source clock non sÃƒÂ©curisÃƒÂ©e dÃƒÂ©tectÃƒÂ©e" << std::endl;
    }
    
    std::cout << "Ã¢Å“â€¦ ClockAttackProtection initialisÃƒÂ© (freq nominale: " 
              << nominal_frequency << " MHz)" << std::endl;
    return true;
}

bool ClockAttackProtection::start_clock_monitoring() {
    if (clock_monitor_active.load()) {
        std::cout << "Ã¢Å¡Â Ã¯Â¸Â Monitoring clock dÃƒÂ©jÃƒÂ  actif" << std::endl;
        return true;
    }
    
    clock_monitor_active = true;
    monitoring_thread = std::thread(clock_monitoring_loop);
    
    std::cout << "Ã°Å¸â€¢Â Monitoring clock dÃƒÂ©marrÃƒÂ©" << std::endl;
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
    
    std::cout << "Ã°Å¸â€¢Â Monitoring clock arrÃƒÂªtÃƒÂ©" << std::endl;
}

bool ClockAttackProtection::is_monitoring_active() {
    return clock_monitor_active.load();
}

bool ClockAttackProtection::is_clock_source_secure() {
    return clock_source_secure.load();
}

void ClockAttackProtection::clock_monitoring_loop() {
    std::cout << "Ã°Å¸â€â€ž DÃƒÂ©marrage boucle monitoring clock..." << std::endl;
    
    while (clock_monitor_active.load()) {
        try {
            ClockSample sample = read_clock_sample();
            
            // Ajouter ÃƒÂ  l'historique
            clock_history.push_back(sample);
            if (clock_history.size() > 1000) {
                clock_history.erase(clock_history.begin());
            }
            
            // DÃƒÂ©tection d'anomalies
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
            std::cerr << "Ã¢ÂÅ’ Erreur monitoring clock: " << e.what() << std::endl;
        }

        if (ExternalWatchdog::is_active()) {
            ExternalWatchdog::pet_watchdog();
        }
        
        // Pause de 100ms entre les ÃƒÂ©chantillons
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "Ã°Å¸â€â€ž Boucle monitoring clock terminÃƒÂ©e" << std::endl;
}

ClockAttackProtection::ClockSample ClockAttackProtection::read_clock_sample() {
    ClockSample sample;
    
    // Lire la frÃƒÂ©quence CPU
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

    {
        const ClockSample& previous_sample = clock_history.back();
        const double time_diff = (current.timestamp - previous_sample.timestamp) / 1000000000.0;
        if (!std::isfinite(time_diff) || time_diff <= 0.0) {
            return 0.0;
        }
#if !defined(_WIN32) && !defined(__x86_64__) && !defined(__i386__)
        // Sur ARM/Jetson, le fallback __rdtsc() n'est pas un compteur invariant.
        // Un jitter dÃƒÂ©rivÃƒÂ© des transitions DVFS remonte des faux positifs massifs.
        (void)current;
        return 0.0;
#else
        const double freq_diff = std::abs(current.frequency_mhz - previous_sample.frequency_mhz);
        return (freq_diff / time_diff) * 1000.0;
#endif
    }
    
    const ClockSample& previous = clock_history.back();
    
    // Calculer le jitter basÃƒÂ© sur la diffÃƒÂ©rence de frÃƒÂ©quence
    double freq_diff = std::abs(current.frequency_mhz - previous.frequency_mhz);
    double time_diff = (current.timestamp - previous.timestamp) / 1000000000.0; // secondes
    
    return (freq_diff / time_diff) * 1000.0; // nanosecondes
}

// ===== DÃƒâ€°TECTION D'ATTAQUES =====

bool ClockAttackProtection::detect_frequency_manipulation(const ClockSample& current) {
    double nominal = nominal_frequency.load();
    double tolerance = nominal * (frequency_tolerance_percent.load() / 100.0);
    
    double deviation = std::abs(current.frequency_mhz - nominal);
    
    if (deviation > tolerance) {
        std::cout << "Ã°Å¸Å¡Â¨ Manipulation frÃƒÂ©quence dÃƒÂ©tectÃƒÂ©e: " 
                  << current.frequency_mhz << " MHz (deviation: " 
                  << deviation << " MHz)" << std::endl;
        return true;
    }
    
    return false;
}

bool ClockAttackProtection::detect_clock_jamming(const ClockSample& current) {
    (void)current;

    if (!has_invariant_cycle_counter()) {
        return false;
    }
    if (clock_history.size() < kJammingWarmupSamples) {
        return false;
    }
    if (!clock_source_secure.load()) {
        g_consecutive_jamming_windows = 0;
        return false;
    }

    const std::size_t start = clock_history.size() - kJammingWindowSamples;
    double min_freq = std::numeric_limits<double>::infinity();
    double max_freq = 0.0;
    double avg_freq = 0.0;
    std::size_t valid = 0;
    for (std::size_t i = start; i < clock_history.size(); ++i) {
        const double freq = clock_history[i].frequency_mhz;
        if (!std::isfinite(freq) || freq < kMinStableFrequencyMhz) {
            continue;
        }
        min_freq = std::min(min_freq, freq);
        max_freq = std::max(max_freq, freq);
        avg_freq += freq;
        ++valid;
    }
    if (valid < kJammingWindowSamples / 2) {
        g_consecutive_jamming_windows = 0;
        return false;
    }

    avg_freq /= static_cast<double>(valid);
    if (!std::isfinite(avg_freq) || avg_freq < kMinStableFrequencyMhz) {
        g_consecutive_jamming_windows = 0;
        return false;
    }

    const double spread_ratio = (max_freq - min_freq) / avg_freq;
    if (spread_ratio > kStableFrequencySpreadRatio) {
        g_consecutive_jamming_windows = 0;
        return false;
    }

    double avg_jitter = 0.0;
    for (std::size_t i = start; i < clock_history.size(); ++i) {
        avg_jitter += clock_history[i].jitter_ns;
    }
    avg_jitter /= static_cast<double>(kJammingWindowSamples);

    double baseline = g_jamming_baseline_ns.load();
    if (baseline <= 0.0 || avg_jitter < baseline) {
        g_jamming_baseline_ns = avg_jitter;
        baseline = avg_jitter;
    } else {
        g_jamming_baseline_ns = (baseline * 0.95) + (avg_jitter * 0.05);
        baseline = g_jamming_baseline_ns.load();
    }

    const double adaptive_threshold = std::max<double>(
        static_cast<double>(jitter_threshold_ns.load()),
        std::max(kArmJammingThresholdNs, baseline * 5.0));

    if (avg_jitter > adaptive_threshold) {
        const std::uint32_t consecutive = g_consecutive_jamming_windows.fetch_add(1) + 1;
        if (consecutive < kRequiredConsecutiveJammingWindows) {
            return false;
        }
        std::cout << "[CLOCK] Jamming detected (avg jitter="
                  << avg_jitter << " ns, baseline=" << baseline
                  << " ns, spread=" << (spread_ratio * 100.0) << "%)" << std::endl;
        return true;
    }

    g_consecutive_jamming_windows = 0;
    return false;
}

bool ClockAttackProtection::detect_clock_glitch(const ClockSample& current) {
    if (clock_history.size() < 2) {
        return false;
    }
    
    const ClockSample& previous = clock_history[clock_history.size() - 2];
    
    // DÃƒÂ©tection de saut soudain de frÃƒÂ©quence
    double freq_change = std::abs(current.frequency_mhz - previous.frequency_mhz);
    double time_diff = (current.timestamp - previous.timestamp) / 1000000.0; // ms
    
    if (time_diff > 0 && freq_change / time_diff > 100.0) { // 100 MHz/ms
        std::cout << "Ã°Å¸Å¡Â¨ Clock glitch dÃƒÂ©tectÃƒÂ© (changement: " 
                  << freq_change << " MHz en " << time_diff << " ms)" << std::endl;
        return true;
    }
    
    return false;
}

bool ClockAttackProtection::detect_underclock_attack(const ClockSample& current) {
    double nominal = nominal_frequency.load();
    double threshold = nominal * 0.8; // 80% de la nominale
    
    if (current.frequency_mhz < threshold) {
        std::cout << "Ã°Å¸Å¡Â¨ Underclock attack dÃƒÂ©tectÃƒÂ©e: " 
                  << current.frequency_mhz << " MHz" << std::endl;
        return true;
    }
    
    return false;
}

bool ClockAttackProtection::detect_overclock_attack(const ClockSample& current) {
    double nominal = nominal_frequency.load();
    double threshold = nominal * 1.2; // 120% de la nominale
    
    if (current.frequency_mhz > threshold) {
        std::cout << "Ã°Å¸Å¡Â¨ Overclock attack dÃƒÂ©tectÃƒÂ©e: " 
                  << current.frequency_mhz << " MHz" << std::endl;
        return true;
    }
    
    return false;
}

// ===== CONTRE-MESURES =====

void ClockAttackProtection::implement_clock_stabilization() {
    std::cout << "Ã°Å¸â€ºÂ¡Ã¯Â¸Â ImplÃƒÂ©mentation stabilisation clock..." << std::endl;
    
    // Activer les contre-mesures de stabilisation
#ifdef _WIN32
    // Windows: ajuster les paramÃƒÂ¨tres de performance
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
#else
    // Linux: ajuster la politique du scheduler
    struct sched_param param;
    param.sched_priority = 99;
    sched_setscheduler(0, SCHED_FIFO, &param);
#endif
    
    // Ajouter du bruit pour contrer les attaques
    std::mt19937 gen(static_cast<std::mt19937::result_type>(secure_seed64()));
    std::uniform_int_distribution<> delay_dist(1, 10);
    
    for (int i = 0; i < 10; i++) {
        std::this_thread::sleep_for(std::chrono::microseconds(delay_dist(gen)));
    }
}

void ClockAttackProtection::enable_secure_clock_source() {
    std::cout << "Ã°Å¸â€â€™ Activation source clock sÃƒÂ©curisÃƒÂ©e..." << std::endl;
    
    // Essayer de basculer vers une source clock sÃƒÂ©curisÃƒÂ©e
#ifdef _WIN32
    // Windows: utiliser QueryPerformanceCounter
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    if (freq.QuadPart > 0) {
        std::cout << "Ã¢Å“â€¦ Source clock haute rÃƒÂ©solution activÃƒÂ©e" << std::endl;
    }
#else
    // Linux: vÃƒÂ©rifier et utiliser clocksource sÃƒÂ©curisÃƒÂ©e
    std::ifstream clocksource(clock_source_path);
    std::string source;
    if (clocksource >> source) {
        if (source == "tsc" || source == "hpet" || source == "arch_sys_counter" || source == "arm_arch_timer") {
            std::cout << "Ã¢Å“â€¦ Source clock sÃƒÂ©curisÃƒÂ©e: " << source << std::endl;
        } else {
            std::cout << "Ã¢Å¡Â Ã¯Â¸Â Source clock non sÃƒÂ©curisÃƒÂ©e: " << source << std::endl;
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
    std::cout << "Ã¢Å¡â„¢Ã¯Â¸Â Calibration frÃƒÂ©quence nominale..." << std::endl;
    
    // Prendre 10 ÃƒÂ©chantillons pour calibrer
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
        std::cout << "Ã¢Å“â€¦ FrÃƒÂ©quence nominale calibrÃƒÂ©e: " << avg_freq << " MHz" << std::endl;
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
    if (!has_invariant_cycle_counter()) {
        jitter_threshold_ns = static_cast<uint64_t>(kArmJammingThresholdNs);
        frequency_tolerance_percent = 5.0;
        std::cout << "[CLOCK] Active jamming detection disabled on this platform (no invariant cycle counter)" << std::endl;
        return;
    }

    jitter_threshold_ns = 5000;
    frequency_tolerance_percent = 2.0;
    std::cout << "[CLOCK] Jamming detection configured" << std::endl;
}

void ClockAttackProtection::setup_secure_clock_source() {
    enable_secure_clock_source();
}

bool ClockAttackProtection::test_clock_protection() {
    std::cout << "Ã°Å¸Â§Âª Test protection clock..." << std::endl;
    
    // Test de dÃƒÂ©tection de manipulation
    double original_freq = nominal_frequency.load();
    nominal_frequency = 1000.0; // Simuler manipulation
    
    ClockSample test_sample;
    test_sample.frequency_mhz = 500.0; // FrÃƒÂ©quence anormale
    test_sample.timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    
    bool detection_works = detect_frequency_manipulation(test_sample);
    
    nominal_frequency = original_freq; // Restaurer
    
    std::cout << "Test clock protection: " << (detection_works ? "Ã¢Å“â€¦ PASS" : "Ã¢ÂÅ’ FAIL") << std::endl;
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
        // Simuler la dÃƒÂ©tection d'anomalies (ÃƒÂ  implÃƒÂ©menter correctement)
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
