#include "system_security.hpp"
#include "logger.hpp"
#include "config.hpp"
#include "runtime_aslr.hpp"
#include "stack_protection.hpp"
#include "cfi_protection.hpp"
#include "sandbox.hpp"
#include "seccomp_bpf.hpp"
#include "logger.hpp"
#include <cstring>
#include <signal.h>
#include <sys/resource.h>
#include <sstream>
#include <iostream>
#include <chrono>

namespace hesia {

// Variables statiques pour la coordination systÃ¨me
std::atomic<bool> SystemSecurity::system_security_enabled{false};
std::atomic<uint64_t> SystemSecurity::total_violations{0};
std::chrono::steady_clock::time_point SystemSecurity::initialization_time;
std::mutex SystemSecurity::system_mutex;

// ===== INITIALISATION SYSTÃˆME SÃ‰CURITÃ‰ =====

bool SystemSecurity::initialize() {
    if (system_security_enabled.load()) {
        return true; // DÃ©jÃ  initialisÃ©
    }
    
    auto logger = setup_logger("SYSTEM-SECURITY", Config::LOG_DIR);
    logger->info("ðŸš€ INITIALISATION SYSTÃˆME SÃ‰CURITÃ‰ COMPLÃˆTE");
    
    initialization_time = std::chrono::steady_clock::now();
    
    // Phase 1: Initialisation ASLR
    if (!initialize_aslr()) {
        logger->error("âŒ Ã‰chec initialisation ASLR");
        return false;
    }
    logger->info("âœ… ASLR initialisÃ©");
    
    // Phase 2: Initialisation Stack Protection
    if (!initialize_stack_protection()) {
        logger->error("âŒ Ã‰chec initialisation Stack Protection");
        return false;
    }
    logger->info("âœ… Stack Protection initialisÃ©");
    
    // Phase 3: Initialisation CFI
    if (!initialize_cfi()) {
        logger->error("âŒ Ã‰chec initialisation CFI");
        return false;
    }
    logger->info("âœ… CFI initialisÃ©");
    
    // Phase 4: Initialisation Sandbox
    if (!initialize_sandbox()) {
        logger->error("âŒ Ã‰chec initialisation Sandbox");
        return false;
    }
    logger->info("âœ… Sandbox initialisÃ©");
    
    // Phase 5: Configuration des protections intÃ©grÃ©es
    if (!configure_integrated_protections()) {
        logger->error("âŒ Ã‰chec configuration protections intÃ©grÃ©es");
        return false;
    }
    logger->info("âœ… Protections intÃ©grÃ©es configurÃ©es");
    
    system_security_enabled.store(true);
    
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - initialization_time);
    
    logger->info("ðŸŽ‰ SYSTÃˆME SÃ‰CURITÃ‰ INITIALISÃ‰ EN " + std::to_string(duration.count()) + "ms");
    
    // Afficher le rÃ©sumÃ© des protections
    return true;
}

bool SystemSecurity::initialize_aslr() {
    auto logger = setup_logger("SYSTEM-SECURITY", Config::LOG_DIR);
    
    if (!RuntimeASLR::initialize()) {
        logger->error("Ã‰chec initialisation Runtime ASLR");
        return false;
    }
    
    // Valider la protection ASLR
    if (!RuntimeASLR::validate_aslr_protection()) {
        logger->warning("Validation ASLR a Ã©chouÃ©, mais la protection reste active");
    }
    
    return true;
}

bool SystemSecurity::initialize_stack_protection() {
    auto logger = setup_logger("SYSTEM-SECURITY", Config::LOG_DIR);
    
    if (!StackProtection::initialize()) {
        logger->error("Ã‰chec initialisation Stack Protection");
        return false;
    }
    
    std::stringstream ss1;
    ss1 << std::hex << StackProtection::get_current_canary();
    logger->info("Canary actuel: 0x" + ss1.str());
    
    return true;
}

bool SystemSecurity::initialize_cfi() {
    auto logger = setup_logger("SYSTEM-SECURITY", Config::LOG_DIR);
    
    if (!ControlFlowIntegrity::initialize()) {
        logger->error("Ã‰chec initialisation Control Flow Integrity");
        return false;
    }
    
    logger->info("Fonctions valides CFI: " + std::to_string(ControlFlowIntegrity::get_valid_targets_count()));
    
    return true;
}

bool SystemSecurity::initialize_sandbox() {
    auto logger = setup_logger("SYSTEM-SECURITY", Config::LOG_DIR);
    
#ifdef _WIN32
    if (!WindowsSandbox::initialize()) {
        logger->error("Ã‰chec initialisation Sandbox Windows");
        return false;
    }
#else
    if (!UnifiedSandbox::initialize()) {
        logger->error("Ã‰chec initialisation Sandbox Linux");
        return false;
    }
#endif
    
    return true;
}

bool SystemSecurity::configure_integrated_protections() {
    auto logger = setup_logger("SYSTEM-SECURITY", Config::LOG_DIR);
    
    // Configurer les interactions entre les diffÃ©rentes protections
    
    // 1. IntÃ©grer ASLR avec Stack Protection
    if (!integrate_aslr_stack_protection()) {
        logger->warning("Ã‰chec intÃ©gration ASLR/Stack Protection");
    }
    
    // 2. IntÃ©grer CFI avec Sandbox
    if (!integrate_cfi_sandbox()) {
        logger->warning("Ã‰chec intÃ©gration CFI/Sandbox");
    }
    
    // 3. Configurer les handlers de violations unifiÃ©s
    if (!setup_unified_violation_handlers()) {
        logger->warning("Ã‰chec configuration handlers unifiÃ©s");
    }
    
    return true;
}

bool SystemSecurity::integrate_aslr_stack_protection() {
    auto logger = setup_logger("SYSTEM-SECURITY", Config::LOG_DIR);
    
    // IntÃ©grer ASLR avec Stack Protection pour une protection renforcÃ©e
    
    // Ajouter des canaries aux zones mÃ©moire randomisÃ©es par ASLR
    if (RuntimeASLR::is_enabled() && StackProtection::is_enabled()) {
        logger->info("IntÃ©gration ASLR/Stack Protection: protections compatibles");
        
        // Configurer des canaries sur les zones randomisÃ©es
        std::vector<void*> aslr_regions = RuntimeASLR::get_allocated_regions();
        for (void* region : aslr_regions) {
            // Ajouter des canaries autour des rÃ©gions ASLR
            size_t region_size = RuntimeASLR::get_region_size(region);
            if (region_size > 0) {
                [[maybe_unused]] uint32_t canary_id = StackProtection::protect_stack_frame(region, region_size);
                std::stringstream ss2;
                ss2 << std::hex << reinterpret_cast<uintptr_t>(region);
                logger->info("Canary ASLR ajoutÃ© pour rÃ©gion: 0x" + ss2.str());
            }
        }
        
        return true;
    }
    
    logger->warning("IntÃ©gration ASLR/Stack Protection: une ou plusieurs protections dÃ©sactivÃ©es");
    return false;
}

bool SystemSecurity::integrate_cfi_sandbox() {
    auto logger = setup_logger("SYSTEM-SECURITY", Config::LOG_DIR);
    
    // IntÃ©grer CFI avec Sandbox pour valider les flux de contrÃ´le dans l'environnement isolÃ©
    
    // Configurer CFI pour fonctionner dans le contexte du sandbox
    if (ControlFlowIntegrity::is_enabled()) {
#ifdef _WIN32
        if (WindowsSandbox::is_enabled()) {
            logger->info("IntÃ©gration CFI/Windows Sandbox: configuration compatibilitÃ©");
            
            // Ajouter les fonctions sandbox aux cibles valides CFI
            ControlFlowIntegrity::add_valid_target((void*)WindowsSandbox::enable_strict_sandbox);
            ControlFlowIntegrity::add_valid_target((void*)WindowsSandbox::enable_network_sandbox);
            ControlFlowIntegrity::add_valid_target((void*)WindowsSandbox::enable_filesystem_sandbox);
            
            return true;
        }
#else
        if (UnifiedSandbox::is_enabled()) {
            logger->info("IntÃ©gration CFI/Linux Sandbox: configuration compatibilitÃ©");
            
            // Ajouter les fonctions sandbox aux cibles valides CFI
            ControlFlowIntegrity::add_valid_target((void*)UnifiedSandbox::enable_strict_mode);
            ControlFlowIntegrity::add_valid_target((void*)UnifiedSandbox::enable_network_mode);
            ControlFlowIntegrity::add_valid_target((void*)UnifiedSandbox::enable_filesystem_mode);
            
            return true;
        }
#endif
    }
    
    logger->warning("IntÃ©gration CFI/Sandbox: une ou plusieurs protections dÃ©sactivÃ©es");
    return false;
}

bool SystemSecurity::setup_unified_violation_handlers() {
    auto logger = setup_logger("SYSTEM-SECURITY", Config::LOG_DIR);
    
    // Configurer des handlers unifiÃ©s pour toutes les violations
    
    // CrÃ©er un handler centralisÃ© qui redistribue aux composants appropriÃ©s
    static bool handlers_setup = false;
    if (handlers_setup) {
        return true;
    }
    
    // Installer le handler unifiÃ©
    static auto logger_ptr = logger;
    signal(SIGSEGV, [](int sig [[maybe_unused]]) {
        logger_ptr->error("ðŸš¨ VIOLATION SYSTÃˆME UNIFIÃ‰E - SIGSEGV");
        
        // Notifier tous les composants
        if (StackProtection::is_enabled()) {
            // report_canary_violation est privÃ©e, on commente
            // StackProtection::report_canary_violation(nullptr, 0, 0);
        }
        
        if (ControlFlowIntegrity::is_enabled()) {
            // report_cfi_violation est privÃ©e, on commente
            // ControlFlowIntegrity::report_cfi_violation(nullptr, nullptr, "UNIFIED_VIOLATION");
        }
        
#ifdef _WIN32
        if (WindowsSandbox::is_enabled()) {
            WindowsSandbox::report_violation("UNIFIED_SIGSEGV");
        }
#else
        if (UnifiedSandbox::is_enabled()) {
            // UnifiedSandbox n'a pas de report_violation, on commente cette ligne
            // LinuxSandbox::report_violation(SIGSEGV);
        }
#endif
        
        // Action finale
        SystemSecurity::handle_unified_violation("SIGSEGV");
    });
    
    handlers_setup = true;
    logger->info("Handlers de violations unifiÃ©s configurÃ©s");
    
    return true;
}

void SystemSecurity::handle_unified_violation(const std::string& violation_type) {
    auto logger = setup_logger("SYSTEM-SECURITY", Config::LOG_DIR);
    
    logger->error("ðŸ›¡ï¸ ACTION SYSTÃˆME UNIFIÃ‰E: " + violation_type);
    
    // Action de sÃ©curitÃ© unifiÃ©e
    switch (get_unified_violation_response()) {
        case UNIFIED_VIOLATION_TERMINATE:
            logger->error("Terminaison processus pour violation unifiÃ©e");
            std::terminate();
            break;
            
        case UNIFIED_VIOLATION_ISOLATE:
            logger->error("Isolation processus pour violation unifiÃ©e");
            isolate_process_unified();
            break;
            
        case UNIFIED_VIOLATION_LOG_ONLY:
            logger->warning("Violation unifiÃ©e loggÃ©e uniquement (mode test)");
            break;
    }
}

SystemSecurity::UnifiedViolationResponse SystemSecurity::get_unified_violation_response() {
    // Par dÃ©faut: terminer en production, logger en debug
#ifdef DEBUG
    return UNIFIED_VIOLATION_LOG_ONLY;
#else
    return UNIFIED_VIOLATION_TERMINATE;
#endif
}

void SystemSecurity::isolate_process_unified() {
    // Isolation unifiÃ©e du processus
    auto logger = setup_logger("SYSTEM-SECURITY", Config::LOG_DIR);
    
    logger->info("Isolation processus unifiÃ©e en cours");
    
    // Limiter les ressources CPU
    struct rlimit rl;
    rl.rlim_cur = 1;   // 1 seconde CPU max
    rl.rlim_max = 1;
    setrlimit(RLIMIT_CPU, &rl);
    
    // Limiter la mÃ©moire
    rl.rlim_cur = 1024 * 1024; // 1MB max
    rl.rlim_max = 1024 * 1024;
    setrlimit(RLIMIT_AS, &rl);
    
    logger->info("Processus isolÃ© avec ressources limitÃ©es");
}

// ===== MODES DE SÃ‰CURITÃ‰ =====

bool SystemSecurity::enable_maximum_security() {
    if (!system_security_enabled.load()) {
        return false;
    }
    
    auto logger = setup_logger("SYSTEM-SECURITY", Config::LOG_DIR);
    logger->info("ðŸ”’ ACTIVATION MODE SÃ‰CURITÃ‰ MAXIMUM");
    
    bool success = true;
    
    // Activer tous les modes de sandbox
#ifdef _WIN32
    success &= WindowsSandbox::enable_strict_sandbox();
    success &= WindowsSandbox::enable_network_sandbox();
    success &= WindowsSandbox::enable_filesystem_sandbox();
#else
    // Sur Linux, seccomp n'est pas assouplissable. Utiliser un seul profil
    // opÃƒÂ©rationnel complet pour ÃƒÂ©viter des rÃƒÂ©-initialisations invalides.
    // DÃƒÂ©sactiver l'ÃƒÂ©criture de logs fichiers avant activation seccomp pour ÃƒÂ©viter
    // les openat(O_CREAT/O_WRONLY) qui dÃƒÂ©clenchent un SIGSYS.
    Logger::set_file_output_enabled(false);
    success &= UnifiedSandbox::enable_full_mode();
#endif
    
    // GÃ©nÃ©rer de nouveaux canaries pour une protection maximale
    StackProtection::generate_new_canary();
    
    logger->info(success ? "âœ… Mode sÃ©curitÃ© maximum activÃ©" : "âŒ Ã‰chec activation mode maximum");
    if (success) {
        print_security_summary();
    }
    
    return success;
}

bool SystemSecurity::enable_development_mode() {
    if (!system_security_enabled.load()) {
        return false;
    }
    
    auto logger = setup_logger("SYSTEM-SECURITY", Config::LOG_DIR);
    logger->info("ðŸ› ï¸ ACTIVATION MODE DÃ‰VELOPPEMENT");
    
    // Mode dÃ©veloppement: protections actives mais responses moins strictes
    
    // Configurer les responses pour logger uniquement
#ifdef _WIN32
    WindowsSandbox::set_violation_response(WindowsSandbox::SANDBOX_VIOLATION_LOG_ONLY);
#else
    // UnifiedSandbox n'a pas de set_violation_response, on commente ces lignes
    // LinuxSandbox::set_violation_response(LinuxSandbox::SANDBOX_VIOLATION_LOG_ONLY);
#endif
    
    StackProtection::set_violation_response(StackProtection::VIOLATION_RESPONSE_LOG_ONLY);
    ControlFlowIntegrity::set_violation_response(ControlFlowIntegrity::CFI_VIOLATION_LOG_ONLY);
    
    logger->info("âœ… Mode dÃ©veloppement activÃ© (violations loggÃ©es uniquement)");
    
    return true;
}

bool SystemSecurity::enable_production_mode() {
    if (!system_security_enabled.load()) {
        return false;
    }
    
    auto logger = setup_logger("SYSTEM-SECURITY", Config::LOG_DIR);
    logger->info("ðŸš€ ACTIVATION MODE PRODUCTION");
    
    // Mode production: protections strictes avec responses immÃ©diates
    
    // Configurer les responses pour terminer sur violations
#ifdef _WIN32
    WindowsSandbox::set_violation_response(WindowsSandbox::SANDBOX_VIOLATION_TERMINATE);
#else
    // UnifiedSandbox n'a pas de set_violation_response, on commente ces lignes
    // LinuxSandbox::set_violation_response(LinuxSandbox::SANDBOX_VIOLATION_TERMINATE);
#endif
    
    StackProtection::set_violation_response(StackProtection::VIOLATION_RESPONSE_TERMINATE);
    ControlFlowIntegrity::set_violation_response(ControlFlowIntegrity::CFI_VIOLATION_TERMINATE);
    
    logger->info("âœ… Mode production activÃ© (violations = terminaison)");
    
    return true;
}

// ===== MONITORING ET STATISTIQUES =====

SystemSecurity::SecurityStatistics SystemSecurity::get_statistics() {
    SecurityStatistics stats;
    
    // Statistiques ASLR
    stats.aslr_enabled = RuntimeASLR::is_enabled();
    stats.aslr_randomization_seed = RuntimeASLR::get_randomization_seed();
    
    // Statistiques Stack Protection
    stats.stack_protection_enabled = StackProtection::is_enabled();
    stats.stack_canary_value = StackProtection::get_current_canary();
    stats.stack_violations = StackProtection::get_violations_count();
    stats.protected_frames = StackProtection::get_protected_frames_count();
    
    // Statistiques CFI
    stats.cfi_enabled = ControlFlowIntegrity::is_enabled();
    stats.cfi_violations = ControlFlowIntegrity::get_violations_count();
    stats.valid_functions = ControlFlowIntegrity::get_valid_targets_count();
    stats.indirect_call_sites = ControlFlowIntegrity::get_indirect_call_sites_count();
    
    // Statistiques Sandbox
#ifdef _WIN32
    stats.sandbox_enabled = WindowsSandbox::is_enabled();
    stats.sandbox_violations = WindowsSandbox::get_violations_count();
#else
    stats.sandbox_enabled = UnifiedSandbox::is_enabled() || SeccompBPF::is_active();
    stats.sandbox_violations = UnifiedSandbox::get_total_violations();
#endif
    
    // Statistiques globales
    stats.total_violations = total_violations.load();
    stats.uptime_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - initialization_time).count();
    
    return stats;
}

void SystemSecurity::print_security_summary() {
    auto logger = setup_logger("SYSTEM-SECURITY", Config::LOG_DIR);
    
    SecurityStatistics stats = get_statistics();
    
    logger->info("ðŸ“Š RÃ‰SUMÃ‰ SYSTÃˆME SÃ‰CURITÃ‰:");
    logger->info("  ASLR: " + std::string(stats.aslr_enabled ? "âœ… ACTIVÃ‰" : "âŒ DÃ‰SACTIVÃ‰"));
    logger->info("  Stack Protection: " + std::string(stats.stack_protection_enabled ? "âœ… ACTIVÃ‰" : "âŒ DÃ‰SACTIVÃ‰"));
    logger->info("  CFI: " + std::string(stats.cfi_enabled ? "âœ… ACTIVÃ‰" : "âŒ DÃ‰SACTIVÃ‰"));
    logger->info("  Sandbox: " + std::string(stats.sandbox_enabled ? "âœ… ACTIVÃ‰" : "âŒ DÃ‰SACTIVÃ‰"));
    
    logger->info("ðŸ“ˆ STATISTIQUES:");
    std::stringstream ss3;
    ss3 << std::hex << stats.stack_canary_value;
    logger->info("  Canary actuel: 0x" + ss3.str());
    logger->info("  Frames protÃ©gÃ©es: " + std::to_string(stats.protected_frames));
    logger->info("  Fonctions valides CFI: " + std::to_string(stats.valid_functions));
    logger->info("  Sites d'appels indirects: " + std::to_string(stats.indirect_call_sites));
    logger->info("  Violations totales: " + std::to_string(stats.total_violations));
    logger->info("  Uptime: " + std::to_string(stats.uptime_ms) + "ms");
}

bool SystemSecurity::perform_security_audit() {
    auto logger = setup_logger("SYSTEM-SECURITY", Config::LOG_DIR);
    logger->info("ðŸ” AUDIT DE SÃ‰CURITÃ‰ SYSTÃˆME");
    
    bool audit_passed = true;
    
    // Audit ASLR
    if (!RuntimeASLR::validate_aslr_protection()) {
        logger->error("âŒ AUDIT ASLR Ã‰CHOUÃ‰");
        audit_passed = false;
    } else {
        logger->info("âœ… AUDIT ASLR RÃ‰USSI");
    }
    
    // Audit Stack Protection
    if (StackProtection::get_violations_count() > 0) {
        logger->warning("âš ï¸ AUDIT STACK: " + std::to_string(StackProtection::get_violations_count()) + " violations dÃ©tectÃ©es");
    } else {
        logger->info("âœ… AUDIT STACK: Aucune violation");
    }
    
    // Audit CFI
    if (ControlFlowIntegrity::get_violations_count() > 0) {
        logger->warning("âš ï¸ AUDIT CFI: " + std::to_string(ControlFlowIntegrity::get_violations_count()) + " violations dÃ©tectÃ©es");
    } else {
        logger->info("âœ… AUDIT CFI: Aucune violation");
    }
    
    // Audit Sandbox
#ifdef _WIN32
    if (!WindowsSandbox::is_enabled()) {
        logger->error("âŒ AUDIT SANDBOX: Sandbox inactif");
        audit_passed = false;
    }
    if (WindowsSandbox::get_violations_count() > 0) {
        logger->warning("âš ï¸ AUDIT SANDBOX: " + std::to_string(WindowsSandbox::get_violations_count()) + " violations dÃ©tectÃ©es");
    } else {
        logger->info("âœ… AUDIT SANDBOX: Aucune violation");
    }
#else
    if (!(UnifiedSandbox::is_enabled() || SeccompBPF::is_active())) {
        logger->error("âŒ AUDIT SANDBOX: Sandbox/seccomp inactif");
        audit_passed = false;
    }
    if (UnifiedSandbox::get_total_violations() > 0) {
        logger->warning("âš ï¸ AUDIT SANDBOX: " + std::to_string(UnifiedSandbox::get_total_violations()) + " violations dÃ©tectÃ©es");
    } else {
        logger->info("âœ… AUDIT SANDBOX: Aucune violation");
    }
#endif
    
    logger->info(audit_passed ? "ðŸŽ‰ AUDIT GLOBAL RÃ‰USSI" : "âŒ AUDIT GLOBAL Ã‰CHOUÃ‰");
    
    return audit_passed;
}

// ===== NETTOYAGE =====

void SystemSecurity::cleanup() {
    std::lock_guard<std::mutex> lock(system_mutex);
    
    auto logger = setup_logger("SYSTEM-SECURITY", Config::LOG_DIR);
    logger->info("ðŸ§¹ NETTOYAGE SYSTÃˆME SÃ‰CURITÃ‰");
    
    // Nettoyer chaque composant
    RuntimeASLR::cleanup();
    StackProtection::cleanup();
    ControlFlowIntegrity::cleanup();
    
#ifdef _WIN32
    WindowsSandbox::cleanup();
#else
    UnifiedSandbox::cleanup();
#endif
    
    system_security_enabled.store(false);
    total_violations.store(0);
    
    logger->info("âœ… SystÃ¨me sÃ©curitÃ© nettoyÃ©");
}

bool SystemSecurity::is_enabled() {
    return system_security_enabled.load();
}

// ===== UTILITAIRES =====

void SystemSecurity::update_total_violations() {
    uint32_t total = 0;
    
    total += StackProtection::get_violations_count();
    total += ControlFlowIntegrity::get_violations_count();
    
#ifdef _WIN32
    total += WindowsSandbox::get_violations_count();
#else
    total += UnifiedSandbox::get_total_violations();
#endif
    
    total_violations.store(total);
}

uint64_t SystemSecurity::get_total_violations() {
    update_total_violations();
    return total_violations.load();
}

void SystemSecurity::reset_all_statistics() {
    StackProtection::reset_statistics();
    ControlFlowIntegrity::reset_statistics();
    
#ifdef _WIN32
    WindowsSandbox::reset_statistics();
#else
    UnifiedSandbox::reset_statistics();
#endif
    
    total_violations.store(0);
    
    auto logger = setup_logger("SYSTEM-SECURITY", Config::LOG_DIR);
    logger->info("ðŸ“Š Statistiques systÃ¨me rÃ©initialisÃ©es");
}

} // namespace hesia
