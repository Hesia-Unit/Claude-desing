#ifndef STATE_DRONE_HPP
#define STATE_DRONE_HPP

#include <string>

namespace hesia {

// Etats du drone pendant l'initialisation et la session sécurisée.
// NOTE: Garder un enchaînement simple et explicite pour limiter la surface d'attaque.

enum class DroneState {
    IDLE = 0,
    HELLO_SENT,
    KEY_EXCHANGE,
    KEY_CONFIRMED,
    DRONE_AUTH_SENT,
    SERVER_AUTH_VERIFIED,
    SECURE_SESSION,
    ERROR
};

inline std::string drone_state_to_string(DroneState s) {
    switch (s) {
        case DroneState::IDLE: return "IDLE";
        case DroneState::HELLO_SENT: return "HELLO_SENT";
        case DroneState::KEY_EXCHANGE: return "KEY_EXCHANGE";
        case DroneState::KEY_CONFIRMED: return "KEY_CONFIRMED";
        case DroneState::DRONE_AUTH_SENT: return "DRONE_AUTH_SENT";
        case DroneState::SERVER_AUTH_VERIFIED: return "SERVER_AUTH_VERIFIED";
        case DroneState::SECURE_SESSION: return "SECURE_SESSION";
        case DroneState::ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

} // namespace hesia

#endif // STATE_DRONE_HPP
