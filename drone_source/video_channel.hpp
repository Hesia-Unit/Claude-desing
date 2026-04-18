#ifndef VIDEO_CHANNEL_HPP
#define VIDEO_CHANNEL_HPP

#include <vector>
#include <cstdint>
#include <string>
#include <atomic>
#include <limits>
#include "exceptions.hpp"
#include "logger.hpp"
#include "config.hpp"
#include "security_utils.hpp"

namespace hesia {

struct VideoPacket {
    uint32_t stream_id;
    uint64_t frame_id;
    std::vector<uint8_t> iv;
    std::vector<uint8_t> payload;
    
    VideoPacket() : stream_id(0), frame_id(0) {}
    VideoPacket(uint32_t sid, uint64_t fid, const std::vector<uint8_t>& iv_data, const std::vector<uint8_t>& pld)
        : stream_id(sid), frame_id(fid), iv(iv_data), payload(pld) {}
    
    std::vector<uint8_t> serialize() const;
    static VideoPacket deserialize(const std::vector<uint8_t>& data);
    
    // Destructeur sécurisé
    ~VideoPacket() {
        SecureMemory::zeroize(iv);
        SecureMemory::zeroize(payload);
    }
};

class VideoChannel {
private:
    std::vector<uint8_t> video_key;
    uint32_t stream_id;
    std::vector<uint8_t> iv_salt;
    std::atomic<uint64_t> tx_counter;
    std::atomic<int64_t> rx_last_frame;
    std::shared_ptr<Logger> logger;
    
    // Protection contre les fautes
    std::vector<uint8_t> key_backup;
    bool fault_detection_enabled;
    
    std::vector<uint8_t> make_iv(uint64_t frame_id);
    bool verify_key_integrity();
    void secure_cleanup();
    
public:
    VideoChannel(const std::vector<uint8_t>& vkey, uint32_t sid = 1);
    ~VideoChannel();
    
    VideoPacket encrypt_frame(const std::vector<uint8_t>& payload);
    std::vector<uint8_t> decrypt_frame(const VideoPacket& packet);
    
    // Méthodes de sécurité
    void enable_fault_detection(bool enable = true);
    bool is_fault_detection_enabled() const;
    void rotate_key(const std::vector<uint8_t>& new_key);
};

} // namespace hesia

#endif // VIDEO_CHANNEL_HPP

