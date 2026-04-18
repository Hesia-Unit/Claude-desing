#include "hardware_monitor.hpp"
#include "logger.hpp"
#include "config.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#include <thread>
#include <filesystem>
#include <limits>

#ifdef __linux__
#include <sys/ioctl.h>
#include <linux/watchdog.h>
#endif

#ifdef _WIN32
#include <psapi.h>
#include <tlhelp32.h>
#include <pdh.h>
#pragma comment(lib, "pdh.lib")
#endif

namespace {
#ifdef __linux__
std::string find_hwmon_input(const std::filesystem::path& hwmon_dir) {
    const std::vector<std::string> preferred_labels = {
        "VDD_CPU_GPU_CV",
        "VDD_IN",
        "VDD_SOC"
    };

    std::error_code ec;
    if (std::filesystem::exists(hwmon_dir, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(hwmon_dir, ec)) {
            if (ec || !entry.is_regular_file()) {
                continue;
            }
            const std::string name = entry.path().filename().string();
            if (name.rfind("in", 0) != 0 || name.find("_label") == std::string::npos) {
                continue;
            }
            std::ifstream label_file(entry.path());
            if (!label_file.is_open()) {
                continue;
            }
            std::string label;
            std::getline(label_file, label);
            label.erase(label.find_last_not_of(" \t\r\n") + 1);
            label.erase(0, label.find_first_not_of(" \t\r\n"));
            if (label.empty()) {
                continue;
            }
            for (const auto& preferred : preferred_labels) {
                if (label == preferred) {
                    std::string idx = name.substr(2, name.size() - 2 - 6);
                    std::filesystem::path input_path = hwmon_dir / ("in" + idx + "_input");
                    if (std::filesystem::exists(input_path, ec)) {
                        return input_path.string();
                    }
                }
            }
        }
    }

    const std::vector<std::string> candidates = {
        "in1_input", "in0_input", "in2_input", "in3_input"
    };
    for (const auto& name : candidates) {
        std::filesystem::path p = hwmon_dir / name;
        if (std::filesystem::exists(p, ec)) {
            return p.string();
        }
    }
    return {};
}

std::string resolve_ina3221_voltage_path() {
    const char* override_path = std::getenv("HESIA_VOLTAGE_SENSOR_PATH");
    if (override_path && *override_path) {
        std::error_code ec;
        std::filesystem::path p(override_path);
        if (std::filesystem::exists(p, ec)) {
            return p.string();
        }
    }

    const std::vector<std::filesystem::path> driver_roots = {
        "/sys/bus/i2c/drivers/ina3221",
        "/sys/bus/i2c/drivers/ina3221x"
    };

    for (const auto& root : driver_roots) {
        std::error_code ec;
        if (!std::filesystem::exists(root, ec)) {
            continue;
        }
        for (const auto& dev : std::filesystem::directory_iterator(root, ec)) {
            if (ec || !dev.is_directory()) {
                continue;
            }
            std::filesystem::path hwmon_root = dev.path() / "hwmon";
            if (!std::filesystem::exists(hwmon_root, ec)) {
                continue;
            }
            for (const auto& hw : std::filesystem::directory_iterator(hwmon_root, ec)) {
                if (ec || !hw.is_directory()) {
                    continue;
                }
                auto found = find_hwmon_input(hw.path());
                if (!found.empty()) {
                    return found;
                }
            }
        }
    }

    // Fallback: search I2C devices by name
    std::filesystem::path devices_root = "/sys/bus/i2c/devices";
    std::error_code ec;
    if (std::filesystem::exists(devices_root, ec)) {
        for (const auto& dev : std::filesystem::directory_iterator(devices_root, ec)) {
            if (ec || !dev.is_directory()) {
                continue;
            }
            std::filesystem::path name_path = dev.path() / "name";
            std::ifstream name_file(name_path);
            if (!name_file.is_open()) {
                continue;
            }
            std::string name;
            std::getline(name_file, name);
            if (name.find("ina3221") == std::string::npos) {
                continue;
            }
            std::filesystem::path hwmon_root = dev.path() / "hwmon";
            if (!std::filesystem::exists(hwmon_root, ec)) {
                continue;
            }
            for (const auto& hw : std::filesystem::directory_iterator(hwmon_root, ec)) {
                if (ec || !hw.is_directory()) {
                    continue;
                }
                auto found = find_hwmon_input(hw.path());
                if (!found.empty()) {
                    return found;
                }
            }
        }
    }

    std::filesystem::path hwmon_root = "/sys/class/hwmon";
    if (std::filesystem::exists(hwmon_root, ec)) {
        for (const auto& hw : std::filesystem::directory_iterator(hwmon_root, ec)) {
            if (ec || !hw.is_directory()) {
                continue;
            }
            std::filesystem::path name_path = hw.path() / "name";
            std::ifstream name_file(name_path);
            if (!name_file.is_open()) {
                continue;
            }
            std::string name;
            std::getline(name_file, name);
            if (name.find("ina3221") == std::string::npos) {
                continue;
            }
            auto found = find_hwmon_input(hw.path());
            if (!found.empty()) {
                return found;
            }
        }
    }

    return {};
}
#endif
} // namespace

namespace hesia {

// ===== VARIABLES STATIQUES =====

// ExternalWatchdog
std::atomic<bool> ExternalWatchdog::watchdog_active{false};
std::atomic<uint64_t> ExternalWatchdog::last_heartbeat{0};
std::atomic<uint32_t> ExternalWatchdog::watchdog_timeout_ms{5000};
std::thread ExternalWatchdog::watchdog_thread;
std::string ExternalWatchdog::watchdog_device{"/dev/watchdog"};

// GlitchVoltageDetector
std::atomic<bool> GlitchVoltageDetector::detector_active{false};
std::vector<GlitchVoltageDetector::VoltageSample> GlitchVoltageDetector::voltage_history;
std::thread GlitchVoltageDetector::monitoring_thread;
std::atomic<double> GlitchVoltageDetector::nominal_voltage{1.2};
std::atomic<double> GlitchVoltageDetector::nominal_frequency{2400.0};
std::atomic<uint32_t> GlitchVoltageDetector::glitch_threshold_mv{100};
std::atomic<uint32_t> GlitchVoltageDetector::frequency_threshold_hz{200};
std::atomic<double> GlitchVoltageDetector::cpu_min_freq_mhz{0.0};
std::atomic<double> GlitchVoltageDetector::cpu_max_freq_mhz{0.0};
std::atomic<uint32_t> GlitchVoltageDetector::temperature_threshold_celsius{85};

// Paths Linux pour monitoring hardware
std::string GlitchVoltageDetector::voltage_sysfs_path{"/sys/class/power_supply/BAT0/voltage_now"};
std::string GlitchVoltageDetector::frequency_sysfs_path{"/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq"};
std::string GlitchVoltageDetector::temperature_sysfs_path{"/sys/class/thermal/thermal_zone0/temp"};

// HardwareSecurityIntegration
std::atomic<bool> HardwareSecurityIntegration::integration_active{false};
std::atomic<uint32_t> HardwareSecurityIntegration::hardware_alerts_count{0};

// ===== IMPLEMENTATION WATCHDOG EXTERNE =====

uint64_t ExternalWatchdog::get_current_timestamp_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()
    ).count();
}

bool ExternalWatchdog::initialize(uint32_t timeout_ms) {
    if (watchdog_active.load()) {
        return true; // Déjà initialisé
    }
    
    watchdog_timeout_ms.store(timeout_ms);
    last_heartbeat.store(get_current_timestamp_ms());
    
    // Tenter d'ouvrir le device watchdog
    int watchdog_fd = -1;
    
#ifdef __linux__
    watchdog_fd = open(watchdog_device.c_str(), O_WRONLY);
    if (watchdog_fd == -1) {
        // Essayer /dev/watchdog0
        watchdog_fd = open("/dev/watchdog0", O_WRONLY);
    }
    
    if (watchdog_fd != -1) {
        // Configurer le timeout watchdog hardware
        int timeout_seconds = timeout_ms / 1000;
        if (timeout_seconds < 1) timeout_seconds = 1;
        
#ifdef WDIOC_SETTIMEOUT
        if (ioctl(watchdog_fd, WDIOC_SETTIMEOUT, &timeout_seconds) != 0) {
            std::cerr << "Erreur configuration timeout watchdog hardware" << std::endl;
        } else {
            std::cout << "Watchdog hardware initialisé avec timeout " << timeout_seconds << "s" << std::endl;
        }
#endif
        close(watchdog_fd);
    } else {
        std::cout << "Watchdog hardware non disponible, utilisation watchdog logiciel" << std::endl;
    }
#endif
    
    // Démarrer le thread de monitoring
    watchdog_active.store(true);
    watchdog_thread = std::thread(watchdog_monitor_loop);
    
    return true;
}

void ExternalWatchdog::watchdog_monitor_loop() {
    auto logger = setup_logger("HARDWARE-WATCHDOG", Config::LOG_DIR);
    
    while (watchdog_active.load()) {
        auto now = get_current_timestamp_ms();
        auto last_hb = last_heartbeat.load();
        
        // Vérifier si le timeout est dépassé
        if (now - last_hb > watchdog_timeout_ms.load()) {
            logger->error("⚠️ WATCHDOG TIMEOUT DÉTECTÉ - Système potentiellement compromis");
            
            // Déclencher l'alerte
            HardwareSecurityIntegration::on_watchdog_timeout();
            
            // Tenter de réinitialiser le watchdog
            reset_watchdog_hardware();
        }
        
        // Envoyer un heartbeat au watchdog hardware
        send_heartbeat_to_watchdog();
        
        // Attendre avant prochaine vérification
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

bool ExternalWatchdog::send_heartbeat_to_watchdog() {
#ifdef __linux__
    static int watchdog_fd = -1;
    if (watchdog_fd == -1) {
        watchdog_fd = open(watchdog_device.c_str(), O_WRONLY);
        if (watchdog_fd == -1) {
            watchdog_fd = open("/dev/watchdog0", O_WRONLY);
        }
    }
    
    if (watchdog_fd != -1) {
        // Envoyer le magic character pour reset le timer
        const char magic = 'V';
        return write(watchdog_fd, &magic, 1) == 1;
    }
#endif
    
    return false; // Watchdog hardware non disponible
}

void ExternalWatchdog::reset_watchdog_hardware() {
#ifdef __linux__
    int watchdog_fd = open(watchdog_device.c_str(), O_WRONLY);
    if (watchdog_fd == -1) {
        watchdog_fd = open("/dev/watchdog0", O_WRONLY);
    }
    
    if (watchdog_fd != -1) {
#ifdef WDIOC_KEEPALIVE
        // Reset le watchdog hardware
        ioctl(watchdog_fd, WDIOC_KEEPALIVE, nullptr);
#endif
        close(watchdog_fd);
        std::cout << "Watchdog hardware réinitialisé" << std::endl;
    }
#endif
}

void ExternalWatchdog::pet_watchdog() {
    last_heartbeat.store(get_current_timestamp_ms());
    send_heartbeat_to_watchdog();
}

bool ExternalWatchdog::is_active() {
    return watchdog_active.load();
}

void ExternalWatchdog::shutdown() {
    watchdog_active.store(false);
    
    if (watchdog_thread.joinable()) {
        watchdog_thread.join();
    }
    
    std::cout << "Watchdog externe arrêté" << std::endl;
}

uint64_t ExternalWatchdog::get_time_since_last_heartbeat() {
    return get_current_timestamp_ms() - last_heartbeat.load();
}

void ExternalWatchdog::set_watchdog_device(const std::string& device) {
    watchdog_device = device;
}

bool ExternalWatchdog::test_watchdog_communication() {
#ifdef __linux__
    int fd = open(watchdog_device.c_str(), O_WRONLY);
    if (fd != -1) {
        close(fd);
        return true;
    }
#endif
    return false;
}

// ===== IMPLEMENTATION DÉTECTION GLITCH/VOLTAGE RÉEL =====

bool GlitchVoltageDetector::initialize(double nominal_volt, double nominal_freq) {
    if (detector_active.load()) {
        return true; // Déjà initialisé
    }
    
    nominal_voltage.store(nominal_volt);
    nominal_frequency.store(nominal_freq);

#ifdef __linux__
    auto resolved = resolve_ina3221_voltage_path();
    if (resolved.empty()) {
        std::cerr << "Capteur INA3221 introuvable (sysfs/hwmon). Fail-closed." << std::endl;
        return false;
    }
    voltage_sysfs_path = resolved;
#endif
    
    // Calibration des valeurs nominales
    if (!calibrate_nominal_values()) {
        std::cerr << "Erreur calibration valeurs nominales" << std::endl;
        return false;
    }
    
    // Réserver l'historique
    voltage_history.reserve(1000);
    
    std::cout << "Détecteur glitch/voltage initialisé:" << std::endl;
    std::cout << "  Tension nominale: " << nominal_volt << "V" << std::endl;
    std::cout << "  Fréquence nominale: " << nominal_freq << "MHz" << std::endl;
    std::cout << "  Seuil glitch tension: " << glitch_threshold_mv.load() << "mV" << std::endl;
    std::cout << "  Seuil glitch fréquence: " << frequency_threshold_hz.load() << "MHz" << std::endl;
    
    return true;
}

bool GlitchVoltageDetector::calibrate_nominal_values() {
    auto logger = setup_logger("HARDWARE-GLITCH", Config::LOG_DIR);
    
    // Prendre 10 échantillons pour calibration
    std::vector<double> voltage_samples;
    std::vector<double> frequency_samples;
    
    for (int i = 0; i < 10; i++) {
        double voltage = read_cpu_voltage();
        double frequency = read_cpu_frequency();
        
        if (voltage > 0) voltage_samples.push_back(voltage);
        if (frequency > 0) frequency_samples.push_back(frequency);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Calculer les moyennes
    if (!voltage_samples.empty()) {
        double avg_voltage = 0;
        for (double v : voltage_samples) {
            avg_voltage += v;
        }
        avg_voltage /= voltage_samples.size();
        nominal_voltage.store(avg_voltage);
        
        logger->info("Calibration tension: " + std::to_string(avg_voltage) + "V");
    }
    
    if (!frequency_samples.empty()) {
        double avg_frequency = 0;
        for (double f : frequency_samples) {
            avg_frequency += f;
        }
        avg_frequency /= frequency_samples.size();
        nominal_frequency.store(avg_frequency);
        
        logger->info("Calibration fréquence: " + std::to_string(avg_frequency) + "MHz");
    }

#ifdef __linux__
    auto read_freq_khz = [](const std::string& path) -> double {
        std::ifstream file(path);
        if (!file.is_open()) {
            return 0.0;
        }
        uint64_t freq_khz = 0;
        if (file >> freq_khz) {
            return static_cast<double>(freq_khz) / 1000.0;
        }
        return 0.0;
    };

    const double min_freq = read_freq_khz("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_min_freq");
    const double max_freq = read_freq_khz("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq");
    if (min_freq > 0.0 && max_freq > 0.0 && max_freq >= min_freq) {
        cpu_min_freq_mhz.store(min_freq);
        cpu_max_freq_mhz.store(max_freq);
        logger->info("CPU freq range: " + std::to_string(min_freq) + " MHz - " +
                     std::to_string(max_freq) + " MHz");
    }
#endif
    
    return !voltage_samples.empty() && !frequency_samples.empty();
}

bool GlitchVoltageDetector::start_monitoring() {
    if (detector_active.load()) {
        return true; // Déjà actif
    }
    
    detector_active.store(true);
    monitoring_thread = std::thread(voltage_monitoring_loop);
    
    std::cout << "Monitoring glitch/voltage démarré" << std::endl;
    return true;
}

void GlitchVoltageDetector::voltage_monitoring_loop() {
    auto logger = setup_logger("HARDWARE-GLITCH", Config::LOG_DIR);
    
    while (detector_active.load()) {
        // Nourrir le watchdog logiciel d'abord pour éviter un timeout en cas de lecture capteur lente.
        if (ExternalWatchdog::is_active()) {
            ExternalWatchdog::pet_watchdog();
        }

        VoltageSample current = read_voltage_sample();
        
        // Ajouter à l'historique
        voltage_history.push_back(current);
        
        // Limiter la taille de l'historique
        if (voltage_history.size() > 1000) {
            voltage_history.erase(voltage_history.begin());
        }
        
        // Détection de glitches
        bool glitch_detected = false;
        
        if (detect_voltage_glitch(current)) {
            logger->error("⚠️ GLITCH DE TENSION DÉTECTÉ: " + 
                        std::to_string(current.voltage) + "V (nominal: " + 
                        std::to_string(nominal_voltage.load()) + "V)");
            glitch_detected = true;
            HardwareSecurityIntegration::on_voltage_anomaly_detected();
        }
        
        if (detect_frequency_glitch(current)) {
            logger->error("⚠️ GLITCH DE FRÉQUENCE DÉTECTÉ: " + 
                        std::to_string(current.frequency) + "MHz (nominal: " + 
                        std::to_string(nominal_frequency.load()) + "MHz)");
            glitch_detected = true;
            HardwareSecurityIntegration::on_voltage_anomaly_detected();
        }
        
        if (detect_temperature_anomaly(current)) {
            logger->error("⚠️ ANOMALIE TEMPÉRATURE DÉTECTÉE: " + 
                        std::to_string(current.temperature) + "°C (seuil: " + 
                        std::to_string(temperature_threshold_celsius.load()) + "°C)");
            glitch_detected = true;
            HardwareSecurityIntegration::on_voltage_anomaly_detected();
        }
        
        if (glitch_detected) {
            // Action de sécurité en cas de glitch
            logger->error("🚨 DÉTECTION GLITCH - Activation mesures de sécurité");
            
            // Déclencher l'alerte hardware
            HardwareSecurityIntegration::on_hardware_glitch_detected();
        }
        
        // Attendre avant prochain échantillon
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

GlitchVoltageDetector::VoltageSample GlitchVoltageDetector::read_voltage_sample() {
    VoltageSample sample;
    sample.timestamp = ExternalWatchdog::get_current_timestamp_ms();  // Utiliser la méthode de ExternalWatchdog
    sample.voltage = read_cpu_voltage();
    sample.frequency = read_cpu_frequency();
    sample.temperature = read_cpu_temperature();
    
    return sample;
}

double GlitchVoltageDetector::read_cpu_voltage() {
#ifdef __linux__
    if (!voltage_sysfs_path.empty()) {
        std::ifstream file(voltage_sysfs_path);
        if (file.is_open()) {
            uint32_t value;
            if (file >> value) {
                // INA3221 expose en millivolts
                return static_cast<double>(value) / 1000.0;
            }
        }
    }
    return std::numeric_limits<double>::quiet_NaN();
#endif

#ifdef _WIN32
    // Sur Windows, utiliser l'API de monitoring
    SYSTEM_POWER_STATUS powerStatus;
    if (GetSystemPowerStatus(&powerStatus)) {
        // Approximation basee sur le niveau de batterie
        return (powerStatus.BatteryLifePercent / 100.0) * 12.0; // Approx 12V max
    }
#endif

    return std::numeric_limits<double>::quiet_NaN();
}

double GlitchVoltageDetector::read_cpu_frequency() {
#ifdef __linux__
    std::ifstream freq_file(frequency_sysfs_path);
    if (freq_file.is_open()) {
        uint64_t freq_khz;
        if (freq_file >> freq_khz) {
            return freq_khz / 1000.0; // kHz -> MHz
        }
    }

    // Alternative: /proc/cpuinfo
    std::ifstream cpuinfo("/proc/cpuinfo");
    if (cpuinfo.is_open()) {
        std::string line;
        while (std::getline(cpuinfo, line)) {
            if (line.find("cpu MHz") != std::string::npos) {
                size_t colon = line.find(':');
                if (colon != std::string::npos) {
                    std::string freq_str = line.substr(colon + 1);
                    freq_str.erase(0, freq_str.find_first_not_of(" 	"));
                    size_t mhz_pos = freq_str.find("MHz");
                    if (mhz_pos != std::string::npos) {
                        freq_str = freq_str.substr(0, mhz_pos);
                    }
                    return std::stod(freq_str);
                }
            }
        }
    }
#endif

#ifdef _WIN32
    LARGE_INTEGER frequency;
    if (QueryPerformanceFrequency(&frequency)) {
        return frequency.QuadPart / 1000000.0; // Ticks -> MHz (approx)
    }
#endif

    return std::numeric_limits<double>::quiet_NaN();
}

uint32_t GlitchVoltageDetector::read_cpu_temperature() {
#ifdef __linux__
    std::ifstream temp_file(temperature_sysfs_path);
    if (temp_file.is_open()) {
        int temp_millidegrees;
        if (temp_file >> temp_millidegrees) {
            return temp_millidegrees / 1000; // mC -> C
        }
    }

    std::vector<std::string> temp_paths = {
        "/sys/class/thermal/thermal_zone0/temp",
        "/sys/class/hwmon/hwmon0/temp1_input",
        "/sys/devices/platform/coretemp.0/hwmon/hwmon0/temp1_input"
    };

    for (const auto& path : temp_paths) {
        std::ifstream file(path);
        if (file.is_open()) {
            int temp_millidegrees;
            if (file >> temp_millidegrees) {
                return temp_millidegrees / 1000;
            }
        }
    }
#endif

#ifdef _WIN32
    return 50; // Valeur par defaut Windows
#endif

    return std::numeric_limits<uint32_t>::max();
}

bool GlitchVoltageDetector::detect_voltage_glitch(const VoltageSample& current) {
    if (!std::isfinite(current.voltage) || current.voltage <= 0.0) {
        return true;
    }
    double nominal = nominal_voltage.load();
    uint32_t threshold_mv = glitch_threshold_mv.load();
    
    // Calculer la déviation en millivolts
    double deviation_mv = std::abs(current.voltage - nominal) * 1000.0;
    
    return deviation_mv > threshold_mv;
}

bool GlitchVoltageDetector::detect_frequency_glitch(const VoltageSample& current) {
    if (!std::isfinite(current.frequency) || current.frequency <= 0.0) {
        return true;
    }
    const double min_freq = cpu_min_freq_mhz.load();
    const double max_freq = cpu_max_freq_mhz.load();
    if (min_freq > 0.0 && max_freq > 0.0 && max_freq >= min_freq) {
        const double lower = min_freq * 0.8;
        const double upper = max_freq * 1.2;
        if (current.frequency >= lower && current.frequency <= upper) {
            return false;
        }
    }

    double nominal = nominal_frequency.load();
    double abs_threshold_mhz = static_cast<double>(frequency_threshold_hz.load());
    double pct_threshold_mhz = nominal * 0.6;
    double threshold_mhz = std::max(abs_threshold_mhz, pct_threshold_mhz);
    double deviation_mhz = std::abs(current.frequency - nominal);
    
    return deviation_mhz > threshold_mhz;
}

bool GlitchVoltageDetector::detect_temperature_anomaly(const VoltageSample& current) {
    if (current.temperature == std::numeric_limits<uint32_t>::max()) {
        return true;
    }
    uint32_t threshold = temperature_threshold_celsius.load();
    
    return current.temperature > threshold;
}

void GlitchVoltageDetector::stop_monitoring() {
    detector_active.store(false);
    if (monitoring_thread.joinable()) {
        monitoring_thread.join();
    }
}

bool GlitchVoltageDetector::is_active() {
    return detector_active.load();
}

GlitchVoltageDetector::GlitchStats GlitchVoltageDetector::get_statistics() {
    GlitchStats stats = {};
    stats.total_samples = voltage_history.size();
    
    if (voltage_history.empty()) {
        return stats;
    }
    
    double voltage_sum = 0;
    double freq_sum = 0;
    uint32_t max_temp = 0;
    
    for (const auto& sample : voltage_history) {
        voltage_sum += sample.voltage;
        freq_sum += sample.frequency;
        if (sample.temperature > max_temp) {
            max_temp = sample.temperature;
        }
        
        // Détecter les glitches dans l'historique
        if (detect_voltage_glitch(sample) || detect_frequency_glitch(sample)) {
            stats.glitch_count++;
        }
        if (detect_temperature_anomaly(sample)) {
            stats.anomaly_count++;
        }
    }
    
    stats.avg_voltage = voltage_sum / voltage_history.size();
    stats.avg_frequency = freq_sum / voltage_history.size();
    stats.max_temperature = max_temp;
    
    return stats;
}

void GlitchVoltageDetector::set_voltage_threshold(uint32_t threshold_mv) {
    glitch_threshold_mv.store(threshold_mv);
}

void GlitchVoltageDetector::set_frequency_threshold(uint32_t threshold_hz) {
    frequency_threshold_hz.store(threshold_hz);
}

void GlitchVoltageDetector::set_temperature_threshold(uint32_t threshold_celsius) {
    temperature_threshold_celsius.store(threshold_celsius);
}

bool GlitchVoltageDetector::test_glitch_detection() {
    // Test simple pour vérifier que la détection fonctionne
    VoltageSample test_sample;
    test_sample.voltage = nominal_voltage.load() + (glitch_threshold_mv.load() / 1000.0) + 0.1;
    test_sample.frequency = nominal_frequency.load() + frequency_threshold_hz.load() + 10;
    test_sample.temperature = temperature_threshold_celsius.load() + 10;
    
    return detect_voltage_glitch(test_sample) || 
           detect_frequency_glitch(test_sample) || 
           detect_temperature_anomaly(test_sample);
}

void GlitchVoltageDetector::reset_statistics() {
    voltage_history.clear();
}

GlitchVoltageDetector::SystemHealth GlitchVoltageDetector::get_system_health() {
    if (voltage_history.empty()) {
        return SYSTEM_HEALTH_GOOD;
    }
    
    GlitchStats stats = get_statistics();
    
    if (stats.anomaly_count > 10) {
        return SYSTEM_HEALTH_EMERGENCY;
    } else if (stats.anomaly_count > 5) {
        return SYSTEM_HEALTH_CRITICAL;
    } else if (stats.anomaly_count > 2) {
        return SYSTEM_HEALTH_WARNING;
    }
    
    return SYSTEM_HEALTH_GOOD;
}

// ===== INTÉGRATION AVEC RUNTIME PROTECTION =====

bool HardwareSecurityIntegration::initialize() {
    if (integration_active.load()) {
        return true; // Déjà initialisé
    }
    
    // Initialiser le watchdog externe
    if (!ExternalWatchdog::initialize(15000)) { // 15 secondes timeout (évite faux positifs au boot)
        std::cerr << "Erreur initialisation watchdog externe" << std::endl;
        return false;
    }
    
    // Initialiser le détecteur de glitch
    if (!GlitchVoltageDetector::initialize()) {
        std::cerr << "Erreur initialisation détecteur glitch" << std::endl;
        return false;
    }
    
    integration_active.store(true);
    hardware_alerts_count.store(0);
    
    std::cout << "Intégration hardware sécurité initialisée" << std::endl;
    return true;
}

bool HardwareSecurityIntegration::start_hardware_monitoring() {
    if (!integration_active.load()) {
        return false;
    }
    
    // Démarrer le monitoring de glitch
    if (!GlitchVoltageDetector::start_monitoring()) {
        std::cerr << "Erreur démarrage monitoring glitch" << std::endl;
        return false;
    }
    
    std::cout << "Monitoring hardware sécurité démarré" << std::endl;
    return true;
}

void HardwareSecurityIntegration::stop_hardware_monitoring() {
    GlitchVoltageDetector::stop_monitoring();
    ExternalWatchdog::shutdown();
    integration_active.store(false);
}

void HardwareSecurityIntegration::on_hardware_glitch_detected() {
    hardware_alerts_count.fetch_add(1);
    
    auto logger = setup_logger("HARDWARE-SECURITY", Config::LOG_DIR);
    logger->error("🚨 ALERTE HARDWARE: Glitch détecté - Activation sécurité");
    
    // Action de sécurité - arrêt du programme
    std::terminate();
}

void HardwareSecurityIntegration::on_watchdog_timeout() {
    hardware_alerts_count.fetch_add(1);
    
    auto logger = setup_logger("HARDWARE-SECURITY", Config::LOG_DIR);
    logger->error("🚨 ALERTE WATCHDOG: Timeout détecté - Système compromis");
    
    std::cout << "🚨 ALERTE WATCHDOG: Timeout détecté - Système compromis" << std::endl;
    
    // Action de sécurité
    std::terminate();
}

void HardwareSecurityIntegration::on_voltage_anomaly_detected() {
    hardware_alerts_count.fetch_add(1);

    std::cout << "â ï¸ ALERTE VOLTAGE: Anomalie detectee" << std::endl;
    std::terminate();
}

uint32_t HardwareSecurityIntegration::get_hardware_alerts_count() {
    return hardware_alerts_count.load();
}

bool HardwareSecurityIntegration::is_integration_active() {
    return integration_active.load();
}

bool HardwareSecurityIntegration::test_hardware_integration() {
    // Test simple de l'intégration
    bool watchdog_ok = ExternalWatchdog::test_watchdog_communication();
    bool glitch_ok = GlitchVoltageDetector::test_glitch_detection();
    
    return watchdog_ok && glitch_ok;
}

} // namespace hesia
