// fips_module.cpp

#include "fips_module.hpp"

#include "fips_common.hpp"
#include "fips_selftest.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>

#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/core_names.h>
#include <openssl/params.h>
#include <openssl/provider.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/pem.h>
#include <openssl/bio.h>

namespace hesia::fips {

namespace {

std::string openssl_err_string() {
    unsigned long err = ERR_get_error();
    if (err == 0) return {};
    char buf[256];
    ERR_error_string_n(err, buf, sizeof(buf));
    return std::string(buf);
}

std::vector<uint8_t> read_file_all(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return {};
    f.seekg(0, std::ios::end);
    std::streamoff n = f.tellg();
    if (n < 0) return {};
    f.seekg(0, std::ios::beg);

    std::vector<uint8_t> buf(static_cast<size_t>(n));
    if (n > 0) {
        f.read(reinterpret_cast<char*>(buf.data()), n);
        if (!f) return {};
    }
    return buf;
}

// PBKDF2-HMAC-SHA256 using EVP_KDF so that provider properties apply.
bool pbkdf2_sha256(const char* propq,
                   const std::string& password,
                   const std::vector<uint8_t>& salt,
                   size_t iterations,
                   std::vector<uint8_t>& out,
                   std::string& err) {
    EVP_KDF* kdf = EVP_KDF_fetch(nullptr, "PBKDF2", propq);
    if (!kdf) {
        err = "PBKDF2 fetch failed: " + openssl_err_string();
        return false;
    }
    EVP_KDF_CTX* kctx = EVP_KDF_CTX_new(kdf);
    if (!kctx) {
        EVP_KDF_free(kdf);
        err = "EVP_KDF_CTX_new failed";
        return false;
    }

    // OpenSSL wants a void* for password bytes; keep a mutable copy.
    std::vector<uint8_t> pw_bytes(password.begin(), password.end());

    // OSSL_KDF_PARAM_ITER expects an unsigned int.
    unsigned int iter_u = static_cast<unsigned int>(iterations);

    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, const_cast<char*>("SHA256"), 0),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_PASSWORD,
                                          pw_bytes.empty() ? nullptr : pw_bytes.data(),
                                          pw_bytes.size()),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT,
                                          salt.empty() ? nullptr : const_cast<uint8_t*>(salt.data()),
                                          salt.size()),
        OSSL_PARAM_construct_uint(OSSL_KDF_PARAM_ITER, &iter_u),
        OSSL_PARAM_construct_end()
    };

    bool ok = (EVP_KDF_derive(kctx,
                             out.empty() ? nullptr : out.data(),
                             out.size(),
                             params) == 1);

    // Best-effort cleanse
    if (!pw_bytes.empty()) secure_zero(pw_bytes.data(), pw_bytes.size());

    EVP_KDF_CTX_free(kctx);
    EVP_KDF_free(kdf);

    if (!ok) {
        err = "PBKDF2 derive failed: " + openssl_err_string();
        return false;
    }
    return true;
}

// TOTP (RFC 6238-style) but using HMAC-SHA-256 (allowed by RFC 6238).
std::string totp_hmac_sha256(const std::vector<uint8_t>& secret,
                             uint64_t unix_time_seconds,
                             uint32_t step_seconds,
                             uint32_t digits,
                             const char* propq,
                             std::string& err) {
    if (step_seconds == 0) {
        err = "TOTP step_seconds invalid";
        return {};
    }
    uint64_t counter = unix_time_seconds / step_seconds;

    std::array<uint8_t, 8> msg{};
    for (int i = 7; i >= 0; --i) {
        msg[i] = static_cast<uint8_t>(counter & 0xFF);
        counter >>= 8;
    }

    // Compute HMAC-SHA256(secret, msg)
    EVP_MAC* mac = EVP_MAC_fetch(nullptr, "HMAC", propq);
    if (!mac) {
        err = "HMAC fetch failed: " + openssl_err_string();
        return {};
    }
    EVP_MAC_CTX* mctx = EVP_MAC_CTX_new(mac);
    if (!mctx) {
        EVP_MAC_free(mac);
        err = "EVP_MAC_CTX_new failed";
        return {};
    }

    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST, const_cast<char*>("SHA256"), 0),
        OSSL_PARAM_construct_end()
    };

    std::array<uint8_t, 32> out{};
    size_t out_len = out.size();

    bool ok = (EVP_MAC_init(mctx,
                            secret.empty() ? nullptr : secret.data(),
                            secret.size(),
                            params) == 1) &&
              (EVP_MAC_update(mctx, msg.data(), msg.size()) == 1) &&
              (EVP_MAC_final(mctx, out.data(), &out_len, out.size()) == 1) &&
              (out_len == out.size());

    EVP_MAC_CTX_free(mctx);
    EVP_MAC_free(mac);

    if (!ok) {
        err = "TOTP HMAC failed: " + openssl_err_string();
        return {};
    }

    uint8_t offset = out.back() & 0x0F;
    if (offset + 4 > out.size()) {
        err = "TOTP truncation offset invalid";
        return {};
    }
    uint32_t bin_code = (static_cast<uint32_t>(out[offset]) & 0x7F) << 24;
    bin_code |= static_cast<uint32_t>(out[offset + 1]) << 16;
    bin_code |= static_cast<uint32_t>(out[offset + 2]) << 8;
    bin_code |= static_cast<uint32_t>(out[offset + 3]);

    uint32_t mod = 1;
    for (uint32_t i = 0; i < digits; i++) mod *= 10;
    uint32_t otp = (mod == 0) ? bin_code : (bin_code % mod);

    std::ostringstream ss;
    ss.width(digits);
    ss.fill('0');
    ss << otp;
    return ss.str();
}

bool ct_string_equal(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    return ct_equal(a.data(), b.data(), a.size());
}

} // namespace

FipsModule& FipsModule::Instance() {
    static FipsModule g;
    return g;
}

FipsModule::FipsModule() = default;

FipsModule::~FipsModule() {
    // Intentionally empty.
    //
    // Rationale:
    // - Provider unload + OpenSSL global cleanup ordering can be platform/
    //   build dependent, and unloading providers during static destruction
    //   can crash in some environments.
    // - For FIPS-style zeroisation requirements, call Shutdown() explicitly
    //   as part of the module's lifecycle.
}

ModuleState FipsModule::State() const noexcept {
    std::lock_guard<std::mutex> lock(mu_);
    return state_;
}

bool FipsModule::IsApprovedMode() const noexcept {
    std::lock_guard<std::mutex> lock(mu_);
    return state_ == ModuleState::kApproved && fips_enabled_;
}

std::string FipsModule::LastError() const {
    std::lock_guard<std::mutex> lock(mu_);
    return last_error_;
}

void FipsModule::SetAuthPolicy(const AuthPolicy& p) {
    std::lock_guard<std::mutex> lock(mu_);
    auth_policy_ = p;
}

AuthPolicy FipsModule::GetAuthPolicy() const {
    std::lock_guard<std::mutex> lock(mu_);
    return auth_policy_;
}

const char* FipsModule::propq() const noexcept {
    return fips_enabled_ ? "fips=yes" : nullptr;
}

void FipsModule::set_error_locked(const std::string& msg) {
    state_ = ModuleState::kError;
    last_error_ = msg;
}

bool FipsModule::ensure_approved_locked(const char* api) {
    if (state_ != ModuleState::kApproved || !fips_enabled_) {
        last_error_ = std::string(api) + ": module not in Approved mode";
        return false;
    }
    return true;
}

bool FipsModule::load_providers_locked(bool require_fips) {
    // Clear OpenSSL error queue.
    ERR_clear_error();

    // Load "default" provider so non-FIPS builds remain functional.
    // In a FIPS deployment, we still force algorithm fetches with "fips=yes"
    // (and enable default properties for FIPS) so calls resolve to the FIPS
    // provider.
    if (!default_provider_) {
        default_provider_ = OSSL_PROVIDER_load(nullptr, "default");
        if (!default_provider_) {
            last_error_ = "Default provider load failed: " + openssl_err_string();
            return false;
        }
    }

    // Load "base" provider (sometimes needed by OpenSSL internally).
    if (!base_provider_) {
        base_provider_ = OSSL_PROVIDER_load(nullptr, "base");
        // base may not exist in some builds; don't fail solely for this.
    }

    // Load "fips" provider.
    if (!fips_provider_) {
        fips_provider_ = OSSL_PROVIDER_load(nullptr, "fips");
    }

    if (!fips_provider_) {
        if (require_fips) {
            last_error_ = "FIPS provider load failed: " + openssl_err_string();
            return false;
        }
        // Non-approved mode.
        fips_enabled_ = false;
        return true;
    }

    if (EVP_default_properties_enable_fips(nullptr, 1) != 1) {
        last_error_ = "EVP_default_properties_enable_fips failed: " + openssl_err_string();
        return false;
    }

    fips_enabled_ = true;
    return true;
}

void FipsModule::unload_providers_locked() {
    // Best effort: disable fips default properties.
    if (fips_enabled_) {
        (void)EVP_default_properties_enable_fips(nullptr, 0);
    }

    if (fips_provider_) {
        OSSL_PROVIDER_unload(reinterpret_cast<OSSL_PROVIDER*>(fips_provider_));
        fips_provider_ = nullptr;
    }
    if (base_provider_) {
        OSSL_PROVIDER_unload(reinterpret_cast<OSSL_PROVIDER*>(base_provider_));
        base_provider_ = nullptr;
    }
    if (default_provider_) {
        OSSL_PROVIDER_unload(reinterpret_cast<OSSL_PROVIDER*>(default_provider_));
        default_provider_ = nullptr;
    }
    fips_enabled_ = false;
}

bool FipsModule::verify_signature_integrity_locked(const InitOptions& opts) {
    if (!opts.enable_signature_integrity_check) return true;

    if (opts.integrity_target_path.empty() ||
        opts.integrity_public_key_pem_path.empty() ||
        opts.integrity_signature_path.empty()) {
        last_error_ = "Integrity check enabled but paths are missing";
        return false;
    }

    std::vector<uint8_t> sig = read_file_all(opts.integrity_signature_path);
    if (sig.empty()) {
        last_error_ = "Failed to read integrity signature";
        return false;
    }

    BIO* bio = BIO_new_file(opts.integrity_public_key_pem_path.c_str(), "r");
    if (!bio) {
        last_error_ = "Failed to open public key PEM";
        return false;
    }
    EVP_PKEY* pkey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!pkey) {
        last_error_ = "Failed to parse public key PEM: " + openssl_err_string();
        return false;
    }

    EVP_MD* md = EVP_MD_fetch(nullptr, "SHA256", propq());
    if (!md) {
        EVP_PKEY_free(pkey);
        last_error_ = "SHA256 fetch failed: " + openssl_err_string();
        return false;
    }

    EVP_MD_CTX* mctx = EVP_MD_CTX_new();
    if (!mctx) {
        EVP_MD_free(md);
        EVP_PKEY_free(pkey);
        last_error_ = "EVP_MD_CTX_new failed";
        return false;
    }

    bool ok = (EVP_DigestVerifyInit(mctx, nullptr, md, nullptr, pkey) == 1);

    std::ifstream f(opts.integrity_target_path, std::ios::binary);
    if (!f.is_open()) {
        ok = false;
        last_error_ = "Failed to open integrity target file";
    }

    if (ok) {
        std::array<char, 4096> buf{};
        while (f.good()) {
            f.read(buf.data(), buf.size());
            std::streamsize n = f.gcount();
            if (n > 0) {
                if (EVP_DigestVerifyUpdate(mctx, buf.data(), static_cast<size_t>(n)) != 1) {
                    ok = false;
                    last_error_ = "DigestVerifyUpdate failed: " + openssl_err_string();
                    break;
                }
            }
        }
    }

    if (ok) {
        int v = EVP_DigestVerifyFinal(mctx, sig.data(), sig.size());
        if (v != 1) {
            ok = false;
            last_error_ = "Integrity signature verification failed";
        }
    }

    EVP_MD_CTX_free(mctx);
    EVP_MD_free(md);
    EVP_PKEY_free(pkey);
    return ok;
}

bool FipsModule::run_self_tests_locked(bool additional) {
    // The OpenSSL FIPS provider runs its own power-up self-tests when loaded.
    // Optionally run additional deterministic tests.
    if (!additional) return true;
    std::string err;
    if (!selftest::RunAll(propq(), err)) {
        last_error_ = err;
        return false;
    }
    return true;
}

bool FipsModule::Initialize(const InitOptions& opts) {
    std::lock_guard<std::mutex> lock(mu_);

    if (state_ == ModuleState::kApproved || state_ == ModuleState::kNonApproved) {
        return true;
    }
    if (state_ == ModuleState::kSelfTest) {
        last_error_ = "Initialize re-entrancy";
        return false;
    }

    state_ = ModuleState::kSelfTest;
    last_error_.clear();

    if (!load_providers_locked(opts.require_fips_provider)) {
        set_error_locked(last_error_.empty() ? "Provider init failed" : last_error_);
        return false;
    }

    if (!verify_signature_integrity_locked(opts)) {
        set_error_locked(last_error_.empty() ? "Integrity check failed" : last_error_);
        return false;
    }

    if (!run_self_tests_locked(opts.run_additional_selftests)) {
        set_error_locked(last_error_.empty() ? "Self-tests failed" : last_error_);
        return false;
    }

    state_ = fips_enabled_ ? ModuleState::kApproved : ModuleState::kNonApproved;
    return true;
}

void FipsModule::Shutdown() {
    std::lock_guard<std::mutex> lock(mu_);

    // Zeroize credentials
    for (auto& c : creds_) {
        if (!c.pw_salt.empty()) secure_zero(c.pw_salt.data(), c.pw_salt.size());
        if (!c.pw_hash.empty()) secure_zero(c.pw_hash.data(), c.pw_hash.size());
        if (!c.totp_secret.empty()) secure_zero(c.totp_secret.data(), c.totp_secret.size());
        c.identity.clear();
    }
    creds_.clear();

    current_identity_.clear();
    current_role_ = Role::kNone;
    failed_attempts_ = 0;
    lockout_until_ = 0;

    unload_providers_locked();
    state_ = ModuleState::kUninitialized;
    last_error_.clear();
}

bool FipsModule::IsAuthenticated() const noexcept {
    std::lock_guard<std::mutex> lock(mu_);
    return current_role_ != Role::kNone;
}

Role FipsModule::CurrentRole() const noexcept {
    std::lock_guard<std::mutex> lock(mu_);
    return current_role_;
}

bool FipsModule::is_role_configured_locked(Role role) const {
    for (const auto& c : creds_) {
        if (c.role == role) return true;
    }
    return false;
}

bool FipsModule::require_role_locked(Role required, const char* api) {
    if (current_role_ != required) {
        last_error_ = std::string(api) + ": not authorized";
        return false;
    }
    return true;
}

bool FipsModule::ProvisionIdentity(Role role,
                                  const std::string& identity,
                                  const std::string& password,
                                  const std::vector<uint8_t>& totp_secret) {
    std::lock_guard<std::mutex> lock(mu_);

    if (role != Role::kCryptoOfficer && role != Role::kUser) {
        last_error_ = "ProvisionIdentity: invalid role";
        return false;
    }
    if (identity.empty()) {
        last_error_ = "ProvisionIdentity: empty identity";
        return false;
    }
    if (password.size() < auth_policy_.min_password_len) {
        last_error_ = "ProvisionIdentity: password too short";
        return false;
    }

    if (!ensure_approved_locked("ProvisionIdentity")) {
        // Note: provisioning is intentionally disabled unless module is in Approved mode.
        return false;
    }

    bool bootstrap = (!is_role_configured_locked(Role::kCryptoOfficer) && role == Role::kCryptoOfficer);
    if (!bootstrap) {
        if (!require_role_locked(Role::kCryptoOfficer, "ProvisionIdentity")) {
            return false;
        }
    }

    for (const auto& c : creds_) {
        if (c.role == role && c.identity == identity) {
            last_error_ = "ProvisionIdentity: identity already exists";
            return false;
        }
    }

    const bool mfa_required = (role == Role::kCryptoOfficer)
                                  ? auth_policy_.require_mfa_for_crypto_officer
                                  : auth_policy_.require_mfa_for_user;

    if (mfa_required && totp_secret.empty()) {
        last_error_ = "ProvisionIdentity: MFA required but no TOTP secret provided";
        return false;
    }

    // Generate salt
    std::vector<uint8_t> salt(16);
    if (RAND_bytes(salt.data(), (int)salt.size()) != 1) {
        set_error_locked("ProvisionIdentity: RAND_bytes failed: " + openssl_err_string());
        return false;
    }

    std::vector<uint8_t> hash(32);
    std::string kdf_err;
    if (!pbkdf2_sha256(propq(), password, salt, auth_policy_.pbkdf2_iterations, hash, kdf_err)) {
        set_error_locked("ProvisionIdentity: " + kdf_err);
        return false;
    }

    CredentialRecord rec;
    rec.role = role;
    rec.identity = identity;
    rec.pw_salt = std::move(salt);
    rec.pw_hash = std::move(hash);
    rec.totp_secret = totp_secret;

    creds_.push_back(std::move(rec));
    return true;
}

bool FipsModule::Login(Role role,
                      const std::string& identity,
                      const std::string& password,
                      const std::optional<std::string>& totp_code,
                      uint64_t unix_time_seconds) {
    std::lock_guard<std::mutex> lock(mu_);

    if (!ensure_approved_locked("Login")) {
        return false;
    }

    if (unix_time_seconds < lockout_until_) {
        last_error_ = "Login: locked out";
        return false;
    }

    const CredentialRecord* rec = nullptr;
    for (const auto& c : creds_) {
        if (c.role == role && c.identity == identity) {
            rec = &c;
            break;
        }
    }
    if (!rec) {
        failed_attempts_++;
        if (failed_attempts_ >= auth_policy_.max_failed_attempts) {
            lockout_until_ = unix_time_seconds + auth_policy_.lockout_seconds;
        }
        last_error_ = "Login: unknown identity";
        return false;
    }

    std::vector<uint8_t> hash(32);
    std::string kdf_err;
    if (!pbkdf2_sha256(propq(), password, rec->pw_salt, auth_policy_.pbkdf2_iterations, hash, kdf_err)) {
        set_error_locked("Login: " + kdf_err);
        return false;
    }

    if (!ct_equal(hash.data(), rec->pw_hash.data(), rec->pw_hash.size())) {
        if (!hash.empty()) secure_zero(hash.data(), hash.size());
        failed_attempts_++;
        if (failed_attempts_ >= auth_policy_.max_failed_attempts) {
            lockout_until_ = unix_time_seconds + auth_policy_.lockout_seconds;
        }
        last_error_ = "Login: invalid credentials";
        return false;
    }
    if (!hash.empty()) secure_zero(hash.data(), hash.size());

    const bool mfa_required = (role == Role::kCryptoOfficer)
                                  ? auth_policy_.require_mfa_for_crypto_officer
                                  : auth_policy_.require_mfa_for_user;

    if (mfa_required) {
        if (rec->totp_secret.empty()) {
            last_error_ = "Login: MFA required but not configured";
            return false;
        }
        if (!totp_code.has_value()) {
            last_error_ = "Login: MFA required";
            return false;
        }

        bool totp_ok = false;
        std::string totp_err;
        const int drift = auth_policy_.totp_allowed_drift_steps;
        for (int d = -drift; d <= drift; d++) {
            uint64_t t = unix_time_seconds + static_cast<int64_t>(d) * static_cast<int64_t>(auth_policy_.totp_time_step_seconds);
            std::string candidate = totp_hmac_sha256(rec->totp_secret, t,
                                                     auth_policy_.totp_time_step_seconds,
                                                     6, propq(), totp_err);
            if (!candidate.empty() && ct_string_equal(candidate, *totp_code)) {
                totp_ok = true;
                break;
            }
        }
        if (!totp_ok) {
            failed_attempts_++;
            if (failed_attempts_ >= auth_policy_.max_failed_attempts) {
                lockout_until_ = unix_time_seconds + auth_policy_.lockout_seconds;
            }
            last_error_ = "Login: invalid TOTP";
            return false;
        }
    }

    current_role_ = role;
    current_identity_ = identity;
    failed_attempts_ = 0;
    lockout_until_ = 0;
    last_error_.clear();
    return true;
}

void FipsModule::Logout() {
    std::lock_guard<std::mutex> lock(mu_);
    current_role_ = Role::kNone;
    current_identity_.clear();
}

// --- Approved crypto services ---

std::vector<uint8_t> FipsModule::RandomBytes(size_t len) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!ensure_approved_locked("RandomBytes")) return {};
    }
    std::vector<uint8_t> out(len);
    if (len == 0) return out;
    if (RAND_bytes(out.data(), (int)out.size()) != 1) {
        std::lock_guard<std::mutex> lock(mu_);
        set_error_locked("RandomBytes: RAND_bytes failed: " + openssl_err_string());
        return {};
    }
    return out;
}

std::vector<uint8_t> FipsModule::Sha256(const std::vector<uint8_t>& data) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!ensure_approved_locked("Sha256")) return {};
    }

    EVP_MD* md = EVP_MD_fetch(nullptr, "SHA256", propq());
    if (!md) {
        std::lock_guard<std::mutex> lock(mu_);
        set_error_locked("Sha256: fetch failed: " + openssl_err_string());
        return {};
    }
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        EVP_MD_free(md);
        std::lock_guard<std::mutex> lock(mu_);
        set_error_locked("Sha256: EVP_MD_CTX_new failed");
        return {};
    }

    std::vector<uint8_t> out(32);
    unsigned int out_len = 0;
    bool ok = (EVP_DigestInit_ex(ctx, md, nullptr) == 1) &&
              (data.empty() || EVP_DigestUpdate(ctx, data.data(), data.size()) == 1) &&
              (EVP_DigestFinal_ex(ctx, out.data(), &out_len) == 1) &&
              (out_len == out.size());

    EVP_MD_CTX_free(ctx);
    EVP_MD_free(md);

    if (!ok) {
        std::lock_guard<std::mutex> lock(mu_);
        set_error_locked("Sha256 failed: " + openssl_err_string());
        return {};
    }
    return out;
}

std::vector<uint8_t> FipsModule::Sha512(const std::vector<uint8_t>& data) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!ensure_approved_locked("Sha512")) return {};
    }

    EVP_MD* md = EVP_MD_fetch(nullptr, "SHA512", propq());
    if (!md) {
        std::lock_guard<std::mutex> lock(mu_);
        set_error_locked("Sha512: fetch failed: " + openssl_err_string());
        return {};
    }
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        EVP_MD_free(md);
        std::lock_guard<std::mutex> lock(mu_);
        set_error_locked("Sha512: EVP_MD_CTX_new failed");
        return {};
    }

    std::vector<uint8_t> out(64);
    unsigned int out_len = 0;
    bool ok = (EVP_DigestInit_ex(ctx, md, nullptr) == 1) &&
              (data.empty() || EVP_DigestUpdate(ctx, data.data(), data.size()) == 1) &&
              (EVP_DigestFinal_ex(ctx, out.data(), &out_len) == 1) &&
              (out_len == out.size());

    EVP_MD_CTX_free(ctx);
    EVP_MD_free(md);

    if (!ok) {
        std::lock_guard<std::mutex> lock(mu_);
        set_error_locked("Sha512 failed: " + openssl_err_string());
        return {};
    }
    return out;
}

std::vector<uint8_t> FipsModule::HmacSha256(const std::vector<uint8_t>& key,
                                           const std::vector<uint8_t>& data) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!ensure_approved_locked("HmacSha256")) return {};
    }

    EVP_MAC* mac = EVP_MAC_fetch(nullptr, "HMAC", propq());
    if (!mac) {
        std::lock_guard<std::mutex> lock(mu_);
        set_error_locked("HmacSha256: fetch failed: " + openssl_err_string());
        return {};
    }
    EVP_MAC_CTX* mctx = EVP_MAC_CTX_new(mac);
    if (!mctx) {
        EVP_MAC_free(mac);
        std::lock_guard<std::mutex> lock(mu_);
        set_error_locked("HmacSha256: EVP_MAC_CTX_new failed");
        return {};
    }

    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST, const_cast<char*>("SHA256"), 0),
        OSSL_PARAM_construct_end()
    };

    std::vector<uint8_t> out(32);
    size_t out_len = out.size();
    bool ok = (EVP_MAC_init(mctx,
                            key.empty() ? nullptr : key.data(),
                            key.size(),
                            params) == 1) &&
              (data.empty() || EVP_MAC_update(mctx, data.data(), data.size()) == 1) &&
              (EVP_MAC_final(mctx, out.data(), &out_len, out.size()) == 1) &&
              (out_len == out.size());

    EVP_MAC_CTX_free(mctx);
    EVP_MAC_free(mac);

    if (!ok) {
        std::lock_guard<std::mutex> lock(mu_);
        set_error_locked("HmacSha256 failed: " + openssl_err_string());
        return {};
    }
    return out;
}

std::vector<uint8_t> FipsModule::HmacSha512(const std::vector<uint8_t>& key,
                                           const std::vector<uint8_t>& data) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!ensure_approved_locked("HmacSha512")) return {};
    }

    EVP_MAC* mac = EVP_MAC_fetch(nullptr, "HMAC", propq());
    if (!mac) {
        std::lock_guard<std::mutex> lock(mu_);
        set_error_locked("HmacSha512: fetch failed: " + openssl_err_string());
        return {};
    }
    EVP_MAC_CTX* mctx = EVP_MAC_CTX_new(mac);
    if (!mctx) {
        EVP_MAC_free(mac);
        std::lock_guard<std::mutex> lock(mu_);
        set_error_locked("HmacSha512: EVP_MAC_CTX_new failed");
        return {};
    }

    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST, const_cast<char*>("SHA512"), 0),
        OSSL_PARAM_construct_end()
    };

    std::vector<uint8_t> out(64);
    size_t out_len = out.size();
    bool ok = (EVP_MAC_init(mctx,
                            key.empty() ? nullptr : key.data(),
                            key.size(),
                            params) == 1) &&
              (data.empty() || EVP_MAC_update(mctx, data.data(), data.size()) == 1) &&
              (EVP_MAC_final(mctx, out.data(), &out_len, out.size()) == 1) &&
              (out_len == out.size());

    EVP_MAC_CTX_free(mctx);
    EVP_MAC_free(mac);

    if (!ok) {
        std::lock_guard<std::mutex> lock(mu_);
        set_error_locked("HmacSha512 failed: " + openssl_err_string());
        return {};
    }
    return out;
}

std::vector<uint8_t> FipsModule::HkdfSha256(const std::vector<uint8_t>& salt,
                                           const std::vector<uint8_t>& ikm,
                                           const std::vector<uint8_t>& info,
                                           size_t out_len) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!ensure_approved_locked("HkdfSha256")) return {};
    }

    EVP_KDF* kdf = EVP_KDF_fetch(nullptr, "HKDF", propq());
    if (!kdf) {
        std::lock_guard<std::mutex> lock(mu_);
        set_error_locked("HkdfSha256: HKDF fetch failed: " + openssl_err_string());
        return {};
    }
    EVP_KDF_CTX* kctx = EVP_KDF_CTX_new(kdf);
    if (!kctx) {
        EVP_KDF_free(kdf);
        std::lock_guard<std::mutex> lock(mu_);
        set_error_locked("HkdfSha256: EVP_KDF_CTX_new failed");
        return {};
    }

    std::vector<uint8_t> out(out_len);

    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, const_cast<char*>("SHA256"), 0),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT,
                                          salt.empty() ? nullptr : const_cast<uint8_t*>(salt.data()),
                                          salt.size()),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY,
                                          ikm.empty() ? nullptr : const_cast<uint8_t*>(ikm.data()),
                                          ikm.size()),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO,
                                          info.empty() ? nullptr : const_cast<uint8_t*>(info.data()),
                                          info.size()),
        OSSL_PARAM_construct_end()
    };

    bool ok = (EVP_KDF_derive(kctx, out.empty() ? nullptr : out.data(), out.size(), params) == 1);

    EVP_KDF_CTX_free(kctx);
    EVP_KDF_free(kdf);

    if (!ok) {
        std::lock_guard<std::mutex> lock(mu_);
        set_error_locked("HkdfSha256 failed: " + openssl_err_string());
        return {};
    }
    return out;
}

AeadGcmResult FipsModule::Aes256GcmEncrypt(const std::vector<uint8_t>& key,
                                         const std::vector<uint8_t>& iv,
                                         const std::vector<uint8_t>& aad,
                                         const std::vector<uint8_t>& plaintext) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!ensure_approved_locked("Aes256GcmEncrypt")) return {};
    }

    if (key.size() != 32) {
        std::lock_guard<std::mutex> lock(mu_);
        last_error_ = "Aes256GcmEncrypt: key must be 32 bytes";
        return {};
    }
    if (iv.size() != 12) {
        std::lock_guard<std::mutex> lock(mu_);
        last_error_ = "Aes256GcmEncrypt: iv must be 12 bytes";
        return {};
    }

    EVP_CIPHER* cipher = EVP_CIPHER_fetch(nullptr, "AES-256-GCM", propq());
    if (!cipher) {
        std::lock_guard<std::mutex> lock(mu_);
        set_error_locked("Aes256GcmEncrypt: cipher fetch failed: " + openssl_err_string());
        return {};
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        EVP_CIPHER_free(cipher);
        std::lock_guard<std::mutex> lock(mu_);
        set_error_locked("Aes256GcmEncrypt: EVP_CIPHER_CTX_new failed");
        return {};
    }

    AeadGcmResult res;
    res.ciphertext.resize(plaintext.size());
    res.tag.resize(16);

    int outl = 0;
    bool ok = (EVP_EncryptInit_ex(ctx, cipher, nullptr, nullptr, nullptr) == 1) &&
              (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, (int)iv.size(), nullptr) == 1) &&
              (EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), iv.data()) == 1);

    if (ok && !aad.empty()) {
        ok = (EVP_EncryptUpdate(ctx, nullptr, &outl, aad.data(), (int)aad.size()) == 1);
    }
    if (ok && !plaintext.empty()) {
        ok = (EVP_EncryptUpdate(ctx, res.ciphertext.data(), &outl, plaintext.data(), (int)plaintext.size()) == 1);
    }
    if (ok) {
        ok = (EVP_EncryptFinal_ex(ctx, nullptr, &outl) == 1) &&
             (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, (int)res.tag.size(), res.tag.data()) == 1);
    }

    EVP_CIPHER_CTX_free(ctx);
    EVP_CIPHER_free(cipher);

    if (!ok) {
        std::lock_guard<std::mutex> lock(mu_);
        set_error_locked("Aes256GcmEncrypt failed: " + openssl_err_string());
        return {};
    }
    return res;
}

std::vector<uint8_t> FipsModule::Aes256GcmDecrypt(const std::vector<uint8_t>& key,
                                                 const std::vector<uint8_t>& iv,
                                                 const std::vector<uint8_t>& aad,
                                                 const std::vector<uint8_t>& ciphertext,
                                                 const std::vector<uint8_t>& tag) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!ensure_approved_locked("Aes256GcmDecrypt")) return {};
    }

    if (key.size() != 32) {
        std::lock_guard<std::mutex> lock(mu_);
        last_error_ = "Aes256GcmDecrypt: key must be 32 bytes";
        return {};
    }
    if (iv.size() != 12) {
        std::lock_guard<std::mutex> lock(mu_);
        last_error_ = "Aes256GcmDecrypt: iv must be 12 bytes";
        return {};
    }
    if (tag.size() != 16) {
        std::lock_guard<std::mutex> lock(mu_);
        last_error_ = "Aes256GcmDecrypt: tag must be 16 bytes";
        return {};
    }

    EVP_CIPHER* cipher = EVP_CIPHER_fetch(nullptr, "AES-256-GCM", propq());
    if (!cipher) {
        std::lock_guard<std::mutex> lock(mu_);
        set_error_locked("Aes256GcmDecrypt: cipher fetch failed: " + openssl_err_string());
        return {};
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        EVP_CIPHER_free(cipher);
        std::lock_guard<std::mutex> lock(mu_);
        set_error_locked("Aes256GcmDecrypt: EVP_CIPHER_CTX_new failed");
        return {};
    }

    std::vector<uint8_t> plaintext(ciphertext.size());
    int outl = 0;

    bool ok = (EVP_DecryptInit_ex(ctx, cipher, nullptr, nullptr, nullptr) == 1) &&
              (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, (int)iv.size(), nullptr) == 1) &&
              (EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), iv.data()) == 1);

    if (ok && !aad.empty()) {
        ok = (EVP_DecryptUpdate(ctx, nullptr, &outl, aad.data(), (int)aad.size()) == 1);
    }
    if (ok && !ciphertext.empty()) {
        ok = (EVP_DecryptUpdate(ctx, plaintext.data(), &outl, ciphertext.data(), (int)ciphertext.size()) == 1);
    }

    if (ok) {
        ok = (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, (int)tag.size(), const_cast<uint8_t*>(tag.data())) == 1);
    }
    if (ok) {
        ok = (EVP_DecryptFinal_ex(ctx, nullptr, &outl) == 1);
    }

    EVP_CIPHER_CTX_free(ctx);
    EVP_CIPHER_free(cipher);

    if (!ok) {
        // Do NOT automatically set module error state on bad tags; that's an
        // expected operational event.
        std::lock_guard<std::mutex> lock(mu_);
        last_error_ = "Aes256GcmDecrypt: authentication failed";
        return {};
    }
    return plaintext;
}

} // namespace hesia::fips
