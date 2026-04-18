#ifndef SECCOMP_BPF_HPP
#define SECCOMP_BPF_HPP

#include <vector>
#include <cstdint>
#include <string>

// signal.h est généralement disponible même sur Windows (MSVC), mais siginfo_t
// n'est pas portable : on ne l'utilise que sur Linux.
#include <signal.h>

#if defined(__linux__)
#include <sys/syscall.h>
#include <linux/seccomp.h>
#include <linux/filter.h>

// Include seccomp library si disponible
#ifdef HAVE_LIBSECCOMP
#include <seccomp.h>
#endif
#endif

namespace hesia {

// Politiques seccomp prédéfinies
enum class SeccompPolicy {
    STRICT_MINIMAL = 0,      // Seulement les syscalls essentiels
    DRONE_OPERATIONAL = 1,   // Syscalls nécessaires pour drone
    NETWORK_DISABLED = 2,    // Pas de syscalls réseau
    FILESYSTEM_RESTRICTED = 3, // Filesystem limité
    DEBUG_DISABLED = 4       // Pas de syscalls de debug
};

// Types de filtres BPF
struct BPFRule {
    uint32_t syscall_nr;
    uint32_t action;  // SECCOMP_RET_ALLOW, KILL, TRAP, etc.
    std::string description;
};

// Configuration seccomp
struct SeccompConfig {
    SeccompPolicy policy;
    std::vector<BPFRule> custom_rules;
    bool enforce_strict;
    uint32_t default_action;
    bool allow_ptrace;
    bool allow_execve;
    std::string log_file;
};

// Statistiques seccomp
struct SeccompStats {
    uint64_t syscalls_allowed;
    uint64_t syscalls_blocked;
    uint64_t violations_total;
    std::vector<uint32_t> blocked_syscalls;
    std::vector<std::string> violation_details;
};

class SeccompBPF {
private:
    static bool seccomp_active;
#if defined(__linux__) && defined(HAVE_LIBSECCOMP)
    static scmp_filter_ctx filter_ctx;
#endif
    static SeccompConfig current_config;
    static SeccompStats stats;
    static std::string last_error;

    // Méthodes internes
    static bool install_filter();
    static void log_syscall(uint32_t syscall_nr, const char* action);
    static const char* get_syscall_name(uint32_t nr);
    static bool is_syscall_allowed(uint32_t syscall_nr);
    static void setup_default_rules(SeccompPolicy policy);
    static bool add_custom_rule(const BPFRule& rule);
    static void cleanup_filter();

public:
    // Initialisation et configuration
    static bool initialize(const SeccompConfig& config);
    static void cleanup();
    static bool is_active();
    static const std::string& get_last_error();

    // Gestion des politiques
    static bool apply_policy(SeccompPolicy policy);
    static bool add_rule(uint32_t syscall_nr, uint32_t action,
                         const std::string& description = "");
    static bool remove_rule(uint32_t syscall_nr);
    static bool update_rule(uint32_t syscall_nr, uint32_t new_action);

    // Politiques prédéfinies
    static bool apply_strict_minimal_policy();
    static bool apply_drone_operational_policy();
    static bool apply_network_disabled_policy();
    static bool apply_filesystem_restricted_policy();
    static bool apply_debug_disabled_policy();

    // Validation et test
    static bool validate_configuration();
    static bool test_syscall_access(uint32_t syscall_nr);
    static std::vector<uint32_t> get_allowed_syscalls();
    static std::vector<uint32_t> get_blocked_syscalls();

    // Monitoring et statistiques
    static bool setup_monitoring();
    static void log_violation(uint32_t syscall_nr, const std::string& details);
    static SeccompStats get_statistics();
    static void reset_statistics();

    // Gestion des erreurs
#if defined(__linux__)
    static void handle_violation(int signum, siginfo_t* info, void* context);
#endif
    static std::string get_error_description(int error_code);

#ifdef _WIN32
    // Support Windows
    static bool initialize_windows_sandbox(const SeccompConfig& config);
#endif
};

} // namespace hesia

#endif // SECCOMP_BPF_HPP
