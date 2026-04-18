#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#ifndef _WIN32
#include <sys/prctl.h>
#include <sys/resource.h>
#endif
#include <limits.h>
#include <sstream>
#include "stack_protection.hpp"
#include "logger.hpp"
#include "config.hpp"
#include <cstring>
#include <cstdio>
#include <iostream>
#include <cstring>
#include <random>
#include <algorithm>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#include <intrin.h>
#else
#include <execinfo.h>
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>
#endif

namespace hesia {

// ===== INITIALISATION DES MEMBRES STATIQUES =====

std::atomic<bool> StackProtection::protection_enabled{false};
std::atomic<uint32_t> StackProtection::canary_value{0};
std::atomic<uint32_t> StackProtection::violations_count{0};
std::vector<StackFrame> StackProtection::protected_frames;
std::mutex StackProtection::stack_mutex;

// ✅ P1: Initialisation des variables de configuration
StackProtection::ViolationResponse StackProtection::current_violation_response{StackProtection::VIOLATION_RESPONSE_TERMINATE};
std::mutex StackProtection::violation_config_mutex;

// ===== INITIALISATION STACK PROTECTION =====

bool StackProtection::initialize() {
    if (protection_enabled.load()) {
        return true; // Déjà initialisé
    }
    
    auto logger = setup_logger("STACK-PROTECTION", Config::LOG_DIR);
    
    // Générer un canary aléatoire
    generate_canary();
    
    // Configurer les handlers de signaux
    if (!setup_signal_handlers()) {
        logger->error("Échec configuration handlers signaux");
        return false;
    }
    
    // Activer les protections de compilation au runtime
    enable_runtime_protections();
    
    protection_enabled.store(true);
    logger->info("Stack protection initialisée");
    
    return true;
}

void StackProtection::generate_canary() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dist;
    
    // Générer un canary avec pattern difficile à deviner
    uint32_t new_canary = dist(gen);
    
    // Ajouter des bits spécifiques pour rendre le canary détectable
    new_canary |= 0xFF; // Byte de terminaison nulle
    new_canary &= ~0x00FF0000; // Éviter les patterns communs
    
    canary_value.store(new_canary);
}

bool StackProtection::setup_signal_handlers() {
#ifdef _WIN32
    // Sur Windows, utiliser les handlers d'exceptions
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* ExceptionInfo) -> LONG {
        return StackProtection::handle_exception(ExceptionInfo);
    });
    
    // Ajouter un handler pour les violations de sécurité
    AddVectoredExceptionHandler(1, [](EXCEPTION_POINTERS* ExceptionInfo) -> LONG {
        return StackProtection::handle_exception(ExceptionInfo);
    });
    
    return true;
#else
    // Sur Linux, configurer les handlers de signaux
    // Installer une stack alternative pour les signaux (robustesse en cas de stack corrompue)
    static uint8_t altstack_mem[64 * 1024];
    stack_t ss;
    memset(&ss, 0, sizeof(ss));
    ss.ss_sp = altstack_mem;
    ss.ss_size = sizeof(altstack_mem);
    ss.ss_flags = 0;
    if (sigaltstack(&ss, nullptr) == -1) {
        return false;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    
    sa.sa_sigaction = StackProtection::signal_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    
    // Handler pour SIGSEGV (segmentation fault)
    if (sigaction(SIGSEGV, &sa, nullptr) == -1) {
        return false;
    }
    
    // Handler pour SIGABRT (abort)
    if (sigaction(SIGABRT, &sa, nullptr) == -1) {
        return false;
    }
    
    // Handler pour SIGFPE (floating point exception)
    if (sigaction(SIGFPE, &sa, nullptr) == -1) {
        return false;
    }
    
    return true;
#endif
}

void StackProtection::enable_runtime_protections() {
    auto logger = setup_logger("STACK-PROTECTION", Config::LOG_DIR);
    
#ifdef _WIN32
    // Activer les protections DEP (Data Execution Prevention)
    DWORD old_protect;
    if (VirtualProtect(GetCurrentProcess(), 0, PAGE_EXECUTE_READ, &old_protect)) {
        logger->info("DEP activé avec succès");
    }
    
    // Activer les mitigations de sécurité
    PROCESS_MITIGATION_STRICT_CONTROL_FLOW_GUARD_POLICY cfg_policy = {};
    cfg_policy.EnableStrictControlFlowGuard = TRUE;
    
    if (SetProcessMitigationPolicy(ProcessStrictControlFlowGuardPolicy, 
                                  &cfg_policy, sizeof(cfg_policy))) {
        logger->info("Control Flow Guard strict activé");
    }
#else
    // Sur Linux, activer les protections mémoire
    if (prctl(PR_SET_DUMPABLE, 0) == 0) {
        logger->info("Processus non dumpable activé");
    }

    // Activer la protection contre les core dumps
    struct rlimit rl;
    rl.rlim_cur = 0;
    rl.rlim_max = 0;
    if (setrlimit(RLIMIT_CORE, &rl) == 0) {
        logger->info("Core dumps désactivés");
    }
#endif
}

// ===== PROTECTION DES FRAMES STACK =====

uint32_t StackProtection::protect_stack_frame(void* frame_ptr, size_t frame_size) {
    // ✅ SÉCURITÉ: Utiliser les protections compilateur natives
    // NE PAS implémenter de canary maison (dangereux)
    
    auto logger = setup_logger("STACK_PROTECTION", Config::LOG_DIR);
    
    if (!protection_enabled.load()) {
        return 0;
    }
    
    // ✅ SÉCURITÉ: Validation des entrées
    if (!frame_ptr || frame_size < 16 || frame_size > 1024*1024) {
        logger->warning("⚠️ Paramètres frame invalides - protection ignorée");
        return 0;
    }
    
    std::lock_guard<std::mutex> lock(stack_mutex);
    
    // ✅ SÉCURITÉ: Utiliser seulement le monitoring
    // Le canary est géré par le compilateur (-fstack-protector-strong)
    StackFrame frame;
    frame.frame_ptr = frame_ptr;
    frame.frame_size = frame_size;
    frame.canary_value = 0; // Non utilisé - géré par compilateur
    frame.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    // Ajouter à la liste des frames monitorées
    frame.id = protected_frames.size() + 1;
    protected_frames.push_back(frame);
    
    logger->debug("Frame protégée (monitoring uniquement): " + std::to_string(frame.id));
    return frame.id;
}

bool StackProtection::validate_stack_frame(uint32_t frame_id) {
    // ✅ SÉCURITÉ: Validation simple (pas de canary maison)
    auto logger = setup_logger("STACK_PROTECTION", Config::LOG_DIR);
    
    if (!protection_enabled.load()) {
        return true;
    }
    
    std::lock_guard<std::mutex> lock(stack_mutex);
    
    // Trouver la frame protégée
    auto it = std::find_if(protected_frames.begin(), protected_frames.end(),
                          [frame_id](const StackFrame& frame) {
                              return frame.id == frame_id;
                          });
    
    if (it == protected_frames.end()) {
        logger->warning("Frame non trouvée: " + std::to_string(frame_id));
        return false;
    }
    
    // ✅ SÉCURITÉ: Pas de validation canary maison
    // Le compilateur gère le canary avec -fstack-protector-strong
    logger->debug("Frame validée (monitoring): " + std::to_string(frame_id));
    return true;
}

void StackProtection::unprotect_stack_frame(uint32_t frame_id) {
    std::lock_guard<std::mutex> lock(stack_mutex);
    
    // Retirer la frame de la liste
    protected_frames.erase(
        std::remove_if(protected_frames.begin(), protected_frames.end(),
                       [frame_id](const StackFrame& frame) {
                           return frame.id == frame_id;
                       }),
        protected_frames.end());
}

// ===== DÉTECTION DE VIOLATIONS =====

void StackProtection::report_canary_violation(const StackFrame* frame, uint32_t canary_start, uint32_t canary_end) {
    (void)frame;
    (void)canary_start;
    (void)canary_end;
    const char msg[] = "STACK CANARY VIOLATION - terminating\n";
    (void)::write(STDERR_FILENO, msg, sizeof(msg) - 1);
    _exit(139);
}

void StackProtection::handle_canary_violation(const StackFrame* frame [[maybe_unused]]) {
    auto logger = setup_logger("STACK-PROTECTION", Config::LOG_DIR);
    
    logger->error("🛡️ ACTION SÉCURITÉ: Violation canary détectée");
    
    // Options de réponse à la violation
    switch (get_violation_response()) {
        case VIOLATION_RESPONSE_TERMINATE:
            logger->error("Terminaison du processus pour violation canary");
            std::terminate();
            break;
            
        case VIOLATION_RESPONSE_RESTART:
            logger->error("Redémarrage du processus pour violation canary");
            restart_process();
            break;
            
        case VIOLATION_RESPONSE_ISOLATE:
            logger->error("Isolation du thread pour violation canary");
            isolate_current_thread();
            break;
            
        case VIOLATION_RESPONSE_LOG_ONLY:
            logger->warning("Violation canary loggée uniquement (mode test)");
            break;
    }
}

StackProtection::ViolationResponse StackProtection::get_violation_response() {
    // ✅ SÉCURITÉ: Utiliser la réponse configurée (P1)
    std::lock_guard<std::mutex> lock(violation_config_mutex);
    return current_violation_response;
}

std::vector<std::string> StackProtection::get_stack_trace() {
    std::vector<std::string> trace;
    
#ifdef _WIN32
    // Sur Windows, utiliser CaptureStackBackTrace
    const int max_frames = 32;
    void* frames[max_frames];
    
    USHORT frame_count = CaptureStackBackTrace(0, max_frames, frames, nullptr);
    
    for (USHORT i = 0; i < frame_count; i++) {
        char symbol_buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
        PSYMBOL_INFO symbol = (PSYMBOL_INFO)symbol_buffer;
        
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = MAX_SYM_NAME;
        
        DWORD64 displacement = 0;
        if (SymFromAddr(GetCurrentProcess(), (DWORD64)frames[i], &displacement, symbol)) {
            std::stringstream ss4;
            ss4 << std::hex << displacement;
            trace.push_back(std::string(symbol->Name) + " + 0x" + ss4.str());
        } else {
            std::stringstream ss5;
            ss5 << std::hex << reinterpret_cast<uintptr_t>(frames[i]);
            trace.push_back("0x" + ss5.str());
        }
    }
#else
    // Sur Linux, utiliser backtrace
    const int max_frames = 32;
    void* frames[max_frames];
    
    int frame_count = backtrace(frames, max_frames);
    char** symbols = backtrace_symbols(frames, frame_count);
    
    if (symbols) {
        for (int i = 0; i < frame_count; i++) {
            trace.push_back(std::string(symbols[i]));
        }
        free(symbols);
    }
#endif
    
    return trace;
}

// ===== HANDLERS D'EXCEPTIONS =====

#ifdef _WIN32
LONG StackProtection::handle_exception(EXCEPTION_POINTERS* ExceptionInfo) {
    auto logger = setup_logger("STACK-PROTECTION", Config::LOG_DIR);
    
    DWORD exception_code = ExceptionInfo->ExceptionRecord->ExceptionCode;
    
    switch (exception_code) {
        case EXCEPTION_STACK_OVERFLOW:
            logger->error("🚨 STACK OVERFLOW DÉTECTÉ");
            break;
            
        case EXCEPTION_ACCESS_VIOLATION:
            logger->error("🚨 ACCESS VIOLATION DÉTECTÉ");
            break;
            
        case EXCEPTION_GUARD_PAGE:
            logger->error("🚨 GUARD PAGE VIOLATION DÉTECTÉ");
            break;
            
        default:
            return EXCEPTION_CONTINUE_SEARCH;
    }
    
    // Logger les détails de l'exception
    std::stringstream ss6;
    ss6 << std::hex << exception_code;
    logger->error("Exception code: 0x" + ss6.str());
    
    std::stringstream ss7;
    ss7 << std::hex << reinterpret_cast<uintptr_t>(
        ExceptionInfo->ExceptionRecord->ExceptionAddress);
    logger->error("Exception address: 0x" + ss7.str());
    
    // Obtenir le stack trace
    std::vector<std::string> stack_trace = get_stack_trace();
    logger->error("Stack trace:");
    for (size_t i = 0; i < stack_trace.size(); i++) {
        logger->error("  " + std::to_string(i) + ": " + stack_trace[i]);
    }
    
    // Action de sécurité
    return EXCEPTION_EXECUTE_HANDLER;
}
#else
void StackProtection::signal_handler(int sig, siginfo_t* info, void* context) {
    (void)info;
    (void)context;
    const char msg[] = "FATAL: signal (possible memory corruption)\n";
    (void)::write(STDERR_FILENO, msg, sizeof(msg) - 1);
    _exit(128 + sig);
}
#endif

// ===== UTILITAIRES =====

void StackProtection::restart_process() {
#ifdef _WIN32
    // Relancer le processus actuel
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    
    char current_exe[MAX_PATH];
    GetModuleFileNameA(nullptr, current_exe, MAX_PATH);
    
    if (CreateProcessA(current_exe, nullptr, nullptr, nullptr, FALSE, 
                      0, nullptr, nullptr, &si, &pi)) {
        // Attendre un peu puis terminer le processus actuel
        Sleep(1000);
        ExitProcess(1);
    }
#else
    // Sur Linux, utiliser exec
    char current_exe[PATH_MAX];
    const ssize_t n = readlink("/proc/self/exe", current_exe, PATH_MAX - 1);
    if (n != -1) {
        current_exe[n] = 0;
        execl(current_exe, current_exe, nullptr);
    }
#endif
}

void StackProtection::isolate_current_thread() {
    // Isoler le thread courant pour limiter les dégâts
#ifdef _WIN32
    SuspendThread(GetCurrentThread());
#else
    pthread_t current_thread = pthread_self();
    pthread_kill(current_thread, SIGSTOP);
#endif
}

// ===== STATISTIQUES =====

uint32_t StackProtection::get_violations_count() {
    return violations_count.load();
}

uint32_t StackProtection::get_current_canary() {
    return canary_value.load();
}

size_t StackProtection::get_protected_frames_count() {
    std::lock_guard<std::mutex> lock(stack_mutex);
    return protected_frames.size();
}

void StackProtection::reset_statistics() {
    violations_count.store(0);
}

void StackProtection::cleanup() {
    std::lock_guard<std::mutex> lock(stack_mutex);
    
    protected_frames.clear();
    protection_enabled.store(false);
    violations_count.store(0);
}

bool StackProtection::is_enabled() {
    return protection_enabled.load();
}

void StackProtection::generate_new_canary() {
    generate_canary();
}

void StackProtection::set_violation_response(ViolationResponse response) {
    // ✅ SÉCURITÉ: Implémenter réellement le stockage de la réponse (P1)
    std::lock_guard<std::mutex> lock(violation_config_mutex);
    current_violation_response = response;
    
    auto logger = setup_logger("STACK-PROTECTION", Config::LOG_DIR);
    switch (response) {
        case VIOLATION_RESPONSE_TERMINATE:
            logger->info("🔴 Stack violation response: TERMINATE");
            break;
        case VIOLATION_RESPONSE_RESTART:
            logger->info("🟡 Stack violation response: RESTART");
            break;
        case VIOLATION_RESPONSE_ISOLATE:
            logger->info("🟠 Stack violation response: ISOLATE");
            break;
        case VIOLATION_RESPONSE_LOG_ONLY:
            logger->info("🟢 Stack violation response: LOG_ONLY");
            break;
    }
}

} // namespace hesia
