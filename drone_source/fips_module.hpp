// fips_module.hpp
//
// A small "FIPS-oriented" wrapper intended to:
//   1) Load/enable the OpenSSL FIPS provider (when available),
//   2) Expose a restricted set of Approved-mode cryptographic services,
//   3) Provide a place to implement module-level rules (state machine, self-tests,
//      zeroisation) and operator authentication hooks.
//
// IMPORTANT DISCLAIMER
// --------------------
// This code does not make your product "FIPS validated". Validation is a
// lab/NIST (CMVP) process.
//
// Practically, most software-only cryptographic libraries are validated at
// overall Level 1 (see the OpenSSL FIPS Provider security policy). Higher levels
// (overall Level 3/4) generally involve hardware/physical protections and/or a
// tightly controlled operational environment.
//
// This wrapper helps you *prepare* a clean cryptographic boundary and enforce an
// Approved-mode posture, which is a prerequisite for a credible FIPS 140-3
// certification effort.

#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <optional>
#include <mutex>

namespace hesia::fips {

enum class ModuleState : uint8_t {
    kUninitialized = 0,
    kSelfTest      = 1,
    kApproved      = 2,
    kNonApproved   = 3,
    kError         = 4,
};

enum class Role : uint8_t {
    kNone = 0,
    kCryptoOfficer = 1,
    kUser = 2,
};

struct InitOptions {
    // If true, Initialize() fails unless the "fips" provider loads and
    // EVP_default_properties_enable_fips() succeeds.
    bool require_fips_provider = true;

    // Optional: run additional KAT/CAST-style tests from fips_selftest.*
    bool run_additional_selftests = true;

    // Optional: Verify integrity of a target file (typically the module binary)
    // using an ECDSA/RSA signature.
    //
    // If enabled, you must provide:
    //   - integrity_target_path
    //   - integrity_public_key_pem_path
    //   - integrity_signature_path
    bool enable_signature_integrity_check = false;
    std::string integrity_target_path;
    std::string integrity_public_key_pem_path;
    std::string integrity_signature_path;
};

struct AuthPolicy {
    size_t min_password_len = 12;
    size_t pbkdf2_iterations = 200'000; // tune for your platform

    // Multi-factor: TOTP (HMAC-SHA-256 variant). If enabled for a role, Login()
    // requires a TOTP code.
    bool require_mfa_for_crypto_officer = true;
    bool require_mfa_for_user = false;
    uint32_t totp_time_step_seconds = 30;
    int totp_allowed_drift_steps = 1; // allow ±N time steps

    // Rate limiting / lockout
    uint32_t max_failed_attempts = 10;
    uint32_t lockout_seconds = 30;
};

struct AeadGcmResult {
    std::vector<uint8_t> ciphertext;
    std::vector<uint8_t> tag; // 16 bytes
};

class FipsModule final {
public:
    static FipsModule& Instance();

    // Initializes providers, runs self-tests, and transitions to Approved mode.
    // Returns false on failure (state becomes kError).
    bool Initialize(const InitOptions& opts);
    void Shutdown();

    ModuleState State() const noexcept;
    bool IsApprovedMode() const noexcept;
    std::string LastError() const;

    // --- Authentication / Roles ---
    void SetAuthPolicy(const AuthPolicy& p);
    AuthPolicy GetAuthPolicy() const;

    // Bootstrap/Provisioning:
    //   - If no Crypto Officer is configured yet, the first call that provisions
    //     a Crypto Officer identity is allowed without being logged in.
    //   - After that, provisioning requires the Crypto Officer to be logged in.
    bool ProvisionIdentity(Role role,
                           const std::string& identity,
                           const std::string& password,
                           const std::vector<uint8_t>& totp_secret);

    bool Login(Role role,
               const std::string& identity,
               const std::string& password,
               const std::optional<std::string>& totp_code,
               uint64_t unix_time_seconds);

    void Logout();
    bool IsAuthenticated() const noexcept;
    Role CurrentRole() const noexcept;

    // --- Approved crypto services (symmetric + hashes + KDF) ---
    // These services require the module to be in Approved mode.
    std::vector<uint8_t> RandomBytes(size_t len);
    std::vector<uint8_t> Sha256(const std::vector<uint8_t>& data);
    std::vector<uint8_t> Sha512(const std::vector<uint8_t>& data);
    std::vector<uint8_t> HmacSha256(const std::vector<uint8_t>& key,
                                    const std::vector<uint8_t>& data);
    std::vector<uint8_t> HmacSha512(const std::vector<uint8_t>& key,
                                    const std::vector<uint8_t>& data);
    std::vector<uint8_t> HkdfSha256(const std::vector<uint8_t>& salt,
                                    const std::vector<uint8_t>& ikm,
                                    const std::vector<uint8_t>& info,
                                    size_t out_len);

    AeadGcmResult Aes256GcmEncrypt(const std::vector<uint8_t>& key,
                                  const std::vector<uint8_t>& iv,
                                  const std::vector<uint8_t>& aad,
                                  const std::vector<uint8_t>& plaintext);

    std::vector<uint8_t> Aes256GcmDecrypt(const std::vector<uint8_t>& key,
                                          const std::vector<uint8_t>& iv,
                                          const std::vector<uint8_t>& aad,
                                          const std::vector<uint8_t>& ciphertext,
                                          const std::vector<uint8_t>& tag);

private:
    FipsModule();
    ~FipsModule();
    FipsModule(const FipsModule&) = delete;
    FipsModule& operator=(const FipsModule&) = delete;

    // Helpers
    const char* propq() const noexcept;
    void set_error_locked(const std::string& msg);
    bool ensure_approved_locked(const char* api);

    bool load_providers_locked(bool require_fips);
    void unload_providers_locked();
    bool run_self_tests_locked(bool additional);
    bool verify_signature_integrity_locked(const InitOptions& opts);

    // Auth helpers
    bool require_role_locked(Role required, const char* api);
    bool is_role_configured_locked(Role role) const;

private:
    mutable std::mutex mu_;
    ModuleState state_ = ModuleState::kUninitialized;
    bool fips_enabled_ = false;
    std::string last_error_;

    // OpenSSL providers (opaque pointers)
    void* default_provider_ = nullptr;
    void* base_provider_ = nullptr;
    void* fips_provider_ = nullptr;

    // Auth
    AuthPolicy auth_policy_;
    Role current_role_ = Role::kNone;
    std::string current_identity_;
    uint32_t failed_attempts_ = 0;
    uint64_t lockout_until_ = 0;

    struct CredentialRecord {
        Role role;
        std::string identity;
        std::vector<uint8_t> pw_salt;
        std::vector<uint8_t> pw_hash; // PBKDF2-HMAC-SHA256
        std::vector<uint8_t> totp_secret; // empty if not configured
    };
    std::vector<CredentialRecord> creds_;
};

} // namespace hesia::fips
