#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <memory>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <set>
#include <unordered_map>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/resource.h>
  #include <sys/socket.h>
  #include <sys/prctl.h>
  #include <unistd.h>
#endif

#include "../../drone_source/logger.hpp"
#include "../../drone_source/config.hpp"
#include "hesia_server_session.hpp"
#include "tls_utils.hpp"
#include "security_audit.hpp"
#include "../include/policy.hpp"

using hesia::Logger;
using hesia::Config;
using hesia::HesiaServerSession;
using hesia::SecurityPolicy;
using hesia::resolve_path;
using hesia::load_security_policy_or_throw;

static std::string openssl_err_stack() {
    std::string out;
    unsigned long e = 0;
    char buf[256];
    while ((e = ERR_get_error()) != 0) {
        ERR_error_string_n(e, buf, sizeof(buf));
        if (!out.empty()) out += " | ";
        out += buf;
    }
    return out;
}

static void require_file_exists(const std::string& path, const std::shared_ptr<Logger>& logger);

static void apply_process_secret_hardening(const std::shared_ptr<Logger>& logger) {
#ifndef _WIN32
    struct rlimit rl{};
    rl.rlim_cur = 0;
    rl.rlim_max = 0;
    if (setrlimit(RLIMIT_CORE, &rl) != 0 && logger) {
        logger->warning("Failed to disable core dumps");
    }
    if (prctl(PR_SET_DUMPABLE, 0, 0, 0, 0) != 0 && logger) {
        logger->warning("Failed to set PR_SET_DUMPABLE=0");
    }
#else
    (void)logger;
#endif
}

static void reject_forensic_env_in_production_or_throw(const SecurityPolicy& policy) {
    if (!policy.prod_fuse) {
        return;
    }
    const char* vars[] = {
        "HESIA_FORENSIC_MESSAGE_CAPTURE",
        "HESIA_FORENSIC_VIDEO_CAPTURE",
    };
    for (const char* name : vars) {
        const char* value = std::getenv(name);
        if (value && *value) {
            throw std::runtime_error(std::string("Production mode forbids forensic environment override: ") + name);
        }
    }
}

static std::string asn1_to_string(const ASN1_STRING* value) {
    if (!value) {
        return {};
    }
    unsigned char* utf8 = nullptr;
    const int len = ASN1_STRING_to_UTF8(&utf8, value);
    if (len < 0 || !utf8) {
        return {};
    }
    std::string out(reinterpret_cast<char*>(utf8), static_cast<size_t>(len));
    OPENSSL_free(utf8);
    return out;
}

static std::set<std::string> get_dev_cert_markers() {
    return {
        "localhost",
        "127.0.0.1",
        "192.168.1.29",
    };
}

static void reject_lab_certificate_in_production_or_throw(const SecurityPolicy& policy,
                                                          const std::string& cert_path) {
    if (!policy.prod_fuse) {
        return;
    }

    FILE* fp = fopen(cert_path.c_str(), "rb");
    if (!fp) {
        throw std::runtime_error("Cannot open certificate for production validation: " + cert_path);
    }
    X509* cert = PEM_read_X509(fp, nullptr, nullptr, nullptr);
    fclose(fp);
    if (!cert) {
        throw std::runtime_error("Cannot parse server certificate: " + cert_path);
    }

    const auto markers = get_dev_cert_markers();
    auto matches_marker = [&](const std::string& value) {
        return markers.find(value) != markers.end();
    };

    std::string reason;
    X509_NAME* subject = X509_get_subject_name(cert);
    const int cn_idx = X509_NAME_get_index_by_NID(subject, NID_commonName, -1);
    if (cn_idx >= 0) {
        X509_NAME_ENTRY* entry = X509_NAME_get_entry(subject, cn_idx);
        ASN1_STRING* data = X509_NAME_ENTRY_get_data(entry);
        const std::string cn = asn1_to_string(data);
        if (matches_marker(cn)) {
            reason = "CN=" + cn;
        }
    }

    if (reason.empty()) {
        GENERAL_NAMES* sans = reinterpret_cast<GENERAL_NAMES*>(
            X509_get_ext_d2i(cert, NID_subject_alt_name, nullptr, nullptr));
        if (sans) {
            const int san_count = sk_GENERAL_NAME_num(sans);
            for (int i = 0; i < san_count && reason.empty(); ++i) {
                const GENERAL_NAME* name = sk_GENERAL_NAME_value(sans, i);
                if (name->type == GEN_DNS) {
                    const std::string dns = asn1_to_string(name->d.dNSName);
                    if (matches_marker(dns)) {
                        reason = "DNS SAN=" + dns;
                    }
                } else if (name->type == GEN_IPADD &&
                           name->d.iPAddress &&
                           ASN1_STRING_length(name->d.iPAddress) == 4) {
                    const unsigned char* raw = ASN1_STRING_get0_data(name->d.iPAddress);
                    char ipbuf[INET_ADDRSTRLEN]{0};
                    if (raw && inet_ntop(AF_INET, raw, ipbuf, sizeof(ipbuf)) != nullptr) {
                        const std::string ip = ipbuf;
                        if (matches_marker(ip)) {
                            reason = "IP SAN=" + ip;
                        }
                    }
                }
            }
            GENERAL_NAMES_free(sans);
        }
    }

    X509_free(cert);
    if (!reason.empty()) {
        throw std::runtime_error("Production mode rejects lab/development TLS certificate (" + reason + ")");
    }
}

static std::vector<uint8_t> read_file_binary(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        throw std::runtime_error("Cannot open file: " + path);
    }
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
}

static bool looks_base64(const std::vector<uint8_t>& data) {
    for (uint8_t c : data) {
        if (c == '\n' || c == '\r' || c == '\t' || c == ' ') continue;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=') {
            continue;
        }
        return false;
    }
    return !data.empty();
}

static std::vector<uint8_t> base64_decode(const std::vector<uint8_t>& data) {
    std::string compact;
    compact.reserve(data.size());
    for (uint8_t c : data) {
        if (c == '\n' || c == '\r' || c == '\t' || c == ' ') continue;
        compact.push_back(static_cast<char>(c));
    }
    if (compact.empty()) {
        return {};
    }
    if ((compact.size() % 4) != 0) {
        throw std::runtime_error("Base64 decode failed: length not multiple of 4");
    }

    size_t padding = 0;
    if (!compact.empty() && compact.back() == '=') {
        padding++;
        if (compact.size() >= 2 && compact[compact.size() - 2] == '=') {
            padding++;
        }
    }

    const int len = static_cast<int>(compact.size());
    const int out_len = (len * 3) / 4;
    std::vector<uint8_t> out(static_cast<size_t>(out_len));
    int n = EVP_DecodeBlock(out.data(),
                            reinterpret_cast<const unsigned char*>(compact.data()),
                            len);
    if (n < 0) {
        throw std::runtime_error("Base64 decode failed");
    }
    if (padding > 2 || n < static_cast<int>(padding)) {
        throw std::runtime_error("Base64 decode failed: invalid padding");
    }
    n -= static_cast<int>(padding);
    out.resize(static_cast<size_t>(n));
    return out;
}

static bool verify_ed25519_signature(const std::vector<uint8_t>& data,
                                     const std::vector<uint8_t>& sig,
                                     const std::string& pubkey_path) {
    if (sig.empty()) return false;
    FILE* fp = fopen(pubkey_path.c_str(), "rb");
    if (!fp) {
        throw std::runtime_error("Cannot open public key: " + pubkey_path);
    }
    EVP_PKEY* pkey = PEM_read_PUBKEY(fp, nullptr, nullptr, nullptr);
    fclose(fp);
    if (!pkey) {
        throw std::runtime_error("Failed to read public key: " + pubkey_path);
    }
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        EVP_PKEY_free(pkey);
        throw std::runtime_error("EVP_MD_CTX_new failed");
    }
    int ok = EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, pkey);
    if (ok != 1) {
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        throw std::runtime_error("EVP_DigestVerifyInit failed");
    }
    ok = EVP_DigestVerify(ctx, sig.data(), sig.size(), data.data(), data.size());
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return ok == 1;
}

static void verify_release_signature_or_throw(const SecurityPolicy& policy,
                                              const std::shared_ptr<Logger>& logger,
                                              const std::string& keys_dir,
                                              const std::string& secure_dir) {
    if (!policy.require_release_signature) return;
    if (policy.release_target_path.empty() ||
        policy.release_sig_path.empty() ||
        policy.release_pubkey_path.empty()) {
        throw std::runtime_error("Release signature required but paths not configured");
    }

    auto resolve_release_input = [&](const std::string& value) -> std::string {
        std::filesystem::path configured(value);
        if (configured.is_absolute()) {
            return configured.string();
        }

        const std::filesystem::path secure_candidate = std::filesystem::path(secure_dir) / configured;
        if (std::filesystem::exists(secure_candidate)) {
            return secure_candidate.string();
        }

        const std::filesystem::path keys_candidate = std::filesystem::path(keys_dir) / configured;
        if (!policy.prod_fuse && std::filesystem::exists(keys_candidate)) {
            return keys_candidate.string();
        }
        return secure_candidate.string();
    };

    const std::string target = resolve_release_input(policy.release_target_path);
    const std::string sig_path = resolve_release_input(policy.release_sig_path);
    const std::string pub_path = resolve_release_input(policy.release_pubkey_path);

    require_file_exists(target, logger);
    require_file_exists(sig_path, logger);
    require_file_exists(pub_path, logger);

    std::vector<uint8_t> data = read_file_binary(target);
    std::vector<uint8_t> sig_raw = read_file_binary(sig_path);
    std::vector<uint8_t> sig = looks_base64(sig_raw) ? base64_decode(sig_raw) : sig_raw;

    if (!verify_ed25519_signature(data, sig, pub_path)) {
        throw std::runtime_error("Release signature verification failed");
    }
}

static void require_file_exists(const std::string& path, const std::shared_ptr<Logger>& logger) {
    if (!std::filesystem::exists(path)) {
        logger->error("Missing required file: " + path);
        throw std::runtime_error("Missing required file: " + path);
    }
}

static int create_listen_socket(const std::string& bind_addr, int port) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);
#endif
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, bind_addr.c_str(), &addr.sin_addr) != 1) {
#ifdef _WIN32
        closesocket(fd);
#else
        ::close(fd);
#endif
        return -1;
    }

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
#ifdef _WIN32
        closesocket(fd);
#else
        ::close(fd);
#endif
        return -1;
    }

    if (::listen(fd, 64) < 0) {
#ifdef _WIN32
        closesocket(fd);
#else
        ::close(fd);
#endif
        return -1;
    }
    return fd;
}

static void set_socket_timeouts(int fd, int read_timeout_sec, int write_timeout_sec) {
#ifdef _WIN32
    DWORD rcv_ms = static_cast<DWORD>(read_timeout_sec) * 1000;
    DWORD snd_ms = static_cast<DWORD>(write_timeout_sec) * 1000;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&rcv_ms), sizeof(rcv_ms));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&snd_ms), sizeof(snd_ms));
#else
    struct timeval rcv{};
    rcv.tv_sec = read_timeout_sec;
    rcv.tv_usec = 0;
    struct timeval snd{};
    snd.tv_sec = write_timeout_sec;
    snd.tv_usec = 0;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rcv, sizeof(rcv));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &snd, sizeof(snd));
#endif
}

struct AcceptedConn {
    int fd;
    std::string ip;
    int port;
};

class ConnectionLimiter {
public:
    ConnectionLimiter(int max_total, int max_per_ip)
        : max_total_(max_total), max_per_ip_(max_per_ip) {}

    bool try_acquire(const std::string& ip) {
        std::lock_guard<std::mutex> lock(mu_);
        if (max_total_ > 0 && total_ >= max_total_) {
            return false;
        }
        int& count = per_ip_[ip];
        if (max_per_ip_ > 0 && count >= max_per_ip_) {
            return false;
        }
        ++count;
        ++total_;
        return true;
    }

    void release(const std::string& ip) {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = per_ip_.find(ip);
        if (it != per_ip_.end()) {
            if (it->second > 0) {
                --it->second;
            }
            if (it->second == 0) {
                per_ip_.erase(it);
            }
        }
        if (total_ > 0) {
            --total_;
        }
    }

private:
    int max_total_;
    int max_per_ip_;
    int total_{0};
    std::unordered_map<std::string, int> per_ip_;
    std::mutex mu_;
};

int main() {
    // Init config paths for logger helpers used by shared components.
    Config::init();

    auto root_logger = std::make_shared<Logger>("HESIA-SERVER-CPP", Config::LOG_DIR);
    root_logger->info("Initialisation serveur HESIA (C++)...");

    SecurityPolicy policy = load_security_policy_or_throw("server");
    apply_process_secret_hardening(root_logger);
    reject_forensic_env_in_production_or_throw(policy);
    Logger::set_debug_enabled(!policy.prod_fuse);

    const std::string bind_addr = policy.bind_addr;
    const int port = policy.port;

    const std::string cert_dir = policy.cert_dir;
    const std::string cert_path = resolve_path(cert_dir, policy.server_cert);
    const std::string key_path  = resolve_path(cert_dir, policy.server_key);
    const std::string ca_path   = resolve_path(cert_dir, policy.ca_cert);
    const std::string keys_dir  = resolve_path(policy.policy_dir, policy.keys_dir);
    const std::string secure_dir = resolve_path(policy.policy_dir, policy.secure_dir);

    verify_release_signature_or_throw(policy, root_logger, keys_dir, secure_dir);

    // OpenSSL init
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    SSL_CTX* ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx) {
        root_logger->error("SSL_CTX_new failed: " + openssl_err_stack());
        return 1;
    }

    // TLS1.3 only
    SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION);
    SSL_CTX_set_options(ctx, SSL_OP_NO_RENEGOTIATION);

    require_file_exists(cert_path, root_logger);
    require_file_exists(key_path, root_logger);
    require_file_exists(ca_path, root_logger);
    reject_lab_certificate_in_production_or_throw(policy, cert_path);

    if (SSL_CTX_use_certificate_file(ctx, cert_path.c_str(), SSL_FILETYPE_PEM) != 1) {
        root_logger->error("Load cert failed: " + cert_path + " : " + openssl_err_stack());
        SSL_CTX_free(ctx);
        return 1;
    }
    if (SSL_CTX_use_PrivateKey_file(ctx, key_path.c_str(), SSL_FILETYPE_PEM) != 1) {
        root_logger->error("Load key failed: " + key_path + " : " + openssl_err_stack());
        SSL_CTX_free(ctx);
        return 1;
    }
    if (SSL_CTX_check_private_key(ctx) != 1) {
        root_logger->error("Private key mismatch: " + openssl_err_stack());
        SSL_CTX_free(ctx);
        return 1;
    }

    // mTLS: require client cert + verify against CA
    if (SSL_CTX_load_verify_locations(ctx, ca_path.c_str(), nullptr) != 1) {
        root_logger->error("Load CA failed: " + ca_path + " : " + openssl_err_stack());
        SSL_CTX_free(ctx);
        return 1;
    }
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
    SSL_CTX_set_verify_depth(ctx, 2);

    int listen_fd = create_listen_socket(bind_addr, port);
    if (listen_fd < 0) {
        root_logger->error("Impossible d'ouvrir le socket d'écoute sur " + bind_addr + ":" + std::to_string(port));
        SSL_CTX_free(ctx);
        return 1;
    }

    root_logger->info("✓ Serveur en écoute sur " + bind_addr + ":" + std::to_string(port));
    root_logger->info("mTLS required");
    root_logger->info("TLS cert=" + cert_path + " key=" + key_path + " ca=" + ca_path);

    hesia::AuditConfig audit_cfg;
    audit_cfg.enabled = policy.audit_enabled;
    audit_cfg.log_path = resolve_path(Config::LOG_DIR, policy.audit_log_path);
    audit_cfg.secure_dir = secure_dir;
    audit_cfg.key_path = policy.audit_key_path.empty()
        ? resolve_path(secure_dir, "audit.key")
        : resolve_path(secure_dir, policy.audit_key_path);
    audit_cfg.alert_path = resolve_path(Config::LOG_DIR, policy.audit_alert_path);
    audit_cfg.signing_key_path = resolve_path(keys_dir, policy.audit_signing_key);
    audit_cfg.signing_pub_path = resolve_path(keys_dir, policy.audit_signing_pub);
    audit_cfg.export_path = resolve_path(Config::LOG_DIR, policy.audit_export_path);
    audit_cfg.require_signing = policy.require_audit_signing;
    audit_cfg.rotate_on_start = policy.audit_rotate_on_start;
    audit_cfg.rotate_interval_sec = policy.audit_rotate_interval_sec;

    auto audit = std::make_shared<hesia::SecurityAudit>("server", Config::LOG_DIR, root_logger, audit_cfg);
    audit->rotate_key_if_requested();

    if (policy.incident_mode) {
        root_logger->error("Incident mode enabled - refusing new sessions");
        if (audit) {
            audit->event("INCIDENT_MODE", "ERROR", "server refusing connections");
        }
        SSL_CTX_free(ctx);
        return 2;
    }

    ConnectionLimiter limiter(policy.max_conn_total, policy.max_conn_per_ip);
    std::mutex queue_mu;
    std::condition_variable queue_cv;
    std::deque<AcceptedConn> queue;
    const int worker_threads = std::max(1, policy.worker_threads);
    const std::size_t max_pending = static_cast<std::size_t>(std::max(1, policy.max_pending_queue));

    auto worker = [&]() {
        while (true) {
            AcceptedConn conn{};
            {
                std::unique_lock<std::mutex> lock(queue_mu);
                queue_cv.wait(lock, [&]() { return !queue.empty(); });
                conn = std::move(queue.front());
                queue.pop_front();
            }

            set_socket_timeouts(conn.fd, policy.ssl_read_timeout_sec, policy.ssl_write_timeout_sec);

            std::string client_label = conn.ip + ":" + std::to_string(conn.port);
            auto sess_logger = std::make_shared<Logger>("SERVERCPP." + client_label, Config::LOG_DIR);
            sess_logger->info("Nouvelle connexion " + client_label);

            SSL* ssl = SSL_new(ctx);
            if (!ssl) {
                sess_logger->error("SSL_new failed: " + openssl_err_stack());
#ifdef _WIN32
                closesocket(conn.fd);
#else
                ::close(conn.fd);
#endif
                limiter.release(conn.ip);
                continue;
            }
            SSL_set_fd(ssl, conn.fd);

            if (SSL_accept(ssl) != 1) {
                sess_logger->error("TLS handshake failed: " + openssl_err_stack());
                SSL_free(ssl);
#ifdef _WIN32
                closesocket(conn.fd);
#else
                ::close(conn.fd);
#endif
                limiter.release(conn.ip);
                continue;
            }

            X509* peer_cert = SSL_get_peer_certificate(ssl);
            if (!peer_cert) {
                sess_logger->error("mTLS required: client certificate missing");
                SSL_shutdown(ssl);
                SSL_free(ssl);
#ifdef _WIN32
                closesocket(conn.fd);
#else
                ::close(conn.fd);
#endif
                limiter.release(conn.ip);
                continue;
            }
            X509_free(peer_cert);

            try {
                HesiaServerSession session(ssl, client_label, keys_dir, secure_dir, sess_logger, audit, policy);
                session.run();
            } catch (const std::exception& e) {
                sess_logger->error(std::string("Session error: ") + e.what());
                if (audit) {
                    audit->event("SESSION_ERROR", "ERROR", "client=" + client_label + " err=" + e.what());
                }
            }
            SSL_shutdown(ssl);
            SSL_free(ssl);
#ifdef _WIN32
            closesocket(conn.fd);
#else
            ::close(conn.fd);
#endif
            sess_logger->info("FIN DE SESSION (" + client_label + ")");
            limiter.release(conn.ip);
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(static_cast<std::size_t>(worker_threads));
    for (int i = 0; i < worker_threads; ++i) {
        workers.emplace_back(worker);
    }

    while (true) {
        sockaddr_in caddr{};
        socklen_t clen = sizeof(caddr);
        int cfd = ::accept(listen_fd, reinterpret_cast<sockaddr*>(&caddr), &clen);
        if (cfd < 0) continue;

        char ipbuf[64]{0};
        inet_ntop(AF_INET, &caddr.sin_addr, ipbuf, sizeof(ipbuf));
        int cport = ntohs(caddr.sin_port);
        std::string ip = ipbuf;

        if (!limiter.try_acquire(ip)) {
            if (audit) {
                audit->event("CONN_LIMIT", "WARN", "ip=" + ip);
            }
#ifdef _WIN32
            closesocket(cfd);
#else
            ::close(cfd);
#endif
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(queue_mu);
            if (queue.size() >= max_pending) {
                limiter.release(ip);
                if (audit) {
                    audit->event("CONN_QUEUE_FULL", "WARN", "ip=" + ip);
                }
#ifdef _WIN32
                closesocket(cfd);
#else
                ::close(cfd);
#endif
                continue;
            }
            queue.push_back({cfd, ip, cport});
        }
        queue_cv.notify_one();
    }

    // Unreachable
    SSL_CTX_free(ctx);
    return 0;
}
