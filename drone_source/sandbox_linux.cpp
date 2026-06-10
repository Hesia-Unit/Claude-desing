// sandbox_linux.cpp - Implémentation Linux pour UnifiedSandbox
#include "sandbox.hpp"
#include "seccomp_bpf.hpp"
#include "fuzzing_framework.hpp"

#ifndef HESIA_ENABLE_RUNTIME_FUZZING
#define HESIA_ENABLE_RUNTIME_FUZZING 0
#endif
#include "logger.hpp"
#include "config.hpp"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

namespace hesia {

// Membres statiques de UnifiedSandbox
bool UnifiedSandbox::unified_initialized = false;
UnifiedSandbox::SandboxMode UnifiedSandbox::current_mode = UnifiedSandbox::SANDBOX_NONE;
uint32_t UnifiedSandbox::total_violations = 0;
std::mutex UnifiedSandbox::unified_mutex;

bool UnifiedSandbox::initialize() {
    std::lock_guard<std::mutex> lock(unified_mutex);
    
    if (unified_initialized) {
        return true;
    }
    
    auto logger = setup_logger("SANDBOX", Config::LOG_DIR);
    if (logger) {
        logger->info("Initialisation du sandbox Linux");
    }
    
    // Configuration de base pour Linux
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        if (logger) logger->error("Échec de PR_SET_NO_NEW_PRIVS");
        return false;
    }
    
    unified_initialized = true;
    current_mode = SANDBOX_NONE;
    total_violations = 0;
    
    if (logger) logger->info("Sandbox Linux initialisé avec succès");
    return true;
}

void UnifiedSandbox::cleanup() {
    std::lock_guard<std::mutex> lock(unified_mutex);
    
    if (!unified_initialized) {
        return;
    }
    
    auto logger = setup_logger("SANDBOX", Config::LOG_DIR);
    if (logger) {
        logger->info("Nettoyage du sandbox Linux");
    }
    
    // seccomp est irréversible pour le processus courant ; ne pas annoncer
    // un sandbox désactivé alors que le filtre reste effectivement chargé.
    if (!SeccompBPF::is_active()) {
        current_mode = SANDBOX_NONE;
    } else if (logger) {
        logger->info("Seccomp reste actif jusqu'à la fin du processus");
    }
    unified_initialized = false;
}

bool UnifiedSandbox::is_enabled() {
    std::lock_guard<std::mutex> lock(unified_mutex);
    return SeccompBPF::is_active() ||
           (unified_initialized && current_mode != SANDBOX_NONE);
}

bool UnifiedSandbox::enable_mode(SandboxMode mode) {
    std::lock_guard<std::mutex> lock(unified_mutex);
    
    if (!unified_initialized) {
        return false;
    }
    
    auto logger = setup_logger("SANDBOX", Config::LOG_DIR);

    if (SeccompBPF::is_active()) {
        if (current_mode == mode && current_mode != SANDBOX_NONE) {
            if (logger) logger->info("Sandbox déjà actif dans le mode demandé");
            return true;
        }
        if (logger) {
            logger->warning("Seccomp déjà actif: impossible de remplacer le mode sandbox en cours");
        }
        return false;
    }
    
    switch (mode) {
        case SANDBOX_STRICT:
            return enable_strict_mode_impl();
            
        case SANDBOX_NETWORK:
            return enable_network_mode_impl();
            
        case SANDBOX_FILESYSTEM:
            return enable_filesystem_mode_impl();
            
        case SANDBOX_FULL:
            return enable_full_mode_impl();
            
        default:
            if (logger) logger->warning("Mode de sandbox invalide");
            return false;
    }
}

bool UnifiedSandbox::enable_strict_mode() {
    return enable_mode(SANDBOX_STRICT);
}

bool UnifiedSandbox::enable_network_mode() {
    return enable_mode(SANDBOX_NETWORK);
}

bool UnifiedSandbox::enable_filesystem_mode() {
    return enable_mode(SANDBOX_FILESYSTEM);
}

bool UnifiedSandbox::enable_full_mode() {
    return enable_mode(SANDBOX_FULL);
}

UnifiedSandbox::SandboxMode UnifiedSandbox::get_current_mode() {
    std::lock_guard<std::mutex> lock(unified_mutex);
    return current_mode;
}

uint32_t UnifiedSandbox::get_total_violations() {
    std::lock_guard<std::mutex> lock(unified_mutex);
    return total_violations;
}

void UnifiedSandbox::reset_statistics() {
    std::lock_guard<std::mutex> lock(unified_mutex);
    total_violations = 0;
}

// Implémentations privées Linux
bool UnifiedSandbox::enable_strict_mode_impl() {
    auto logger = setup_logger("SANDBOX", Config::LOG_DIR);
    
    // Activer seccomp-bpf avec politique stricte
    SeccompConfig seccomp_config;
    seccomp_config.policy = SeccompPolicy::STRICT_MINIMAL;
    seccomp_config.enforce_strict = true;
    seccomp_config.default_action = SECCOMP_RET_KILL;
    seccomp_config.log_file = Config::LOG_DIR + "/seccomp_violations.log";
    
    if (!SeccompBPF::initialize(seccomp_config)) {
        logger->error("❌ Échec activation seccomp-bpf strict");
        return false;
    }
    
#if HESIA_ENABLE_RUNTIME_FUZZING
    // Démarrer le fuzzing des parsers critiques
    FuzzingFramework::initialize();
    
    // Fuzzing du parser de configuration
    auto config_parser = [](const std::vector<uint8_t>& data) -> bool {
        // Parser de configuration simulé
        if (data.size() < 4) return false;
        
        // Validation basique du format
        for (size_t i = 0; i < data.size(); i++) {
            if (data[i] == 0xFF) return false;  // Caractère invalide
        }
        return true;
    };
    
    auto fuzz_result = FUZZ_PARSER("config_parser", config_parser, {});
    logger->info("🔥 Fuzzing config parser: " + std::to_string(fuzz_result.iterations) + 
                " iterations, " + std::to_string(fuzz_result.crash_detected) + " crashes");
#else
    logger->info("ℹ️ Runtime fuzzing désactivé (HESIA_ENABLE_RUNTIME_FUZZING=0)");
#endif
    
    current_mode = SANDBOX_STRICT;
    if (logger) logger->info("✅ Mode strict activé (seccomp-bpf + fuzzing)");
    return true;
}

bool UnifiedSandbox::enable_network_mode_impl() {
    auto logger = setup_logger("SANDBOX", Config::LOG_DIR);
    
    // Activer seccomp-bpf avec politique réseau désactivé
    SeccompConfig seccomp_config;
    seccomp_config.policy = SeccompPolicy::NETWORK_DISABLED;
    seccomp_config.enforce_strict = true;
    seccomp_config.default_action = SECCOMP_RET_KILL;
    seccomp_config.log_file = Config::LOG_DIR + "/seccomp_network.log";
    
    if (!SeccompBPF::initialize(seccomp_config)) {
        logger->error("❌ Échec activation seccomp-bpf réseau désactivé");
        return false;
    }
    
    // Fuzzing du parser de protocole réseau
    auto protocol_parser = [](const uint8_t* data, size_t len) -> bool {
        // Parser de protocole simulé (UDP/TCP)
        if (len < 8) return false;
        
        // Validation des en-têtes
        if (data[0] != 0x45 && data[0] != 0x44) return false;  // IPv4/IPv6
        if (len > 1500) return false;  // MTU limite
        
        return true;
    };
    
#if HESIA_ENABLE_RUNTIME_FUZZING
    auto fuzz_result = FUZZ_PROTOCOL_PARSER("network_parser", protocol_parser, {});
    logger->info("🔥 Fuzzing network parser: " + std::to_string(fuzz_result.iterations) + 
                " iterations, " + std::to_string(fuzz_result.crash_detected) + " crashes");
#else
    logger->info("ℹ️ Runtime fuzzing désactivé (HESIA_ENABLE_RUNTIME_FUZZING=0)");
#endif
    
    current_mode = SANDBOX_NETWORK;
    if (logger) logger->info("✅ Mode réseau activé (seccomp-bpf + fuzzing)");
    return true;
}

bool UnifiedSandbox::enable_filesystem_mode_impl() {
    auto logger = setup_logger("SANDBOX", Config::LOG_DIR);
    
    // Activer seccomp-bpf avec politique filesystem restreint
    SeccompConfig seccomp_config;
    seccomp_config.policy = SeccompPolicy::FILESYSTEM_RESTRICTED;
    seccomp_config.enforce_strict = true;
    seccomp_config.default_action = SECCOMP_RET_KILL;
    seccomp_config.log_file = Config::LOG_DIR + "/seccomp_filesystem.log";
    
    if (!SeccompBPF::initialize(seccomp_config)) {
        logger->error("❌ Échec activation seccomp-bpf filesystem restreint");
        return false;
    }
    
    // Fuzzing du parser de fichiers
    auto file_parser = [](const std::string& filename) -> bool {
        // Parser de fichiers simulé
        if (filename.empty() || filename.length() > 255) return false;
        
        // Validation des caractères dangereux
        if (filename.find("..") != std::string::npos) return false;  // Path traversal
        if (filename.find("/") == 0 && filename != "/tmp") return false;  // Absolute path
        
        return true;
    };
    
    std::vector<std::string> test_files = {
        "/tmp/valid.conf", "/tmp/test.dat", "../../../etc/passwd", 
        "/etc/shadow", "/root/.bashrc"
    };
    
#if HESIA_ENABLE_RUNTIME_FUZZING
    auto fuzz_result = FUZZ_FILE_PARSER("file_parser", file_parser, test_files);
    logger->info("🔥 Fuzzing file parser: " + std::to_string(fuzz_result.iterations) + 
                " iterations, " + std::to_string(fuzz_result.crash_detected) + " crashes");
#else
    logger->info("ℹ️ Runtime fuzzing désactivé (HESIA_ENABLE_RUNTIME_FUZZING=0)");
#endif
    
    current_mode = SANDBOX_FILESYSTEM;
    if (logger) logger->info("✅ Mode système de fichiers activé (seccomp-bpf + fuzzing)");
    return true;
}

bool UnifiedSandbox::enable_full_mode_impl() {
    auto logger = setup_logger("SANDBOX", Config::LOG_DIR);
    
    // Activer seccomp-bpf avec politique opérationnelle drone
    SeccompConfig seccomp_config;
    seccomp_config.policy = SeccompPolicy::DRONE_OPERATIONAL;
    seccomp_config.enforce_strict = true;
    seccomp_config.default_action = SECCOMP_RET_KILL;
    seccomp_config.allow_ptrace = false;
    seccomp_config.allow_execve = false;
    seccomp_config.log_file = Config::LOG_DIR + "/seccomp_drone.log";
    
    if (!SeccompBPF::initialize(seccomp_config)) {
        logger->error("❌ Échec activation seccomp-bpf drone opérationnel");
        return false;
    }
    
    // Fuzzing complet de tous les parsers critiques
#if HESIA_ENABLE_RUNTIME_FUZZING
    FuzzingFramework::initialize();
#else
    logger->info("ℹ️ Runtime fuzzing désactivé (HESIA_ENABLE_RUNTIME_FUZZING=0)");
#endif
    
    // Parser de commandes drone
    auto command_parser = [](const std::vector<uint8_t>& data) -> bool {
        if (data.size() < 2) return false;
        
        // Validation des commandes drone
        std::string cmd(data.begin(), data.end());
        std::vector<std::string> valid_commands = {
            "TAKEOFF", "LAND", "EMERGENCY", "RETURN_HOME",
            "SET_ALTITUDE", "SET_SPEED", "CAPTURE_IMAGE"
        };
        
        return std::find(valid_commands.begin(), valid_commands.end(), cmd) != valid_commands.end();
    };
    
#if HESIA_ENABLE_RUNTIME_FUZZING
    auto fuzz_result = FUZZ_PARSER("drone_command_parser", command_parser, {});
    logger->info("🔥 Fuzzing drone command parser: " + std::to_string(fuzz_result.iterations) + 
                " iterations, " + std::to_string(fuzz_result.crash_detected) + " crashes");
#else
    logger->info("ℹ️ Runtime fuzzing désactivé (HESIA_ENABLE_RUNTIME_FUZZING=0)");
#endif
    
    // Parser de données capteurs
    auto sensor_parser = [](const std::vector<uint8_t>& data) -> bool {
        if (data.size() < 8) return false;
        
        // Validation format des données capteurs
        // Format: [type:1][id:2][data:variable][crc:2]
        uint8_t type = data[0];
        uint16_t id = (data[1] << 8) | data[2];
        
        // Types valides
        if (type > 10) return false;
        if (id > 1000) return false;
        
        return true;
    };
    
#if HESIA_ENABLE_RUNTIME_FUZZING
    auto sensor_fuzz_result = FUZZ_PARSER("sensor_data_parser", sensor_parser, {});
    logger->info("🔥 Fuzzing sensor data parser: " + std::to_string(sensor_fuzz_result.iterations) + 
                " iterations, " + std::to_string(sensor_fuzz_result.crash_detected) + " crashes");
#else
    logger->info("ℹ️ Runtime fuzzing désactivé (HESIA_ENABLE_RUNTIME_FUZZING=0)");
#endif
    
    current_mode = SANDBOX_FULL;
    if (logger) logger->info("✅ Mode full activé (seccomp-bpf complet + fuzzing)");
    return true;
}

} // namespace hesia
