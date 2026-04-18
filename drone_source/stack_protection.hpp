#ifndef STACK_PROTECTION_HPP
#define STACK_PROTECTION_HPP

#include <vector>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <signal.h>
#include <ucontext.h>
#endif

namespace hesia {

// Stack Protection avec canaries et détection d'overflow
// Implémentation complète de protection de pile

struct StackFrame {
    uint32_t id;
    void* frame_ptr;
    size_t frame_size;
    uint32_t canary_value;
    uint64_t timestamp;
};

class StackProtection {
public:
    // Types de réponse aux violations
    enum ViolationResponse {
        VIOLATION_RESPONSE_TERMINATE = 0,
        VIOLATION_RESPONSE_RESTART = 1,
        VIOLATION_RESPONSE_ISOLATE = 2,
        VIOLATION_RESPONSE_LOG_ONLY = 3
    };
    
private:
    static std::atomic<bool> protection_enabled;
    static std::atomic<uint32_t> canary_value;
    static std::atomic<uint32_t> violations_count;
    static std::vector<StackFrame> protected_frames;
    static std::mutex stack_mutex;
    
    // ✅ P1: Variables pour configuration des violations
    static ViolationResponse current_violation_response;
    static std::mutex violation_config_mutex;
    
    // Fonctions internes
    static void generate_canary();
    static bool setup_signal_handlers();
    static void enable_runtime_protections();
    static void report_canary_violation(const StackFrame* frame, 
                                       uint32_t canary_start, 
                                       uint32_t canary_end);
    static void handle_canary_violation(const StackFrame* frame);
    static std::vector<std::string> get_stack_trace();
    static void restart_process();
    static void isolate_current_thread();
    
#ifdef _WIN32
    static LONG handle_exception(EXCEPTION_POINTERS* ExceptionInfo);
#else
    static void signal_handler(int sig, siginfo_t* info, void* context);
#endif
    
public:
    static bool initialize();
    static void cleanup();
    static bool is_enabled();
    
    // Gestion des canaries
    static void generate_new_canary();
    static uint32_t get_current_canary();
    
    // Protection des frames stack
    static uint32_t protect_stack_frame(void* frame_ptr, size_t frame_size);
    static bool validate_stack_frame(uint32_t frame_id);
    static void unprotect_stack_frame(uint32_t frame_id);
    
    // Détection de violations
    static ViolationResponse get_violation_response();
    static void set_violation_response(ViolationResponse response);
    
    // Statistiques
    static uint32_t get_violations_count();
    static size_t get_protected_frames_count();
    static void reset_statistics();
    
    // Macros pour protection automatique
    #define PROTECT_STACK_FRAME(size) \
        uint32_t __canary_id = StackProtection::protect_stack_frame(__builtin_frame_address(0), size)
    
    #define VALIDATE_STACK_FRAME() \
        StackProtection::validate_stack_frame(__canary_id)
    
    #define UNPROTECT_STACK_FRAME() \
        StackProtection::unprotect_stack_frame(__canary_id)
};

} // namespace hesia

#endif // STACK_PROTECTION_HPP
