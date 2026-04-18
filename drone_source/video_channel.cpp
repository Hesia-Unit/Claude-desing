#include "video_channel.hpp"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <array>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <atomic>
#include "config.hpp"
#include "security_utils.hpp"

#ifdef HESIA_USE_FIPS_MODULE
#include "fips_module.hpp"
#endif

namespace hesia {

static int sidechannel_every_n() {
    return 1;
}

static bool should_run_sidechannel() {
    const int every = sidechannel_every_n();
    if (every <= 0) return false;
    if (every == 1) return true;
    static std::atomic<uint64_t> counter{0};
    const uint64_t n = counter.fetch_add(1, std::memory_order_relaxed);
    return (n % static_cast<uint64_t>(every)) == 0;
}

std::vector<uint8_t> VideoPacket::serialize() const {
    std::vector<uint8_t> result;
    result.reserve(12 + iv.size() + payload.size());
    
    // ✅ SÉCURITÉ: Big-endian encoding sécurisé avec cast explicite pour éviter UB
    result.push_back(static_cast<uint8_t>((stream_id >> 24) & 0xFF));
    result.push_back(static_cast<uint8_t>((stream_id >> 16) & 0xFF));
    result.push_back(static_cast<uint8_t>((stream_id >> 8) & 0xFF));
    result.push_back(static_cast<uint8_t>(stream_id & 0xFF));
    
    // Debug log pour vérifier la sérialisation
    #ifdef DEBUG
    printf("SERIALIZE: stream_id=%u (0x%02X%02X%02X%02X)\n", 
           stream_id, 
           result[0], result[1], result[2], result[3]);
    #endif
    
    // ✅ SÉCURITÉ: Big-endian encoding sécurisé avec cast explicite
    for (int i = 7; i >= 0; i--) {
        result.push_back(static_cast<uint8_t>((frame_id >> (i * 8)) & 0xFF));
    }
    
    result.insert(result.end(), iv.begin(), iv.end());
    result.insert(result.end(), payload.begin(), payload.end());
    
    return result;
}

VideoPacket VideoPacket::deserialize(const std::vector<uint8_t>& data) {
    if (data.size() < 24) {
        throw std::runtime_error("Paquet trop court");
    }
    
    // ✅ SÉCURITÉ: Extraction sécurisée avec cast explicite pour éviter UB
    uint32_t sid = (static_cast<uint32_t>(data[0]) << 24) | 
                    (static_cast<uint32_t>(data[1]) << 16) | 
                    (static_cast<uint32_t>(data[2]) << 8) | 
                    static_cast<uint32_t>(data[3]);
    // ✅ SÉCURITÉ: Extraction sécurisée frame_id avec cast explicite
    uint64_t fid = 0;
    for (int i = 0; i < 8; i++) {
        fid = (fid << 8) | static_cast<uint64_t>(data[4 + i]);
    }
    
    std::vector<uint8_t> iv(data.begin() + 12, data.begin() + 24);
    std::vector<uint8_t> payload(data.begin() + 24, data.end());
    
    return VideoPacket(sid, fid, iv, payload);
}

std::vector<uint8_t> VideoChannel::make_iv(uint64_t frame_id) {
    // AES-GCM requiert l'unicité (clé, IV). Un schéma robuste consiste à utiliser
    // un préfixe fixe par session + un compteur monotone.
    // Ici : 4 octets "fixed" + 8 octets compteur (frame_id) en big-endian.
    // Cela permet 2^64 trames uniques par clé.

    if (iv_salt.size() < 4) {
        throw std::runtime_error("iv_salt insuffisant pour dériver un préfixe IV");
    }

    std::vector<uint8_t> iv(12, 0);
    // Préfixe fixe 32-bit (par session)
    std::copy_n(iv_salt.data(), 4, iv.data());

    // Compteur 64-bit big-endian
    uint64_t counter = frame_id;
    for (int i = 0; i < 8; i++) {
        iv[11 - i] = static_cast<uint8_t>((counter >> (i * 8)) & 0xFF);
    }

    return iv;
}

VideoChannel::VideoChannel(const std::vector<uint8_t>& key, uint32_t stream_id) 
    : video_key(key), stream_id(stream_id), iv_salt(), tx_counter(0), rx_last_frame(-1),
      key_backup(), fault_detection_enabled(false) {
    
    // ✅ SÉCURITÉ: Validation stricte de la clé
    if (key.size() != 32) {
        throw std::invalid_argument("Clé vidéo doit faire exactement 32 octets pour AES-256");
    }
    
    // ✅ SÉCURITÉ: Validation du stream_id
    if (stream_id == 0 || stream_id > 0xFFFFFFFF) {
        throw std::invalid_argument("Stream ID invalide: doit être entre 1 et 2^32-1");
    }
    
    // ✅ SÉCURITÉ: Génération du sel avec validation
    iv_salt = SecureMemory::secure_alloc(32); // Augmenté à 32 bytes pour plus d'entropie
    if (iv_salt.empty()) {
        throw std::runtime_error("Échec génération sel aléatoire");
    }
    
    // ✅ SÉCURITÉ: Initialisation du backup avec zeroization
    key_backup = SecureMemory::secure_alloc(32);
    if (key_backup.empty()) {
        throw std::runtime_error("Échec allocation backup clé");
    }
    SecureMemory::copy(key_backup.data(), key.data(), 32);
    
    logger = setup_logger("HESIA-VIDEO-CHANNEL", Config::LOG_DIR);
    logger->info("VideoChannel initialisé (stream_id=" + std::to_string(stream_id) + 
                ", sel=32 bytes, mode=AES-256-GCM)");
}

VideoPacket VideoChannel::encrypt_frame(const std::vector<uint8_t>& payload) {
    // ✅ SÉCURITÉ: Validation des entrées
    if (payload.empty()) {
        throw std::invalid_argument("Payload vide");
    }
    
    if (payload.size() > 10 * 1024 * 1024) { // Max 10MB
        throw std::invalid_argument("Payload trop grand: max 10MB");
    }
    
    // ✅ SÉCURITÉ: Vérification d'intégrité de la clé
    if (!verify_key_integrity()) {
        throw std::runtime_error("Intégrité clé compromise");
    }
    
    uint64_t fid = tx_counter.load(std::memory_order_relaxed);
    while (true) {
        if (fid == std::numeric_limits<uint64_t>::max()) {
            throw VideoChannelError("Compteur frames épuisé (risque de réutilisation de nonce)");
        }
        if (tx_counter.compare_exchange_weak(fid, fid + 1, std::memory_order_relaxed, std::memory_order_relaxed)) {
            break;
        }
    }
    std::vector<uint8_t> iv = make_iv(fid);
    
    // ✅ SÉCURITÉ: Construction AAD sécurisée
    std::vector<uint8_t> aad(12);
    aad[0] = static_cast<uint8_t>((stream_id >> 24) & 0xFF);
    aad[1] = static_cast<uint8_t>((stream_id >> 16) & 0xFF);
    aad[2] = static_cast<uint8_t>((stream_id >> 8) & 0xFF);
    aad[3] = static_cast<uint8_t>(stream_id & 0xFF);
    for (int i = 0; i < 8; i++) {
        aad[11 - i] = static_cast<uint8_t>((fid >> (i * 8)) & 0xFF);
    }
    
    std::vector<uint8_t> ciphertext;
    std::vector<uint8_t> tag;

#ifdef HESIA_USE_FIPS_MODULE
    auto& fm = hesia::fips::FipsModule::Instance();
    auto res = fm.Aes256GcmEncrypt(video_key, iv, aad, payload);
    ciphertext = std::move(res.ciphertext);
    tag = std::move(res.tag);
    if (tag.size() != 16 || ciphertext.size() != payload.size()) {
        throw VideoChannelError("Échec déchiffrement");
    }
#else
    // ✅ PROTECTION: Activer cache timing et power masking avant chiffrement vidéo
    if (should_run_sidechannel()) {
        CacheProtection::enable_cache_timing_protection();
        PowerMasking::enable_power_masking();
    }
    
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        throw std::runtime_error("EVP_CIPHER_CTX_new failed");
    }
    
    // Utiliser AES-GCM pour la protection contre le padding oracle
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, video_key.data(), iv.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_EncryptInit_ex failed");
    }
    
    int len;
    ciphertext.resize(payload.size());
    
    if (EVP_EncryptUpdate(ctx, nullptr, &len, aad.data(), aad.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_EncryptUpdate AAD failed");
    }
    
    if (EVP_EncryptUpdate(ctx, ciphertext.data(), &len, payload.data(), payload.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_EncryptUpdate failed");
    }
    
    tag.resize(16);
    int final_len = 0;
    std::array<uint8_t, 16> final_buf{};
    if (EVP_EncryptFinal_ex(ctx, final_buf.data(), &final_len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_EncryptFinal_ex failed");
    }
    
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_CIPHER_CTX_ctrl failed");
    }
    
    EVP_CIPHER_CTX_free(ctx);
#endif
    
    // Concaténer ciphertext + tag
    std::vector<uint8_t> full_payload;
    full_payload.reserve(ciphertext.size() + tag.size());
    full_payload.insert(full_payload.end(), ciphertext.begin(), ciphertext.end());
    full_payload.insert(full_payload.end(), tag.begin(), tag.end());
    
    // Debug log pour vérifier le stream_id
    #ifdef DEBUG
    logger->debug("Création VideoPacket - stream_id=" + std::to_string(stream_id) + 
                 ", frame_id=" + std::to_string(fid) + 
                 ", payload_size=" + std::to_string(full_payload.size()));
    #endif
    
    return VideoPacket(stream_id, fid, iv, full_payload);
}

std::vector<uint8_t> VideoChannel::decrypt_frame(const VideoPacket& packet) {
    if (packet.stream_id != stream_id) {
        throw VideoChannelError("Mauvais stream_id: " + std::to_string(packet.stream_id));
    }
    
    // Protection contre le replay avec fenêtre temporelle stricte
    int64_t current_last = rx_last_frame.load(std::memory_order_acquire);
    
    // Protection contre les attaques par rejeu de fenêtre large
    // Rejeter tout frame_id en dehors de la fenêtre autorisée (±1000 frames)
    int64_t max_acceptable = current_last + 1000;
    
    if (static_cast<int64_t>(packet.frame_id) <= current_last) {
        throw VideoChannelError("Replay détecté (frame_id=" + std::to_string(packet.frame_id) + ")");
    }
    
    if (static_cast<int64_t>(packet.frame_id) > max_acceptable) {
        throw VideoChannelError("Frame hors fenêtre temporelle (frame_id=" + std::to_string(packet.frame_id) + ")");
    }
    
    std::vector<uint8_t> aad(12);
    aad[0] = (packet.stream_id >> 24) & 0xFF;
    aad[1] = (packet.stream_id >> 16) & 0xFF;
    aad[2] = (packet.stream_id >> 8) & 0xFF;
    aad[3] = packet.stream_id & 0xFF;
    for (int i = 0; i < 8; i++) {
        aad[11 - i] = (packet.frame_id >> (i * 8)) & 0xFF;
    }
    
    if (packet.iv.size() != 12) {
        throw VideoChannelError("IV invalide");
    }

    if (packet.payload.size() < 16) {
        throw VideoChannelError("Payload trop court");
    }
    
    std::vector<uint8_t> ciphertext(packet.payload.begin(), packet.payload.end() - 16);
    std::vector<uint8_t> tag(packet.payload.end() - 16, packet.payload.end());
    
    std::vector<uint8_t> plaintext;

#ifdef HESIA_USE_FIPS_MODULE
    auto& fm = hesia::fips::FipsModule::Instance();
    plaintext = fm.Aes256GcmDecrypt(video_key, packet.iv, aad, ciphertext, tag);
    if (plaintext.empty() && !ciphertext.empty()) {
        throw VideoChannelError("Échec déchiffrement");
    }
#else
    // PROTECTION: Activer cache timing et power masking avant déchiffrement vidéo
    if (should_run_sidechannel()) {
        CacheProtection::detect_cache_timing();
        PowerMasking::detect_power_analysis();
    }
    
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        throw std::runtime_error("EVP_CIPHER_CTX_new failed");
    }
    
    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, video_key.data(), packet.iv.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_DecryptInit_ex failed");
    }
    
    int len;
    plaintext.resize(ciphertext.size());
    
    if (EVP_DecryptUpdate(ctx, nullptr, &len, aad.data(), aad.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw VideoChannelError("EVP_DecryptUpdate AAD failed");
    }
    
    if (EVP_DecryptUpdate(ctx, plaintext.data(), &len, ciphertext.data(), ciphertext.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw VideoChannelError("Échec déchiffrement");
    }
    
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, tag.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw VideoChannelError("Échec déchiffrement");
    }
    
    int final_len = 0;
    std::array<uint8_t, 16> final_buf{};
    if (EVP_DecryptFinal_ex(ctx, final_buf.data(), &final_len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw VideoChannelError("Échec déchiffrement");
    }
    
    EVP_CIPHER_CTX_free(ctx);
#endif
    
    // Mise à jour monotone du compteur (ne jamais diminuer en cas de concurrence)
    const int64_t new_frame = static_cast<int64_t>(packet.frame_id);
    int64_t observed = rx_last_frame.load(std::memory_order_relaxed);
    while (observed < new_frame &&
           !rx_last_frame.compare_exchange_weak(observed, new_frame,
                                               std::memory_order_release,
                                               std::memory_order_relaxed)) {
        // observed mis à jour automatiquement
    }

    return plaintext;
}

bool VideoChannel::verify_key_integrity() {
    if (!fault_detection_enabled) return true;
    
    // Vérification simple : comparer avec la sauvegarde
    return ConstantTime::equals(video_key, key_backup);
}

void VideoChannel::secure_cleanup() {
    SecureMemory::zeroize(video_key);
    SecureMemory::zeroize(key_backup);
    SecureMemory::zeroize(iv_salt);
    tx_counter.store(0);
    rx_last_frame.store(-1);
}

VideoChannel::~VideoChannel() {
    secure_cleanup();
}

void VideoChannel::enable_fault_detection(bool enable) {
    fault_detection_enabled = enable;
    if (enable && key_backup.empty()) {
        key_backup = video_key;
    }
}

bool VideoChannel::is_fault_detection_enabled() const {
    return fault_detection_enabled;
}

void VideoChannel::rotate_key(const std::vector<uint8_t>& new_key) {
    if (new_key.size() != 32) {
        throw std::runtime_error("Nouvelle clé doit faire 32 octets pour AES-256");
    }
    
    // Rotation déterministe : le salt HKDF est dérivé de l'état courant,
    // afin que deux extrémités qui appliquent la même rotation (même clé
    // courante + même new_key) aboutissent à la même clé.
    //
    // NOTE: la synchronisation de rotation reste un sujet protocolaire.
    std::vector<uint8_t> hkdf_salt = video_key;
    
    // 2. Préparer les infos pour HKDF (contexte de rotation vidéo)
    std::vector<uint8_t> hkdf_info;
    std::string context_str = "hesia-video-channel-key-rotation-v1";
    hkdf_info.insert(hkdf_info.end(), context_str.begin(), context_str.end());
    
    // 3. Dériver la nouvelle clé avec HKDF standard
    std::vector<uint8_t> derived_key = HKDF::derive(hkdf_salt, new_key, hkdf_info, 32);
    
    // 4. Nettoyage sécurisé de l'ancienne clé
    SecureMemory::zeroize(video_key);
    SecureMemory::zeroize(key_backup);
    
    // 5. Configuration de la nouvelle clé dérivée
    video_key = derived_key;
    key_backup = derived_key;
    
    // 6. Régénérer le sel IV pour nouvelle session
    SecureMemory::zeroize(iv_salt);
    iv_salt = SecureMemory::secure_alloc(32);
    if (iv_salt.empty()) {
        throw std::runtime_error("Échec régénération sel IV");
    }
    
    // 7. Réinitialiser les compteurs pour forward secrecy
    tx_counter.store(0);
    rx_last_frame.store(-1);
    
    // 8. Nettoyage sécurisé des données temporaires
    SecureMemory::zeroize(hkdf_salt);
    SecureMemory::zeroize(derived_key);
    SecureMemory::zeroize(hkdf_info);
    
    logger->info("Clé vidéo rotée avec HKDF standard (Perfect Forward Secrecy)");
}

} // namespace hesia
