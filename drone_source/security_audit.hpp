#pragma once

#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include <cstdint>

#include "logger.hpp"

namespace hesia {

struct AuditConfig {
    bool enabled = true;
    std::string log_path;
    std::string key_path;
    std::string secure_dir;
    std::string alert_path;
    std::string signing_key_path;
    std::string signing_pub_path;
    std::string export_path;
    bool require_signing = true;
    bool rotate_on_start = false;
    std::uint64_t rotate_interval_sec = 0;
    std::string oem_k1_path;
    std::string oem_k2_path;
    std::string oem_kdf_label;
    std::string oem_kdf_context;
};

class SecurityAudit {
public:
    SecurityAudit(const std::string& role,
                  const std::string& log_dir,
                  std::shared_ptr<Logger> logger,
                  const AuditConfig& config = {});

    void rotate_key_if_requested();
    void event(const std::string& type,
               const std::string& severity,
               const std::string& message);

private:
    void load_or_create_key(bool rotate);
    void maybe_rotate_key();
    void append_encrypted_record(const std::string& plaintext);
    void maybe_alert(const std::string& severity, const std::string& message);
    std::vector<uint8_t> derive_oem_key_if_available();
    void load_last_hash_from_log();
    std::vector<uint8_t> sign_record_hash(const std::vector<uint8_t>& hash);

    std::string role_;
    std::vector<uint8_t> key_;
    std::vector<uint8_t> last_hash_;
    std::filesystem::path log_path_;
    std::filesystem::path key_path_;
    std::filesystem::path alert_path_;
    std::filesystem::path export_path_;
    std::filesystem::path signing_key_path_;
    std::filesystem::path signing_pub_path_;
    bool require_signing_{true};
    bool rotate_on_start_{false};
    std::uint64_t rotate_interval_sec_{0};
    std::uint64_t last_rotate_ms_{0};
    std::string oem_k1_path_;
    std::string oem_k2_path_;
    std::string oem_kdf_label_;
    std::string oem_kdf_context_;
    std::shared_ptr<Logger> logger_;
    bool enabled_{true};
};

} // namespace hesia
