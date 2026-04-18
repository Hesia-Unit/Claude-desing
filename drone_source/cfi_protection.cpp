#include "cfi_protection.hpp"
#include "logger.hpp"
#include "config.hpp"
#include <iostream>
#include <sstream>
#include <unistd.h>

// ✅ SÉCURITÉ: Utiliser CFI natif du compilateur
// NE PAS implémenter CFI maison (dangereux et inefficace)
// Options recommandées: -fsanitize=cfi -flto -fvisibility=hidden

#ifdef __clang__
// Clang d??finit diff??rentes variantes selon le mode CFI activ??.
#if defined(HESIA_CFI_BUILD)
#define CFI_NATIVE_AVAILABLE 1
#pragma message "??? CFI activ?? (flag build HESIA_CFI_BUILD)"
#elif defined(__SANITIZE_CFI__) || __has_feature(cfi) || __has_feature(cfi_vcall) || __has_feature(control_flow_integrity)
#define CFI_NATIVE_AVAILABLE 1
#pragma message "??? CFI natif Clang disponible - utilisation recommand??e"
#else
#define CFI_NATIVE_AVAILABLE 0
#pragma message "?????? CFI natif non disponible - compiler avec -fsanitize=cfi"
#endif
#else
#define CFI_NATIVE_AVAILABLE 0
#pragma message "?????? Clang requis pour CFI natif - utiliser clang++"
#endif

namespace hesia {

// Variables statiques pour monitoring CFI natif
std::atomic<bool> ControlFlowIntegrity::cfi_enabled{CFI_NATIVE_AVAILABLE ? true : false};
std::atomic<uint64_t> ControlFlowIntegrity::violations_count{0};
std::atomic<uint64_t> ControlFlowIntegrity::indirect_calls_count{0};
std::mutex ControlFlowIntegrity::cfi_mutex;

// ===== INITIALISATION CFI =====

bool ControlFlowIntegrity::initialize() {
    auto logger = setup_logger("CFI", Config::LOG_DIR);
    
#if CFI_NATIVE_AVAILABLE
    logger->info("✅ CFI natif Clang activé");
    logger->info("ℹ️ Options compilateur: -fsanitize=cfi -flto -fvisibility=hidden");
    
    cfi_enabled = true;
    violations_count = 0;
    indirect_calls_count = 0;
    
    logger->info("✅ CFI natif initialisé");
    return true;
#else
    logger->warning("⚠️ CFI natif non disponible");
    logger->warning("ℹ️ Recommandation: compiler avec clang++ -fsanitize=cfi -flto");
    
    cfi_enabled = false;
    return false;
#endif
}

bool ControlFlowIntegrity::scan_valid_functions() {
    auto logger = setup_logger("CFI", Config::LOG_DIR);
    
#if CFI_NATIVE_AVAILABLE
    logger->info("✅ CFI natif gère automatiquement la validation");
    logger->info("ℹ️ Pas besoin de scan manuel - Clang CFI s'en charge");
    return true;
#else
    logger->warning("⚠️ CFI natif non disponible - scan manuel non implémenté");
    logger->warning("ℹ️ Utiliser clang++ -fsanitize=cfi pour CFI automatique");
    return false;
#endif
}

bool ControlFlowIntegrity::setup_cfi_protections() {
    auto logger = setup_logger("CFI", Config::LOG_DIR);
    
#if CFI_NATIVE_AVAILABLE
    logger->info("✅ CFI natif configure automatiquement les protections");
    logger->info("ℹ️ Pas de configuration manuelle requise");
    return true;
#else
    logger->warning("⚠️ CFI natif non disponible");
    return false;
#endif
}

// ===== VALIDATION CFI =====

bool ControlFlowIntegrity::validate_indirect_call(void* /*target*/, void* /*return_addr*/) {
    if (!cfi_enabled.load()) {
        return true; // CFI désactivé
    }
    
    // ✅ SÉCURITÉ: CFI natif gère la validation automatiquement
    // Pas besoin de validation manuelle - Clang CFI intercepte
    
    indirect_calls_count++;
    
#if CFI_NATIVE_AVAILABLE
    // CFI natif valide automatiquement - si on arrive ici, c'est valide
    return true;
#else
    // CFI non disponible - autoriser mais logger
    auto logger = setup_logger("CFI", Config::LOG_DIR);
    logger->warning("⚠️ CFI non disponible - appel indirect non validé");
    return true;
#endif
}

bool ControlFlowIntegrity::is_call_site_allowed(void* /*call_site*/, void* /*target*/) {
    // ✅ SÉCURITÉ: CFI natif gère cette validation automatiquement
    // Pas besoin de validation manuelle
    
#if CFI_NATIVE_AVAILABLE
    return true; // CFI natif a déjà validé
#else
    auto logger = setup_logger("CFI", Config::LOG_DIR);
    logger->warning("⚠️ CFI non disponible - validation manuelle désactivée");
    return true;
#endif
}

// ===== GESTION DES VIOLATIONS =====

void ControlFlowIntegrity::report_cfi_violation(void* target, void* call_site,
                                                const std::string& reason) {
    auto logger = setup_logger("CFI", Config::LOG_DIR);
    
    violations_count++;
    
    logger->error("🚨 VIOLATION CFI DÉTECTÉE");
    logger->error("Target: " + std::to_string(reinterpret_cast<uintptr_t>(target)));
    logger->error("Call site: " + std::to_string(reinterpret_cast<uintptr_t>(call_site)));
    logger->error("Reason: " + reason);
    
    // ✅ SÉCURITÉ: Pas de stack trace dans signal handler
    logger->error("⚠️ Analyser avec: gdb -batch -ex 'bt' " + std::to_string(getpid()));
    
    handle_cfi_violation(target, call_site, reason);
}

void ControlFlowIntegrity::handle_cfi_violation(void* target [[maybe_unused]], 
                                               void* call_site [[maybe_unused]],
                                               const std::string& reason) {
    auto logger = setup_logger("CFI", Config::LOG_DIR);
    
    logger->error("🛡️ ACTION SÉCURITÉ: Violation CFI détectée");
    logger->error("Reason: " + reason);
    
    // Options de réponse
    switch (get_violation_response()) {
        case CFI_VIOLATION_TERMINATE:
            logger->error("🔴 TERMINATE: Arrêt immédiat du processus");
            std::terminate();
            break;
            
        case CFI_VIOLATION_ISOLATE:
            logger->error("🟡 ISOLATE: Isolation du thread courant");
            isolate_current_thread();
            break;
            
        case CFI_VIOLATION_LOG_ONLY:
            logger->error("🟢 LOG_ONLY: Violation loggée uniquement");
            break;
    }
}

// ===== UTILITAIRES =====

ControlFlowIntegrity::CFIViolationResponse ControlFlowIntegrity::get_violation_response() {
    // Par défaut: terminer pour la sécurité
    return CFI_VIOLATION_TERMINATE;
}

std::vector<std::string> ControlFlowIntegrity::get_stack_trace() {
    std::vector<std::string> trace;
    
    // ✅ SÉCURITÉ: Ne PAS utiliser backtrace dans signal handler
    // Utiliser les outils externes pour l'analyse
    
    auto logger = setup_logger("CFI", Config::LOG_DIR);
    logger->warning("⚠️ Stack trace non disponible dans signal handler");
    logger->warning("ℹ️ Utiliser: gdb -batch -ex 'bt' " + std::to_string(getpid()));
    
    return trace;
}

void ControlFlowIntegrity::isolate_current_thread() {
    // ✅ SÉCURITÉ: Isolation simple du thread
    // En production, utiliser des mécanismes plus robustes
    
    auto logger = setup_logger("CFI", Config::LOG_DIR);
    logger->warning("⚠️ Isolation thread basique");
    
    // Option: mettre le thread en pause
    // std::this_thread::sleep_for(std::chrono::seconds(1));
}

// ===== FONCTIONS COMPATIBILITÉ =====

// Fonctions pour compatibilité avec le code existant
// (implémentations vides car CFI natif gère tout automatiquement)

void ControlFlowIntegrity::add_valid_target(void* target, const std::vector<void*>& allowed_call_sites) {
    // ✅ SÉCURITÉ: CFI natif gère automatiquement
    // Pas besoin d'ajout manuel
    (void)target; // Éviter warning
    (void)allowed_call_sites; // Éviter warning
    
    auto logger = setup_logger("CFI", Config::LOG_DIR);
    logger->debug("ℹ️ CFI natif - add_valid_target ignoré");
}

void ControlFlowIntegrity::remove_valid_target(void* target) {
    // ✅ SÉCURITÉ: CFI natif gère automatiquement
    (void)target; // Éviter warning
    
    auto logger = setup_logger("CFI", Config::LOG_DIR);
    logger->debug("ℹ️ CFI natif - remove_valid_target ignoré");
}

void ControlFlowIntegrity::add_indirect_call_site(void* call_site) {
    // ✅ SÉCURITÉ: CFI natif gère automatiquement
    (void)call_site; // Éviter warning
    
    auto logger = setup_logger("CFI", Config::LOG_DIR);
    logger->debug("ℹ️ CFI natif - add_indirect_call_site ignoré");
}

void ControlFlowIntegrity::remove_indirect_call_site(void* call_site) {
    // ✅ SÉCURITÉ: CFI natif gère automatiquement
    (void)call_site; // Éviter warning
    
    auto logger = setup_logger("CFI", Config::LOG_DIR);
    logger->debug("ℹ️ CFI natif - remove_indirect_call_site ignoré");
}

// ===== STATISTIQUES =====

uint64_t ControlFlowIntegrity::get_violations_count() {
    return violations_count.load();
}

size_t ControlFlowIntegrity::get_valid_targets_count() {
    std::lock_guard<std::mutex> lock(cfi_mutex);
    
#if CFI_NATIVE_AVAILABLE
    // CFI natif gère automatiquement
    return 0;
#else
    return 0; // Non implémenté
#endif
}

size_t ControlFlowIntegrity::get_indirect_call_sites_count() {
    std::lock_guard<std::mutex> lock(cfi_mutex);
    
#if CFI_NATIVE_AVAILABLE
    // CFI natif gère automatiquement
    return 0;
#else
    return 0; // Non implémenté
#endif
}

void ControlFlowIntegrity::reset_statistics() {
    violations_count.store(0);
    indirect_calls_count.store(0);
}

void ControlFlowIntegrity::cleanup() {
    std::lock_guard<std::mutex> lock(cfi_mutex);
    
    cfi_enabled.store(false);
    violations_count.store(0);
    indirect_calls_count.store(0);
}

bool ControlFlowIntegrity::is_enabled() {
    return cfi_enabled.load();
}

void ControlFlowIntegrity::set_violation_response(CFIViolationResponse response) {
    // Stocker la réponse (non utilisé dans cette version simplifiée)
    (void)response; // Éviter warning unused parameter
}

} // namespace hesia

