#ifdef _WIN32
#include "sandbox.hpp"
#include "config.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <windows.h>
#include <sddl.h>
#include <aclapi.h>
#include <userenv.h>
#include <winternl.h>

namespace hesia {

// Variables statiques pour UnifiedSandbox (Windows)
bool UnifiedSandbox::initialized = false;
UnifiedSandbox::SandboxConfig UnifiedSandbox::current_config;
std::mutex UnifiedSandbox::config_mutex;
std::vector<UnifiedSandbox::ViolationReport> UnifiedSandbox::violations;
std::mutex UnifiedSandbox::violations_mutex;
std::function<void(const UnifiedSandbox::ViolationReport&)> UnifiedSandbox::violation_callback;

// Variables spécifiques Windows
HANDLE UnifiedSandbox::app_container_token = nullptr;

// ===== IMPLÉMENTATION WINDOWS =====

bool UnifiedSandbox::initialize_windows() {
    auto logger = setup_logger("SANDBOX-WINDOWS", Config::LOG_DIR);
    
    // Vérifier la version Windows
    OSVERSIONINFOEX osvi;
    ZeroMemory(&osvi, sizeof(OSVERSIONINFOEX));
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
    
    if (!GetVersionEx((OSVERSIONINFO*)&osvi)) {
        logger->error("Impossible d'obtenir la version Windows");
        return false;
    }
    
    bool is_windows_8_plus = (osvi.dwMajorVersion >= 6 && osvi.dwMinorVersion >= 2);
    
    if (is_windows_8_plus) {
        // Utiliser AppContainer si disponible
        if (!create_app_container()) {
            logger->warning("AppContainer échoué, utilisation fallback");
            return initialize_fallback();
        }
    } else {
        // Utiliser les mitigations de processus comme fallback
        logger->info("Windows < 8, utilisation des mitigations de processus");
        return initialize_fallback();
    }
    
    logger->info("Sandbox Windows initialisé avec succès");
    return true;
}

bool UnifiedSandbox::initialize_linux() {
    return false; // Non applicable sur Windows
}

void UnifiedSandbox::cleanup_windows() {
    if (app_container_token) {
        CloseHandle(app_container_token);
        app_container_token = nullptr;
    }
    
    std::lock_guard<std::mutex> lock(config_mutex);
    initialized = false;
    violations.clear();
}

void UnifiedSandbox::cleanup_linux() {
    // Non applicable sur Windows
}

bool UnifiedSandbox::create_app_container() {
    auto logger = setup_logger("SANDBOX-WINDOWS", Config::LOG_DIR);
    
    // Créer un SID pour l'AppContainer
    PSID app_container_sid = nullptr;
    HRESULT hr = CreateAppContainerSid(L"HESIA-Sandbox", L"HESIA Security Sandbox", &app_container_sid);
    
    if (FAILED(hr)) {
        logger->error("Échec création AppContainer SID");
        return false;
    }
    
    // Créer le token AppContainer
    HANDLE process_token;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_ADJUST_DEFAULT | TOKEN_QUERY, &process_token)) {
        if (DuplicateTokenEx(process_token, TOKEN_ALL_ACCESS, nullptr, 
                               SecurityImpersonation, TokenPrimary, &app_container_token)) {
            // Configurer le token pour l'AppContainer
            if (SetTokenInformation(app_container_token, TokenAppContainerSid, 
                                  &app_container_sid, sizeof(app_container_sid))) {
                logger->info("AppContainer token créé avec succès");
            }
        }
        CloseHandle(process_token);
    }
    
    if (app_container_sid) {
        FreeSid(app_container_sid);
    }
    
    return app_container_token != nullptr;
}

bool UnifiedSandbox::initialize_fallback() {
    auto logger = setup_logger("SANDBOX-WINDOWS", Config::LOG_DIR);
    
    // Activer les mitigations de processus
    PROCESS_MITIGATION_POLICY policy;
    ZeroMemory(&policy, sizeof(policy));
    
    // DEP (Data Execution Prevention)
    PROCESS_MITIGATION_DEP_POLICY dep_policy = {};
    dep_policy.Enable = TRUE;
    dep_policy.DisableAtlThunkEmulation = TRUE;
    
    if (SetProcessMitigationPolicy(ProcessDEPPolicy, &dep_policy, sizeof(dep_policy))) {
        logger->info("DEP activé");
    }
    
    // ASLR
    PROCESS_MITIGATION_ASLR_POLICY aslr_policy = {};
    aslr_policy.EnableBottomUpRandomization = TRUE;
    aslr_policy.EnableForceRelocateImages = TRUE;
    aslr_policy.EnableHighEntropy = TRUE;
    
    if (SetProcessMitigationPolicy(ProcessASLRPolicy, &aslr_policy, sizeof(aslr_policy))) {
        logger->info("ASLR activé");
    }
    
    // Strict Handle Checks
    PROCESS_MITIGATION_STRICT_HANDLE_CHECK_POLICY handle_policy = {};
    handle_policy.RaiseExceptionOnInvalidHandleReference = TRUE;
    handle_policy.HandleExceptionPermanentlyEnabled = TRUE;
    
    if (SetProcessMitigationPolicy(ProcessStrictHandleCheckPolicy, &handle_policy, sizeof(handle_policy))) {
        logger->info("Strict handle checks activés");
    }
    
    return true;
}

// ===== FONCTIONS PUBLIQUES =====

bool UnifiedSandbox::initialize() {
    std::lock_guard<std::mutex> lock(config_mutex);
    
    if (initialized) {
        return true;
    }
    
    bool result = initialize_windows();
    if (result) {
        initialized = true;
    }
    
    return result;
}

bool UnifiedSandbox::initialize(const SandboxConfig& config) {
    std::lock_guard<std::mutex> lock(config_mutex);
    
    if (initialized) {
        return true;
    }
    
    current_config = config;
    bool result = initialize_windows();
    
    if (result) {
        initialized = true;
        
        // Appliquer la configuration
        if (config.type == SandboxType::STRICT) {
            enable_strict_mode();
        } else if (config.type == SandboxType::NETWORK) {
            enable_network_mode();
        } else if (config.type == SandboxType::FILESYSTEM) {
            enable_filesystem_mode();
        } else if (config.type == SandboxType::FULL) {
            enable_full_mode();
        }
    }
    
    return result;
}

void UnifiedSandbox::enable_strict_mode() {
    if (!initialized) return;
    
    auto logger = setup_logger("SANDBOX-WINDOWS", Config::LOG_DIR);
    
    // Activer les mitigations strictes
    PROCESS_MITIGATION_POLICY policy;
    ZeroMemory(&policy, sizeof(policy));
    
    // Font Disable
    PROCESS_MITIGATION_FONT_DISABLE_POLICY font_policy = {};
    font_policy.DisableNonSystemFonts = TRUE;
    
    if (SetProcessMitigationPolicy(ProcessFontDisablePolicy, &font_policy, sizeof(font_policy))) {
        logger->info("Polices non-système désactivées");
    }
    
    // Image Load
    PROCESS_MITIGATION_IMAGE_LOAD_POLICY image_policy = {};
    image_policy.NoRemoteImages = TRUE;
    image_policy.NoLowMandatoryLabelImages = TRUE;
    
    if (SetProcessMitigationPolicy(ProcessImageLoadPolicy, &image_policy, sizeof(image_policy))) {
        logger->info("Chargement d'images restrictif");
    }
    
    // Binary Signature
    PROCESS_MITIGATION_BINARY_SIGNATURE_POLICY signature_policy = {};
    signature_policy.MitigationOptIn = TRUE;
    
    if (SetProcessMitigationPolicy(ProcessSignaturePolicy, &signature_policy, sizeof(signature_policy))) {
        logger->info("Vérification de signature binaire activée");
    }
    
    current_config.type = SandboxType::STRICT;
    logger->info("🔒 Mode strict activé");
}

void UnifiedSandbox::enable_network_mode() {
    if (!initialized) return;
    
    auto logger = setup_logger("SANDBOX-WINDOWS", Config::LOG_DIR);
    
    // Configurer les capacités réseau limitées
    if (!setup_network_capabilities()) {
        logger->error("Échec configuration capacités réseau");
        return;
    }
    
    current_config.type = SandboxType::NETWORK;
    logger->info("🌐 Mode réseau activé");
}

void UnifiedSandbox::enable_filesystem_mode() {
    if (!initialized) return;
    
    auto logger = setup_logger("SANDBOX-WINDOWS", Config::LOG_DIR);
    
    // Configurer les restrictions filesystem
    if (!setup_filesystem_restrictions()) {
        logger->error("Échec configuration restrictions filesystem");
        return;
    }
    
    current_config.type = SandboxType::FILESYSTEM;
    logger->info("📁 Mode filesystem activé");
}

void UnifiedSandbox::enable_full_mode() {
    if (!initialized) return;
    
    auto logger = setup_logger("SANDBOX-WINDOWS", Config::LOG_DIR);
    
    // Mode full: toutes les restrictions
    enable_strict_mode();
    enable_network_mode();
    enable_filesystem_mode();
    
    current_config.type = SandboxType::FULL;
    logger->info("🛡️ Mode full activé");
}

UnifiedSandbox::SandboxMode UnifiedSandbox::get_current_mode() {
    std::lock_guard<std::mutex> lock(config_mutex);

    switch (current_config.type) {
        case SandboxType::STRICT:
            return SANDBOX_STRICT;
        case SandboxType::NETWORK:
            return SANDBOX_NETWORK;
        case SandboxType::FILESYSTEM:
            return SANDBOX_FILESYSTEM;
        case SandboxType::FULL:
            return SANDBOX_FULL;
        case SandboxType::NONE:
        default:
            return SANDBOX_NONE;
    }
}

void UnifiedSandbox::configure(const SandboxConfig& config) {
    std::lock_guard<std::mutex> lock(config_mutex);
    current_config = config;
    
    auto logger = setup_logger("SANDBOX-WINDOWS", Config::LOG_DIR);
    logger->info("Configuration sandbox mise à jour");
}

bool UnifiedSandbox::is_active() {
    std::lock_guard<std::mutex> lock(config_mutex);
    return initialized;
}

void UnifiedSandbox::activate() {
    if (!initialized) {
        initialize();
    }
}

void UnifiedSandbox::deactivate() {
    std::lock_guard<std::mutex> lock(config_mutex);
    initialized = false;
}

void UnifiedSandbox::reset() {
    std::lock_guard<std::mutex> lock(violations_mutex);
    violations.clear();
}

std::vector<UnifiedSandbox::ViolationReport> UnifiedSandbox::get_violations() {
    std::lock_guard<std::mutex> lock(violations_mutex);
    return violations;
}

void UnifiedSandbox::clear_violations() {
    std::lock_guard<std::mutex> lock(violations_mutex);
    violations.clear();
}

bool UnifiedSandbox::has_violations() {
    std::lock_guard<std::mutex> lock(violations_mutex);
    return !violations.empty();
}

size_t UnifiedSandbox::get_violation_count() {
    std::lock_guard<std::mutex> lock(violations_mutex);
    return violations.size();
}

void UnifiedSandbox::on_violation(const std::function<void(const ViolationReport&)>& handler) {
    violation_callback = handler;
}

void UnifiedSandbox::set_violation_callback(std::function<void(const ViolationReport&)> callback) {
    violation_callback = callback;
}

UnifiedSandbox::SandboxType UnifiedSandbox::get_current_type() {
    std::lock_guard<std::mutex> lock(config_mutex);
    return current_config.type;
}

UnifiedSandbox::SandboxConfig UnifiedSandbox::get_current_config() {
    std::lock_guard<std::mutex> lock(config_mutex);
    return current_config;
}

std::string UnifiedSandbox::get_status_string() {
    std::lock_guard<std::mutex> lock(config_mutex);
    
    switch (current_config.type) {
        case SandboxType::NONE: return "Aucun sandbox";
        case SandboxType::STRICT: return "Mode strict";
        case SandboxType::NETWORK: return "Mode réseau";
        case SandboxType::FILESYSTEM: return "Mode filesystem";
        case SandboxType::FULL: return "Mode full";
        default: return "Inconnu";
    }
}

bool UnifiedSandbox::is_path_allowed(const std::string& path) {
    for (const auto& blocked : current_config.blocked_paths) {
        if (!blocked.empty() && path.find(blocked) == 0) {
            return false;
        }
    }

    const bool restrictions_expected =
        current_config.type == SandboxType::FILESYSTEM ||
        current_config.type == SandboxType::FULL;
    if (current_config.allowed_paths.empty()) {
        return !restrictions_expected;
    }
    
    for (const auto& allowed : current_config.allowed_paths) {
        if (path.find(allowed) == 0) {
            return true;
        }
    }
    
    return false;
}

bool UnifiedSandbox::is_domain_allowed(const std::string& domain) {
    for (const auto& blocked : current_config.blocked_domains) {
        if (!blocked.empty() && domain.find(blocked) != std::string::npos) {
            return false;
        }
    }

    const bool restrictions_expected =
        current_config.type == SandboxType::NETWORK ||
        current_config.type == SandboxType::FULL;
    if (current_config.allowed_domains.empty()) {
        return !restrictions_expected;
    }
    
    for (const auto& allowed : current_config.allowed_domains) {
        if (!allowed.empty() && domain.find(allowed) != std::string::npos) {
            return true;
        }
    }
    
    return false;
}

void UnifiedSandbox::log_violation(const ViolationReport& report) {
    std::lock_guard<std::mutex> lock(violations_mutex);
    violations.push_back(report);
    
    auto logger = setup_logger("SANDBOX-WINDOWS", Config::LOG_DIR);
    logger->critical("🚨 VIOLATION SANDBOX: " + report.description);
    
    if (violation_callback) {
        violation_callback(report);
    }
}

bool UnifiedSandbox::setup_network_capabilities() {
    auto logger = setup_logger("SANDBOX-WINDOWS", Config::LOG_DIR);
    
    if (!app_container_token) {
        logger->error("AppContainer token non disponible");
        return false;
    }
    
    // Définir les capacités réseau autorisées
    std::vector<SID_AND_ATTRIBUTES> capabilities;
    
    // Capacité Internet Client
    SID_AND_ATTRIBUTES internet_capability = {};
    internet_capability.Sid = get_capability_sid(L"internetClient");
    internet_capability.Attributes = SE_GROUP_ENABLED;
    if (internet_capability.Sid) {
        capabilities.push_back(internet_capability);
    }
    
    // Capacité Réseau Privé
    SID_AND_ATTRIBUTES private_network_capability = {};
    private_network_capability.Sid = get_capability_sid(L"privateNetworkClientServer");
    private_network_capability.Attributes = SE_GROUP_ENABLED;
    if (private_network_capability.Sid) {
        capabilities.push_back(private_network_capability);
    }
    
    if (!capabilities.empty()) {
        if (SetTokenInformation(app_container_token, TokenCapabilities, 
                              capabilities.data(), 
                              static_cast<DWORD>(capabilities.size() * sizeof(SID_AND_ATTRIBUTES)))) {
            logger->info("Capacités réseau configurées");
            return true;
        }
    }
    
    return false;
}

bool UnifiedSandbox::setup_filesystem_restrictions() {
    auto logger = setup_logger("SANDBOX-WINDOWS", Config::LOG_DIR);
    
    // Créer des restrictions sur les répertoires accessibles
    std::vector<std::wstring> allowed_directories = {
        L"C:\\ProgramData\\HESIA\\temp",
        L"C:\\Users\\Public\\Documents\\HESIA",
        L"C:\\Program Files\\HESIA"
    };
    
    for (const auto& dir : allowed_directories) {
        if (!create_directory_restriction(dir)) {
            logger->warning("Impossible de créer restriction pour: " + 
                          std::string(dir.begin(), dir.end()));
        }
    }
    
    logger->info("Restrictions filesystem configurées");
    return true;
}

bool UnifiedSandbox::create_directory_restriction(const std::wstring& directory) {
    // Créer une entrée de contrôle d'accès pour le répertoire
    PACL pACL = nullptr;
    EXPLICIT_ACCESS ea[2];
    ZeroMemory(ea, sizeof(ea));
    
    // Autoriser l'accès en lecture/écriture pour l'utilisateur actuel
    ea[0].grfAccessPermissions = GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE;
    ea[0].grfAccessMode = SET_ACCESS;
    ea[0].grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    ea[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea[0].Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    ea[0].Trustee.ptstrName = L"CURRENT_USER";
    
    // Refuser l'accès pour les autres utilisateurs
    ea[1].grfAccessPermissions = GENERIC_ALL;
    ea[1].grfAccessMode = DENY_ACCESS;
    ea[1].grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    ea[1].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea[1].Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    ea[1].Trustee.ptstrName = L"EVERYONE";
    
    DWORD dwRes = SetEntriesInAcl(2, ea, nullptr, &pACL);
    if (dwRes == ERROR_SUCCESS) {
        // Appliquer le DACL au répertoire
        if (SetNamedSecurityInfo(const_cast<LPWSTR>(directory.c_str()), 
                                 SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                 nullptr, nullptr, pACL, nullptr) == ERROR_SUCCESS) {
            LocalFree(pACL);
            return true;
        }
        LocalFree(pACL);
    }
    
    return false;
}

PSID UnifiedSandbox::get_capability_sid(const std::wstring& capability_name) {
    // Cette fonction devrait utiliser les API Windows pour obtenir les SID des capacités
    // Pour l'exemple, retourne nullptr (implémentation simplifiée)
    return nullptr;
}

} // namespace hesia

#endif  // _WIN32
