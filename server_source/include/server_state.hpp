#ifndef HESIA_SERVER_STATE_HPP
#define HESIA_SERVER_STATE_HPP

#include <string>

namespace hesia {

enum class ServerState {
    IDLE = 0,
    HELLO_RECEIVED,
    KEY_INIT_SENT,
    KEY_EXCHANGE_DONE,
    DRONE_AUTH_VERIFIED,
    SERVER_AUTH_SENT,
    SECURE_SESSION
};

inline const char* server_state_to_string(ServerState st) {
    switch (st) {
        case ServerState::IDLE: return "IDLE";
        case ServerState::HELLO_RECEIVED: return "HELLO_RECEIVED";
        case ServerState::KEY_INIT_SENT: return "KEY_INIT_SENT";
        case ServerState::KEY_EXCHANGE_DONE: return "KEY_EXCHANGE_DONE";
        case ServerState::DRONE_AUTH_VERIFIED: return "DRONE_AUTH_VERIFIED";
        case ServerState::SERVER_AUTH_SENT: return "SERVER_AUTH_SENT";
        case ServerState::SECURE_SESSION: return "SECURE_SESSION";
        default: return "UNKNOWN";
    }
}

} // namespace hesia

#endif
