#ifndef SYSTEM_SECURITY_HPP
#define SYSTEM_SECURITY_HPP

#include <vector>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <string>
#include <chrono>

namespace hesia {

// Coordination unifiée de toutes les protections système

class SystemSecurity {
private:
    static std::atomic<bool> system_security_enabled;
    static std::atomic<uint64_t> total_violations;
    static std::chrono::steady_clock::time_point initialization_time;
    static std::mutex system_mutex;
    
    // Fonctions d'initialisation
    static bool initialize_aslr();
    static bool initialize_stack_protection();
    static bool initialize_cfi();
    static bool initialize_sandbox();
    static bool configure_integrated_protections();
    
    // Intégration entre protections
    static bool integrate_aslr_stack_protection();
    static bool integrate_cfi_sandbox();
    static bool setup_unified_violation_handlers();
    
    // Utilitaires
    static void update_total_violations();
    
    // Types de réponse aux violations unifiées
    enum UnifiedViolationResponse {
        UNIFIED_VIOLATION_TERMINATE = 0,
        UNIFIED_VIOLATION_ISOLATE = 1,
        UNIFIED_VIOLATION_LOG_ONLY = 2
    };
    
    // Fonctions utilitaires unifiées
    static void handle_unified_violation(const std::string& violation_type);
    static UnifiedViolationResponse get_unified_violation_response();
    static void isolate_process_unified();
    
public:
    // Statistiques complètes du système
    struct SecurityStatistics {
        // ASLR
        bool aslr_enabled;
        uint64_t aslr_randomization_seed;
        
        // Stack Protection
        bool stack_protection_enabled;
        uint32_t stack_canary_value;
        uint32_t stack_violations;
        size_t protected_frames;
        
        // CFI
        bool cfi_enabled;
        uint64_t cfi_violations;
        size_t valid_functions;
        size_t indirect_call_sites;
        
        // Sandbox
        bool sandbox_enabled;
        uint32_t sandbox_violations;
        
        // Global
        uint64_t total_violations;
        uint64_t uptime_ms;
    };
    
    // Initialisation et configuration
    static bool initialize();
    static void cleanup();
    static bool is_enabled();
    
    // Modes de sécurité
    static bool enable_maximum_security();
    static bool enable_development_mode();
    static bool enable_production_mode();
    
    // Monitoring et statistiques
    static SecurityStatistics get_statistics();
    static void print_security_summary();
    static bool perform_security_audit();
    
    // Gestion des violations
    static uint64_t get_total_violations();
    static void reset_all_statistics();
    
    // Macros pour utilisation facile
    #define SYSTEM_SECURITY_INIT() SystemSecurity::initialize()
    #define SYSTEM_SECURITY_MAX() SystemSecurity::enable_maximum_security()
    #define SYSTEM_SECURITY_DEV() SystemSecurity::enable_development_mode()
    #define SYSTEM_SECURITY_PROD() SystemSecurity::enable_production_mode()
    #define SYSTEM_SECURITY_AUDIT() SystemSecurity::perform_security_audit()
};

} // namespace hesia

#endif // SYSTEM_SECURITY_HPP
