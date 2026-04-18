#ifndef SECURE_CHANNEL_HPP
#define SECURE_CHANNEL_HPP

#include <vector>
#include <cstdint>
#include <memory>
#include <atomic>
#include <array>
#include <limits>
#include "exceptions.hpp"
#include "logger.hpp"
#include "config.hpp"
#include "security_utils.hpp"

namespace hesia {

struct EncryptedMessage {
    std::vector<uint8_t> iv;
    std::vector<uint8_t> ciphertext;
    std::vector<uint8_t> tag;
    std::vector<uint8_t> raw;
    
    EncryptedMessage() {}
    EncryptedMessage(const std::vector<uint8_t>& iv_data,
                     const std::vector<uint8_t>& ct,
                     const std::vector<uint8_t>& tag_data)
        : iv(iv_data), ciphertext(ct), tag(tag_data) {
        raw.reserve(iv.size() + tag.size() + ciphertext.size());
        raw.insert(raw.end(), iv.begin(), iv.end());
        raw.insert(raw.end(), tag.begin(), tag.end());
        raw.insert(raw.end(), ciphertext.begin(), ciphertext.end());
    }
    
    // Destructeur sécurisé
    ~EncryptedMessage() {
        SecureMemory::zeroize(iv);
        SecureMemory::zeroize(ciphertext);
        SecureMemory::zeroize(tag);
        SecureMemory::zeroize(raw);
    }
};

class SecureChannel {
private:
    std::vector<uint8_t> key;
    std::array<uint8_t, 4> iv_prefix; // Préfixe IV par instance (anti-réutilisation de nonce)
    std::atomic<uint64_t> send_counter;
    std::atomic<int64_t> recv_counter;
    std::shared_ptr<Logger> logger;
    
    // Protection contre les attaques
    std::vector<uint8_t> key_backup; // Pour détection de fautes
    bool fault_detection_enabled;
    
    std::vector<uint8_t> build_iv(uint64_t counter);
    bool verify_key_integrity();
    void secure_cleanup();
    
public:
    SecureChannel(const std::vector<uint8_t>& session_key);
    ~SecureChannel();
    
    EncryptedMessage encrypt(const std::vector<uint8_t>& plaintext, const std::vector<uint8_t>& aad);
    std::vector<uint8_t> decrypt(const std::vector<uint8_t>& iv,
                                 const std::vector<uint8_t>& ciphertext,
                                 const std::vector<uint8_t>& tag,
                                 const std::vector<uint8_t>& aad);
    
    // Méthodes de sécurité
    void enable_fault_detection(bool enable = true);
    bool is_fault_detection_enabled() const;
    void rotate_key(const std::vector<uint8_t>& new_key);
};

} // namespace hesia

#endif // SECURE_CHANNEL_HPP

