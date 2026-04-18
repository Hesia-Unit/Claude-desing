#include <oqs/oqs.h>
#include <openssl/evp.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static void die(const std::string& msg) {
    std::cerr << "[pqc-policy] " << msg << "\n";
    std::exit(1);
}

static std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        die("Cannot open file: " + path);
    }
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

static void write_file(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        die("Cannot write file: " + path);
    }
    f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

static std::string base64_encode(const std::vector<uint8_t>& data) {
    if (data.empty()) return "";
    const int in_len = static_cast<int>(data.size());
    const int out_len = 4 * ((in_len + 2) / 3);
    std::string out(static_cast<size_t>(out_len), '\0');
    int n = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(&out[0]),
                            data.data(),
                            in_len);
    if (n < 0) {
        die("Base64 encode failed");
    }
    out.resize(static_cast<size_t>(n));
    return out;
}

static void write_cpp_pubkey(const std::string& path, const std::vector<uint8_t>& pk) {
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        die("Cannot write file: " + path);
    }
    f << "#include \"policy_pqc_public_key.h\"\n\n";
    f << "namespace hesia {\n\n";
    f << "const unsigned char kPolicyPqcPublicKey[] = {";
    for (size_t i = 0; i < pk.size(); ++i) {
        if ((i % 12) == 0) {
            f << "\n  ";
        }
        f << "0x" << std::hex << std::setw(2) << std::setfill('0')
          << static_cast<int>(pk[i]) << std::dec;
        if (i + 1 != pk.size()) {
            f << ", ";
        }
    }
    if (!pk.empty()) {
        f << "\n";
    }
    f << "};\n";
    f << "const unsigned int kPolicyPqcPublicKeyLen = " << pk.size() << ";\n\n";
    f << "} // namespace hesia\n";
}

static void usage(const char* argv0) {
    std::cerr
        << "Usage:\n"
        << "  " << argv0 << " --policy <policy.conf> --out-dir <dir> [--regen] [--no-raw]\n"
        << "  " << argv0 << " --policy <policy.conf> --out-dir <dir> --emit-cpp <file.cpp>\n\n"
        << "Output files (in out-dir):\n"
        << "  policy_pqc_pk.bin     (public key, raw)\n"
        << "  policy_pqc_sk.bin     (secret key, raw)\n"
        << "  policy.sig.pqc.bin    (signature, raw)\n"
        << "  policy_pub.pqc        (public key, base64)\n"
        << "  policy.sig.pqc        (signature, base64)\n";
}

int main(int argc, char** argv) {
    std::string policy_path;
    std::string out_dir = ".";
    std::string emit_cpp_path;
    bool regen = false;
    bool keep_raw = true;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--policy" && i + 1 < argc) {
            policy_path = argv[++i];
        } else if (a == "--out-dir" && i + 1 < argc) {
            out_dir = argv[++i];
        } else if (a == "--emit-cpp" && i + 1 < argc) {
            emit_cpp_path = argv[++i];
        } else if (a == "--regen") {
            regen = true;
        } else if (a == "--no-raw") {
            keep_raw = false;
        } else if (a == "--help" || a == "-h") {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    if (policy_path.empty()) {
        usage(argv[0]);
        return 1;
    }

    fs::create_directories(out_dir);
    const std::string pk_path = (fs::path(out_dir) / "policy_pqc_pk.bin").string();
    const std::string sk_path = (fs::path(out_dir) / "policy_pqc_sk.bin").string();
    const std::string sig_path = (fs::path(out_dir) / "policy.sig.pqc.bin").string();
    const std::string pk_b64_path = (fs::path(out_dir) / "policy_pub.pqc").string();
    const std::string sig_b64_path = (fs::path(out_dir) / "policy.sig.pqc").string();

    const char* alg = "ML-DSA-87";
    OQS_SIG* sig = OQS_SIG_new(alg);
    if (!sig) {
        die("OQS_SIG_new failed (ML-DSA-87)");
    }

    const bool need_gen = regen || !fs::exists(pk_path) || !fs::exists(sk_path);
    if (need_gen) {
        std::vector<uint8_t> pk(sig->length_public_key);
        std::vector<uint8_t> sk(sig->length_secret_key);
        if (OQS_SIG_keypair(sig, pk.data(), sk.data()) != OQS_SUCCESS) {
            OQS_SIG_free(sig);
            die("OQS_SIG_keypair failed");
        }
        write_file(pk_path, pk);
        write_file(sk_path, sk);
        std::cout << "[pqc-policy] Generated ML-DSA-87 keypair\n";
    }

    std::vector<uint8_t> policy = read_file(policy_path);
    std::vector<uint8_t> sk = read_file(sk_path);
    if (sk.size() != sig->length_secret_key) {
        OQS_SIG_free(sig);
        die("Secret key size mismatch");
    }

    std::vector<uint8_t> sig_bytes(sig->length_signature);
    size_t sig_len = sig_bytes.size();
    if (OQS_SIG_sign(sig, sig_bytes.data(), &sig_len,
                     policy.data(), policy.size(), sk.data()) != OQS_SUCCESS) {
        OQS_SIG_free(sig);
        die("OQS_SIG_sign failed");
    }
    sig_bytes.resize(sig_len);
    write_file(sig_path, sig_bytes);

    std::vector<uint8_t> pk = read_file(pk_path);
    const std::string pk_b64 = base64_encode(pk);
    const std::string sig_b64 = base64_encode(sig_bytes);
    write_file(pk_b64_path, std::vector<uint8_t>(pk_b64.begin(), pk_b64.end()));
    write_file(sig_b64_path, std::vector<uint8_t>(sig_b64.begin(), sig_b64.end()));

    if (!emit_cpp_path.empty()) {
        write_cpp_pubkey(emit_cpp_path, pk);
    }

    if (!keep_raw) {
        fs::remove(sig_path);
    }

    OQS_SIG_free(sig);
    std::cout << "[pqc-policy] OK\n";
    std::cout << "  pub (b64): " << pk_b64_path << "\n";
    std::cout << "  sig (b64): " << sig_b64_path << "\n";
    return 0;
}
