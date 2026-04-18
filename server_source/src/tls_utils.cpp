#include "tls_utils.hpp"

#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/evp.h>
#include <stdexcept>
#include <sstream>

namespace hesia {

std::string openssl_error_stack() {
    std::ostringstream oss;
    unsigned long e = 0;
    bool first = true;
    while ((e = ERR_get_error()) != 0) {
        char buf[256];
        ERR_error_string_n(e, buf, sizeof(buf));
        if (!first) oss << " | ";
        first = false;
        oss << buf;
    }
    return oss.str();
}

std::vector<uint8_t> tls_exporter_32(SSL* ssl) {
    const std::string label = "HESIA-EXPORTER-HYBRID-V1";
    std::vector<uint8_t> out(32);
    if (SSL_export_keying_material(
            ssl,
            out.data(),
            out.size(),
            label.c_str(),
            static_cast<int>(label.size()),
            nullptr, 0, 0) != 1) {
        throw std::runtime_error("SSL_export_keying_material failed: " + openssl_error_stack());
    }
    return out;
}

std::vector<uint8_t> tls_server_cert_sha256_der(SSL* ssl) {
    X509* cert = SSL_get_certificate(ssl);
    if (!cert) {
        throw std::runtime_error("SSL_get_certificate returned null");
    }
    unsigned int n = 0;
    unsigned char md[EVP_MAX_MD_SIZE];
    if (X509_digest(cert, EVP_sha256(), md, &n) != 1 || n == 0) {
        throw std::runtime_error("X509_digest failed: " + openssl_error_stack());
    }
    return std::vector<uint8_t>(md, md + n);
}

} // namespace hesia
