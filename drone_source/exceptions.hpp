#ifndef EXCEPTIONS_HPP
#define EXCEPTIONS_HPP

#include <stdexcept>
#include <string>

namespace hesia {

class HesiaException : public std::exception {
protected:
    std::string message;
public:
    HesiaException(const std::string& msg) : message(msg) {}
    const char* what() const noexcept override { return message.c_str(); }
};

// Erreurs d'état / FSM
class InvalidStateError : public HesiaException {
public:
    InvalidStateError(const std::string& current_state, const std::string& expected_state)
        : HesiaException("État invalide : " + current_state + " (attendu : " + expected_state + ")") {}
};

// Erreurs protocole
class ProtocolViolation : public HesiaException {
public:
    ProtocolViolation(const std::string& msg = "Structure de bloc invalide ou non conforme")
        : HesiaException(msg) {}
};

class ReplayDetected : public HesiaException {
public:
    ReplayDetected(const std::string& msg = "Bloc déjà vu ou hash précédent invalide")
        : HesiaException(msg) {}
};

class UnsupportedVersion : public HesiaException {
public:
    UnsupportedVersion(const std::string& msg = "Version de protocole non supportée")
        : HesiaException(msg) {}
};

// Erreurs crypto
class CryptoError : public HesiaException {
public:
    CryptoError(const std::string& msg = "Erreur cryptographique générique")
        : HesiaException(msg) {}
};

class CryptoInitError : public CryptoError {
public:
    CryptoInitError(const std::string& msg = "Initialisation cryptographique impossible")
        : CryptoError(msg) {}
};

class InvalidSignature : public CryptoError {
public:
    InvalidSignature(const std::string& msg = "Signature invalide (Dilithium)")
        : CryptoError(msg) {}
};

class KeyExchangeFailed : public CryptoError {
public:
    KeyExchangeFailed(const std::string& msg = "Échec lors de l'établissement du secret (Kyber)")
        : CryptoError(msg) {}
};

class DecryptionFailed : public CryptoError {
public:
    DecryptionFailed(const std::string& msg = "Échec de déchiffrement AES-GCM")
        : CryptoError(msg) {}
};

// Erreurs identité / sécurité
class AuthenticationFailed : public HesiaException {
public:
    AuthenticationFailed(const std::string& msg = "Authentification échouée (drone ou serveur)")
        : HesiaException(msg) {}
};

class SecurityViolation : public HesiaException {
public:
    SecurityViolation(const std::string& msg = "Violation de sécurité détectée")
        : HesiaException(msg) {}
};

class PUFMismatch : public AuthenticationFailed {
public:
    PUFMismatch(const std::string& msg = "Réponse PUF ne correspondant pas à la base de données")
        : AuthenticationFailed(msg) {}
};

class FirmwareMismatch : public AuthenticationFailed {
public:
    FirmwareMismatch(const std::string& msg = "Hash firmware non conforme")
        : AuthenticationFailed(msg) {}
};

// Erreurs session
class SessionExpired : public HesiaException {
public:
    SessionExpired(const std::string& msg = "Session expirée ou invalide")
        : HesiaException(msg) {}
};

class SessionNotEstablished : public HesiaException {
public:
    SessionNotEstablished(const std::string& msg = "Tentative d'utilisation d'une session non établie")
        : HesiaException(msg) {}
};

// Erreurs canal sécurisé
class SecureChannelError : public HesiaException {
public:
    SecureChannelError(const std::string& msg) : HesiaException(msg) {}
};

class InvalidTagError : public SecureChannelError {
public:
    InvalidTagError(const std::string& msg = "Échec authentification GCM")
        : SecureChannelError(msg) {}
};

// Erreurs canal vidéo
class VideoChannelError : public HesiaException {
public:
    VideoChannelError(const std::string& msg) : HesiaException(msg) {}
};

} // namespace hesia

#endif // EXCEPTIONS_HPP
