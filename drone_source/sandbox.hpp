// sandbox.hpp - FICHIER CORRIGÉ
#ifndef HESIA_SANDBOX_HPP
#define HESIA_SANDBOX_HPP

#ifdef _WIN32
    #include <windows.h>
    #include <sddl.h>
#endif

#include <vector>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <string>
#include <memory>
#include <unordered_map>
#include <chrono>
#include <functional>

namespace hesia {

// ===== TYPES DE BASE =====

// Types de sandbox
enum class SandboxType {
    NONE,
    STRICT,
    NETWORK,
    FILESYSTEM,
    FULL
};

// Types de violation
enum class ViolationType {
    FILE_ACCESS,
    NETWORK_ACCESS,
    SYSTEM_CALL,
    MEMORY_ACCESS,
    PROCESS_CREATION,
    REGISTRY_ACCESS
};

// Réponses aux violations
enum class SandboxViolationResponse {
    TERMINATE = 0,
    ISOLATE = 1,
    LOG_ONLY = 2
};

// ===== STRUCTURES DE CONFIGURATION =====

// Configuration du sandbox
struct SandboxConfig {
    SandboxType type = SandboxType::NONE;
    bool allow_network = false;
    bool allow_filesystem = false;
    bool allow_system_calls = false;
    std::vector<std::string> allowed_paths;
    std::vector<std::string> blocked_paths;
    std::vector<std::string> allowed_domains;
    std::vector<std::string> blocked_domains;
    SandboxViolationResponse violation_response = SandboxViolationResponse::LOG_ONLY;
};

// Rapport de violation
struct ViolationReport {
    ViolationType type;
    std::string description;
    std::chrono::system_clock::time_point timestamp;
    std::string process_name;
    uint32_t pid;
    
    ViolationReport(ViolationType t, const std::string& desc, 
                   const std::string& proc_name = "", uint32_t proc_id = 0)
        : type(t), description(desc), timestamp(std::chrono::system_clock::now()),
          process_name(proc_name), pid(proc_id) {}
};

// ===== CLASSE SANDBOX PRINCIPALE =====

class Sandbox {
public:
    // Initialisation et configuration
    static bool initialize();
    static bool initialize(const SandboxConfig& config);
    static void cleanup();
    static bool is_enabled();
    
    // Modes de sandbox
    static bool enable_strict_sandbox();
    static bool enable_network_sandbox();
    static bool enable_filesystem_sandbox();
    
    // Gestion des violations
    static void report_violation(const std::string& operation);
    static SandboxViolationResponse get_violation_response();
    static void set_violation_response(SandboxViolationResponse response);
    
    // Statistiques
    static uint32_t get_violations_count();
    static void reset_statistics();
    
    // Configuration
    static void configure(const SandboxConfig& config);
    static SandboxConfig get_current_config();
    static SandboxType get_current_type();
    
    // Monitoring
    static std::vector<ViolationReport> get_violations();
    static bool has_violations();
    static size_t get_violation_count();
    static void clear_violations();
    
    // Callbacks
    static void set_violation_callback(std::function<void(const ViolationReport&)> callback);
    static void on_violation(const std::function<void(const ViolationReport&)>& handler);
    
    // Utilitaires
    static bool is_path_allowed(const std::string& path);
    static bool is_domain_allowed(const std::string& domain);
    static void log_violation(const ViolationReport& report);
    
    // Activation/désactivation
    static void activate();
    static void deactivate();
    static void reset();

private:
    // Membres statiques
    static bool initialized;
    static bool active;
    static SandboxConfig current_config;
    static std::mutex config_mutex;
    static std::vector<ViolationReport> violations;
    static std::mutex violations_mutex;
    static std::function<void(const ViolationReport&)> violation_callback;
    static SandboxViolationResponse violation_response;
    static uint32_t violation_count;
    
    // Implémentation spécifique à la plateforme
    static bool initialize_windows();
    static bool initialize_linux();
    static void cleanup_windows();
    static void cleanup_linux();
    static bool restrict_process_windows();
    static bool restrict_process_linux();
    
#ifdef _WIN32
    static HANDLE app_container_token;
    static PSID get_capability_sid(const std::wstring& capability_name);
#endif
};

// ===== CLASSE SANDBOX UNIFIÉE (pour compatibilité) =====

class UnifiedSandbox {
public:
    // Types de modes de sandbox
    enum SandboxMode {
        SANDBOX_NONE = 0,
        SANDBOX_STRICT = 1,
        SANDBOX_NETWORK = 2,
        SANDBOX_FILESYSTEM = 3,
        SANDBOX_FULL = 4
    };
    
    // Initialisation multi-plateforme
    static bool initialize();
    static void cleanup();
    static bool is_enabled();
    
    // Activation des modes
    static bool enable_mode(SandboxMode mode);
    static bool enable_strict_mode();
    static bool enable_network_mode();
    static bool enable_filesystem_mode();
    static bool enable_full_mode();
    
    // Statistiques unifiées
    static uint32_t get_total_violations();
    static void reset_statistics();
    
private:
    static bool unified_initialized;
    static SandboxMode current_mode;
    static uint32_t total_violations;
    static std::mutex unified_mutex;
    
    // Implémentations privées plateforme-spécifiques
    static bool enable_strict_mode_impl();
    static bool enable_network_mode_impl();
    static bool enable_filesystem_mode_impl();
    static bool enable_full_mode_impl();
};

// ===== MACROS POUR UTILISATION FACILE =====

#define SANDBOX_INIT() hesia::UnifiedSandbox::initialize()
#define SANDBOX_STRICT() hesia::UnifiedSandbox::enable_strict_mode()
#define SANDBOX_NETWORK() hesia::UnifiedSandbox::enable_network_mode()
#define SANDBOX_FILESYSTEM() hesia::UnifiedSandbox::enable_filesystem_mode()
#define SANDBOX_FULL() hesia::UnifiedSandbox::enable_full_mode()
#define SANDBOX_CHECK() hesia::UnifiedSandbox::is_enabled()

} // namespace hesia

#endif // HESIA_SANDBOX_HPP