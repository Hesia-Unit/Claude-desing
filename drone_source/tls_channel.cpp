#include "tls_channel.hpp"

#include <stdexcept>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <chrono>
#include <thread>
#include <cerrno>

#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/sha.h>

#ifdef _WIN32
  #include <ws2tcpip.h>
#else
  #include <arpa/inet.h>
  #include <sys/select.h>
#endif

namespace hesia {

static bool wait_fd_ready(int fd, bool want_read, int timeout_ms) {
    if (fd < 0) return false;
#ifdef _WIN32
    SOCKET s = static_cast<SOCKET>(fd);
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(s, &fds);
    TIMEVAL tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    int ret = select(0, want_read ? &fds : nullptr, want_read ? nullptr : &fds, nullptr, &tv);
    return ret > 0;
#else
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    int ret = select(fd + 1, want_read ? &fds : nullptr, want_read ? nullptr : &fds, nullptr, &tv);
    return ret > 0;
#endif
}

static std::string openssl_err_string_single() {
    unsigned long e = ERR_get_error();
    if (e == 0) return "";
    char buf[256];
    ERR_error_string_n(e, buf, sizeof(buf));
    return std::string(buf);
}

static std::string openssl_err_stack_string() {
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

static const char* ssl_error_name(int code) {
    switch (code) {
        case SSL_ERROR_NONE: return "SSL_ERROR_NONE";
        case SSL_ERROR_ZERO_RETURN: return "SSL_ERROR_ZERO_RETURN";
        case SSL_ERROR_WANT_READ: return "SSL_ERROR_WANT_READ";
        case SSL_ERROR_WANT_WRITE: return "SSL_ERROR_WANT_WRITE";
        case SSL_ERROR_WANT_CONNECT: return "SSL_ERROR_WANT_CONNECT";
        case SSL_ERROR_WANT_ACCEPT: return "SSL_ERROR_WANT_ACCEPT";
        case SSL_ERROR_WANT_X509_LOOKUP: return "SSL_ERROR_WANT_X509_LOOKUP";
        case SSL_ERROR_SYSCALL: return "SSL_ERROR_SYSCALL";
        case SSL_ERROR_SSL: return "SSL_ERROR_SSL";
        default: return "SSL_ERROR_UNKNOWN";
    }
}

static std::string path_join(const std::string& dir, const std::string& name) {
    if (dir.empty()) return name;
    if (dir.back() == '/' || dir.back() == '\\') return dir + name;
    return dir + "/" + name;
}

static std::string default_cert_dir() {
#ifdef HESIA_CERT_DIR
    return std::string(HESIA_CERT_DIR);
#else
    return std::string("/etc/hesia/certs");
#endif
}

static void require_file_exists(const std::string& path) {
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error("Missing required file: " + path);
    }
}

static std::vector<uint8_t> extract_spki_der_from_material(const std::vector<uint8_t>& material) {
    if (material.empty()) {
        return {};
    }

    EVP_PKEY* pkey = nullptr;
    BIO* bio = BIO_new_mem_buf(material.data(), static_cast<int>(material.size()));
    if (bio) {
        X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
        if (cert) {
            pkey = X509_get_pubkey(cert);
            X509_free(cert);
        }
        if (!pkey) {
            BIO_free(bio);
            bio = BIO_new_mem_buf(material.data(), static_cast<int>(material.size()));
            if (bio) {
                pkey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
            }
        }
    }
    if (bio) {
        BIO_free(bio);
    }

    if (!pkey) {
        const unsigned char* ptr = material.data();
        X509* cert = d2i_X509(nullptr, &ptr, static_cast<long>(material.size()));
        if (cert) {
            pkey = X509_get_pubkey(cert);
            X509_free(cert);
        }
    }

    if (!pkey) {
        const unsigned char* ptr = material.data();
        pkey = d2i_PUBKEY(nullptr, &ptr, static_cast<long>(material.size()));
    }

    if (!pkey) {
        return {};
    }

    const int len = i2d_PUBKEY(pkey, nullptr);
    if (len <= 0) {
        EVP_PKEY_free(pkey);
        return {};
    }
    std::vector<uint8_t> spki(static_cast<size_t>(len));
    unsigned char* out = spki.data();
    if (i2d_PUBKEY(pkey, &out) != len) {
        EVP_PKEY_free(pkey);
        return {};
    }
    EVP_PKEY_free(pkey);
    return spki;
}

TLSChannel::TLSChannel() {
    // OpenSSL 1.1.0+ auto-initializes; still safe to call.
    SSL_library_init();
    SSL_load_error_strings();

    const SSL_METHOD* method = TLS_client_method();
    ctx_ = SSL_CTX_new(method);
    if (!ctx_) {
        throw std::runtime_error("SSL_CTX_new failed: " + openssl_err_string_single());
    }

    // Enforce TLS 1.3 only
    SSL_CTX_set_min_proto_version(ctx_, TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(ctx_, TLS1_3_VERSION);

    // Disable renegotiation (TLS1.3 doesn't do renegotiation)
    SSL_CTX_set_options(ctx_, SSL_OP_NO_RENEGOTIATION);
}

void TLSChannel::set_cert_paths(const TLSPaths& paths) {
    cert_paths_ = paths;
}

void TLSChannel::set_timeouts(int read_timeout_ms, int write_timeout_ms) {
    if (read_timeout_ms > 0) {
        read_timeout_ms_ = read_timeout_ms;
    }
    if (write_timeout_ms > 0) {
        write_timeout_ms_ = write_timeout_ms;
    }
}

TLSChannel::~TLSChannel() {
    close();
    if (ctx_) {
        SSL_CTX_free(ctx_);
        ctx_ = nullptr;
    }
}

bool TLSChannel::is_ip_literal(const std::string& host) {
    sockaddr_in sa4{};
    if (inet_pton(AF_INET, host.c_str(), &(sa4.sin_addr)) == 1) return true;
    sockaddr_in6 sa6{};
    if (inet_pton(AF_INET6, host.c_str(), &(sa6.sin6_addr)) == 1) return true;
    return false;
}

std::vector<uint8_t> TLSChannel::sha256(const uint8_t* data, size_t len) {
    std::vector<uint8_t> out(SHA256_DIGEST_LENGTH);
    SHA256(data, len, out.data());
    return out;
}

void TLSChannel::enable_spki_pinning(const std::vector<uint8_t>& pin_or_bytes) {
    if (pin_or_bytes.empty()) {
        use_spki_pinning_ = false;
        spki_sha256_pin_.clear();
        return;
    }
    use_spki_pinning_ = true;
    if (pin_or_bytes.size() == SHA256_DIGEST_LENGTH) {
        spki_sha256_pin_ = pin_or_bytes;
    } else {
        const std::vector<uint8_t> spki = extract_spki_der_from_material(pin_or_bytes);
        if (!spki.empty()) {
            spki_sha256_pin_ = sha256(spki.data(), spki.size());
        } else {
            spki_sha256_pin_ = sha256(pin_or_bytes.data(), pin_or_bytes.size());
        }
    }
}

bool TLSChannel::verify_peer_spki() const {
    if (!use_spki_pinning_) return true;
    if (spki_sha256_pin_.size() != SHA256_DIGEST_LENGTH) return false;

    X509* cert = SSL_get_peer_certificate(ssl_);
    if (!cert) return false;

    // Extract SubjectPublicKeyInfo (DER)
    int len = i2d_X509_PUBKEY(X509_get_X509_PUBKEY(cert), nullptr);
    if (len <= 0) {
        X509_free(cert);
        return false;
    }
    std::vector<uint8_t> spki(static_cast<size_t>(len));
    unsigned char* p = spki.data();
    if (i2d_X509_PUBKEY(X509_get_X509_PUBKEY(cert), &p) != len) {
        X509_free(cert);
        return false;
    }
    X509_free(cert);

    auto h = sha256(spki.data(), spki.size());
    return h == spki_sha256_pin_;
}

std::vector<uint8_t> TLSChannel::peer_cert_sha256() const {
    if (!ssl_) throw std::runtime_error("TLSChannel: SSL not initialized");
    X509* cert = SSL_get_peer_certificate(ssl_);
    if (!cert) throw std::runtime_error("TLSChannel: no peer certificate");

    unsigned int n = 0;
    unsigned char md[EVP_MAX_MD_SIZE];
    if (X509_digest(cert, EVP_sha256(), md, &n) != 1 || n == 0) {
        X509_free(cert);
        throw std::runtime_error("TLSChannel: X509_digest failed");
    }
    X509_free(cert);
    return std::vector<uint8_t>(md, md + n);
}

std::vector<uint8_t> TLSChannel::export_keying_material(const std::string& label, size_t length) const {
    if (!ssl_) throw std::runtime_error("TLSChannel: SSL not initialized");
    std::vector<uint8_t> out(length);
    if (SSL_export_keying_material(ssl_, out.data(), out.size(), label.c_str(), label.size(), nullptr, 0, 0) != 1) {
        throw std::runtime_error("TLSChannel: SSL_export_keying_material failed: " + openssl_err_string_single());
    }
    return out;
}

bool TLSChannel::connect_on_socket(int socket_fd, const std::string& host, bool verify_peer) {
    if (!ctx_) return false;

    const std::string cert_dir = default_cert_dir();
    const std::string ca_path = cert_paths_.ca_path.empty()
        ? path_join(cert_dir, "ca.crt")
        : cert_paths_.ca_path;
    const std::string cert_path = cert_paths_.cert_path.empty()
        ? path_join(cert_dir, "drone.crt")
        : cert_paths_.cert_path;
    const std::string key_path = cert_paths_.key_path.empty()
        ? path_join(cert_dir, "drone.key")
        : cert_paths_.key_path;

    // mTLS required: CA + client cert + client key
    require_file_exists(ca_path);
    require_file_exists(cert_path);
    require_file_exists(key_path);

    if (SSL_CTX_use_certificate_file(ctx_, cert_path.c_str(), SSL_FILETYPE_PEM) != 1) {
        throw std::runtime_error("SSL_CTX_use_certificate_file failed: " + openssl_err_string_single());
    }
    if (SSL_CTX_use_PrivateKey_file(ctx_, key_path.c_str(), SSL_FILETYPE_PEM) != 1) {
        throw std::runtime_error("SSL_CTX_use_PrivateKey_file failed: " + openssl_err_string_single());
    }
    if (SSL_CTX_check_private_key(ctx_) != 1) {
        throw std::runtime_error("SSL_CTX_check_private_key failed: " + openssl_err_string_single());
    }
    if (SSL_CTX_load_verify_locations(ctx_, ca_path.c_str(), nullptr) != 1) {
        throw std::runtime_error("SSL_CTX_load_verify_locations failed: " + openssl_err_string_single());
    }

    ssl_ = SSL_new(ctx_);
    if (!ssl_) {
        throw std::runtime_error("SSL_new failed: " + openssl_err_string_single());
    }

    SSL_set_verify(ssl_, SSL_VERIFY_PEER, nullptr);

    SSL_set_fd(ssl_, socket_fd);

    // Hostname or IP SAN verification
    if (verify_peer) {
        X509_VERIFY_PARAM* param = SSL_get0_param(ssl_);
        // Hard-fail if name/IP doesn't match
        X509_VERIFY_PARAM_set_hostflags(param, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);

        if (is_ip_literal(host)) {
            // For IP literals, validate IP SAN.
            if (X509_VERIFY_PARAM_set1_ip_asc(param, host.c_str()) != 1) {
                throw std::runtime_error("X509_VERIFY_PARAM_set1_ip_asc failed");
            }
        } else {
            // For DNS names, validate dNSName SAN.
            if (X509_VERIFY_PARAM_set1_host(param, host.c_str(), 0) != 1) {
                throw std::runtime_error("X509_VERIFY_PARAM_set1_host failed");
            }
            // SNI only for DNS names
            SSL_set_tlsext_host_name(ssl_, host.c_str());
        }
    } else {
        // Still set SNI for DNS names (harmless) to support virtual hosts
        if (!is_ip_literal(host)) {
            SSL_set_tlsext_host_name(ssl_, host.c_str());
        }
    }

    ERR_clear_error();

    int ret = SSL_connect(ssl_);
    if (ret != 1) {
        const int ssl_err = SSL_get_error(ssl_, ret);
        long verify_res = X509_V_OK;
        const char* verify_str = "disabled";
        if (verify_peer) {
            verify_res = SSL_get_verify_result(ssl_);
            verify_str = X509_verify_cert_error_string(verify_res);
        }
        std::string stack = openssl_err_stack_string();
        std::string msg = "TLS handshake failed (SSL_connect ret=" + std::to_string(ret) +
                          ", ssl_error=" + ssl_error_name(ssl_err) +
                          ", verify=" + std::string(verify_str) +
                          ")";
        if (!stack.empty()) {
            msg += ": " + stack;
        }
        SSL_free(ssl_);
        ssl_ = nullptr;
        connected_ = false;
        throw std::runtime_error(msg);
    }

    if (verify_peer) {
        // SPKI pinning (optional)
        if (!verify_peer_spki()) {
            SSL_free(ssl_);
            ssl_ = nullptr;
            connected_ = false;
            throw std::runtime_error("TLS peer SPKI pinning failed");
        }
    }

    connected_ = true;
    return true;
}

int TLSChannel::write(const uint8_t* data, int len) {
    if (!ssl_ || !connected_) return -1;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(write_timeout_ms_);
    while (true) {
        int ret = SSL_write(ssl_, data, len);
        if (ret > 0) {
            return ret;
        }
        int err = SSL_get_error(ssl_, ret);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            int fd = SSL_get_fd(ssl_);
            int remaining_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()).count());
            if (remaining_ms <= 0) return -1;
            if (!wait_fd_ready(fd, err == SSL_ERROR_WANT_READ, remaining_ms)) {
                return -1;
            }
            continue;
        }
        if (err == SSL_ERROR_SYSCALL) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                int fd = SSL_get_fd(ssl_);
                int remaining_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now()).count());
                if (remaining_ms <= 0) return -1;
                if (!wait_fd_ready(fd, true, remaining_ms)) {
                    return -1;
                }
                continue;
            }
        }
        return -1;
    }
}

int TLSChannel::read(uint8_t* out, int maxlen) {
    if (!ssl_ || !connected_) return -1;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(read_timeout_ms_);
    while (true) {
        int ret = SSL_read(ssl_, out, maxlen);
        if (ret > 0) {
            return ret;
        }
        int err = SSL_get_error(ssl_, ret);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            int fd = SSL_get_fd(ssl_);
            int remaining_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()).count());
            if (remaining_ms <= 0) return -1;
            if (!wait_fd_ready(fd, err == SSL_ERROR_WANT_READ, remaining_ms)) {
                return -1;
            }
            continue;
        }
        if (err == SSL_ERROR_SYSCALL) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                int fd = SSL_get_fd(ssl_);
                int remaining_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now()).count());
                if (remaining_ms <= 0) return -1;
                if (!wait_fd_ready(fd, true, remaining_ms)) {
                    return -1;
                }
                continue;
            }
        }
        return -1;
    }
}

void TLSChannel::request_key_update() {
    if (!ssl_ || !connected_) return;
    // TLS 1.3 KeyUpdate
    SSL_key_update(ssl_, SSL_KEY_UPDATE_REQUESTED);
    // Force sending KeyUpdate now.
    SSL_do_handshake(ssl_);
}

void TLSChannel::close() {
    if (ssl_) {
        SSL_shutdown(ssl_);
        SSL_free(ssl_);
        ssl_ = nullptr;
    }
    connected_ = false;
}

} // namespace hesia
