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

    int ssl_read_timeout_sec = 10;
    int ssl_write_timeout_sec = 10;

    std::size_t max_control_msg_bytes = 10 * 1024 * 1024; // 10 MiB
    std::size_t max_frame_bytes = 2 * 1024 * 1024;        // 2 MiB

    int max_conn_total = 64;
    int max_conn_per_ip = 4;
    int max_pending_queue = 128;
    int worker_threads = 4;

    std::uint64_t rate_limit_bps = 20'000'000; // 20 Mbps
    std::uint64_t rate_limit_burst = 4'000'000; // 4 MB

    std::string bind_addr = "0.0.0.0";
    int port = 9000;

    std::string cert_dir = "/etc/hesia/certs";
    std::string server_cert = "server.crt";
    std::string server_key = "server.key";
    std::string ca_cert = "ca.crt";
    std::string keys_dir = "/etc/hesia/keys";
    std::string secure_dir = "/etc/hesia/secure";
    std::string drone_pubkey_file = "";
    bool require_pinned_drone_pubkey = false;

    std::string forensic_dir = "decrypted_frames";
    std::string ui_dir = "/var/log/hesia/ui";

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

    bool require_release_signature = false;
    std::string release_target_path = "";
    std::string release_sig_path = "";
    std::string release_pubkey_path = "";

    std::string policy_dir = "/etc/hesia/policy";
    std::string policy_path = "/etc/hesia/policy/policy.conf";
    std::string policy_sig_path = "/etc/hesia/policy/policy.sig";
    std::string policy_pubkey_path = "/etc/hesia/policy/policy_pub.pem";
};

// Load, verify signature, and parse policy. Throws on any failure.
SecurityPolicy load_security_policy_or_throw(const std::string& role);

// Resolve path relative to base dir unless absolute.
std::string resolve_path(const std::string& base_dir, const std::string& value);

} // namespace hesia
