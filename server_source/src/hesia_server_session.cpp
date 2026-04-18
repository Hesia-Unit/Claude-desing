#include "hesia_server_session.hpp"

#include <chrono>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <unordered_set>
#include <algorithm>

#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include "net_framing.hpp"
#include "tls_utils.hpp"
#include "security_audit.hpp"
#include "../../../IMPLEMENTATION/serialization.hpp"
#include "../../../IMPLEMENTATION/crypto_real.hpp"
#include "../../../IMPLEMENTATION/security_utils.hpp"
#include "../../../IMPLEMENTATION/exceptions.hpp"

namespace hesia {

static bool env_flag_enabled(const char* name) {
    const char* value = std::getenv(name);
    if (!value || !*value) {
        return false;
    }
    std::string normalized(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on";
}

static std::string sanitize_path_component(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (unsigned char c : input) {
        if (std::isalnum(c) || c == '.' || c == '_' || c == '-') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('_');
        }
    }
    if (out.empty()) {
        out = "client";
    }
    return out;
}

static uint64_t now_ms() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
}

// Fonction pour sauvegarder les frames déchiffrées
static void save_decrypted_frame(const std::vector<uint8_t>& frame_data,
                                 uint64_t frame_id,
                                 const std::string& client_id,
                                 const std::filesystem::path& base_dir) {
    try {
        if (base_dir.empty()) {
            return;
        }
        // Creer le dossier forensic s'il n'existe pas
        std::filesystem::path frames_dir = base_dir;
        if (!std::filesystem::exists(frames_dir)) {
            std::filesystem::create_directories(frames_dir);
        }

        // Creer un sous-dossier pour ce client
        std::filesystem::path client_dir = frames_dir / sanitize_path_component(client_id);
        if (!std::filesystem::exists(client_dir)) {
            std::filesystem::create_directories(client_dir);
        }

        // Nom de fichier avec timestamp et frame_id
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

        std::string filename = client_dir / ("frame_" + std::to_string(frame_id) + "_" + std::to_string(timestamp) + ".bin");

        // Sauvegarder les donnees brutes
        std::ofstream file(filename, std::ios::binary);
        if (file.is_open()) {
            file.write(reinterpret_cast<const char*>(frame_data.data()), frame_data.size());
            file.close();
        }

        // Sauvegarder aussi une version hex pour inspection
        std::string hex_filename = client_dir / ("frame_" + std::to_string(frame_id) + "_" + std::to_string(timestamp) + ".hex");
        std::ofstream hex_file(hex_filename);
        if (hex_file.is_open()) {
            hex_file << std::hex << std::setfill('0');
            for (size_t i = 0; i < frame_data.size(); ++i) {
                hex_file << std::setw(2) << static_cast<int>(frame_data[i]);
                if ((i + 1) % 32 == 0) hex_file << "\n";
                else if ((i + 1) % 4 == 0) hex_file << " ";
            }
            hex_file.close();
        }

    } catch (const std::exception& e) {
        // Ne pas echouer si la sauvegarde echoue, juste logger
        // (le logger n'est pas disponible ici, donc on ignore silencieusement)
        (void)e;
    }
}

static void write_binary_atomic(const std::filesystem::path& path,
                                const std::vector<uint8_t>& data) {
    try {
        if (!path.parent_path().empty()) {
            std::filesystem::create_directories(path.parent_path());
        }
        std::filesystem::path tmp = path;
        tmp += ".tmp";
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f.is_open()) return;
        if (!data.empty()) {
            f.write(reinterpret_cast<const char*>(data.data()), data.size());
        }
        f.close();
        std::error_code ec;
        std::filesystem::rename(tmp, path, ec);
        if (ec) {
            std::filesystem::remove(tmp, ec);
        }
    } catch (...) {
        // best-effort
    }
}

static void write_text_atomic(const std::filesystem::path& path, const std::string& text) {
    try {
        if (!path.parent_path().empty()) {
            std::filesystem::create_directories(path.parent_path());
        }
        std::filesystem::path tmp = path;
        tmp += ".tmp";
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f.is_open()) return;
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
        f.close();
        std::error_code ec;
        std::filesystem::rename(tmp, path, ec);
        if (ec) {
            std::filesystem::remove(tmp, ec);
        }
    } catch (...) {
        // best-effort
    }
}

static bool json_extract_number(const std::string& s, const std::string& key, double& out) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = s.find(needle);
    if (pos == std::string::npos) return false;
    pos = s.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;
    pos += 1;
    while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) pos++;
    if (pos >= s.size()) return false;
    char* endptr = nullptr;
    const char* start = s.c_str() + pos;
    double val = std::strtod(start, &endptr);
    if (endptr == start) return false;
    out = val;
    return true;
}

static void write_telemetry_json(const std::filesystem::path& ui_dir,
                                 const std::string& drone_id,
                                 const std::string& payload) {
    if (ui_dir.empty()) return;
    double cpu_temp = -1.0, cpu_usage = -1.0, ram_used = -1.0, ram_total = -1.0;
    double voltage = -1.0, current = -1.0, power = -1.0;
    double gps_lat = 0.0, gps_lon = 0.0, gps_alt = 0.0;
    double ts_ms = 0.0;

    json_extract_number(payload, "cpu_temp_c", cpu_temp);
    json_extract_number(payload, "cpu_usage_pct", cpu_usage);
    json_extract_number(payload, "ram_used_mb", ram_used);
    json_extract_number(payload, "ram_total_mb", ram_total);
    json_extract_number(payload, "voltage_v", voltage);
    json_extract_number(payload, "current_a", current);
    json_extract_number(payload, "power_w", power);
    json_extract_number(payload, "gps_lat", gps_lat);
    json_extract_number(payload, "gps_lon", gps_lon);
    json_extract_number(payload, "gps_alt_m", gps_alt);
    json_extract_number(payload, "ts_ms", ts_ms);
    if (ts_ms <= 0.0) {
        ts_ms = static_cast<double>(now_ms());
    }

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    oss << "{"
        << "\"ts_ms\":" << ts_ms
        << ",\"drone_id\":\"" << drone_id << "\""
        << ",\"cpu_temp_c\":" << cpu_temp
        << ",\"cpu_usage_pct\":" << cpu_usage
        << ",\"ram_used_mb\":" << ram_used
        << ",\"ram_total_mb\":" << ram_total
        << ",\"voltage_v\":" << voltage
        << ",\"current_a\":" << current
        << ",\"power_w\":" << power
        << ",\"gps_lat\":" << gps_lat
        << ",\"gps_lon\":" << gps_lon
        << ",\"gps_alt_m\":" << gps_alt
        << "}";

    write_text_atomic(ui_dir / "telemetry.json", oss.str());
}

static std::string hex_prefix(const std::vector<uint8_t>& v, size_t n=16) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < v.size() && i < n; ++i) {
        oss << std::setw(2) << static_cast<int>(v[i]);
    }
    if (v.size() > n) oss << "...";
    return oss.str();
}

static std::string bytes_to_hex(const std::vector<uint8_t>& v) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (uint8_t b : v) {
        oss << std::setw(2) << static_cast<int>(b);
    }
    return oss.str();
}

static std::vector<uint8_t> sha256_bytes(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> out(SHA256_DIGEST_LENGTH);
    SHA256(data.data(), data.size(), out.data());
    return out;
}

static std::string trim_copy(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(start, end - start);
}

static std::string normalize_hex_line(const std::string& line) {
    std::string t = trim_copy(line);
    if (t.empty()) return t;
    if (t[0] == '#') return std::string();
    const std::string sha256_prefix = "sha256:";
    const std::string sha3512_prefix = "sha3-512:";
    if (t.rfind(sha256_prefix, 0) == 0) {
        t = t.substr(sha256_prefix.size());
    } else if (t.rfind(sha3512_prefix, 0) == 0) {
        t = t.substr(sha3512_prefix.size());
    }
    t = trim_copy(t);
    t.erase(std::remove_if(t.begin(), t.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
    }), t.end());
    std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return t;
}

static std::unordered_set<std::string> load_hex_list(const std::string& path, size_t expected_bytes) {
    std::unordered_set<std::string> out;
    if (!std::filesystem::exists(path)) return out;
    std::ifstream f(path);
    if (!f.is_open()) return out;
    std::string line;
    const size_t expected_len = expected_bytes * 2;
    while (std::getline(f, line)) {
        std::string hex = normalize_hex_line(line);
        if (hex.empty()) continue;
        if (hex.size() != expected_len) continue;
        out.insert(hex);
    }
    return out;
}

class RateLimiter {
public:
    RateLimiter(std::uint64_t rate_bytes_per_sec, std::uint64_t burst_bytes)
        : rate_per_sec_(rate_bytes_per_sec),
          burst_bytes_(burst_bytes ? burst_bytes : rate_bytes_per_sec),
          tokens_(static_cast<double>(burst_bytes_)),
          last_refill_(std::chrono::steady_clock::now()) {}

    bool consume(std::size_t bytes) {
        if (rate_per_sec_ == 0) {
            return true;
        }
        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(now - last_refill_).count();
        if (elapsed > 0) {
            tokens_ = std::min<double>(static_cast<double>(burst_bytes_),
                                       tokens_ + elapsed * static_cast<double>(rate_per_sec_));
            last_refill_ = now;
        }
        if (tokens_ < static_cast<double>(bytes)) {
            return false;
        }
        tokens_ -= static_cast<double>(bytes);
        return true;
    }

private:
    std::uint64_t rate_per_sec_;
    std::uint64_t burst_bytes_;
    double tokens_;
    std::chrono::steady_clock::time_point last_refill_;
};

HesiaServerSession::HesiaServerSession(SSL* ssl,
                                       const std::string& client_label,
                                       const std::string& keys_dir,
                                       std::shared_ptr<Logger> logger,
                                       std::shared_ptr<SecurityAudit> audit,
                                       const SecurityPolicy& policy)
    : ssl_(ssl),
      client_label_(client_label),
      keys_dir_(keys_dir),
      log_(std::move(logger)),
      audit_(std::move(audit)),
      policy_(policy) {
    last_block_hash_.assign(64, 0x00);
    rate_limiter_ = std::make_unique<RateLimiter>(policy_.rate_limit_bps, policy_.rate_limit_burst);
}

HesiaServerSession::~HesiaServerSession() = default;

void HesiaServerSession::run() {
    if (audit_) {
        audit_->event("SESSION_START", "INFO", "client=" + client_label_);
    }
    // Require TLS exporter + cert digest (max alignment / hybrid binding required).
    tls_exporter_secret_ = tls_exporter_32(ssl_);
    if (tls_exporter_secret_.size() != 32) {
        throw std::runtime_error("TLS exporter length invalid");
    }
    tls_server_cert_sha256_ = tls_server_cert_sha256_der(ssl_);
    if (tls_server_cert_sha256_.size() != 32) {
        throw std::runtime_error("TLS server cert SHA256 length invalid");
    }

    log_->info("TLS exporter OK (32B), cert_sha256(DER)=" + hex_prefix(tls_server_cert_sha256_, 16));
    if (audit_) {
        audit_->event("TLS_BINDING_OK", "INFO", "client=" + client_label_);
    }

    load_server_keys();
    init_kyber();

    step_hello();
    step_key_exchange();
    step_drone_auth();
    step_confirm();

    secure_loop();
}

void HesiaServerSession::load_server_keys() {
    auto read_bin = [](const std::filesystem::path& path) -> std::vector<uint8_t> {
        std::ifstream f(path, std::ios::binary);
        if (!f) throw std::runtime_error("Cannot open key file: " + path.string());
        std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (buf.empty()) throw std::runtime_error("Empty key file: " + path.string());
        return buf;
    };

    struct KeyPairCandidate {
        const char* secret_name;
        const char* public_name;
        bool demo;
    };
    const std::vector<KeyPairCandidate> candidates = {
        {"server_secret.bin", "server_public.bin", false},
        {"mldsa87_secret.bin", "mldsa87_public.bin", false},
        {"demo_secret.bin", "demo_public.bin", true},
    };

    std::filesystem::path selected_secret;
    std::filesystem::path selected_public;
    bool using_demo_pair = false;
    for (const auto& candidate : candidates) {
        const std::filesystem::path secret_path = std::filesystem::path(keys_dir_) / candidate.secret_name;
        const std::filesystem::path public_path = std::filesystem::path(keys_dir_) / candidate.public_name;
        if (std::filesystem::exists(secret_path) && std::filesystem::exists(public_path)) {
            selected_secret = secret_path;
            selected_public = public_path;
            using_demo_pair = candidate.demo;
            break;
        }
    }

    if (selected_secret.empty() || selected_public.empty()) {
        throw std::runtime_error("Missing server ML-DSA keypair in keys dir. "
                                 "Expected server_secret.bin/server_public.bin or mldsa87_* files.");
    }
    if (policy_.prod_fuse && using_demo_pair) {
        throw std::runtime_error("Refusing demo ML-DSA server keys in production mode");
    }

    server_sk_ = read_bin(selected_secret);
    server_pk_ = read_bin(selected_public);

    log_->info("Server ML-DSA keys loaded (pk=" + std::to_string(server_pk_.size()) +
               "B, sk=" + std::to_string(server_sk_.size()) + "B)");

    load_drone_pubkey();
}

void HesiaServerSession::load_drone_pubkey() {
    auto read_bin = [](const std::string& path) -> std::vector<uint8_t> {
        std::ifstream f(path, std::ios::binary);
        if (!f) throw std::runtime_error("Cannot open key file: " + path);
        std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (buf.empty()) throw std::runtime_error("Empty key file: " + path);
        return buf;
    };

    std::string path;
    if (!policy_.drone_pubkey_file.empty()) {
        path = policy_.drone_pubkey_file;
    } else {
        const std::string p1 = keys_dir_ + "/drone_public.bin";
        const std::string p2 = keys_dir_ + "/dilithium5_pk.bin";
        if (std::filesystem::exists(p1)) {
            path = p1;
        } else if (std::filesystem::exists(p2)) {
            path = p2;
        }
    }

    if (!path.empty()) {
        std::filesystem::path p(path);
        if (!p.is_absolute()) {
            path = (std::filesystem::path(keys_dir_) / p).string();
        }
    }

    require_pinned_drone_key_ = policy_.prod_fuse || policy_.require_pinned_drone_pubkey || !path.empty();

    if (!path.empty()) {
        expected_drone_pubkey_ = read_bin(path);
        log_->info("Pinned drone public key loaded (" + std::to_string(expected_drone_pubkey_.size()) +
                   "B) from " + path);
        return;
    }

    if (require_pinned_drone_key_) {
        throw std::runtime_error("Pinned drone public key required but not found. "
                                 "Set policy drone_pubkey_file or place drone_public.bin in keys dir.");
    }

    log_->warning("No pinned drone public key configured; accepting key from DRONE_AUTH");
}

void HesiaServerSession::init_kyber() {
    auto kp = Kyber::generate_keypair();
    kyber_pk_ = std::move(kp.first);
    kyber_sk_ = std::move(kp.second);
    log_->info(std::string("Kyber: ") + Kyber::ALG + " (pk=" + std::to_string(kyber_pk_.size()) +
               "B, sk=" + std::to_string(kyber_sk_.size()) + "B)");
}

std::vector<uint8_t> HesiaServerSession::hkdf_session_key(const std::vector<uint8_t>& shared,
                                                          const std::vector<uint8_t>& nonce_s,
                                                          const std::vector<uint8_t>& nonce_d,
                                                          const std::vector<uint8_t>& session_id,
                                                          const std::vector<uint8_t>& tls_exporter_32) {
    EVP_PKEY_CTX* kdf = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
    if (!kdf) throw std::runtime_error("EVP_PKEY_CTX_new_id failed");

    auto fail = [&](const char* msg) {
        EVP_PKEY_CTX_free(kdf);
        throw std::runtime_error(msg);
    };

    if (EVP_PKEY_derive_init(kdf) <= 0) fail("EVP_PKEY_derive_init failed");
    if (EVP_PKEY_CTX_set_hkdf_md(kdf, EVP_sha3_512()) <= 0) fail("EVP_PKEY_CTX_set_hkdf_md failed");

    // Salt = nonce_s || nonce_d || session_id || tls_exporter(32)  (required)
    std::vector<uint8_t> salt;
    salt.reserve(nonce_s.size() + nonce_d.size() + session_id.size() + 32);
    salt.insert(salt.end(), nonce_s.begin(), nonce_s.end());
    salt.insert(salt.end(), nonce_d.begin(), nonce_d.end());
    salt.insert(salt.end(), session_id.begin(), session_id.end());
    if (tls_exporter_32.size() != 32) fail("tls_exporter_32 invalid size");
    salt.insert(salt.end(), tls_exporter_32.begin(), tls_exporter_32.end());

    if (EVP_PKEY_CTX_set1_hkdf_salt(kdf, salt.data(), static_cast<int>(salt.size())) <= 0) fail("set1_hkdf_salt failed");
    if (EVP_PKEY_CTX_set1_hkdf_key(kdf, shared.data(), static_cast<int>(shared.size())) <= 0) fail("set1_hkdf_key failed");

    const std::string info = "HESIA_SESSION_KEY_v1";
    if (EVP_PKEY_CTX_add1_hkdf_info(kdf,
        reinterpret_cast<const unsigned char*>(info.data()),
        static_cast<int>(info.size())) <= 0) fail("add1_hkdf_info failed");

    std::vector<uint8_t> out(32);
    size_t out_len = out.size();
    if (EVP_PKEY_derive(kdf, out.data(), &out_len) <= 0 || out_len != out.size()) fail("EVP_PKEY_derive failed");
    EVP_PKEY_CTX_free(kdf);
    return out;
}

void HesiaServerSession::step_hello() {
    std::vector<uint8_t> hello_bytes;
    if (!recv_frame(ssl_, hello_bytes, policy_.max_control_msg_bytes)) {
        throw std::runtime_error("Failed to receive HELLO");
    }
    t_hello_ = hello_bytes;

    Hello hello = Serializer::deserialize_hello(hello_bytes);

    if ((hello.features & 0x01u) == 0u) {
        throw std::runtime_error("Client does not speak HESIA v1");
    }

    // Require exporter binding support for maximal alignment (hybrid is mandatory).
    if ((hello.features & 0x02u) == 0u) {
        throw std::runtime_error("Client HELLO missing exporter support (0x02). TLS hybrid required.");
    }

    // HELLO_ACK.response_hash = SHA3-512(hello.random_64 || "DRONE_SALT")
    const std::string salt_str = "DRONE_SALT";
    std::vector<uint8_t> input;
    input.reserve(hello.random_64.size() + salt_str.size());
    input.insert(input.end(), hello.random_64.begin(), hello.random_64.end());
    input.insert(input.end(), salt_str.begin(), salt_str.end());
    std::vector<uint8_t> response_hash = hash_data(input);

    HelloAck ack;
    ack.response_hash = response_hash;

    // Capabilities: 0x02 => exporter binding enabled (both sides support it and server has exporter)
    ack.capabilities = 0x02u;

    std::vector<uint8_t> ack_bytes = Serializer::serialize_hello_ack(ack);
    t_hello_ack_ = ack_bytes;

    if (!send_frame(ssl_, ack_bytes)) {
        throw std::runtime_error("Failed to send HELLO_ACK");
    }

    // Build KEY_INIT
    nonce_s_.assign(16, 0);
    if (RAND_bytes(nonce_s_.data(), static_cast<int>(nonce_s_.size())) != 1) {
        throw std::runtime_error("RAND_bytes nonce_s failed");
    }

    session_id_.assign(16, 0);
    if (RAND_bytes(session_id_.data(), static_cast<int>(session_id_.size())) != 1) {
        throw std::runtime_error("RAND_bytes session_id failed");
    }

    const uint64_t ts = now_ms();

    // context_hash = SHA3-512("HESIA_CTX_v1" || kyber_pk || nonce_s || session_id)
    static const std::string ctx_label = "HESIA_CTX_v1";
    std::vector<uint8_t> ctx;
    ctx.reserve(ctx_label.size() + kyber_pk_.size() + nonce_s_.size() + session_id_.size());
    ctx.insert(ctx.end(), ctx_label.begin(), ctx_label.end());
    ctx.insert(ctx.end(), kyber_pk_.begin(), kyber_pk_.end());
    ctx.insert(ctx.end(), nonce_s_.begin(), nonce_s_.end());
    ctx.insert(ctx.end(), session_id_.begin(), session_id_.end());
    std::vector<uint8_t> context_hash = hash_data(ctx);

    KeyInit init;
    init.kyber_pubkey = kyber_pk_;
    init.nonce_s = nonce_s_;
    init.session_id = session_id_;
    init.timestamp = ts;
    init.context_hash = context_hash;

    std::vector<uint8_t> init_bytes = Serializer::serialize_key_init(init);
    t_key_init_ = init_bytes;

    if (!send_frame(ssl_, init_bytes)) {
        throw std::runtime_error("Failed to send KEY_INIT");
    }

    state_ = ServerState::KEY_INIT_SENT;
    log_->info("HELLO/ACK/KEY_INIT ok (session_id len=" + std::to_string(session_id_.size()) + ")");
}

void HesiaServerSession::step_key_exchange() {
    if (state_ != ServerState::KEY_INIT_SENT) {
        throw std::runtime_error("Invalid state before KEY_RESP");
    }

    std::vector<uint8_t> key_resp_bytes;
    if (!recv_frame(ssl_, key_resp_bytes, policy_.max_control_msg_bytes)) {
        throw std::runtime_error("Failed to receive KEY_RESP");
    }
    t_key_resp_ = key_resp_bytes;

    KeyResp resp = Serializer::deserialize_key_resp(key_resp_bytes);

    if (!ConstantTime::equals(resp.session_id, session_id_)) {
        throw std::runtime_error("KEY_RESP session_id mismatch");
    }

    if (resp.nonce_d.size() < 16 || resp.nonce_d.size() > 64) {
        throw std::runtime_error("KEY_RESP nonce_d invalid size");
    }

    // Decapsulate
    std::vector<uint8_t> shared = Kyber::decaps(kyber_sk_, resp.kyber_ciphertext);

    // Derive session_key (AES-256 key)
    session_key_ = hkdf_session_key(shared, nonce_s_, resp.nonce_d, session_id_, tls_exporter_secret_);

    // Verify response_hash = SHA3-512(ciphertext||nonce_d||session_id||session_key)
    std::vector<uint8_t> rh_data;
    rh_data.reserve(resp.kyber_ciphertext.size() + resp.nonce_d.size() + resp.session_id.size() + session_key_.size());
    rh_data.insert(rh_data.end(), resp.kyber_ciphertext.begin(), resp.kyber_ciphertext.end());
    rh_data.insert(rh_data.end(), resp.nonce_d.begin(), resp.nonce_d.end());
    rh_data.insert(rh_data.end(), resp.session_id.begin(), resp.session_id.end());
    rh_data.insert(rh_data.end(), session_key_.begin(), session_key_.end());
    std::vector<uint8_t> expected_rh = hash_data(rh_data);

    if (!ConstantTime::equals(expected_rh, resp.response_hash)) {
        throw SecurityViolation("Response hash invalide - manipulation détectée");
    }

    // Transcript = HELLO || HELLO_ACK || KEY_INIT || KEY_RESP
    std::vector<uint8_t> transcript;
    transcript.reserve(t_hello_.size() + t_hello_ack_.size() + t_key_init_.size() + t_key_resp_.size());
    transcript.insert(transcript.end(), t_hello_.begin(), t_hello_.end());
    transcript.insert(transcript.end(), t_hello_ack_.begin(), t_hello_ack_.end());
    transcript.insert(transcript.end(), t_key_init_.begin(), t_key_init_.end());
    transcript.insert(transcript.end(), t_key_resp_.begin(), t_key_resp_.end());
    transcript_hash_ = hash_data(transcript);

    // KEY_CONFIRM with server signature (ML-DSA-87)
    KeyConfirm kc;
    kc.session_id = session_id_;
    kc.transcript_hash = transcript_hash_;
    kc.timestamp = now_ms();

    // msg_to_sign = session_id || transcript_hash || timestamp(8)
    std::vector<uint8_t> msg_to_sign;
    msg_to_sign.reserve(session_id_.size() + transcript_hash_.size() + 8);
    msg_to_sign.insert(msg_to_sign.end(), session_id_.begin(), session_id_.end());
    msg_to_sign.insert(msg_to_sign.end(), transcript_hash_.begin(), transcript_hash_.end());
    uint64_t ts = kc.timestamp;
    for (int i = 7; i >= 0; --i) msg_to_sign.push_back(static_cast<uint8_t>((ts >> (8*i)) & 0xFF));

    kc.signature = Dilithium::sign(server_sk_, msg_to_sign);

    std::vector<uint8_t> kc_bytes = Serializer::serialize_key_confirm(kc);
    if (!send_frame(ssl_, kc_bytes)) {
        throw std::runtime_error("Failed to send KEY_CONFIRM");
    }

    // IMPORTANT: Do not send any extra compatibility frames (KEY_OK). Drone C++ does not expect it.
    state_ = ServerState::KEY_EXCHANGE_DONE;
    log_->info("KEY_EXCHANGE ok (session_key=32B, transcript_hash=64B)");
}

void HesiaServerSession::step_drone_auth() {
    if (state_ != ServerState::KEY_EXCHANGE_DONE) {
        throw std::runtime_error("Invalid state before DRONE_AUTH");
    }

    std::vector<uint8_t> auth_bytes;
    if (!recv_frame(ssl_, auth_bytes, policy_.max_control_msg_bytes)) {
        throw std::runtime_error("Failed to receive DRONE_AUTH");
    }

    BlockDroneAuth auth = Serializer::deserialize_drone_auth(auth_bytes);

    // Strict PoP requirements
    if (auth.session_id.size() != 16) {
        throw SecurityViolation("DRONE_AUTH: session_id manquant/invalid");
    }
    if (auth.transcript_hash.size() != 64) {
        throw SecurityViolation("DRONE_AUTH: transcript_hash manquant/invalid");
    }
    if (auth.server_cert_sha256.size() != 32) {
        throw SecurityViolation("DRONE_AUTH: server_cert_sha256 manquant (PoP TLS)");
    }

    if (!ConstantTime::equals(auth.session_id, session_id_)) {
        throw SecurityViolation("DRONE_AUTH: session_id mismatch");
    }
    if (!ConstantTime::equals(auth.transcript_hash, transcript_hash_)) {
        throw SecurityViolation("DRONE_AUTH: transcript_hash mismatch");
    }
    if (!ConstantTime::equals(auth.server_cert_sha256, tls_server_cert_sha256_)) {
        throw SecurityViolation("DRONE_AUTH: server_cert_sha256 mismatch (pinning mismatch or MITM)");
    }

    // Validate chaining
    if (!ConstantTime::equals(auth.last_block_hash, last_block_hash_)) {
        throw SecurityViolation("DRONE_AUTH: last_block_hash mismatch");
    }

    // Rebuild payload (must match drone build_drone_auth)
    std::vector<uint8_t> payload;
    payload.reserve(
        auth.drone_id.size() + auth.firmware_hash.size() + auth.puf_response.size() + auth.last_block_hash.size() +
        auth.session_id.size() + auth.transcript_hash.size() + auth.server_cert_sha256.size()
    );
    payload.insert(payload.end(), auth.drone_id.begin(), auth.drone_id.end());
    payload.insert(payload.end(), auth.firmware_hash.begin(), auth.firmware_hash.end());
    payload.insert(payload.end(), auth.puf_response.begin(), auth.puf_response.end());
    payload.insert(payload.end(), auth.last_block_hash.begin(), auth.last_block_hash.end());
    payload.insert(payload.end(), auth.session_id.begin(), auth.session_id.end());
    payload.insert(payload.end(), auth.transcript_hash.begin(), auth.transcript_hash.end());
    payload.insert(payload.end(), auth.server_cert_sha256.begin(), auth.server_cert_sha256.end());

    if (!expected_drone_pubkey_.empty()) {
        if (!ConstantTime::equals(auth.drone_pubkey, expected_drone_pubkey_)) {
            throw SecurityViolation("DRONE_AUTH: drone_pubkey mismatch (pinned key required)");
        }
        drone_pubkey_ = expected_drone_pubkey_;
    } else {
        drone_pubkey_ = auth.drone_pubkey;
    }

    if (!Dilithium::verify(drone_pubkey_, payload, auth.signature)) {
        throw InvalidSignature("DRONE_AUTH: signature invalide");
    }

    // Remote attestation: firmware allowlist (policy-controlled, fail-closed)
    std::string allow_path = keys_dir_ + "/firmware_allowlist.txt";
    const bool require_attest = policy_.require_attestation;
    if (require_attest) {
        auto allow = load_hex_list(allow_path, 64);
        if (allow.empty()) {
            throw SecurityViolation("Attestation required but allowlist missing/empty");
        }
        const std::string fw_hex = bytes_to_hex(auth.firmware_hash);
        if (allow.find(fw_hex) == allow.end()) {
            if (audit_) {
                audit_->event("ATTEST_FAIL", "ERROR", "client=" + client_label_);
            }
            throw SecurityViolation("Firmware hash not in allowlist");
        }
        if (audit_) {
            audit_->event("ATTEST_OK", "INFO", "client=" + client_label_ + " fw=" + hex_prefix(auth.firmware_hash, 16));
        }
    }

    // Revocation list (SHA-256 of drone public key)
    std::string revoke_path = keys_dir_ + "/revoked_drones.txt";
    if (std::filesystem::exists(revoke_path)) {
        auto revoked = load_hex_list(revoke_path, 32);
        if (!revoked.empty()) {
            const std::string fp_hex = bytes_to_hex(sha256_bytes(drone_pubkey_));
            if (revoked.find(fp_hex) != revoked.end()) {
                if (audit_) {
                    audit_->event("REVOKED_DRONE", "ERROR", "client=" + client_label_);
                }
                throw SecurityViolation("Drone key revoked");
            }
        }
    }

    if (audit_) {
        audit_->event("DRONE_AUTH_OK", "INFO", "client=" + client_label_);
    }

    // Update chain (server mirrors drone behavior)
    last_block_hash_ = hash_data(payload);

    // Build SERVER_AUTH (aligned with drone::handle_server_auth)
    BlockServerAuth sa;
    sa.server_id = "HESIA_SERVER";
    sa.server_pubkey = server_pk_;
    sa.mission_id = "MISSION_ALPHA";

    // Policy hash (placeholder, stable)
    {
        const std::string policy = "POLICY_V1:STRICT_TLS_EXPORTER+PQC+TRANSCRIPT_BINDING";
        std::vector<uint8_t> pol(policy.begin(), policy.end());
        sa.policy_hash = hash_data(pol);
    }

    sa.last_block_hash = last_block_hash_;

    std::vector<uint8_t> sa_payload;
    sa_payload.reserve(sa.server_id.size() + sa.mission_id.size() + sa.last_block_hash.size());
    sa_payload.insert(sa_payload.end(), sa.server_id.begin(), sa.server_id.end());
    sa_payload.insert(sa_payload.end(), sa.mission_id.begin(), sa.mission_id.end());
    sa_payload.insert(sa_payload.end(), sa.last_block_hash.begin(), sa.last_block_hash.end());

    sa.signature = Dilithium::sign(server_sk_, sa_payload);

    std::vector<uint8_t> sa_bytes = Serializer::serialize_server_auth(sa);
    if (!send_frame(ssl_, sa_bytes)) {
        throw std::runtime_error("Failed to send SERVER_AUTH");
    }

    // Update chain (must match drone)
    last_block_hash_ = hash_data(sa_payload);

    // Initialize channels
    secure_channel_ = std::make_unique<SecureChannel>(session_key_);
    // Video key derivation should match drone VideoChannel derivation (HKDF-SHA3-512 info).
    {
        EVP_PKEY_CTX* kdf = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
        if (!kdf) throw std::runtime_error("HKDF ctx failed");
        if (EVP_PKEY_derive_init(kdf) <= 0) { EVP_PKEY_CTX_free(kdf); throw std::runtime_error("HKDF init failed"); }
        if (EVP_PKEY_CTX_set_hkdf_md(kdf, EVP_sha3_512()) <= 0) { EVP_PKEY_CTX_free(kdf); throw std::runtime_error("HKDF md failed"); }
        // No salt for video key
        if (EVP_PKEY_CTX_set1_hkdf_key(kdf, session_key_.data(), static_cast<int>(session_key_.size())) <= 0) {
            EVP_PKEY_CTX_free(kdf); throw std::runtime_error("HKDF key failed");
        }
        const std::string info = "HESIA_VIDEO_STREAM_v1";
        if (EVP_PKEY_CTX_add1_hkdf_info(kdf,
            reinterpret_cast<const unsigned char*>(info.data()), static_cast<int>(info.size())) <= 0) {
            EVP_PKEY_CTX_free(kdf); throw std::runtime_error("HKDF info failed");
        }
        std::vector<uint8_t> video_key(32);
        size_t out_len = video_key.size();
        if (EVP_PKEY_derive(kdf, video_key.data(), &out_len) <= 0 || out_len != video_key.size()) {
            EVP_PKEY_CTX_free(kdf); throw std::runtime_error("HKDF derive failed");
        }
        EVP_PKEY_CTX_free(kdf);
        video_channel_ = std::make_unique<VideoChannel>(video_key, 1);
    }

    state_ = ServerState::SERVER_AUTH_SENT;
    log_->info("DRONE_AUTH ok, SERVER_AUTH sent");
}

void HesiaServerSession::step_confirm() {
    if (state_ != ServerState::SERVER_AUTH_SENT) {
        throw std::runtime_error("Invalid state before CONFIRM");
    }

    std::vector<uint8_t> sig;
    if (!recv_frame(ssl_, sig, policy_.max_control_msg_bytes)) {
        throw std::runtime_error("Failed to receive CONFIRM signature");
    }

    // confirm payload = "OK" || last_block_hash
    std::vector<uint8_t> payload;
    payload.reserve(2 + last_block_hash_.size());
    payload.push_back('O');
    payload.push_back('K');
    payload.insert(payload.end(), last_block_hash_.begin(), last_block_hash_.end());

    // Verify with cached drone public key from DRONE_AUTH
    if (drone_pubkey_.empty()) {
        throw std::runtime_error("CONFIRM: drone public key missing");
    }

    if (!Dilithium::verify(drone_pubkey_, payload, sig)) {
        throw InvalidSignature("CONFIRM: signature invalide");
    }

    // Send OK bytes (framed) as expected by drone
    std::vector<uint8_t> ok = {'O','K'};
    if (!send_frame(ssl_, ok)) {
        throw std::runtime_error("Failed to send CONFIRM OK");
    }

    state_ = ServerState::SECURE_SESSION;
    log_->info("SECURE_SESSION established");
    if (audit_) {
        audit_->event("SECURE_SESSION", "INFO", "client=" + client_label_);
    }
}

void HesiaServerSession::secure_loop() {
    if (state_ != ServerState::SECURE_SESSION) {
        return;
    }

    const std::filesystem::path ui_dir = policy_.ui_dir;

    // Receive loop
    while (true) {
        std::vector<uint8_t> payload;
        if (!recv_frame(ssl_, payload, policy_.max_frame_bytes)) {
            log_->info("Client disconnected");
            return;
        }
        if (payload.empty()) continue;

        if (rate_limiter_ && !rate_limiter_->consume(payload.size())) {
            log_->warning("Rate limit exceeded, closing session");
            if (audit_) {
                audit_->event("RATE_LIMIT", "WARN", "client=" + client_label_);
            }
            return;
        }

        const uint8_t type = payload[0];
        if (type == 0x01) {
            // SECURE_MSG: iv(12) || tag(16) || ciphertext(var)
            std::vector<uint8_t> blob(payload.begin() + 1, payload.end());
            if (blob.size() < 12 + 16) {
                log_->warning("SECURE_MSG too small");
                continue;
            }
            std::vector<uint8_t> iv(blob.begin(), blob.begin() + 12);
            std::vector<uint8_t> tag(blob.begin() + 12, blob.begin() + 12 + 16);
            std::vector<uint8_t> ct(blob.begin() + 12 + 16, blob.end());

            try {
                std::vector<uint8_t> pt = secure_channel_->decrypt(iv, ct, tag, last_block_hash_);
                
                // Sauvegarder le message déchiffré pour analyse
                if (env_flag_enabled("HESIA_FORENSIC_MESSAGE_CAPTURE")) {
                    save_decrypted_frame(pt, 0, client_label_ + "_secure_msg", policy_.forensic_dir);
                }
                
                // Update chaining: SHA3-512(last_block_hash || plaintext)
                std::vector<uint8_t> chain;
                chain.reserve(last_block_hash_.size() + pt.size());
                chain.insert(chain.end(), last_block_hash_.begin(), last_block_hash_.end());
                chain.insert(chain.end(), pt.begin(), pt.end());
                last_block_hash_ = hash_data(chain);

                log_->info("SECURE_MSG ok (pt_len=" + std::to_string(pt.size()) + ")");
                if (!ui_dir.empty()) {
                    std::string pt_str(pt.begin(), pt.end());
                    if (pt_str.find("\"type\":\"TELEMETRY\"") != std::string::npos) {
                        write_telemetry_json(ui_dir, client_label_, pt_str);
                        log_->info("CONST telemetry update ok");
                    }
                }
            } catch (const std::exception& e) {
                log_->error(std::string("SECURE_MSG decrypt failed: ") + e.what());
            }
        } else if (type == 0x02) {
            // VIDEO_DATA: VideoPacket blob (encrypted)
            std::vector<uint8_t> blob(payload.begin() + 1, payload.end());
            try {
                VideoPacket pkt = VideoPacket::deserialize(blob);
                std::vector<uint8_t> frame = video_channel_->decrypt_frame(pkt);

                if (frame.size() > policy_.max_frame_bytes) {
                    log_->warning("VIDEO_DATA too large after decrypt: " + std::to_string(frame.size()));
                    if (audit_) {
                        audit_->event("VIDEO_OVERSIZE", "WARN", "client=" + client_label_);
                    }
                    continue;
                }
                
                // Sauvegarder la frame dechiffree pour analyse
                if (env_flag_enabled("HESIA_FORENSIC_VIDEO_CAPTURE")) {
                    save_decrypted_frame(frame, pkt.frame_id, client_label_, policy_.forensic_dir);
                }
                if (!ui_dir.empty()) {
                    write_binary_atomic(ui_dir / "latest.jpg", frame);
                    std::ostringstream meta;
                    meta << "{"
                         << "\"frame_id\":" << pkt.frame_id
                         << ",\"bytes\":" << frame.size()
                         << ",\"ts_ms\":" << now_ms()
                         << "}";
                    write_text_atomic(ui_dir / "frame_meta.json", meta.str());
                }

                log_->info("VIDEO_DATA ok (frame_id=" + std::to_string(pkt.frame_id) + ", bytes=" + std::to_string(frame.size()) + ")");
            } catch (const std::exception& e) {
                log_->error(std::string("VIDEO_DATA handling failed: ") + e.what());
            }
        } else {
            log_->warning("Unknown payload type: " + std::to_string(type));
            if (audit_) {
                audit_->event("PROTO_ERROR", "WARN", "client=" + client_label_);
            }
            return;
        }
    }
}

} // namespace hesia
