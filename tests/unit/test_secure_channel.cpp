#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "../../drone_source/exceptions.hpp"
#include "../../drone_source/secure_channel.hpp"

using hesia::EncryptedMessage;
using hesia::ReplayDetected;
using hesia::SecureChannel;
using hesia::SecureChannelRole;

namespace {

std::vector<uint8_t> seq_bytes(std::size_t size, uint8_t base) {
    std::vector<uint8_t> out(size);
    for (std::size_t i = 0; i < size; ++i) {
        out[i] = static_cast<uint8_t>(base + (i % 251));
    }
    return out;
}

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Fn>
void require_throw(Fn&& fn, const char* message) {
    bool threw = false;
    try {
        fn();
    } catch (...) {
        threw = true;
    }
    require(threw, message);
}

void test_round_trip_and_replay() {
    const std::vector<uint8_t> session_key = seq_bytes(32, 0x10);
    const std::vector<uint8_t> aad{'H', 'E', 'S', 'I', 'A'};
    const std::vector<uint8_t> payload = seq_bytes(96, 0x40);

    SecureChannel drone(session_key, SecureChannelRole::DroneClient);
    SecureChannel server(session_key, SecureChannelRole::ServerResponder);

    EncryptedMessage msg = drone.encrypt(payload, aad);
    const std::vector<uint8_t> plain = server.decrypt(msg.iv, msg.ciphertext, msg.tag, aad);
    require(plain == payload, "round-trip decryption failed");

    bool replay_rejected = false;
    try {
        (void)server.decrypt(msg.iv, msg.ciphertext, msg.tag, aad);
    } catch (const ReplayDetected&) {
        replay_rejected = true;
    }
    require(replay_rejected, "replay should be rejected");
}

void test_out_of_order_within_window() {
    const std::vector<uint8_t> session_key = seq_bytes(32, 0x21);
    const std::vector<uint8_t> aad{'o', 'o', 'o'};
    SecureChannel drone(session_key, SecureChannelRole::DroneClient);
    SecureChannel server(session_key, SecureChannelRole::ServerResponder);

    EncryptedMessage msg1 = drone.encrypt(seq_bytes(32, 0x01), aad);
    EncryptedMessage msg2 = drone.encrypt(seq_bytes(32, 0x11), aad);

    const std::vector<uint8_t> plain2 = server.decrypt(msg2.iv, msg2.ciphertext, msg2.tag, aad);
    const std::vector<uint8_t> plain1 = server.decrypt(msg1.iv, msg1.ciphertext, msg1.tag, aad);

    require(plain2 == seq_bytes(32, 0x11), "out-of-order message 2 failed");
    require(plain1 == seq_bytes(32, 0x01), "out-of-order message 1 failed");
}

void test_message_older_than_window_is_rejected() {
    const std::vector<uint8_t> session_key = seq_bytes(32, 0x32);
    const std::vector<uint8_t> aad{'w', 'i', 'n'};
    SecureChannel drone(session_key, SecureChannelRole::DroneClient);
    SecureChannel server(session_key, SecureChannelRole::ServerResponder);

    std::vector<EncryptedMessage> messages;
    messages.reserve(70);
    for (int i = 0; i < 70; ++i) {
        messages.push_back(drone.encrypt(seq_bytes(24, static_cast<uint8_t>(i)), aad));
    }

    for (std::size_t i = 1; i < messages.size(); ++i) {
        const auto& msg = messages[i];
        (void)server.decrypt(msg.iv, msg.ciphertext, msg.tag, aad);
    }

    bool rejected = false;
    try {
        const auto& stale = messages.front();
        (void)server.decrypt(stale.iv, stale.ciphertext, stale.tag, aad);
    } catch (const ReplayDetected&) {
        rejected = true;
    }
    require(rejected, "stale message outside replay window should be rejected");
}

void test_directional_key_rotation() {
    const std::vector<uint8_t> session_key = seq_bytes(32, 0x44);
    const std::vector<uint8_t> next_key = seq_bytes(32, 0x91);
    const std::vector<uint8_t> aad{'r', 'o', 't'};

    SecureChannel drone(session_key, SecureChannelRole::DroneClient);
    SecureChannel server(session_key, SecureChannelRole::ServerResponder);

    EncryptedMessage pre_rotation = drone.encrypt(seq_bytes(48, 0x55), aad);

    drone.rotate_key(next_key);
    server.rotate_key(next_key);

    require_throw(
        [&]() {
            (void)server.decrypt(pre_rotation.iv, pre_rotation.ciphertext, pre_rotation.tag, aad);
        },
        "pre-rotation ciphertext should not decrypt after rotation");

    EncryptedMessage post_rotation = drone.encrypt(seq_bytes(48, 0x77), aad);
    const std::vector<uint8_t> plain =
        server.decrypt(post_rotation.iv, post_rotation.ciphertext, post_rotation.tag, aad);
    require(plain == seq_bytes(48, 0x77), "post-rotation ciphertext failed to decrypt");
}

} // namespace

int main() {
    try {
        test_round_trip_and_replay();
        test_out_of_order_within_window();
        test_message_older_than_window_is_rejected();
        test_directional_key_rotation();
    } catch (const std::exception& e) {
        std::cerr << "SecureChannel unit test failure: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "hesia_secure_channel_tests: OK" << std::endl;
    return 0;
}
