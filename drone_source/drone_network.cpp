#include <sys/socket.h>
#include <netinet/in.h>
#ifndef _WIN32
#include <netdb.h>
#include <errno.h>
#endif
#include "drone_network.hpp"
#include "exceptions.hpp"
#include "mldsa87_public_key.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <sstream>
#include <iomanip>
#include <random>
#include <algorithm>
#include <stdexcept>
#include <condition_variable>
#include <deque>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <optional>
#include <array>
#include <cstdlib>
#include <cctype>

#ifndef _WIN32
#include <pthread.h>
#include <sched.h>
#endif

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #define close_socket closesocket
    #define socket_error WSAGetLastError()
    #pragma comment(lib, "ws2_32.lib")
#else
    #define close_socket close
    #define socket_error errno
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
#endif

#include "clean_pipeline.hpp"
#include "security_utils.hpp"

namespace hesia {

namespace {

// Fonction de pinning de thread (optionnelle)
[[maybe_unused]] void pin_current_thread_to_core(int core_id) {
#ifndef _WIN32
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    
    pthread_t current_thread = pthread_self();
    if (pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset) != 0) {
        // Erreur silencieuse - le pinning est optionnel
    }
#else
    (void)core_id; // Supprimer warning sur Windows
#endif
}

// Helper pour convertir bytes en hex
[[maybe_unused]] std::string bytes_to_hex(const std::vector<uint8_t>& bytes) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (size_t i = 0; i < std::min(bytes.size(), static_cast<size_t>(20)); ++i) {
        ss << std::setw(2) << static_cast<int>(bytes[i]);
    }
    if (bytes.size() > 20) {
        ss << "...";
    }
    return ss.str();
}

struct TelemetrySnapshot {
    double cpu_temp_c = -1.0;
    double cpu_usage_pct = -1.0;
    double ram_used_mb = -1.0;
    double ram_total_mb = -1.0;
    double voltage_v = -1.0;
    double current_a = -1.0;
    double power_w = -1.0;
    double gps_lat = -1.0;
    double gps_lon = -1.0;
    double gps_alt_m = -1.0;
    uint64_t ts_ms = 0;
};

static std::string trim_copy(const std::string& input) {
    size_t start = 0;
    while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start])) != 0) {
        ++start;
    }
    size_t end = input.size();
    while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1])) != 0) {
        --end;
    }
    return input.substr(start, end - start);
}

static bool extract_named_number(const std::string& text, const std::string& key, double& out) {
    const std::array<std::string, 4> needles = {
        "\"" + key + "\"",
        key + "=",
        key + ":",
        key + " "
    };

    for (const auto& needle : needles) {
        size_t pos = text.find(needle);
        if (pos == std::string::npos) {
            continue;
        }
        pos += needle.size();
        while (pos < text.size() &&
               (std::isspace(static_cast<unsigned char>(text[pos])) != 0 ||
                text[pos] == '=' || text[pos] == ':' || text[pos] == '"')) {
            ++pos;
        }
        if (pos >= text.size()) {
            continue;
        }
        char* endptr = nullptr;
        const double value = std::strtod(text.c_str() + pos, &endptr);
        if (endptr != text.c_str() + pos) {
            out = value;
            return true;
        }
    }
    return false;
}

static void populate_gps_fix_from_text(const std::string& text, TelemetrySnapshot& snapshot) {
    double lat = -1.0;
    double lon = -1.0;
    double alt = -1.0;
    const bool have_lat = extract_named_number(text, "gps_lat", lat) || extract_named_number(text, "lat", lat);
    const bool have_lon = extract_named_number(text, "gps_lon", lon) || extract_named_number(text, "lon", lon);
    const bool have_alt = extract_named_number(text, "gps_alt_m", alt) ||
                          extract_named_number(text, "alt", alt) ||
                          extract_named_number(text, "altitude", alt);

    if (have_lat && have_lon &&
        lat >= -90.0 && lat <= 90.0 &&
        lon >= -180.0 && lon <= 180.0) {
        snapshot.gps_lat = lat;
        snapshot.gps_lon = lon;
        if (have_alt) {
            snapshot.gps_alt_m = alt;
        }
    }
}

static void try_read_gps_fix(TelemetrySnapshot& snapshot) {
    const char* env_path = std::getenv("HESIA_GPS_FIX_PATH");
    const std::filesystem::path path = (env_path && env_path[0] != '\0')
        ? std::filesystem::path(env_path)
        : std::filesystem::path("/run/hesia/gps.fix");

    if (!std::filesystem::exists(path) || !std::filesystem::is_regular_file(path)) {
        return;
    }

    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        return;
    }

    std::ostringstream buffer;
    buffer << f.rdbuf();
    const std::string text = trim_copy(buffer.str());
    if (text.empty()) {
        return;
    }

    populate_gps_fix_from_text(text, snapshot);
}

static bool read_file_double(const std::filesystem::path& path, double& out) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    double v = 0.0;
    f >> v;
    if (!f.good() && !f.eof()) return false;
    out = v;
    return true;
}

static std::vector<uint8_t> read_file_binary_strict(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        throw std::runtime_error("Impossible d'ouvrir le fichier: " + path.string());
    }
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
}

static double read_cpu_temp_max_c() {
    const std::filesystem::path base("/sys/devices/virtual/thermal");
    if (!std::filesystem::exists(base)) return -1.0;
    double max_c = -1.0;
    for (const auto& entry : std::filesystem::directory_iterator(base)) {
        if (!entry.is_directory()) continue;
        const auto temp_path = entry.path() / "temp";
        double v = 0.0;
        if (!read_file_double(temp_path, v)) continue;
        if (v > 1000.0) v /= 1000.0; // mÂ°C -> Â°C
        if (v > max_c) max_c = v;
    }
    return max_c;
}

static bool read_cpu_times(uint64_t& idle, uint64_t& total) {
    std::ifstream f("/proc/stat");
    if (!f.is_open()) return false;
    std::string cpu;
    uint64_t user=0, nice=0, system=0, idle_t=0, iowait=0, irq=0, softirq=0, steal=0;
    f >> cpu >> user >> nice >> system >> idle_t >> iowait >> irq >> softirq >> steal;
    if (cpu != "cpu") return false;
    idle = idle_t + iowait;
    total = user + nice + system + idle_t + iowait + irq + softirq + steal;
    return true;
}

static double read_cpu_usage_pct() {
    static uint64_t prev_idle = 0;
    static uint64_t prev_total = 0;
    uint64_t idle = 0, total = 0;
    if (!read_cpu_times(idle, total) || total == 0) return -1.0;
    if (prev_total == 0) {
        prev_idle = idle;
        prev_total = total;
        return 0.0;
    }
    const uint64_t diff_total = total - prev_total;
    const uint64_t diff_idle = idle - prev_idle;
    prev_total = total;
    prev_idle = idle;
    if (diff_total == 0) return 0.0;
    double usage = (static_cast<double>(diff_total - diff_idle) / static_cast<double>(diff_total)) * 100.0;
    if (usage < 0.0) usage = 0.0;
    if (usage > 100.0) usage = 100.0;
    return usage;
}

static void read_ram_mb(double& used_mb, double& total_mb) {
    used_mb = -1.0;
    total_mb = -1.0;
    std::ifstream f("/proc/meminfo");
    if (!f.is_open()) return;
    std::string key;
    uint64_t val_kb = 0;
    std::string unit;
    uint64_t total_kb = 0;
    uint64_t avail_kb = 0;
    while (f >> key >> val_kb >> unit) {
        if (key == "MemTotal:") total_kb = val_kb;
        else if (key == "MemAvailable:") avail_kb = val_kb;
    }
    if (total_kb == 0) return;
    if (avail_kb > total_kb) avail_kb = 0;
    const uint64_t used_kb = total_kb - avail_kb;
    total_mb = static_cast<double>(total_kb) / 1024.0;
    used_mb = static_cast<double>(used_kb) / 1024.0;
}

static std::optional<std::filesystem::path> find_ina_file(const std::string& prefix) {
    const std::filesystem::path base("/sys/bus/i2c/drivers/ina3221x");
    if (!std::filesystem::exists(base)) return std::nullopt;
    std::optional<std::filesystem::path> fallback;
    for (const auto& dev : std::filesystem::directory_iterator(base)) {
        if (!dev.is_directory()) continue;
        const auto iio_dir = dev.path() / "iio_device";
        if (!std::filesystem::exists(iio_dir)) continue;
        for (const auto& f : std::filesystem::directory_iterator(iio_dir)) {
            if (!f.is_regular_file()) continue;
            const std::string name = f.path().filename().string();
            if (name == prefix + "0_input") {
                return f.path();
            }
            if (!fallback && name.rfind(prefix, 0) == 0 && name.find("_input") != std::string::npos) {
                fallback = f.path();
            }
        }
    }
    return fallback;
}

static double read_ina_value(const std::string& prefix, double scale) {
    auto path = find_ina_file(prefix);
    if (!path) return -1.0;
    double v = 0.0;
    if (!read_file_double(*path, v)) return -1.0;
    return v * scale;
}

static TelemetrySnapshot collect_telemetry_snapshot() {
    TelemetrySnapshot t;
    t.ts_ms = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    t.gps_lat = -1.0;
    t.gps_lon = -1.0;
    t.gps_alt_m = -1.0;

    t.cpu_temp_c = read_cpu_temp_max_c();
    t.cpu_usage_pct = read_cpu_usage_pct();
    read_ram_mb(t.ram_used_mb, t.ram_total_mb);

    // INA3221 values: often in mV/mA/mW
    t.voltage_v = read_ina_value("in_voltage", 0.001);
    t.current_a = read_ina_value("in_current", 0.001);
    t.power_w = read_ina_value("in_power", 0.001);
    try_read_gps_fix(t);

    return t;
}

} // namespace

DroneNetworkClient::DroneNetworkClient(const std::string& drone_id) 
    : drone(std::make_unique<HesiaDrone>(drone_id)),
      policy_(load_security_policy_or_throw("drone")),
      socket_fd(INVALID_SOCKET), connected(false),
      tls_enabled(true),
      tls_verify_peer(true),
      tls_pin_server_pubkey(policy_.tls_pin),
      tls_rekey_bytes_threshold(policy_.tls_rekey_bytes),
      tls_rekey_seconds(policy_.tls_rekey_seconds),
      video_running(true), workers_running(false),
      frame_counter(0), ping_counter(0) {

    send_queue_max = std::max<std::size_t>(policy_.video_send_queue_max, static_cast<std::size_t>(8));
    Logger::set_debug_enabled(!policy_.prod_fuse);

    logger = setup_logger("HESIA-DRONE", Config::LOG_DIR);
    error_handler = std::make_shared<ErrorHandler>(logger);
    AuditConfig audit_cfg;
    audit_cfg.enabled = policy_.audit_enabled;
    audit_cfg.log_path = resolve_path(Config::LOG_DIR, policy_.audit_log_path);
    audit_cfg.secure_dir = policy_.secure_dir;
    audit_cfg.key_path = policy_.audit_key_path.empty()
        ? resolve_path(policy_.secure_dir, "audit.key")
        : resolve_path(policy_.secure_dir, policy_.audit_key_path);
    audit_cfg.alert_path = resolve_path(Config::LOG_DIR, policy_.audit_alert_path);
    audit_cfg.signing_key_path = resolve_path(policy_.secure_dir, policy_.audit_signing_key);
    audit_cfg.signing_pub_path = resolve_path(policy_.secure_dir, policy_.audit_signing_pub);
    audit_cfg.export_path = resolve_path(Config::LOG_DIR, policy_.audit_export_path);
    audit_cfg.require_signing = policy_.require_audit_signing;
    audit_cfg.rotate_on_start = policy_.audit_rotate_on_start;
    audit_cfg.rotate_interval_sec = policy_.audit_rotate_interval_sec;
    if (policy_.require_oem_kdf) {
        audit_cfg.oem_k1_path = policy_.oem_k1_path;
        audit_cfg.oem_k2_path = policy_.oem_k2_path;
        audit_cfg.oem_kdf_label = policy_.oem_kdf_label;
        audit_cfg.oem_kdf_context = policy_.oem_kdf_context + ":audit";
    }
    audit = std::make_shared<SecurityAudit>("drone", Config::LOG_DIR, logger, audit_cfg);
    audit->rotate_key_if_requested();
    audit->event("START", "INFO", "drone_id=" + drone_id);

    // --- TLS transport configuration (mTLS required) ---
    {
        if (!policy_.require_mtls) {
            throw SecurityViolation("mTLS required by policy");
        }

        tls = std::make_unique<TLSChannel>();

        TLSChannel::TLSPaths paths;
        paths.ca_path = resolve_path(policy_.cert_dir, policy_.ca_cert);
        paths.cert_path = resolve_path(policy_.cert_dir, policy_.client_cert);
        paths.key_path = resolve_path(policy_.cert_dir, policy_.client_key);
        tls->set_cert_paths(paths);

        tls_last_rekey = std::chrono::steady_clock::now();
    }
    
    // Initialiser les protections runtime AVANT toute autre chose
    logger->info(" Initialisation des protections runtime...");
    // RuntimeProtection::setup_protection(); // CommentÃ© pour Ã©viter erreur de compilation
    
    // VÃ©rifier l'environnement au dÃ©marrage
    RuntimeProtection::setup_protection();

    if (!RuntimeProtection::detect_debugger()) {
        logger->info(" Aucun dÃ©bogueur dÃ©tectÃ©");
    } else {
        logger->error(" DÃ©bogueur dÃ©tectÃ© - ArrÃªt sÃ©curitÃ©");
        RuntimeProtection::emergency_shutdown();
        if (false) { // SimplifiÃ© pour compilation
            logger->warning("VÃ©rification bibliothÃ¨ques dÃ©sactivÃ©e pour compilation");
        }    
        return;
    }
    
    logger->info(" Protections runtime actives - Mode production sÃ©curisÃ©");
    
    // Initialiser le pool de frames
    frame_buffer_pool.reserve(FRAME_POOL_SIZE);
    
    // ParamÃ¨tres JPEG
    jpeg_params = {cv::IMWRITE_JPEG_QUALITY, 70, cv::IMWRITE_JPEG_PROGRESSIVE, 1};
    
    // Initialiser le rÃ©pertoire de logging des frames
    if (frame_logging_enabled.load()) {
        frame_log_dir = Config::BASE_DIR / "frame_logs";
        std::filesystem::create_directories(frame_log_dir);
    }
    
    // Callbacks d'erreur
    error_handler->register_callback(ErrorCategory::SYNC, [this](const ErrorInfo& error) {
        logger->warning("Erreur synchronisation: " + error.message);
    });
    
    error_handler->register_callback(ErrorCategory::MEMORY, [this](const ErrorInfo& error) {
        logger->warning("Erreur mÃ©moire: " + error.message);
        std::lock_guard<std::mutex> lock(frame_pool_mutex);
        frame_buffer_pool.clear();
    });
    
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

DroneNetworkClient::~DroneNetworkClient() {
    stop_video();
    close();
    
#ifdef _WIN32
    WSACleanup();
#endif
}

void DroneNetworkClient::mark_transport_failure(const std::string& reason) {
    const bool was_connected = connected.exchange(false);
    transport_failed_.store(true);
    send_running.store(false);
    telemetry_running.store(false);
    video_running.store(false);
    {
        std::lock_guard<std::mutex> lock(send_queue_mutex);
        send_queue.clear();
    }
    send_queue_cv.notify_all();
    frame_sync_cv.notify_all();

    if (audit) {
        audit->event("TRANSPORT_FAIL", "ERROR", reason);
    }

    if (was_connected) {
        logger->error("Transport failure: " + reason);
    }

    if (tls_enabled && tls) {
        try {
            tls->close();
        } catch (...) {
        }
    }

    if (socket_fd != INVALID_SOCKET) {
#ifdef _WIN32
        ::shutdown(socket_fd, SD_BOTH);
#else
        ::shutdown(socket_fd, SHUT_RDWR);
#endif
    }
}

bool DroneNetworkClient::connect(const std::string& host, int port, int timeout, int retries) {
    auto close_current_socket = [&]() {
        if (socket_fd != INVALID_SOCKET) {
            ::close_socket(socket_fd);
            socket_fd = INVALID_SOCKET;
        }
    };

    for (int attempt = 1; attempt <= retries; ++attempt) {
        try {
            logger->info("Connexion " + std::to_string(attempt) + "/" + std::to_string(retries) +
                         " Ã  " + host + ":" + std::to_string(port));

            close_current_socket();

            // Resolve host (supports DNS names, IPv4 and IPv6)
            struct addrinfo hints{};
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_family = AF_UNSPEC;

            struct addrinfo* res = nullptr;
            const std::string port_str = std::to_string(port);
            int gai = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res);
            if (gai != 0 || !res) {
                throw std::runtime_error(std::string("getaddrinfo() failed: ") + gai_strerror(gai));
            }

            bool connected_ok = false;
            for (struct addrinfo* rp = res; rp != nullptr; rp = rp->ai_next) {
                socket_fd = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
                if (socket_fd == INVALID_SOCKET) {
                    continue;
                }

#ifdef _WIN32
                DWORD rcv_ms = static_cast<DWORD>(policy_.ssl_read_timeout_sec) * 1000;
                DWORD snd_ms = static_cast<DWORD>(policy_.ssl_write_timeout_sec) * 1000;
                ::setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&rcv_ms, sizeof(rcv_ms));
                ::setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&snd_ms, sizeof(snd_ms));
#else
                struct timeval rcv;
                rcv.tv_sec = policy_.ssl_read_timeout_sec;
                rcv.tv_usec = 0;
                struct timeval snd;
                snd.tv_sec = policy_.ssl_write_timeout_sec;
                snd.tv_usec = 0;
                ::setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &rcv, sizeof(rcv));
                ::setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &snd, sizeof(snd));
#endif

                if (::connect(socket_fd, rp->ai_addr, rp->ai_addrlen) == 0) {
                    connected_ok = true;
                    break;
                }
                close_current_socket();
            }

            freeaddrinfo(res);

            if (!connected_ok) {
                throw std::runtime_error("connect() failed (no address succeeded)");
            }

            // --- TLS 1.3 transport (mTLS required) ---
            if (!(tls_enabled && tls)) {
                throw SecurityViolation("TLS transport is mandatory for HESIA drone sessions");
            }
            {
                bool verify_peer = tls_verify_peer;

                // Optional SPKI pinning (policy-controlled)
                if (tls_pin_server_pubkey) {
                    const std::vector<std::filesystem::path> pin_candidates = {
                        std::filesystem::path(resolve_path(policy_.secure_dir, "server_tls_spki.pem")),
                        std::filesystem::path(resolve_path(policy_.secure_dir, "server_tls_spki.der")),
                        std::filesystem::path(resolve_path(policy_.cert_dir, "server.crt")),
                    };
                    bool pin_loaded = false;
                    for (const auto& candidate : pin_candidates) {
                        if (!std::filesystem::exists(candidate)) {
                            continue;
                        }
                        tls->enable_spki_pinning(read_file_binary_strict(candidate));
                        logger->info("TLS SPKI pin loaded from " + candidate.string());
                        pin_loaded = true;
                        break;
                    }
                    if (!pin_loaded) {
                        throw SecurityViolation("tls_pin active mais aucun certificat/SPKI serveur epingle n'a ete provisionne");
                    }
                }

                tls->set_timeouts(policy_.ssl_read_timeout_sec * 1000, policy_.ssl_write_timeout_sec * 1000);
                if (!tls->connect_on_socket(socket_fd, host, verify_peer)) {
                    throw std::runtime_error("TLS 1.3 handshake failed");
                }

                // Exporter material for PQC hybrid binding (required when TLS is enabled)
                try {
                    const std::string label = "HESIA-EXPORTER-HYBRID-V1";
                    auto exporter = tls->export_keying_material(label, 32);
                    drone->set_tls_exporter_secret(exporter);

                    auto peer_cert_hash = tls->peer_cert_sha256();
                    drone->set_tls_peer_cert_sha256(peer_cert_hash);
                } catch (const std::exception& e) {
                    throw std::runtime_error(std::string("TLS exporter/cert binding failed: ") + e.what());
                }

                if (audit) {
                    audit->event("TLS_OK", "INFO", "host=" + host + ":" + std::to_string(port));
                }

                // Reset rekey accounting after handshake
                tls_bytes_since_rekey = 0;
                tls_last_rekey = std::chrono::steady_clock::now();
            }

            connected.store(true);
            transport_failed_.store(false);
            stats.start_time_ms.store(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());

            logger->info("âœ“ Connexion Ã©tablie");
            if (audit) {
                audit->event("CONNECT_OK", "INFO", "host=" + host + ":" + std::to_string(port));
            }
            return true;

        } catch (const std::exception& e) {
            error_handler->handle_exception(e, ErrorSeverity::WARNING, ErrorCategory::NETWORK, "connect");
            if (audit) {
                audit->event("CONNECT_FAIL", "WARN", "host=" + host + ":" + std::to_string(port) + " err=" + e.what());
            }
            close_current_socket();

            if (attempt < retries) {
                std::this_thread::sleep_for(std::chrono::seconds(1 << attempt));
            }
        }
    }

    logger->error("Ã‰chec connexion aprÃ¨s " + std::to_string(retries) + " tentatives");
    return false;
}


int DroneNetworkClient::transport_write_all(const uint8_t* data, size_t len) {
    size_t total = 0;
    while (total < len) {
        int w = -1;
        if (!(tls_enabled && tls && tls->is_connected())) {
            mark_transport_failure("TLS transport dropped before write");
            return -1;
        }
        w = tls->write(data + total, static_cast<int>(len - total));
        if (w <= 0) return -1;
        total += static_cast<size_t>(w);
    }
    return static_cast<int>(total);
}

int DroneNetworkClient::transport_read_all(uint8_t* data, size_t len) {
    size_t total = 0;
    while (total < len) {
        int r = -1;
        if (!(tls_enabled && tls && tls->is_connected())) {
            mark_transport_failure("TLS transport dropped before read");
            return -1;
        }
        r = tls->read(data + total, static_cast<int>(len - total));
        if (r <= 0) {
            mark_transport_failure("transport_read_all returned no data");
            return -1;
        }
        total += static_cast<size_t>(r);
    }
    return static_cast<int>(total);
}

void DroneNetworkClient::tls_maybe_rekey() {
    if (!(tls_enabled && tls && tls->is_connected())) return;

    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - tls_last_rekey).count();

    if (tls_bytes_since_rekey >= tls_rekey_bytes_threshold || elapsed >= tls_rekey_seconds) {
        // Request TLS 1.3 KeyUpdate; OpenSSL sends it on next write.
        tls->request_key_update();
        tls_bytes_since_rekey = 0;
        tls_last_rekey = now;
    }
}

bool DroneNetworkClient::send_message(const std::vector<uint8_t>& message, const std::string& message_type) {
    if (transport_failed_.load() || !connected.load() || socket_fd == INVALID_SOCKET) {
        return false;
    }
    try {
        std::lock_guard<std::mutex> lock(send_mutex);
        // 1-byte application prefix to disambiguate payloads in SECURE_SESSION
        // 0x01 = SECURE_MSG (iv||tag||ciphertext)
        // 0x02 = VIDEO_DATA (VideoPacket blob)
        std::vector<uint8_t> payload;
        payload.reserve(message.size() + 1);
        if (message_type == "SECURE_MSG") {
            payload.push_back(0x01);
            payload.insert(payload.end(), message.begin(), message.end());
        } else if (message_type == "VIDEO_DATA") {
            payload.push_back(0x02);
            payload.insert(payload.end(), message.begin(), message.end());
        } else {
            payload = message;
        }

        const std::size_t max_size = (message_type == "VIDEO_DATA")
            ? policy_.max_frame_bytes
            : policy_.max_control_msg_bytes;
        if (payload.size() > max_size) {
            logger->error("Message too large (" + std::to_string(payload.size()) +
                          " bytes > " + std::to_string(max_size) + ")");
            return false;
        }

        uint32_t data_size = static_cast<uint32_t>(payload.size());
        std::vector<uint8_t> header(4);
        header[0] = (data_size >> 24) & 0xFF;
        header[1] = (data_size >> 16) & 0xFF;
        header[2] = (data_size >> 8) & 0xFF;
        header[3] = data_size & 0xFF;

        std::vector<uint8_t> full_data;
        full_data.reserve(4 + payload.size());
        full_data.insert(full_data.end(), header.begin(), header.end());
        full_data.insert(full_data.end(), payload.begin(), payload.end());

        int sent = transport_write_all(full_data.data(), full_data.size());
        if (sent < 0 || static_cast<size_t>(sent) != full_data.size()) {
            throw std::runtime_error("transport_write_all failed");
        }

        stats.bytes_sent.fetch_add(static_cast<uint64_t>(sent));
        stats.messages_sent.fetch_add(1);

        if (tls_enabled) {
            tls_bytes_since_rekey += static_cast<uint64_t>(sent);
            tls_maybe_rekey();
        }

        return true;
    } catch (const std::exception& e) {
        logger->error("Erreur envoi: " + std::string(e.what()));
        mark_transport_failure("send_message(" + message_type + "): " + e.what());
        return false;
    }
}

bool DroneNetworkClient::enqueue_secure_message(const std::string& msg_type,
                                                const std::string& json_data) {
    // La construction du message sécurisé fait avancer la chaîne last_block_hash
    // (utilisée comme AAD AES-GCM) et le compteur de séquence dans HesiaDrone.
    // Cette section doit être atomique avec la mise en file: sinon deux threads
    // peuvent (1) accéder de façon concurrente à last_block_hash (course de
    // données / UB) et (2) enfiler les messages dans un ordre différent de celui
    // du chaînage, ce qui provoque un échec d'authentification GCM côté serveur
    // et la fermeture de la session.
    std::lock_guard<std::mutex> lock(secure_send_mutex);
    std::vector<uint8_t> msg = drone->send_secure_message(msg_type, json_data);
    return enqueue_message(std::move(msg), "SECURE_MSG");
}

bool DroneNetworkClient::enqueue_message(std::vector<uint8_t>&& data, const std::string& type) {
    if (transport_failed_.load() || !connected.load() || socket_fd == INVALID_SOCKET) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(send_queue_mutex);
        if (send_queue.size() >= send_queue_max) {
            if (type == "VIDEO_DATA") {
                ++dropped_video_frames_;
                if (should_log_backpressure(last_video_drop_log_tp_, std::chrono::milliseconds(1000))) {
                    logger->warning("[VIDEO] Drop frame (queue full, depth=" +
                                    std::to_string(send_queue.size()) +
                                    ", dropped=" + std::to_string(dropped_video_frames_) + ")");
                }
                return false;
            }
            auto video_it = std::find_if(send_queue.begin(), send_queue.end(),
                                         [](const auto& item) { return item.second == "VIDEO_DATA"; });
            if (video_it != send_queue.end()) {
                send_queue.erase(video_it);
                ++dropped_video_frames_;
                ++preserved_control_messages_;
                if (should_log_backpressure(last_control_pressure_log_tp_, std::chrono::milliseconds(1000))) {
                    logger->warning("Control traffic preserved under backpressure: oldest VIDEO_DATA evicted "
                                    "(saved=" + std::to_string(preserved_control_messages_) +
                                    ", dropped_video=" + std::to_string(dropped_video_frames_) + ")");
                }
            } else {
                logger->warning("Drop message (queue full, no video to evict) type=" + type);
                return false;
            }
        }
        send_queue.emplace_back(std::move(data), type);
    }
    send_queue_cv.notify_one();
    return true;
}

void DroneNetworkClient::send_loop() {
    while (send_running.load() && connected.load() && !transport_failed_.load()) {
        std::pair<std::vector<uint8_t>, std::string> item;
        {
            std::unique_lock<std::mutex> lock(send_queue_mutex);
            send_queue_cv.wait_for(lock, std::chrono::milliseconds(200), [this] {
                return !send_queue.empty() || !send_running.load();
            });
            if (!send_running.load() || !connected.load() || transport_failed_.load()) {
                return;
            }
            if (send_queue.empty()) {
                continue;
            }
            item = std::move(send_queue.front());
            send_queue.pop_front();
        }
        if (!send_message(item.first, item.second)) {
            if (!connected.load() || transport_failed_.load()) {
                send_running.store(false);
                send_queue_cv.notify_all();
                return;
            }
            if (item.second != "VIDEO_DATA" ||
                should_log_backpressure(last_send_fail_log_tp_, std::chrono::milliseconds(1000))) {
                logger->warning("Send failed (queued) type=" + item.second);
            }
        }
    }
}

bool DroneNetworkClient::should_log_backpressure(std::chrono::steady_clock::time_point& last_log,
                                                 std::chrono::milliseconds interval) {
    const auto now = std::chrono::steady_clock::now();
    if (last_log.time_since_epoch().count() == 0 || (now - last_log) >= interval) {
        last_log = now;
        return true;
    }
    return false;
}

std::vector<uint8_t> DroneNetworkClient::receive_message(int timeout) {
    (void)timeout;
    try {
        std::vector<uint8_t> header(4);
        if (transport_read_all(header.data(), header.size()) != 4) {
            logger->warning("Connexion fermÃ©e par le serveur");
            return {};
        }

        uint32_t expected_size = (static_cast<uint32_t>(header[0]) << 24) |
                                 (static_cast<uint32_t>(header[1]) << 16) |
                                 (static_cast<uint32_t>(header[2]) << 8)  |
                                 (static_cast<uint32_t>(header[3]));

        if (expected_size > policy_.max_control_msg_bytes) {
            logger->error("Taille de message trop grande: " + std::to_string(expected_size));
            return {};
        }

        std::vector<uint8_t> data(expected_size);
        if (expected_size > 0) {
            if (transport_read_all(data.data(), expected_size) != static_cast<int>(expected_size)) {
                logger->warning("DonnÃ©es incomplÃ¨tes");
                return {};
            }
        }

        stats.bytes_received.fetch_add(static_cast<uint64_t>(expected_size) + 4U);
        stats.messages_received.fetch_add(1);

        return data;
    } catch (const std::exception& e) {
        error_handler->handle_exception(e, ErrorSeverity::ERROR, ErrorCategory::NETWORK, "receive_message");
        mark_transport_failure("receive_message: " + std::string(e.what()));
        return {};
    }
}

bool DroneNetworkClient::handshake() {
    logger->info("============================================================");
    logger->info("DÃ‰BUT HANDSHAKE HESIA");
    logger->info("============================================================");
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    try {
        // ------------------------------------------------------------
        // 1. Ã‰change HELLO
        // ------------------------------------------------------------
        logger->info("[1/5] Ã‰change HELLO");
        Hello hello = drone->build_hello();
        std::vector<uint8_t> hello_data = Serializer::serialize_hello(hello);
        
        if (!send_message(hello_data, "HELLO")) {
            throw std::runtime_error("Ã‰chec envoi HELLO");
        }
        
        logger->info("âœ“ HELLO envoyÃ©");
        
        // ------------------------------------------------------------
        // 2. Recevoir HELLO_ACK + KEY_INIT
        // ------------------------------------------------------------
        logger->info("[2/5] Attente HELLO_ACK + KEY_INIT");
        
        // Recevoir HELLO_ACK
        std::vector<uint8_t> hello_ack_data = receive_message();
        if (hello_ack_data.empty()) {
            throw std::runtime_error("Pas de HELLO_ACK reÃ§u");
        }
        
        HelloAck ack = Serializer::deserialize_hello_ack(hello_ack_data);
        drone->handle_hello_ack(ack);
        logger->info("âœ“ HELLO_ACK validÃ©");
        
        // Recevoir KEY_INIT (immÃ©diatement aprÃ¨s HELLO_ACK)
        std::vector<uint8_t> key_init_data = receive_message();
        if (key_init_data.empty()) {
            throw std::runtime_error("Pas de KEY_INIT reÃ§u aprÃ¨s HELLO_ACK");
        }
        
        KeyInit key_init = Serializer::deserialize_key_init(key_init_data);
        logger->info("âœ“ KeyInit reÃ§u - Taille clÃ© Kyber: " + std::to_string(key_init.kyber_pubkey.size()) + " bytes");
        
        // ------------------------------------------------------------
        // 3. Ã‰change KYBER (KEY_RESP)
        // ------------------------------------------------------------
        logger->info("[3/5] Ã‰change KYBER");
        KeyResp key_resp = drone->handle_key_init(key_init);
        std::vector<uint8_t> key_resp_data = Serializer::serialize_key_resp(key_resp);
        
        if (!send_message(key_resp_data, "KEY_RESP")) {
            throw std::runtime_error("Ã‰chec envoi KEY_RESP");
        }
        
        // VÃ©rification anti-DoS: timeout et binding contexte
        auto current_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time);
        
        if (duration.count() > 10000) { // 10 secondes max
            throw SecurityViolation("Timeout rÃ©ponse serveur - possible DoS par fenÃªtre rejeu");
        }
        
        // Attendre KEY_CONFIRM du serveur (preuve + binding transcript)
        std::vector<uint8_t> key_confirm_data = receive_message();
        if (key_confirm_data.empty()) {
            throw std::runtime_error("Pas de KEY_CONFIRM reÃ§u");
        }

        KeyConfirm kc = Serializer::deserialize_key_confirm(key_confirm_data);

        // Transcript = HELLO || HELLO_ACK || KEY_INIT || KEY_RESP
        std::vector<uint8_t> transcript;
        transcript.reserve(hello_data.size() + hello_ack_data.size() + key_init_data.size() + key_resp_data.size());
        transcript.insert(transcript.end(), hello_data.begin(), hello_data.end());
        transcript.insert(transcript.end(), hello_ack_data.begin(), hello_ack_data.end());
        transcript.insert(transcript.end(), key_init_data.begin(), key_init_data.end());
        transcript.insert(transcript.end(), key_resp_data.begin(), key_resp_data.end());
        std::vector<uint8_t> transcript_hash = hash_data(transcript);

        // VÃ©rifier KEY_CONFIRM + signature serveur AVANT d'accepter la session
        drone->handle_key_confirm(kc, transcript_hash);
        logger->info("âœ“ KEY_CONFIRM vÃ©rifiÃ© (transcript + signature serveur)");
        
        // ------------------------------------------------------------
        // 4. DRONE AUTH
        // ------------------------------------------------------------
        logger->info("[4/5] Authentification Drone");
        BlockDroneAuth drone_auth = drone->build_drone_auth();
        std::vector<uint8_t> drone_auth_data = Serializer::serialize_drone_auth(drone_auth);
        
        if (!send_message(drone_auth_data, "DRONE_AUTH")) {
            throw std::runtime_error("Ã‰chec envoi DRONE_AUTH");
        }
        
        // ------------------------------------------------------------
        // 5. SERVER AUTH
        // ------------------------------------------------------------
        logger->info("[5/5] Authentification Serveur");
        std::vector<uint8_t> server_auth_data = receive_message();
        if (server_auth_data.empty()) {
            throw std::runtime_error("Pas de BlockServerAuth reÃ§u");
        }
        
        BlockServerAuth server_auth = Serializer::deserialize_server_auth(server_auth_data);
        logger->info("âœ“ SERVER_AUTH validÃ©");
        std::vector<uint8_t> encrypted_msg = drone->handle_server_auth(server_auth);
        
        // ------------------------------------------------------------
        // 6. CONFIRMATION
        // ------------------------------------------------------------
        logger->info("[6/6] Confirmation mutuelle");
        std::vector<uint8_t> confirm = drone->build_confirm();
        
        if (!send_message(confirm, "CONFIRM")) {
            throw std::runtime_error("Ã‰chec envoi CONFIRM");
        }
        
        logger->info("âœ“ CONFIRM envoyÃ©");
        
        // Attendre rÃ©ponse finale
        std::vector<uint8_t> resp = receive_message();
        if (resp.empty()) {
            throw std::runtime_error("Pas d'ACK CONFIRM reÃ§u");
        }
        drone->finalize_confirm_ok(resp);
        logger->info("âœ“ CONFIRM ACK reÃ§u");
        
        logger->info("============================================================");
        logger->info("âœ“ SESSION HESIA Ã‰TABLIE");
        logger->info("  Drone ID: " + drone->get_drone_id());
        logger->info("============================================================");

        if (audit) {
            audit->event("HANDSHAKE_OK", "INFO", "drone_id=" + drone->get_drone_id());
        }
        
        return true;
    } catch (const std::exception& e) {
        logger->error("âœ— Ã‰chec handshake: " + std::string(e.what()));
        if (audit) {
            audit->event("HANDSHAKE_FAIL", "ERROR", "err=" + std::string(e.what()));
        }
        return false;
    }
}

void DroneNetworkClient::send_secure_ping() {
    try {
        logger->info("ðŸ“¤ Envoi message sÃ©curisÃ© PING #" + std::to_string(ping_counter));
        
        std::stringstream json_data;
        json_data << "{\"timestamp\":\"" << std::time(nullptr) << "\",\"drone_id\":\""
                  << drone->get_drone_id() << "\",\"counter\":" << ping_counter << "}";

        ping_counter++;

        if (!enqueue_secure_message("PING", json_data.str())) {
            logger->error("Ã‰chec envoi ping (queue full)");
            return;
        }
    } catch (const std::exception& e) {
        logger->error("Erreur ping: " + std::string(e.what()));
    }
}

void DroneNetworkClient::send_secure_telemetry() {
    try {
        TelemetrySnapshot t = collect_telemetry_snapshot();

        std::ostringstream json_data;
        json_data << std::fixed << std::setprecision(2);
        json_data << "{"
                  << "\"ts_ms\":" << t.ts_ms
                  << ",\"cpu_temp_c\":" << t.cpu_temp_c
                  << ",\"cpu_usage_pct\":" << t.cpu_usage_pct
                  << ",\"ram_used_mb\":" << t.ram_used_mb
                  << ",\"ram_total_mb\":" << t.ram_total_mb
                  << ",\"voltage_v\":" << t.voltage_v
                  << ",\"current_a\":" << t.current_a
                  << ",\"power_w\":" << t.power_w
                  << ",\"gps_lat\":" << t.gps_lat
                  << ",\"gps_lon\":" << t.gps_lon
                  << ",\"gps_alt_m\":" << t.gps_alt_m
                  << "}";

        if (!enqueue_secure_message("TELEMETRY", json_data.str())) {
            logger->warning("Ã‰chec envoi TELEMETRY (queue full)");
            return;
        }
        logger->debug("âœ… TELEMETRY envoyÃ©e");
    } catch (const std::exception& e) {
        logger->warning("Erreur TELEMETRY: " + std::string(e.what()));
    }
}

void DroneNetworkClient::telemetry_loop() {
    while (telemetry_running.load() && connected.load()) {
        send_secure_telemetry();
        for (int i = 0; i < 10; ++i) {
            if (!telemetry_running.load() || !connected.load()) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void DroneNetworkClient::close() {
    send_running.store(false);
    send_queue_cv.notify_all();
    if (send_thread.joinable()) {
        send_thread.join();
    }

    if (tls_enabled && tls) {
        tls->close();
    }

    telemetry_running.store(false);
    if (telemetry_thread.joinable()) {
        telemetry_thread.join();
    }

    if (socket_fd != INVALID_SOCKET) {
        ::close_socket(socket_fd);
        socket_fd = INVALID_SOCKET;
        logger->info("Connexion fermÃ©e");
    }
    connected.store(false);
}

bool DroneNetworkClient::init_video_pipeline() {
    logger->info("Initialisation du pipeline vidÃ©o...");
    
    try {
        video_manager = std::make_unique<VideoManager>();
        
        // YOLO avec paramÃ¨tres optimisÃ©s
        yolo_processor = std::make_unique<YOLOTrackerProcessor>(
            0,    // GPU ID
            10,   // max_objects (augmentÃ©)
            0.25f, // conf_threshold (baissÃ© pour plus de dÃ©tections)
            0.45f, // nms_iou_threshold
            0.35f, // tracker_iou_thr
            5     // tracker_max_lost
        );
        
        // MiDaS
        try {
            midas_processor = std::make_unique<MiDaSProcessor>(0);
            logger->info("âœ“ MiDaS initialisÃ©");
        } catch (const std::exception& e) {
            logger->warning("MiDaS non disponible: " + std::string(e.what()));
            midas_processor.reset();
        }
        
        video_manager->start_display();
        
        video_running = true;
        video_thread = std::thread(&DroneNetworkClient::video_processing_loop, this);
        
        logger->info("âœ“ Pipeline vidÃ©o initialisÃ©");
        logger->info("  YOLO: " + std::string(yolo_processor ? "ACTIF" : "INACTIF"));
        logger->info("  MiDaS: " + std::string(midas_processor ? "ACTIF" : "INACTIF"));
        
        return true;
    } catch (const std::exception& e) {
        error_handler->handle_exception(e, ErrorSeverity::ERROR, ErrorCategory::VIDEO, "init_video_pipeline");
        logger->error("Ã‰chec initialisation pipeline: " + std::string(e.what()));
        return false;
    }
}

void DroneNetworkClient::video_processing_loop() {
    logger->info("DÃ©marrage du flux vidÃ©o...");
    
    try {
        using clock = std::chrono::steady_clock;
        const int target_frame_ms = 33; // ~30 FPS
        auto last_report = clock::now();
        int frames_since_report = 0;
        
        while (video_running && connected.load()) {
            auto loop_start = clock::now();
            
            // RÃ©cupÃ©rer une frame
            auto [ret, frame] = video_manager->get_frame();
            if (!ret || frame.empty()) {
                logger->info("Fin du flux vidÃ©o");
                break;
            }
            
            int frame_id = frame_counter++;
            
            // Traitement YOLO
            auto [yolo_frame, tracked, depthstate, selected] = yolo_processor->process(frame_id, frame);
            
            // Convertir les dÃ©tections
            std::vector<std::vector<float>> detections;
            for (const auto& [tid, det] : tracked) {
                detections.push_back({det.x1, det.y1, det.x2, det.y2, 
                                    static_cast<float>(det.cls), det.conf});
            }
            
            video_manager->receive_yolo_result(frame_id, yolo_frame, detections);
            
            // Traitement MiDaS
            cv::Mat midas_frame;
            std::vector<float> skel_state;
            
            if (midas_processor) {
                try {
                    auto [midas_colored, midas_map, deep_skel] = midas_processor->process(frame_id, frame);
                    midas_frame = midas_colored;
                    skel_state = deep_skel;
                    
                    // Log Deep-Skel si valeurs significatives
                    if (!skel_state.empty() && skel_state.size() >= 2) {
                        float gradient = skel_state[skel_state.size() - 2];
                        float asymmetry = skel_state[skel_state.size() - 1];
                        
                        if (std::abs(gradient) > 0.05f || std::abs(asymmetry) > 0.05f) {
                            logger->debug("Deep-Skel gradient=" + std::to_string(gradient) + 
                                        " asymmetry=" + std::to_string(asymmetry));
                        }
                    }
                } catch (const std::exception& e) {
                    logger->warning("Erreur MiDaS: " + std::string(e.what()));
                    midas_frame = cv::Mat::zeros(frame.rows, frame.cols, CV_8UC3);
                }
            } else {
                midas_frame = cv::Mat::zeros(frame.rows, frame.cols, CV_8UC3);
            }
            video_manager->receive_midas_result(frame_id, midas_frame);
            
            // Envoyer via canal vidÃ©o
            VideoChannel* video_channel = drone->get_video_channel();
            logger->debug("VÃ©rification canal vidÃ©o: " + std::string(video_channel ? "DISPONIBLE" : "NON DISPONIBLE"));
            
            
            if (video_channel) {
                send_video_frame(yolo_frame, midas_frame, frame_id);
                stats.video_frames_sent.fetch_add(1);
            } else {
                logger->warning("Canal vidÃ©o non disponible - frame non envoyÃ©e au serveur");
            }
            
            auto now = clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - last_report).count() >= 5) {
                if (frames_since_report > 0) {
                    double fps = frames_since_report * 1000.0 / 
                               std::chrono::duration_cast<std::chrono::milliseconds>(now - last_report).count();
                    
                    logger->info("[PERF] FPS: " + std::to_string(fps) + 
                               ", YOLO tracks: " + std::to_string(tracked.size()) +
                               ", Frames total: " + std::to_string(frame_id));
                }
                last_report = now;
                frames_since_report = 0;
            }
            
            // Log toutes les 30 frames
            if (frame_id % 30 == 0) {
                logger->info("Frame " + std::to_string(frame_id) + 
                           " - YOLO: " + std::to_string(tracked.size()) + " objets");
            }
            
            // ContrÃ´le FPS
            auto loop_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                clock::now() - loop_start).count();
            
            if (loop_ms < target_frame_ms) {
                std::this_thread::sleep_for(std::chrono::milliseconds(target_frame_ms - loop_ms));
            }
        }
    } catch (const std::exception& e) {
        error_handler->handle_exception(e, ErrorSeverity::ERROR, ErrorCategory::VIDEO, "video_processing_loop");
    }
    logger->info("Flux vidÃ©o terminÃ©");
}

void DroneNetworkClient::stop_video() {
    logger->info("ArrÃªt du pipeline vidÃ©o...");
    
    // ArrÃªter le pipeline clean
    stop_clean_pipeline();
    
    // ArrÃªter l'ancien pipeline
    workers_running = false;
    video_running = false;
    
    if (video_thread.joinable()) {
        video_thread.join();
    }
    
    if (video_manager) {
        video_manager->cleanup();
    }
    
    // Nettoyage
    {
        std::lock_guard<std::mutex> lock(frame_pool_mutex);
        frame_buffer_pool.clear();
    }
    
    logger->info("Pipeline vidÃ©o arrÃªtÃ©");
}

void DroneNetworkClient::print_stats() {
    auto start_ms = stats.start_time_ms.load();
    if (start_ms > 0) {
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        auto seconds = static_cast<long long>((now_ms - start_ms) / 1000);
        
        logger->info("============================================================");
        logger->info("STATISTIQUES");
        logger->info("============================================================");
        logger->info("DurÃ©e: " + std::to_string(seconds) + " secondes");
        logger->info("Octets envoyÃ©s: " + std::to_string(stats.bytes_sent.load()));
        logger->info("Octets reÃ§us: " + std::to_string(stats.bytes_received.load()));
        logger->info("Messages envoyÃ©s: " + std::to_string(stats.messages_sent.load()));
        logger->info("Messages reÃ§us: " + std::to_string(stats.messages_received.load()));
        logger->info("Frames vidÃ©o envoyÃ©es: " + std::to_string(stats.video_frames_sent.load()));
        logger->info("============================================================");
    }
}

int DroneNetworkClient::main() {
    std::string HOST = policy_.server_host.empty() ? std::string("127.0.0.1") : policy_.server_host;
    int PORT = policy_.server_port > 0 ? policy_.server_port : 9000;

    if (policy_.incident_mode) {
        logger->error("Incident mode enabled - refusing to connect");
        if (audit) {
            audit->event("INCIDENT_MODE", "ERROR", "drone refusing to connect");
        }
        return 1;
    }

    try {
        // Connexion
        if (!connect(HOST, PORT)) {
            return 1;
        }
        
        // Handshake
        if (!handshake()) {
            logger->error("Handshake Ã©chouÃ©");
            return 1;
        }

        // Start async sender (prevents pipeline stalls on slow TLS writes)
        send_running.store(true);
        send_thread = std::thread(&DroneNetworkClient::send_loop, this);

        // Start telemetry heartbeat (independent of video pipeline)
        telemetry_running.store(true);
        telemetry_thread = std::thread(&DroneNetworkClient::telemetry_loop, this);
        
        // Initialiser le pipeline clean (remplace l'ancien pipeline)
        logger->info("ðŸ”„ Initialisation du CleanPipeline aprÃ¨s handshake...");
        init_clean_pipeline();
        
        // Envoyer des messages de test
        logger->info("Envoi des messages de test...");
        
        send_secure_ping();
        
        // Messages supplÃ©mentaires
        try {
            std::stringstream flight_data;
            flight_data << "{\"altitude\":120.5,\"battery\":87,\"speed\":15.2,\"heading\":270}";
            enqueue_secure_message("FLIGHT_DATA", flight_data.str());
        } catch (const std::exception& e) {
            logger->warning("Erreur FLIGHT_DATA: " + std::string(e.what()));
        }
        
        // Main loop pour surveiller le pipeline vidÃ©o
        auto video_start = std::chrono::steady_clock::now();
        while (video_running && connected.load()) {
            auto now = std::chrono::steady_clock::now();
            
            // VÃ©rifications runtime
            // if (!RuntimeProtection::check_honeypot_triggers()) { // CommentÃ© pour Ã©viter erreur
            //     logger->error("âš ï¸ Honeypot dÃ©clenchÃ© - Attaque dÃ©tectÃ©e");
            //     RuntimeProtection::emergency_shutdown();
            //     break;
            // }
            
            // VÃ©rification basique remplacÃ©e
            if (false) { // Placeholder pour vÃ©rification honeypot
                logger->warning("VÃ©rification honeypot dÃ©sactivÃ©e pour compilation");
            }
            
            if (!RuntimeProtection::check_honeypot_triggers()) {
                logger->error("Honeypot declenche ou altere");
                RuntimeProtection::emergency_shutdown();
                break;
            }

            if (!RuntimeProtection::self_healing_check()) {
                logger->warning("âš ï¸ Auto-rÃ©paration requise");
            }
            
            logger->debug("âœ… VÃ©rifications sÃ©curitÃ© OK");

            // Afficher un message toutes les 10 secondes
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - video_start).count();
            
            if (elapsed % 10 == 0 && elapsed > 0) {
                logger->info("Pipeline sÃ©curisÃ© en cours depuis " + std::to_string(elapsed) + " secondes");
                
                // Afficher l'Ã©tat des protections
                auto attestation = RuntimeProtection::generate_attestation_report();
                logger->debug("ðŸ›¡ï¸ Attestation runtime: " + std::to_string(attestation.size()) + " bytes");
            }
            
            // Attendre un peu avant la prochaine vÃ©rification
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        // Afficher les statistiques
        print_stats();
        
        // Afficher les stats du CleanPipeline
        if (clean_pipeline) {
            auto pipeline_stats = get_clean_pipeline_stats();
            logger->info("CleanPipeline Stats - FPS: " + std::to_string(pipeline_stats.avg_fps) + 
                        ", YOLO: " + std::to_string(pipeline_stats.avg_yolo_ms) + "ms" +
                        ", MiDaS: " + std::to_string(pipeline_stats.avg_midas_ms) + "ms");
        }
        
    } catch (const std::exception& e) {
        logger->error("Erreur: " + std::string(e.what()));
        transport_failed_.store(true);
    }
    
    stop_video();
    close();
    return transport_failed_.load() ? 1 : 0;
}

cv::Mat DroneNetworkClient::get_pooled_frame() {
    std::lock_guard<std::mutex> lock(frame_pool_mutex);
    if (!frame_buffer_pool.empty()) {
        cv::Mat frame = frame_buffer_pool.back();
        frame_buffer_pool.pop_back();
        return frame;
    }
    return cv::Mat();
}

void DroneNetworkClient::return_pooled_frame(cv::Mat&& frame) {
    std::lock_guard<std::mutex> lock(frame_pool_mutex);
    if (frame_buffer_pool.size() < FRAME_POOL_SIZE) {
        frame_buffer_pool.emplace_back(std::move(frame));
    }
}

void DroneNetworkClient::init_clean_pipeline() {
    logger->info("Initialisation du CleanPipeline...");
    
    if (!clean_pipeline) {
        clean_pipeline = std::make_unique<CleanPipeline>();
        
        // Configurer les GPU (utiliser GPU 0 par dÃƒÂ©faut)
        clean_pipeline->set_gpu_ids(0, 0);
        
        // Configure the callback that forwards processed frames to the server.
        clean_pipeline->set_frame_callback([this](const cv::Mat& yolo_frame, const cv::Mat& midas_frame, int frame_id) {
            if (video_log_enabled && (frame_id % video_log_every_n == 0)) {
                logger->info("[CALLBACK] CleanPipeline callback for frame " + std::to_string(frame_id));
            }

            // Send the processed frame to the server.
            send_video_frame(yolo_frame, midas_frame, frame_id);

            // Update counters without spamming the logs on every frame.
            stats.video_frames_sent.fetch_add(1);
            if (video_log_enabled && (frame_id % video_log_every_n == 0)) {
                logger->info("[CALLBACK] Frame " + std::to_string(frame_id) + " forwarded");
            }
        });
        
        if (!clean_pipeline->start()) {
            throw std::runtime_error("Impossible de demarrer le CleanPipeline");
        }
        
        logger->info("CleanPipeline demarre avec succes avec callback reseau");
    }
}

void DroneNetworkClient::stop_clean_pipeline() {
    if (clean_pipeline) {
        logger->info("Arret du CleanPipeline...");
        clean_pipeline->stop();
        clean_pipeline.reset();
        logger->info("CleanPipeline arrete");
    }
}

CleanPipeline::PipelineStats DroneNetworkClient::get_clean_pipeline_stats() const {
    if (clean_pipeline) {
        return clean_pipeline->get_stats();
    }
    return CleanPipeline::PipelineStats{};
}

// ImplÃƒÂ©mentation de la fonction send_video_frame
void DroneNetworkClient::send_video_frame(const cv::Mat& yolo_frame, const cv::Mat& midas_frame, int frame_id) {
    try {
        if (!connected.load() || socket_fd == INVALID_SOCKET) {
            logger->warning("Tentative d'envoi vid?o sans connexion");
            return;
        }

        const bool verbose = video_log_enabled && (frame_id % video_log_every_n == 0);
        {
            std::lock_guard<std::mutex> lock(send_queue_mutex);
            if (policy_.video_min_send_interval_ms > 0) {
                const auto now = std::chrono::steady_clock::now();
                if (last_video_enqueue_tp_.time_since_epoch().count() != 0 &&
                    now - last_video_enqueue_tp_ <
                        std::chrono::milliseconds(policy_.video_min_send_interval_ms)) {
                    return;
                }
                last_video_enqueue_tp_ = now;
            }
            if (send_queue.size() >= send_queue_max) {
                ++dropped_video_frames_;
                if (verbose) {
                    logger->warning("[VIDEO] Drop frame (send queue full) #" + std::to_string(frame_id) +
                                    " depth=" + std::to_string(send_queue.size()));
                }
                return;
            }
        }

        if (yolo_frame.empty()) {
            logger->warning("Frame YOLO vide pour l'envoi");
            return;
        }

        if (verbose) {
            logger->info("[VIDEO] ?? Envoi frame #" + std::to_string(frame_id) + " - taille: " +
                         std::to_string(yolo_frame.cols) + "x" + std::to_string(yolo_frame.rows));
        }

        cv::Mat frame_to_send;
        if (!midas_frame.empty() && midas_frame.size() == yolo_frame.size()) {
            cv::hconcat(yolo_frame, midas_frame, frame_to_send);
        } else {
            frame_to_send = yolo_frame.clone();
        }

        const int MAX_WIDTH = 426;
        const int MAX_HEIGHT = 240;
        if (frame_to_send.cols != MAX_WIDTH || frame_to_send.rows != MAX_HEIGHT) {
            cv::resize(frame_to_send, frame_to_send,
                       cv::Size(MAX_WIDTH, MAX_HEIGHT), 0, 0, cv::INTER_LINEAR);
            if (verbose) {
                logger->info("[VIDEO] ?? Frame redimensionn?e ? " + std::to_string(MAX_WIDTH) +
                             "x" + std::to_string(MAX_HEIGHT) + " pour frame #" + std::to_string(frame_id));
            }
        }

        std::vector<uint8_t> jpeg_buffer;
        if (!cv::imencode(".jpg", frame_to_send, jpeg_buffer, jpeg_params)) {
            logger->error("?chec encodage JPEG");
            return;
        }

        if (jpeg_buffer.size() > policy_.max_frame_bytes) {
            logger->error("[VIDEO] Frame trop grande (" + std::to_string(jpeg_buffer.size()) +
                          " > " + std::to_string(policy_.max_frame_bytes) + ") - drop");
            return;
        }

        if (verbose) {
            logger->info("[VIDEO] ?? Frame #" + std::to_string(frame_id) +
                         " encod?e: " + std::to_string(jpeg_buffer.size()) + " bytes");
        }

        VideoChannel* video_channel = drone->get_video_channel();
        if (video_channel) {
            if (verbose) {
                logger->info("[VIDEO] Cr?ation VideoPacket pour frame #" + std::to_string(frame_id) +
                            " - taille JPEG: " + std::to_string(jpeg_buffer.size()) + " bytes");
            }

            VideoPacket packet = video_channel->encrypt_frame(jpeg_buffer);
            std::vector<uint8_t> serialized_packet = packet.serialize();

            if (verbose) {
                logger->info("[VIDEO] VideoPacket s?rialis? - frame #" + std::to_string(frame_id) +
                            " - taille totale: " + std::to_string(serialized_packet.size()) + " bytes" +
                            " - stream_id: " + std::to_string(packet.stream_id) +
                            " - frame_id: " + std::to_string(packet.frame_id));
            }

            if (!enqueue_message(std::move(serialized_packet), "VIDEO_DATA")) {
                logger->error("[VIDEO] ?chec envoi frame #" + std::to_string(frame_id));
            } else if (verbose) {
                logger->info("[VIDEO] ? Frame #" + std::to_string(frame_id) +
                            " mise en file avec succ?s (" + std::to_string(jpeg_buffer.size()) + " bytes)");
            }
        } else {
            logger->error("VideoChannel non disponible pour l'envoi");
            return;
        }

        stats.video_frames_sent.fetch_add(1);

    } catch (const cv::Exception& e) {
        logger->error("Erreur OpenCV: " + std::string(e.what()) +
                     " dans send_video_frame (frame #" + std::to_string(frame_id) + ")");
        error_handler->handle_exception(e, ErrorSeverity::WARNING, ErrorCategory::VIDEO, "send_video_frame");
    }
}


void DroneNetworkClient::log_combined_frame(const cv::Mat& yolo_frame, const cv::Mat& midas_frame, int frame_id) {
    (void)yolo_frame;
    (void)midas_frame;
    (void)frame_id;
    return;
}
} // namespace hesia


