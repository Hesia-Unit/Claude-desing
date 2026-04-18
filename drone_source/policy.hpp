#pragma once

#include <cstdint>
#include <string>

namespace hesia {

struct SecurityPolicy {
    int version = 1;
    bool require_mtls = true;
    bool require_attestation = true;
    bool prod_fuse = true;
    bool incident_mode = false;
    bool require_exporter_binding = true;
    bool require_tee_hkdf = true;
    bool allow_ephemeral_dilithium = false;
    bool allow_ephemeral_puf = false;

    int ssl_read_timeout_sec = 10;
    int ssl_write_timeout_sec = 10;

    std::string server_host = "127.0.0.1";
    int server_port = 9000;

    std::size_t max_control_msg_bytes = 10 * 1024 * 1024; // 10 MiB
    std::size_t max_frame_bytes = 2 * 1024 * 1024;        // 2 MiB

    std::uint64_t rate_limit_bps = 20'000'000; // 20 Mbps
    std::uint64_t rate_limit_burst = 4'000'000; // 4 MB

    bool tls_pin = false;
    std::uint64_t tls_rekey_bytes = 64ULL * 1024ULL * 1024ULL;
    int tls_rekey_seconds = 900;

    std::string secure_dir = "/etc/hesia/secure";
    std::string sealed_puf_path = "";
    std::string sealed_dilithium_path = "";

    bool require_oem_kdf = false;
    std::string oem_k1_path = "";
    std::string oem_k2_path = "";
    std::string oem_kdf_label = "HESIA_OEM_KDF";
    std::string oem_kdf_context = "drone";

    std::uint64_t firmware_version = 0;
    std::string firmware_version_file = "";
    bool require_boot_measure = false;
    std::string boot_measure_path = "";
    std::string boot_measure_sig_path = "";
    std::string boot_measure_pubkey_path = "";

    bool require_release_signature = false;
    std::string release_target_path = "";
    std::string release_sig_path = "";
    std::string release_pubkey_path = "";

    bool audit_enabled = true;
    std::string audit_log_path = "";
    std::string audit_key_path = "";
    std::string audit_alert_path = "";
    std::string audit_signing_key = "";
    std::string audit_signing_pub = "";
    std::string audit_export_path = "";
    bool require_audit_signing = true;
    bool audit_rotate_on_start = false;
    std::uint64_t audit_rotate_interval_sec = 0;

    std::string cert_dir = "/etc/hesia/certs";
    std::string client_cert = "drone.crt";
    std::string client_key = "drone.key";
    std::string ca_cert = "ca.crt";

    std::string policy_dir = "/etc/hesia/policy";
    std::string policy_path = "/etc/hesia/policy/policy.conf";
    std::string policy_sig_path = "/etc/hesia/policy/policy.sig";
    std::string policy_pubkey_path = "/etc/hesia/policy/policy_pub.pem";
    std::string policy_sig_pqc_path = "/etc/hesia/policy/policy.sig.pqc";
    std::string policy_pubkey_pqc_path = "/etc/hesia/policy/policy_pub.pqc";
};

SecurityPolicy load_security_policy_or_throw(const std::string& role);
std::string resolve_path(const std::string& base_dir, const std::string& value);

} // namespace hesia
