#ifndef CFI_PROTECTION_HPP
#define CFI_PROTECTION_HPP

#include <vector>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace hesia {

// Control Flow Integrity (CFI) Protection
// Implémentation complète de validation du flux de contrôle

class ControlFlowIntegrity {
private:
    static std::atomic<bool> cfi_enabled;
    static std::atomic<uint64_t> violations_count;
    static std::atomic<uint64_t> indirect_calls_count;
    static std::mutex cfi_mutex;
    
    // Fonctions internes
    static bool scan_valid_functions();
    static void scan_function_section(void* section_start, size_t section_size);
    static bool identify_indirect_call_sites();
    static bool setup_cfi_protections();
    static bool is_call_site_allowed(void* call_site, void* target);
    static void report_cfi_violation(void* target, void* call_site, const std::string& reason);
    static void handle_cfi_violation(void* target, void* call_site, const std::string& reason);
    static std::vector<std::string> get_stack_trace();
    static void isolate_current_thread();
    
public:
    // Types de réponse aux violations
    enum CFIViolationResponse {
        CFI_VIOLATION_TERMINATE = 0,
        CFI_VIOLATION_ISOLATE = 1,
        CFI_VIOLATION_LOG_ONLY = 2
    };
    
    // Initialisation et configuration
    static bool initialize();
    static void cleanup();
    static bool is_enabled();
    
    // Validation des appels indirects
    static bool validate_indirect_call(void* target, void* call_site);
    static CFIViolationResponse get_violation_response();
    static void set_violation_response(CFIViolationResponse response);
    
    // Gestion des cibles valides
    static void add_valid_target(void* target, const std::vector<void*>& allowed_call_sites = {});
    static void remove_valid_target(void* target);
    
    // Gestion des sites d'appels indirects
    static void add_indirect_call_site(void* call_site);
    static void remove_indirect_call_site(void* call_site);
    
    // Statistiques
    static uint64_t get_violations_count();
    static size_t get_valid_targets_count();
    static size_t get_indirect_call_sites_count();
    static void reset_statistics();
    
    // Macros pour validation automatique
    #define CFI_VALIDATE_CALL(target, call_site) \
        ControlFlowIntegrity::validate_indirect_call((void*)(target), (void*)(call_site))
    
    #define CFI_SAFE_CALL(func_ptr, ...) \
        do { \
            if (ControlFlowIntegrity::validate_indirect_call((void*)(func_ptr), __builtin_return_address(0))) { \
                ((func_ptr)(__VA_ARGS__)); \
            } else { \
                ControlFlowIntegrity::report_cfi_violation((void*)(func_ptr), __builtin_return_address(0), "SAFE_CALL_VIOLATION"); \
            } \
        } while(0)
};

} // namespace hesia

#endif // CFI_PROTECTION_HPP
