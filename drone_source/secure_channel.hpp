#ifndef SECURE_CHANNEL_HPP
#define SECURE_CHANNEL_HPP

#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <vector>

#include "config.hpp"
#include "exceptions.hpp"
#include "logger.hpp"
#include "security_utils.hpp"

namespace hesia {

enum class SecureChannelRole {
    DroneClient = 0,
    ServerResponder = 1,
};

struct EncryptedMessage {
    std::vector<uint8_t> iv;
    std::vector<uint8_t> ciphertext;
    std::vector<uint8_t> tag;
    std::vector<uint8_t> raw;

    EncryptedMessage() = default;
    EncryptedMessage(const std::vector<uint8_t>& iv_data,
                     const std::vector<uint8_t>& ct,
                     const std::vector<uint8_t>& tag_data)
        : iv(iv_data), ciphertext(ct), tag(tag_data) {
        raw.reserve(iv.size() + tag.size() + ciphertext.size());
        raw.insert(raw.end(), iv.begin(), iv.end());
        raw.insert(raw.end(), tag.begin(), tag.end());
        raw.insert(raw.end(), ciphertext.begin(), ciphertext.end());
    }

    ~EncryptedMessage() {
        SecureMemory::zeroize(iv);
        SecureMemory::zeroize(ciphertext);
        SecureMemory::zeroize(tag);
        SecureMemory::zeroize(raw);
    }
};

class SecureChannel {
private:
    std::vector<uint8_t> session_secret_;
    std::vector<uint8_t> tx_key_;
    std::vector<uint8_t> rx_key_;
    std::array<uint8_t, 8> tx_iv_prefix_{};
    std::array<uint8_t, 8> rx_iv_prefix_{};
    SecureChannelRole role_;
    std::atomic<uint64_t> send_counter;
    std::atomic<int64_t> recv_counter;
    std::shared_ptr<Logger> logger;
    mutable std::mutex state_mutex;

    std::vector<uint8_t> tx_key_backup_;
    std::vector<uint8_t> rx_key_backup_;
    std::vector<uint8_t> key_integrity_hmac_key;
    std::array<uint8_t, 32> tx_key_integrity_canary_{};
    std::array<uint8_t, 32> rx_key_integrity_canary_{};
    std::atomic<uint32_t> key_epoch;
    bool fault_detection_enabled;
    bool replay_window_initialized;
    uint64_t replay_window_top;
    uint64_t replay_window_bitmap;

    std::vector<uint8_t> build_iv(const std::array<uint8_t, 8>& prefix, uint32_t counter) const;
    std::vector<uint8_t> build_effective_aad(const std::vector<uint8_t>& aad) const;
    bool verify_key_integrity();
    void refresh_key_integrity_canary();
    void derive_directional_material_or_throw(const std::vector<uint8_t>& session_key);
    bool can_attempt_decrypt_for_counter(uint64_t counter) const;
    uint64_t parse_rx_counter_or_throw(const std::vector<uint8_t>& iv) const;
    bool accept_replay_counter(uint64_t counter);
    void reset_replay_window();
    void secure_cleanup();

public:
    SecureChannel(const std::vector<uint8_t>& session_key, SecureChannelRole role);
    ~SecureChannel();

    EncryptedMessage encrypt(const std::vector<uint8_t>& plaintext, const std::vector<uint8_t>& aad);
    std::vector<uint8_t> decrypt(const std::vector<uint8_t>& iv,
                                 const std::vector<uint8_t>& ciphertext,
                                 const std::vector<uint8_t>& tag,
                                 const std::vector<uint8_t>& aad);

    void enable_fault_detection(bool enable = true);
    bool is_fault_detection_enabled() const;
    void rotate_key(const std::vector<uint8_t>& new_key);
};

} // namespace hesia

#endif // SECURE_CHANNEL_HPP
