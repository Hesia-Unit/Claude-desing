#include "drone_network.hpp"
#include "logger.hpp"
#include "config.hpp"
#include "policy.hpp"
#include "policy_ed25519_public_key.h"
#include "optee_client.hpp"
#include "sentinel_bridge.hpp"
#include "security_utils.hpp"
#include <iostream>
#include <string>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>
#include <openssl/bio.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>
#ifndef _WIN32
#include <unistd.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include <errno.h>
#include <cstring>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#endif

using namespace hesia;

#ifndef _WIN32
static constexpr std::uint8_t kLegacySentinelAllowlistPubPemSha256[32] = {
    0x20, 0x75, 0x06, 0x40, 0x3c, 0x93, 0xd2, 0xf8,
    0xfa, 0x14, 0x48, 0x81, 0xe9, 0x15, 0xc6, 0x11,
    0xb5, 0x8b, 0x34, 0x86, 0x4b, 0x9a, 0x74, 0x2e,
    0x1c, 0x52, 0x66, 0x90, 0x3a, 0xf5, 0x1a, 0xca
};

static std::vector<uint8_t> read_binary_file_or_throw(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        throw std::runtime_error("cannot open " + path.string());
    }
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
}

static bool looks_base64_signature(const std::vector<uint8_t>& raw) {
    if (raw.empty()) {
        return false;
    }
    for (uint8_t c : raw) {
        if (c == '\n' || c == '\r' || c == '\t' || c == ' ') {
            continue;
        }
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=') {
            continue;
        }
        return false;
    }
    return true;
}

static std::vector<uint8_t> decode_base64_or_throw(const std::vector<uint8_t>& raw) {
    std::string compact;
    compact.reserve(raw.size());
    for (uint8_t c : raw) {
        if (c == '\n' || c == '\r' || c == '\t' || c == ' ') {
            continue;
        }
        compact.push_back(static_cast<char>(c));
    }
    if (compact.empty()) {
        return {};
    }
    if ((compact.size() % 4) != 0) {
        throw std::runtime_error("invalid base64 signature length");
    }

    std::vector<uint8_t> out((compact.size() * 3) / 4 + 3);
    const int decoded_len = EVP_DecodeBlock(out.data(),
                                            reinterpret_cast<const unsigned char*>(compact.data()),
                                            static_cast<int>(compact.size()));
    if (decoded_len < 0) {
        throw std::runtime_error("invalid base64 signature");
    }

    size_t padding = 0;
    if (!compact.empty() && compact.back() == '=') {
        padding++;
        if (compact.size() >= 2 && compact[compact.size() - 2] == '=') {
            padding++;
        }
    }
    if (decoded_len < static_cast<int>(padding)) {
        throw std::runtime_error("invalid base64 signature padding");
    }
    out.resize(static_cast<size_t>(decoded_len) - padding);
    return out;
}

static bool verify_ed25519_embedded_signature(const std::vector<uint8_t>& data,
                                              const std::vector<uint8_t>& signature) {
    BIO* bio = BIO_new_mem_buf(kPolicyEd25519PublicKeyPem,
                               static_cast<int>(kPolicyEd25519PublicKeyPemLen));
    if (!bio) {
        throw std::runtime_error("BIO_new_mem_buf failed");
    }
    EVP_PKEY* pkey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!pkey) {
        throw std::runtime_error("Failed to parse embedded policy public key");
    }
    if (EVP_PKEY_base_id(pkey) != EVP_PKEY_ED25519) {
        EVP_PKEY_free(pkey);
        throw std::runtime_error("Embedded policy public key is not Ed25519");
    }

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        EVP_PKEY_free(pkey);
        throw std::runtime_error("EVP_MD_CTX_new failed");
    }

    const int ok_init = EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, pkey);
    const int ok_verify = ok_init == 1
        ? EVP_DigestVerify(ctx, signature.data(), signature.size(), data.data(), data.size())
        : 0;
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return ok_verify == 1;
}

static bool matches_pinned_legacy_allowlist_pubkey(const std::vector<uint8_t>& pubkey_pem) {
    std::uint8_t digest[SHA256_DIGEST_LENGTH];
    SHA256(pubkey_pem.data(), pubkey_pem.size(), digest);
    return CRYPTO_memcmp(digest,
                         kLegacySentinelAllowlistPubPemSha256,
                         sizeof(kLegacySentinelAllowlistPubPemSha256)) == 0;
}

static bool verify_pinned_legacy_allowlist_signature(const std::vector<uint8_t>& data,
                                                     const std::vector<uint8_t>& signature,
                                                     const std::vector<uint8_t>& pubkey_pem) {
    BIO* bio = BIO_new_mem_buf(pubkey_pem.data(), static_cast<int>(pubkey_pem.size()));
    if (!bio) {
        throw std::runtime_error("BIO_new_mem_buf failed");
    }
    EVP_PKEY* pkey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!pkey) {
        throw std::runtime_error("Failed to parse legacy allowlist public key");
    }
    if (EVP_PKEY_base_id(pkey) != EVP_PKEY_RSA) {
        EVP_PKEY_free(pkey);
        throw std::runtime_error("Legacy allowlist public key is not RSA");
    }

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        EVP_PKEY_free(pkey);
        throw std::runtime_error("EVP_MD_CTX_new failed");
    }

    EVP_PKEY_CTX* pctx = nullptr;
    int ok = EVP_DigestVerifyInit(ctx, &pctx, EVP_sha256(), nullptr, pkey);
    if (ok == 1) {
        ok = EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_PADDING);
    }
    const int ok_verify = ok == 1
        ? EVP_DigestVerify(ctx, signature.data(), signature.size(), data.data(), data.size())
        : 0;
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return ok_verify == 1;
}

static bool check_allowlist_lockdown(std::string& reason) {
    const char* dir_path = "/etc/hesia/sentinel";
    const char* file_path = "/etc/hesia/sentinel/allowlist.conf";
    const char* sig_path = "/etc/hesia/sentinel/allowlist.conf.sig";
    const char* legacy_sig_path = "/etc/hesia/sentinel/allowlist.sig";
    const char* legacy_pub_path = "/etc/hesia/sentinel/allowlist_pub.pem";

    auto check_node = [&](const char* path, bool expect_dir) -> bool {
        struct stat st{};
        if (lstat(path, &st) != 0) {
            reason = std::string("cannot stat ") + path + ": " + std::strerror(errno);
            return false;
        }
        if (S_ISLNK(st.st_mode)) {
            reason = std::string("path must not be symlink: ") + path;
            return false;
        }
        if (expect_dir) {
            if (!S_ISDIR(st.st_mode)) {
                reason = std::string("not a directory: ") + path;
                return false;
            }
        } else {
            if (!S_ISREG(st.st_mode)) {
                reason = std::string("not a regular file: ") + path;
                return false;
            }
        }
        if (st.st_uid != 0) {
            reason = std::string("not owned by root: ") + path;
            return false;
        }
        if (st.st_mode & (S_IWGRP | S_IWOTH)) {
            reason = std::string("group/other writable: ") + path;
            return false;
        }
        return true;
    };

    if (!check_node(dir_path, true)) return false;
    if (!check_node(file_path, false)) return false;

    bool have_supported_material = false;
    if (std::filesystem::exists(sig_path)) {
        have_supported_material = true;
        if (!check_node(sig_path, false)) return false;
    }
    if (std::filesystem::exists(legacy_sig_path) || std::filesystem::exists(legacy_pub_path)) {
        have_supported_material = true;
        if (!check_node(legacy_sig_path, false)) return false;
        if (!check_node(legacy_pub_path, false)) return false;
    }
    if (!have_supported_material) {
        reason = "no Sentinel signature material found";
        return false;
    }
    return true;
}

static bool verify_sentinel_allowlist_signature(std::string& reason) {
    const std::filesystem::path allowlist_path("/etc/hesia/sentinel/allowlist.conf");
    const std::filesystem::path signature_path("/etc/hesia/sentinel/allowlist.conf.sig");
    const std::filesystem::path legacy_signature_path("/etc/hesia/sentinel/allowlist.sig");
    const std::filesystem::path legacy_pubkey_path("/etc/hesia/sentinel/allowlist_pub.pem");

    try {
        const std::vector<uint8_t> allowlist = read_binary_file_or_throw(allowlist_path);
        std::string failures;
        bool attempted = false;

        if (std::filesystem::exists(signature_path)) {
            attempted = true;
            try {
                std::vector<uint8_t> signature = read_binary_file_or_throw(signature_path);
                if (looks_base64_signature(signature)) {
                    signature = decode_base64_or_throw(signature);
                }
                if (signature.size() == 64 &&
                    verify_ed25519_embedded_signature(allowlist, signature)) {
                    return true;
                }
                failures = "modern Ed25519 allowlist signature invalid";
            } catch (const std::exception& e) {
                failures = std::string("modern Ed25519 check failed: ") + e.what();
            }
        }

        if (std::filesystem::exists(legacy_signature_path) &&
            std::filesystem::exists(legacy_pubkey_path)) {
            attempted = true;
            try {
                const std::vector<uint8_t> legacy_sig = read_binary_file_or_throw(legacy_signature_path);
                const std::vector<uint8_t> legacy_pub = read_binary_file_or_throw(legacy_pubkey_path);
                if (!matches_pinned_legacy_allowlist_pubkey(legacy_pub)) {
                    if (!failures.empty()) failures += "; ";
                    failures += "legacy allowlist public key does not match pinned fingerprint";
                } else if (verify_pinned_legacy_allowlist_signature(allowlist, legacy_sig, legacy_pub)) {
                    return true;
                } else {
                    if (!failures.empty()) failures += "; ";
                    failures += "legacy RSA allowlist signature invalid";
                }
            } catch (const std::exception& e) {
                if (!failures.empty()) failures += "; ";
                failures += std::string("legacy RSA check failed: ") + e.what();
            }
        }

        if (!attempted) {
            reason = "no supported Sentinel allowlist signature material present";
            return false;
        }
        reason = failures.empty() ? "allowlist signature verification failed" : failures;
        return false;
    } catch (const std::exception& e) {
        reason = e.what();
        return false;
    }
}

static bool drop_privileges(const char* user_name,
                            const char* group_name,
                            std::string& reason) {
    if (!user_name || !*user_name) {
        reason = "HESIA_DROP_USER not set";
        return false;
    }
    if (geteuid() != 0) {
        return true;
    }

    errno = 0;
    struct passwd* pw = getpwnam(user_name);
    if (!pw) {
        reason = std::string("unknown user: ") + user_name;
        return false;
    }

    gid_t gid = pw->pw_gid;
    if (group_name && *group_name) {
        struct group* gr = getgrnam(group_name);
        if (!gr) {
            reason = std::string("unknown group: ") + group_name;
            return false;
        }
        gid = gr->gr_gid;
    }

    if (initgroups(pw->pw_name, gid) != 0) {
        reason = std::string("initgroups failed: ") + std::strerror(errno);
        return false;
    }
    if (setgid(gid) != 0) {
        reason = std::string("setgid failed: ") + std::strerror(errno);
        return false;
    }
    if (setuid(pw->pw_uid) != 0) {
        reason = std::string("setuid failed: ") + std::strerror(errno);
        return false;
    }
    if (geteuid() == 0) {
        reason = "drop privileges failed";
        return false;
    }
    return true;
}

static void maybe_drop_privileges(const std::shared_ptr<Logger>& logger) {
    const char* drop_user = std::getenv("HESIA_DROP_USER");
    if (!drop_user || !*drop_user) {
        return;
    }
    const char* drop_group = std::getenv("HESIA_DROP_GROUP");
    std::string reason;
    if (!drop_privileges(drop_user, drop_group, reason)) {
        const std::string msg = std::string("Drop privileges failed: ") + reason;
        if (logger) {
            logger->error(msg);
        } else {
            std::cerr << msg << std::endl;
        }
        throw std::runtime_error(msg);
    }
    if (logger) {
        logger->info(std::string("Privileges dropped to user ") + drop_user);
    } else {
        std::cerr << "Privileges dropped to user " << drop_user << std::endl;
    }
}

static std::filesystem::path resolve_runtime_secure_path(const std::string& secure_dir,
                                                         const std::string& configured_path,
                                                         const char* fallback_name)
{
    const std::filesystem::path base(secure_dir);
    if (configured_path.empty()) {
        return base / fallback_name;
    }
    const std::filesystem::path candidate(configured_path);
    if (candidate.is_absolute()) {
        return candidate;
    }
    return base / candidate;
}

static void log_optee_auth_preflight(const std::shared_ptr<Logger>& logger,
                                     const char* phase)
{
    const SecurityPolicy policy = load_security_policy_or_throw("drone");
    const bool require_auth = policy.prod_fuse || policy.require_optee_session_auth;
    const std::filesystem::path auth_path = resolve_runtime_secure_path(
        policy.secure_dir,
        policy.optee_session_auth_path,
        "optee_session_auth.sealed");

    optee_set_session_auth_secret_path(auth_path);
    optee_require_session_auth(require_auth);
    optee_require_session_auth_ready_or_throw();

    if (logger) {
        logger->info(std::string("[OPTEE-PREFLIGHT] ") + phase +
                     " OK (require_auth=" + (require_auth ? "1" : "0") +
                     ", path=" + auth_path.string() + ")");
    }
}

static void apply_process_secret_hardening(const std::shared_ptr<Logger>& logger) {
#ifndef _WIN32
    struct rlimit rl{};
    rl.rlim_cur = 0;
    rl.rlim_max = 0;
    if (setrlimit(RLIMIT_CORE, &rl) != 0) {
        const std::string msg = std::string("Failed to disable core dumps: ") + std::strerror(errno);
        if (logger) {
            logger->warning(msg);
        }
    }
    if (prctl(PR_SET_DUMPABLE, 0, 0, 0, 0) != 0) {
        const std::string msg = std::string("Failed to set PR_SET_DUMPABLE=0: ") + std::strerror(errno);
        if (logger) {
            logger->warning(msg);
        }
    }
#else
    (void)logger;
#endif
}
#endif

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    try {
#ifndef _WIN32
        std::string allowlist_reason;
        if (!check_allowlist_lockdown(allowlist_reason)) {
            std::cerr << "Erreur: allowlist non verrouillee: " << allowlist_reason << std::endl;
            return 1;
        }
        if (!verify_sentinel_allowlist_signature(allowlist_reason)) {
            std::cerr << "Erreur: signature Sentinel allowlist invalide: " << allowlist_reason << std::endl;
            return 1;
        }
#endif
        int sentinel_rc = hesia::sentinel_check();
        if (sentinel_rc != 0) {
            std::cerr << "Erreur: Sentinel check failed (code " << sentinel_rc << ")" << std::endl;
            return sentinel_rc;
        }

        Config::init();
        auto logger = setup_logger("HESIA-DRONE", Config::LOG_DIR);
        apply_process_secret_hardening(logger);
#ifdef HESIA_ALLOW_SOFT_SIGN
        logger->warning("HESIA_ALLOW_SOFT_SIGN active: la signature ML-DSA hors OP-TEE est autorisee pour usage non production uniquement");
#endif
#ifndef _WIN32
        log_optee_auth_preflight(logger, "before_runtime_protection");
#endif
#ifndef _WIN32
        // Drop privileges before enabling seccomp to avoid blocked setgroups/setuid.
        maybe_drop_privileges(logger);
#endif
        RuntimeProtection::setup_protection();
#ifndef _WIN32
        log_optee_auth_preflight(logger, "after_runtime_protection");
#endif
        logger->info("Demarrage du drone HESIA");
        
        DroneNetworkClient client("DRONE_001");
        return client.main();
    } catch (const std::exception& e) {
        std::cerr << "Erreur: " << e.what() << std::endl;
        return 1;
    }
}
