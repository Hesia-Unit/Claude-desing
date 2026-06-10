#include "security_audit.hpp"

#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <system_error>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace hesia {

namespace {

static uint64_t now_ms() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
}

static void chmod_owner_only(const std::filesystem::path& path) {
#ifndef _WIN32
    chmod(path.c_str(), 0600);
#else
    (void)path;
#endif
}

static std::string json_escape(const std::string& s) {
    static const char kHex[] = "0123456789abcdef";
    std::ostringstream oss;
    for (char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        switch (c) {
            case '\\': oss << "\\\\"; break;
            case '\"': oss << "\\\""; break;
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            case '\b': oss << "\\b"; break;
            case '\f': oss << "\\f"; break;
            default:
                // Échappe tout caractère de contrôle restant (< 0x20) et DEL
                // pour garantir un JSON valide et empêcher l'injection de
                // séparateurs de ligne dans le journal d'audit chaîné.
                if (c < 0x20 || c == 0x7F) {
                    oss << "\\u00" << kHex[(c >> 4) & 0x0F] << kHex[c & 0x0F];
                } else {
                    oss << ch;
                }
                break;
        }
    }
    return oss.str();
}

static void write_u64_be(std::vector<uint8_t>& out, uint64_t v) {
    for (int i = 7; i >= 0; --i) {
        out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
    }
}

static void write_u32_be(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(v & 0xFF));
}

static void write_u16_be(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(v & 0xFF));
}

static uint16_t read_u16_be(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) |
                                 static_cast<uint16_t>(p[1]));
}

static uint32_t read_u32_be(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

static std::vector<uint8_t> sha256_bytes(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> out(SHA256_DIGEST_LENGTH);
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        throw std::runtime_error("EVP_MD_CTX_new failed");
    }
    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1 ||
        EVP_DigestUpdate(ctx, data.data(), data.size()) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("EVP sha256 init/update failed");
    }
    unsigned int out_len = 0;
    if (EVP_DigestFinal_ex(ctx, out.data(), &out_len) != 1 || out_len != SHA256_DIGEST_LENGTH) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("EVP sha256 final failed");
    }
    EVP_MD_CTX_free(ctx);
    return out;
}

} // namespace

SecurityAudit::SecurityAudit(const std::string& role,
                             const std::string& log_dir,
                             std::shared_ptr<Logger> logger,
                             const AuditConfig& config)
    : role_(role),
      logger_(std::move(logger)) {
    enabled_ = config.enabled;
    log_path_ = config.log_path.empty()
        ? (std::filesystem::path(log_dir) / "audit.log")
        : std::filesystem::path(config.log_path);
    std::filesystem::path secure_dir = config.secure_dir.empty()
        ? std::filesystem::path(log_dir)
        : std::filesystem::path(config.secure_dir);
    key_path_ = config.key_path.empty()
        ? (secure_dir / "audit.key")
        : std::filesystem::path(config.key_path);

    if (!config.alert_path.empty()) {
        alert_path_ = std::filesystem::path(config.alert_path);
    }
    if (!config.export_path.empty()) {
        export_path_ = std::filesystem::path(config.export_path);
    }
    if (!config.signing_key_path.empty()) {
        signing_key_path_ = std::filesystem::path(config.signing_key_path);
    }
    if (!config.signing_pub_path.empty()) {
        signing_pub_path_ = std::filesystem::path(config.signing_pub_path);
    }
    require_signing_ = config.require_signing;
    rotate_on_start_ = config.rotate_on_start;
    rotate_interval_sec_ = config.rotate_interval_sec;
    last_rotate_ms_ = now_ms();

    if (!enabled_) {
        return;
    }

    load_or_create_key(false);
    load_last_hash_from_log();
}

void SecurityAudit::rotate_key_if_requested() {
    if (!enabled_) return;
    if (rotate_on_start_) {
        load_or_create_key(true);
        rotate_on_start_ = false;
        last_rotate_ms_ = now_ms();
        if (logger_) {
            logger_->info("Audit key rotated");
        }
    }
}

void SecurityAudit::load_or_create_key(bool rotate) {
    if (!enabled_) return;
    if (!rotate && std::filesystem::exists(key_path_)) {
        std::ifstream f(key_path_, std::ios::binary);
        key_ = std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
        if (key_.size() == 32) return;
        key_.clear();
    }

    if (rotate && std::filesystem::exists(key_path_)) {
        std::filesystem::path backup = key_path_;
        backup += ".bak." + std::to_string(now_ms());
        std::error_code ec;
        std::filesystem::rename(key_path_, backup, ec);
        if (ec && logger_) {
            logger_->warning("Audit key backup failed: " + ec.message());
        }
    }

    key_.assign(32, 0);
    if (RAND_bytes(key_.data(), static_cast<int>(key_.size())) != 1) {
        throw std::runtime_error("RAND_bytes failed for audit key");
    }

    if (!key_path_.parent_path().empty()) {
        std::filesystem::create_directories(key_path_.parent_path());
    }
    std::ofstream f(key_path_, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) {
        throw std::runtime_error("Cannot write audit key: " + key_path_.string());
    }
    f.write(reinterpret_cast<const char*>(key_.data()), static_cast<std::streamsize>(key_.size()));
    f.close();
    chmod_owner_only(key_path_);
}

void SecurityAudit::maybe_rotate_key() {
    if (!enabled_ || rotate_interval_sec_ == 0) return;
    const uint64_t now = now_ms();
    if (now - last_rotate_ms_ < rotate_interval_sec_ * 1000ULL) return;
    load_or_create_key(true);
    last_rotate_ms_ = now;
    if (logger_) {
        logger_->info("Audit key rotated (interval)");
    }
}

void SecurityAudit::load_last_hash_from_log() {
    last_hash_.assign(32, 0x00);
    if (!std::filesystem::exists(log_path_)) {
        return;
    }

    std::ifstream f(log_path_, std::ios::binary);
    if (!f.is_open()) {
        return;
    }

    while (true) {
        uint8_t magic[4];
        f.read(reinterpret_cast<char*>(magic), sizeof(magic));
        if (!f) break;
        if (!(magic[0] == 'H' && magic[1] == 'E' && magic[2] == 'S' && magic[3] == '2')) {
            break;
        }

        uint8_t version = 0;
        f.read(reinterpret_cast<char*>(&version), sizeof(version));
        if (!f) break;
        if (version != 2) {
            break;
        }

        uint8_t fixed[8 + 12 + 4 + 16 + 32 + 32 + 2];
        f.read(reinterpret_cast<char*>(fixed), sizeof(fixed));
        if (!f) break;

        const uint32_t ct_len = read_u32_be(fixed + 8 + 12);
        const uint16_t sig_len = read_u16_be(fixed + 8 + 12 + 4 + 16 + 32 + 32);

        if (sig_len > 0) {
            f.seekg(sig_len, std::ios::cur);
            if (!f) break;
        }
        if (ct_len > 0) {
            f.seekg(ct_len, std::ios::cur);
            if (!f) break;
        }

        last_hash_.assign(fixed + 8 + 12 + 4 + 16 + 32, fixed + 8 + 12 + 4 + 16 + 32 + 32);
    }
}

std::vector<uint8_t> SecurityAudit::sign_record_hash(const std::vector<uint8_t>& hash) {
    if (hash.empty()) {
        return {};
    }
    if (signing_key_path_.empty()) {
        if (require_signing_) {
            throw std::runtime_error("Audit signing key required but not configured");
        }
        return {};
    }

    FILE* fp = fopen(signing_key_path_.string().c_str(), "rb");
    if (!fp) {
        throw std::runtime_error("Cannot open audit signing key: " + signing_key_path_.string());
    }
    EVP_PKEY* pkey = PEM_read_PrivateKey(fp, nullptr, nullptr, nullptr);
    fclose(fp);
    if (!pkey) {
        throw std::runtime_error("Failed to read audit signing key: " + signing_key_path_.string());
    }

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        EVP_PKEY_free(pkey);
        throw std::runtime_error("EVP_MD_CTX_new failed");
    }
    int ok = EVP_DigestSignInit(ctx, nullptr, nullptr, nullptr, pkey);
    if (ok != 1) {
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        throw std::runtime_error("EVP_DigestSignInit failed");
    }

    size_t sig_len = 0;
    ok = EVP_DigestSign(ctx, nullptr, &sig_len, hash.data(), hash.size());
    if (ok != 1 || sig_len == 0) {
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        throw std::runtime_error("EVP_DigestSign size failed");
    }

    std::vector<uint8_t> sig(sig_len);
    ok = EVP_DigestSign(ctx, sig.data(), &sig_len, hash.data(), hash.size());
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    if (ok != 1) {
        throw std::runtime_error("EVP_DigestSign failed");
    }
    sig.resize(sig_len);
    return sig;
}

void SecurityAudit::maybe_alert(const std::string& severity, const std::string& message) {
    if (alert_path_.empty()) return;
    if (!(severity == "WARN" || severity == "ERROR" || severity == "CRITICAL")) return;
    if (!alert_path_.parent_path().empty()) {
        std::filesystem::create_directories(alert_path_.parent_path());
    }
    std::ofstream f(alert_path_, std::ios::app);
    if (!f.is_open()) return;
    f << now_ms() << " " << severity << " " << message << "\n";
}

void SecurityAudit::append_encrypted_record(const std::string& plaintext) {
    if (!enabled_) return;
    if (key_.size() != 32) {
        throw std::runtime_error("Audit key invalid size");
    }

    std::vector<uint8_t> nonce(12);
    if (RAND_bytes(nonce.data(), static_cast<int>(nonce.size())) != 1) {
        throw std::runtime_error("RAND_bytes failed for nonce");
    }

    std::vector<uint8_t> ciphertext(plaintext.size());
    std::vector<uint8_t> tag(16);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) throw std::runtime_error("EVP_CIPHER_CTX_new failed");
    int ok = EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
    if (ok != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_EncryptInit_ex failed");
    }
    ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size()), nullptr);
    if (ok != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_CTRL_GCM_SET_IVLEN failed");
    }
    ok = EVP_EncryptInit_ex(ctx, nullptr, nullptr, key_.data(), nonce.data());
    if (ok != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_EncryptInit_ex key/iv failed");
    }

    int out_len = 0;
    ok = EVP_EncryptUpdate(ctx,
                           ciphertext.data(),
                           &out_len,
                           reinterpret_cast<const unsigned char*>(plaintext.data()),
                           static_cast<int>(plaintext.size()));
    if (ok != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_EncryptUpdate failed");
    }
    int total = out_len;
    ok = EVP_EncryptFinal_ex(ctx, ciphertext.data() + total, &out_len);
    if (ok != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_EncryptFinal_ex failed");
    }
    total += out_len;
    ciphertext.resize(static_cast<size_t>(total));
    ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, static_cast<int>(tag.size()), tag.data());
    EVP_CIPHER_CTX_free(ctx);
    if (ok != 1) {
        throw std::runtime_error("EVP_CTRL_GCM_GET_TAG failed");
    }

    if (last_hash_.size() != 32) {
        last_hash_.assign(32, 0x00);
    }

    std::vector<uint8_t> header;
    header.reserve(4 + 1 + 8 + 12 + 4 + 16 + 32);
    header.insert(header.end(), {'H','E','S','2'});
    header.push_back(2); // version
    write_u64_be(header, now_ms());
    header.insert(header.end(), nonce.begin(), nonce.end());
    write_u32_be(header, static_cast<uint32_t>(ciphertext.size()));
    header.insert(header.end(), tag.begin(), tag.end());
    header.insert(header.end(), last_hash_.begin(), last_hash_.end());

    std::vector<uint8_t> hash_input;
    hash_input.reserve(header.size() + ciphertext.size());
    hash_input.insert(hash_input.end(), header.begin(), header.end());
    hash_input.insert(hash_input.end(), ciphertext.begin(), ciphertext.end());
    std::vector<uint8_t> record_hash = sha256_bytes(hash_input);

    std::vector<uint8_t> sig = sign_record_hash(record_hash);
    if (sig.size() > 0xFFFF) {
        throw std::runtime_error("Audit signature too large");
    }
    uint16_t sig_len = static_cast<uint16_t>(sig.size());

    if (!log_path_.parent_path().empty()) {
        std::filesystem::create_directories(log_path_.parent_path());
    }
    std::ofstream f(log_path_, std::ios::binary | std::ios::app);
    if (!f.is_open()) {
        throw std::runtime_error("Cannot open audit log: " + log_path_.string());
    }
    f.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
    f.write(reinterpret_cast<const char*>(record_hash.data()), static_cast<std::streamsize>(record_hash.size()));
    std::vector<uint8_t> sig_len_bytes;
    sig_len_bytes.reserve(2);
    write_u16_be(sig_len_bytes, sig_len);
    f.write(reinterpret_cast<const char*>(sig_len_bytes.data()), static_cast<std::streamsize>(sig_len_bytes.size()));
    if (!sig.empty()) {
        f.write(reinterpret_cast<const char*>(sig.data()), static_cast<std::streamsize>(sig.size()));
    }
    if (!ciphertext.empty()) {
        f.write(reinterpret_cast<const char*>(ciphertext.data()), static_cast<std::streamsize>(ciphertext.size()));
    }
    f.close();

    if (!export_path_.empty()) {
        if (!export_path_.parent_path().empty()) {
            std::filesystem::create_directories(export_path_.parent_path());
        }
        std::ofstream ef(export_path_, std::ios::binary | std::ios::app);
        if (ef.is_open()) {
            ef.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
            ef.write(reinterpret_cast<const char*>(record_hash.data()), static_cast<std::streamsize>(record_hash.size()));
            ef.write(reinterpret_cast<const char*>(sig_len_bytes.data()), static_cast<std::streamsize>(sig_len_bytes.size()));
            if (!sig.empty()) {
                ef.write(reinterpret_cast<const char*>(sig.data()), static_cast<std::streamsize>(sig.size()));
            }
            if (!ciphertext.empty()) {
                ef.write(reinterpret_cast<const char*>(ciphertext.data()), static_cast<std::streamsize>(ciphertext.size()));
            }
        }
    }

    last_hash_ = std::move(record_hash);
}

void SecurityAudit::event(const std::string& type,
                          const std::string& severity,
                          const std::string& message) {
    if (!enabled_) return;
    try {
        maybe_rotate_key();
        std::ostringstream oss;
        oss << "{\"ts\":" << now_ms()
            << ",\"role\":\"" << json_escape(role_)
            << "\",\"type\":\"" << json_escape(type)
            << "\",\"severity\":\"" << json_escape(severity)
            << "\",\"msg\":\"" << json_escape(message) << "\"}";

        append_encrypted_record(oss.str());
        maybe_alert(severity, message);
    } catch (const std::exception& e) {
        if (logger_) {
            logger_->warning(std::string("Audit log failure: ") + e.what());
        }
    }
}

} // namespace hesia
