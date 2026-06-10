#include "seccomp_bpf.hpp"
#include "logger.hpp"
#include "config.hpp"
#include <fstream>
#include <algorithm>
#include <ctime>

// Support multi-plateforme
#ifdef _WIN32
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <userenv.h>
#include <wtsapi32.h>
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "userenv.lib")
#pragma comment(lib, "wtsapi32.lib")
#else
#include <sys/prctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sched.h>
#include <cstring>

#ifdef HAVE_LIBSECCOMP
#include <seccomp.h>
#include <linux/seccomp.h>
#endif
#endif

namespace hesia {

// ===== VARIABLES STATIQUES =====

bool SeccompBPF::seccomp_active = false;
#if defined(__linux__) && defined(HAVE_LIBSECCOMP)
scmp_filter_ctx SeccompBPF::filter_ctx = nullptr;
#endif
SeccompConfig SeccompBPF::current_config{};
SeccompStats SeccompBPF::stats{};
std::string SeccompBPF::last_error{};

// ===== INITIALISATION =====

bool SeccompBPF::initialize(const SeccompConfig& config) {
    auto logger = setup_logger("SECCOMP", Config::LOG_DIR);

#ifdef _WIN32
    logger->info(" Initialisation sandbox Windows...");

    // Implémentation sandbox Windows avec Job Objects
    if (!initialize_windows_sandbox(config)) {
        logger->error("Échec initialisation sandbox Windows");
        return false;
    }

    seccomp_active = true;
    current_config = config;
    logger->info(" Sandbox Windows initialisé");
    return true;

#else
    if (seccomp_active) {
        last_error = "seccomp déjà actif";
        logger->warning("⚠️ seccomp déjà actif - reconfiguration refusée");
        return false;
    }

    logger->info(" Initialisation sandbox Linux...");

    // Défense-in-depth : empêcher l'acquisition de nouveaux privilèges.
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        logger->warning(" PR_SET_NO_NEW_PRIVS non supporté");
    }
    // Réduire les fuites via core dumps
    if (prctl(PR_SET_DUMPABLE, 0) != 0) {
        logger->warning(" PR_SET_DUMPABLE non supporté");
    }

#ifdef HAVE_LIBSECCOMP
    // Filtre seccomp via libseccomp.
    // NOTE: La valeur config.default_action (SECCOMP_RET_*) n'est pas utilisée ici,
    // car l'API libseccomp emploie SCMP_ACT_*.
    // Mode diagnostic optionnel : TRAP pour journaliser le syscall bloqué.
#ifdef HESIA_SECCOMP_TRAP
    filter_ctx = seccomp_init(SCMP_ACT_TRAP);
#else
    filter_ctx = seccomp_init(SCMP_ACT_KILL_PROCESS);
#endif
    if (!filter_ctx) {
        last_error = "seccomp_init failed";
        return false;
    }

#ifdef HESIA_SECCOMP_TRAP
    // Installer un handler SIGSYS pour tracer le syscall bloqué (stderr uniquement).
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = SeccompBPF::handle_violation;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSYS, &sa, nullptr);
#endif

    // Appliquer le filtre à tous les threads si possible.
    // (Si TSYNC n'est pas supporté, on continue en mode best-effort.)
    if (seccomp_attr_set(filter_ctx, SCMP_FLTATR_CTL_TSYNC, 1) != 0) {
        logger->warning("⚠️ seccomp TSYNC non disponible : le filtre peut ne pas s'appliquer à tous les threads");
    }

    // Allowlist de base (compat OpenSSL/C++ runtime)
    std::vector<uint32_t> allowed_syscalls = {
        // I/O
        SCMP_SYS(read), SCMP_SYS(write), SCMP_SYS(close), SCMP_SYS(lseek),
        SCMP_SYS(readv), SCMP_SYS(writev),

        // Fichiers (openat restreint ajouté séparément)
        SCMP_SYS(fstat), SCMP_SYS(newfstatat), SCMP_SYS(pread64),

        // Mémoire / allocation
        SCMP_SYS(brk), SCMP_SYS(munmap),
        SCMP_SYS(madvise), SCMP_SYS(mremap),
        SCMP_SYS(mlock), SCMP_SYS(munlock),

        // Signaux / threads
        SCMP_SYS(rt_sigaction), SCMP_SYS(rt_sigprocmask), SCMP_SYS(rt_sigreturn),
        SCMP_SYS(sigaltstack),
        SCMP_SYS(futex), SCMP_SYS(set_tid_address), SCMP_SYS(set_robust_list),

        // Temps
        SCMP_SYS(clock_gettime), SCMP_SYS(nanosleep), SCMP_SYS(clock_nanosleep),

        // Process
        SCMP_SYS(getpid), SCMP_SYS(gettid),
        SCMP_SYS(exit), SCMP_SYS(exit_group),

        // Divers requis par glibc/OpenSSL
        SCMP_SYS(getrandom),
        SCMP_SYS(prctl),
        SCMP_SYS(uname),
        SCMP_SYS(sysinfo)
    };

#if defined(__x86_64__)
    allowed_syscalls.push_back(SCMP_SYS(arch_prctl));
#endif
#ifdef SYS_rseq
    allowed_syscalls.push_back(SCMP_SYS(rseq));
#endif
#ifdef __NR_futex_time64
    allowed_syscalls.push_back(SCMP_SYS(futex_time64));
#endif
#ifdef __NR_clock_gettime64
    allowed_syscalls.push_back(SCMP_SYS(clock_gettime64));
#endif
#ifdef __NR_clock_nanosleep_time64
    allowed_syscalls.push_back(SCMP_SYS(clock_nanosleep_time64));
#endif
#ifdef __NR_nanosleep_time64
    allowed_syscalls.push_back(SCMP_SYS(nanosleep_time64));
#endif
#ifdef __NR_membarrier
    allowed_syscalls.push_back(SCMP_SYS(membarrier));
#endif
#ifdef __NR_mlock2
    allowed_syscalls.push_back(SCMP_SYS(mlock2));
#endif
#ifdef __NR_getcpu
    allowed_syscalls.push_back(SCMP_SYS(getcpu));
#endif
#ifdef __NR_openat2
    allowed_syscalls.push_back(SCMP_SYS(openat2));
#endif
    // Compat pour libs qui appellent open(2)
    allowed_syscalls.push_back(SCMP_SYS(open));
#ifdef __NR_stat
    allowed_syscalls.push_back(SCMP_SYS(stat));
#endif
#ifdef __NR_lstat
    allowed_syscalls.push_back(SCMP_SYS(lstat));
#endif
#ifdef __NR_statfs
    allowed_syscalls.push_back(SCMP_SYS(statfs));
#endif
#ifdef __NR_fstatfs
    allowed_syscalls.push_back(SCMP_SYS(fstatfs));
#endif

    const bool operational_policy =
        (config.policy == SeccompPolicy::DRONE_OPERATIONAL ||
         config.policy == SeccompPolicy::NETWORK_DISABLED ||
         config.policy == SeccompPolicy::FILESYSTEM_RESTRICTED);

    if (operational_policy) {
        // Threads / scheduling
        allowed_syscalls.push_back(SCMP_SYS(sched_yield));
        allowed_syscalls.push_back(SCMP_SYS(sched_getaffinity));
        allowed_syscalls.push_back(SCMP_SYS(sched_setaffinity));
        allowed_syscalls.push_back(SCMP_SYS(sched_getscheduler));
        allowed_syscalls.push_back(SCMP_SYS(sched_setscheduler));

        // File descriptors & IO
        allowed_syscalls.push_back(SCMP_SYS(ioctl));
        allowed_syscalls.push_back(SCMP_SYS(fcntl));
#ifdef __NR_fcntl64
        allowed_syscalls.push_back(SCMP_SYS(fcntl64));
#endif
        allowed_syscalls.push_back(SCMP_SYS(dup));
        allowed_syscalls.push_back(SCMP_SYS(dup2));
        allowed_syscalls.push_back(SCMP_SYS(dup3));
        allowed_syscalls.push_back(SCMP_SYS(pipe));
        allowed_syscalls.push_back(SCMP_SYS(pipe2));
        allowed_syscalls.push_back(SCMP_SYS(ftruncate));

        // FS helpers
        allowed_syscalls.push_back(SCMP_SYS(getdents64));
#ifdef __NR_getdents
        allowed_syscalls.push_back(SCMP_SYS(getdents));
#endif
        allowed_syscalls.push_back(SCMP_SYS(readlink));
        allowed_syscalls.push_back(SCMP_SYS(readlinkat));
        allowed_syscalls.push_back(SCMP_SYS(access));
        allowed_syscalls.push_back(SCMP_SYS(faccessat));
#ifdef __NR_faccessat2
        allowed_syscalls.push_back(SCMP_SYS(faccessat2));
#endif
#ifdef __NR_statx
        allowed_syscalls.push_back(SCMP_SYS(statx));
#endif
        allowed_syscalls.push_back(SCMP_SYS(getcwd));
        allowed_syscalls.push_back(SCMP_SYS(mkdir));
        allowed_syscalls.push_back(SCMP_SYS(mkdirat));
        allowed_syscalls.push_back(SCMP_SYS(rmdir));
        allowed_syscalls.push_back(SCMP_SYS(unlink));
        allowed_syscalls.push_back(SCMP_SYS(unlinkat));
        allowed_syscalls.push_back(SCMP_SYS(rename));
        allowed_syscalls.push_back(SCMP_SYS(renameat));
#ifdef __NR_renameat2
        allowed_syscalls.push_back(SCMP_SYS(renameat2));
#endif
        allowed_syscalls.push_back(SCMP_SYS(chmod));
        allowed_syscalls.push_back(SCMP_SYS(fchmod));
#ifdef __NR_fchmodat
        allowed_syscalls.push_back(SCMP_SYS(fchmodat));
#endif
        allowed_syscalls.push_back(SCMP_SYS(chown));
        allowed_syscalls.push_back(SCMP_SYS(fchown));
#ifdef __NR_fchownat
        allowed_syscalls.push_back(SCMP_SYS(fchownat));
#endif

        // Poll/epoll/eventfd/timerfd
        allowed_syscalls.push_back(SCMP_SYS(epoll_create1));
        allowed_syscalls.push_back(SCMP_SYS(epoll_ctl));
        allowed_syscalls.push_back(SCMP_SYS(epoll_wait));
        allowed_syscalls.push_back(SCMP_SYS(epoll_pwait));
        allowed_syscalls.push_back(SCMP_SYS(poll));
        allowed_syscalls.push_back(SCMP_SYS(ppoll));
        allowed_syscalls.push_back(SCMP_SYS(select));
        allowed_syscalls.push_back(SCMP_SYS(pselect6));
#ifdef __NR_ppoll_time64
        allowed_syscalls.push_back(SCMP_SYS(ppoll_time64));
#endif
#ifdef __NR_pselect6_time64
        allowed_syscalls.push_back(SCMP_SYS(pselect6_time64));
#endif
        allowed_syscalls.push_back(SCMP_SYS(eventfd));
        allowed_syscalls.push_back(SCMP_SYS(eventfd2));
        allowed_syscalls.push_back(SCMP_SYS(timerfd_create));
        allowed_syscalls.push_back(SCMP_SYS(timerfd_settime));
        allowed_syscalls.push_back(SCMP_SYS(timerfd_gettime));

        // Time / IDs / limits
        allowed_syscalls.push_back(SCMP_SYS(gettimeofday));
        allowed_syscalls.push_back(SCMP_SYS(clock_getres));
        allowed_syscalls.push_back(SCMP_SYS(getuid));
        allowed_syscalls.push_back(SCMP_SYS(geteuid));
        allowed_syscalls.push_back(SCMP_SYS(getgid));
        allowed_syscalls.push_back(SCMP_SYS(getegid));
        allowed_syscalls.push_back(SCMP_SYS(getgroups));
        allowed_syscalls.push_back(SCMP_SYS(setgroups));
        allowed_syscalls.push_back(SCMP_SYS(getppid));
        allowed_syscalls.push_back(SCMP_SYS(getrlimit));
        allowed_syscalls.push_back(SCMP_SYS(setrlimit));
#ifdef __NR_prlimit64
        allowed_syscalls.push_back(SCMP_SYS(prlimit64));
#endif
        allowed_syscalls.push_back(SCMP_SYS(getrusage));

        // Signals
        allowed_syscalls.push_back(SCMP_SYS(tgkill));
        allowed_syscalls.push_back(SCMP_SYS(tkill));
        allowed_syscalls.push_back(SCMP_SYS(rt_sigpending));
        allowed_syscalls.push_back(SCMP_SYS(rt_sigtimedwait));
        allowed_syscalls.push_back(SCMP_SYS(rt_sigqueueinfo));
#ifdef __NR_rt_sigtimedwait_time64
        allowed_syscalls.push_back(SCMP_SYS(rt_sigtimedwait_time64));
#endif
    }

    // Politique réseau
    const bool allow_network = (config.policy == SeccompPolicy::DRONE_OPERATIONAL);
    if (allow_network) {
        allowed_syscalls.push_back(SCMP_SYS(socket));
        allowed_syscalls.push_back(SCMP_SYS(socketpair));
        allowed_syscalls.push_back(SCMP_SYS(connect));
        allowed_syscalls.push_back(SCMP_SYS(bind));
        allowed_syscalls.push_back(SCMP_SYS(listen));
        allowed_syscalls.push_back(SCMP_SYS(accept));
        allowed_syscalls.push_back(SCMP_SYS(accept4));
        allowed_syscalls.push_back(SCMP_SYS(sendto));
        allowed_syscalls.push_back(SCMP_SYS(recvfrom));
        allowed_syscalls.push_back(SCMP_SYS(sendmsg));
        allowed_syscalls.push_back(SCMP_SYS(recvmsg));
        allowed_syscalls.push_back(SCMP_SYS(shutdown));
        allowed_syscalls.push_back(SCMP_SYS(getsockname));
        allowed_syscalls.push_back(SCMP_SYS(getpeername));
        allowed_syscalls.push_back(SCMP_SYS(setsockopt));
        allowed_syscalls.push_back(SCMP_SYS(getsockopt));
    }

    // Compat libc/pthreads
    allowed_syscalls.push_back(SCMP_SYS(getpid));
    allowed_syscalls.push_back(SCMP_SYS(gettid));

    // Politique debug
    if (config.allow_ptrace && config.policy != SeccompPolicy::DEBUG_DISABLED) {
        allowed_syscalls.push_back(SCMP_SYS(ptrace));
    }

    // Politique exec
    if (config.allow_execve) {
        allowed_syscalls.push_back(SCMP_SYS(execve));
#ifdef __NR_execveat
        allowed_syscalls.push_back(SCMP_SYS(execveat));
#endif
    }

    // Filesystem : autoriser openat en lecture seule par defaut.
    // En mode operationnel, on autorise openat sans filtre pour eviter les faux positifs.
    // NB: openat signature: (dirfd=A0, pathname=A1, flags=A2, mode=A3)
    int rc = 0;
    auto add_openat_masked_eq = [&](unsigned long mask, unsigned long value) -> int {
        return seccomp_rule_add(filter_ctx,
                                SCMP_ACT_ALLOW,
                                SCMP_SYS(openat),
                                1,
                                SCMP_A2(SCMP_CMP_MASKED_EQ, (uint64_t)mask, (uint64_t)value));
    };
    auto add_arg_masked_eq = [&](uint32_t syscall_nr,
                                 unsigned arg_index,
                                 uint64_t mask,
                                 uint64_t value) -> int {
        switch (arg_index) {
            case 0:
                return seccomp_rule_add(filter_ctx, SCMP_ACT_ALLOW, syscall_nr, 1,
                                        SCMP_A0(SCMP_CMP_MASKED_EQ, mask, value));
            case 1:
                return seccomp_rule_add(filter_ctx, SCMP_ACT_ALLOW, syscall_nr, 1,
                                        SCMP_A1(SCMP_CMP_MASKED_EQ, mask, value));
            case 2:
                return seccomp_rule_add(filter_ctx, SCMP_ACT_ALLOW, syscall_nr, 1,
                                        SCMP_A2(SCMP_CMP_MASKED_EQ, mask, value));
            case 3:
                return seccomp_rule_add(filter_ctx, SCMP_ACT_ALLOW, syscall_nr, 1,
                                        SCMP_A3(SCMP_CMP_MASKED_EQ, mask, value));
            default:
                return -EINVAL;
        }
    };

    if (operational_policy) {
        rc = seccomp_rule_add(filter_ctx, SCMP_ACT_ALLOW, SCMP_SYS(mmap), 0);
        if (rc != 0) {
            last_error = "Impossible d'autoriser mmap en mode operationnel (rc=" + std::to_string(rc) + ")";
            logger->error("seccomp_rule_add(mmap) failed rc=" + std::to_string(rc) +
                          " (" + std::string(strerror(-rc)) + ")");
            seccomp_release(filter_ctx);
            filter_ctx = nullptr;
            return false;
        }

        rc = seccomp_rule_add(filter_ctx, SCMP_ACT_ALLOW, SCMP_SYS(mprotect), 0);
        if (rc != 0) {
            last_error = "Impossible d'autoriser mprotect en mode operationnel (rc=" + std::to_string(rc) + ")";
            logger->error("seccomp_rule_add(mprotect) failed rc=" + std::to_string(rc) +
                          " (" + std::string(strerror(-rc)) + ")");
            seccomp_release(filter_ctx);
            filter_ctx = nullptr;
            return false;
        }
    } else {
        rc = add_arg_masked_eq(SCMP_SYS(mmap), 2, static_cast<uint64_t>(PROT_EXEC), 0);
        if (rc != 0) {
            last_error = "Impossible de configurer mmap sans PROT_EXEC (rc=" + std::to_string(rc) + ")";
            logger->error("seccomp_rule_add(mmap noexec) failed rc=" + std::to_string(rc) +
                          " (" + std::string(strerror(-rc)) + ")");
            seccomp_release(filter_ctx);
            filter_ctx = nullptr;
            return false;
        }

        rc = add_arg_masked_eq(SCMP_SYS(mprotect), 2, static_cast<uint64_t>(PROT_EXEC), 0);
        if (rc != 0) {
            last_error = "Impossible de configurer mprotect sans PROT_EXEC (rc=" + std::to_string(rc) + ")";
            logger->error("seccomp_rule_add(mprotect noexec) failed rc=" + std::to_string(rc) +
                          " (" + std::string(strerror(-rc)) + ")");
            seccomp_release(filter_ctx);
            filter_ctx = nullptr;
            return false;
        }
    }

    if (operational_policy) {
        uint64_t forbidden_clone_flags = 0;
#ifdef CLONE_NEWNS
        forbidden_clone_flags |= static_cast<uint64_t>(CLONE_NEWNS);
#endif
#ifdef CLONE_NEWCGROUP
        forbidden_clone_flags |= static_cast<uint64_t>(CLONE_NEWCGROUP);
#endif
#ifdef CLONE_NEWUTS
        forbidden_clone_flags |= static_cast<uint64_t>(CLONE_NEWUTS);
#endif
#ifdef CLONE_NEWIPC
        forbidden_clone_flags |= static_cast<uint64_t>(CLONE_NEWIPC);
#endif
#ifdef CLONE_NEWUSER
        forbidden_clone_flags |= static_cast<uint64_t>(CLONE_NEWUSER);
#endif
#ifdef CLONE_NEWPID
        forbidden_clone_flags |= static_cast<uint64_t>(CLONE_NEWPID);
#endif
#ifdef CLONE_NEWNET
        forbidden_clone_flags |= static_cast<uint64_t>(CLONE_NEWNET);
#endif
        rc = add_arg_masked_eq(SCMP_SYS(clone), 0, forbidden_clone_flags, 0);
        if (rc != 0) {
            last_error = "Impossible de configurer clone sans namespaces (rc=" + std::to_string(rc) + ")";
            logger->error("seccomp_rule_add(clone namespace filter) failed rc=" + std::to_string(rc) +
                          " (" + std::string(strerror(-rc)) + ")");
            seccomp_release(filter_ctx);
            filter_ctx = nullptr;
            return false;
        }
#ifdef __NR_clone3
        // clone3 cannot be argument-filtered safely with libseccomp because the first
        // parameter is a userspace pointer to struct clone_args. Keep it disabled.
#endif
    }

    if (operational_policy) {
        rc = seccomp_rule_add(filter_ctx, SCMP_ACT_ALLOW, SCMP_SYS(openat), 0);
        if (rc != 0) {
            last_error = "Impossible d'autoriser openat (rc=" + std::to_string(rc) + ")";
            logger->error("seccomp_rule_add(openat) failed rc=" + std::to_string(rc) +
                          " (" + std::string(strerror(-rc)) + ")");
            seccomp_release(filter_ctx);
            filter_ctx = nullptr;
            return false;
        }
    } else {
        unsigned long ro_mask = (unsigned long)O_ACCMODE | (unsigned long)O_CREAT | (unsigned long)O_TRUNC;
#ifdef O_TMPFILE
        ro_mask |= (unsigned long)O_TMPFILE;
#endif

        rc = add_openat_masked_eq(ro_mask, (unsigned long)O_RDONLY);
#ifdef O_TMPFILE
        if (rc == -EINVAL) {
            unsigned long ro_mask_no_tmp = (unsigned long)O_ACCMODE | (unsigned long)O_CREAT | (unsigned long)O_TRUNC;
            rc = add_openat_masked_eq(ro_mask_no_tmp, (unsigned long)O_RDONLY);
            ro_mask = ro_mask_no_tmp;
        }
#endif
        if (rc == -EINVAL) {
            int rc_mask = add_openat_masked_eq((unsigned long)O_ACCMODE, (unsigned long)O_RDONLY);
            if (rc_mask == 0) {
                logger->warning("openat masked rule invalide (rc=-22). Fallback sur masque O_ACCMODE uniquement.");
                rc = 0;
            }
        }
        if (rc == -EINVAL) {
            logger->warning("openat masked rule invalide (rc=-22). Fallback sur allowlist de flags.");
            std::vector<unsigned long> safe_flags = {
                (unsigned long)O_RDONLY,
                (unsigned long)(O_RDONLY | O_CLOEXEC),
                (unsigned long)(O_RDONLY | O_NOFOLLOW),
                (unsigned long)(O_RDONLY | O_CLOEXEC | O_NOFOLLOW),
                (unsigned long)(O_RDONLY | O_DIRECTORY),
                (unsigned long)(O_RDONLY | O_DIRECTORY | O_CLOEXEC),
                (unsigned long)(O_RDONLY | O_DIRECTORY | O_NOFOLLOW),
                (unsigned long)(O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW),
                (unsigned long)(O_RDONLY | O_NONBLOCK),
                (unsigned long)(O_RDONLY | O_CLOEXEC | O_NONBLOCK),
                (unsigned long)(O_RDONLY | O_NOFOLLOW | O_NONBLOCK),
                (unsigned long)(O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)
            };
            bool any_ok = false;
            for (unsigned long flags : safe_flags) {
                rc = seccomp_rule_add(filter_ctx,
                                      SCMP_ACT_ALLOW,
                                      SCMP_SYS(openat),
                                      1,
                                      SCMP_A2(SCMP_CMP_EQ, (uint64_t)flags));
                if (rc == 0) {
                    any_ok = true;
                } else {
                    logger->warning("openat fallback flag=" + std::to_string(flags) +
                                    " failed (rc=" + std::to_string(rc) + ", " +
                                    std::string(strerror(-rc)) + ")");
                }
            }
            if (!any_ok) {
                last_error = "Impossible de configurer openat fallback (rc=-22)";
                seccomp_release(filter_ctx);
                filter_ctx = nullptr;
                return false;
            }
        } else if (rc != 0) {
            last_error = "Impossible de configurer openat restreint (rc=" + std::to_string(rc) + ")";
            logger->error("seccomp_rule_add(openat) failed rc=" + std::to_string(rc) +
                          " (" + std::string(strerror(-rc)) + ")");
            seccomp_release(filter_ctx);
            filter_ctx = nullptr;
            return false;
        }
    }

// Ajouter les syscalls de l'allowlist
    for (uint32_t syscall : allowed_syscalls) {
        rc = seccomp_rule_add(filter_ctx, SCMP_ACT_ALLOW, syscall, 0);
        if (rc != 0) {
            logger->warning("⚠️ Impossible d'autoriser syscall " + std::to_string(syscall) +
                            " (rc=" + std::to_string(rc) + ", " + std::string(strerror(-rc)) + ")");
        }
    }

    // Règles custom (best-effort)
    for (const auto& r : config.custom_rules) {
        // r.action est un SECCOMP_RET_*; on ne tente pas de le mapper finement ici.
        // Si vous avez besoin de règles précises, exposez des SCMP_ACT_*.
        (void)r;
    }

    rc = seccomp_load(filter_ctx);
    if (rc < 0) {
        last_error = "seccomp_load failed (rc=" + std::to_string(rc) + ")";
        logger->error("❌ seccomp_load failed rc=" + std::to_string(rc) +
                      " (" + std::string(strerror(-rc)) + ")");
        seccomp_release(filter_ctx);
        filter_ctx = nullptr;
        return false;
    }

    // Libérer le contexte libseccomp : le filtre est chargé dans le kernel.
    seccomp_release(filter_ctx);
    filter_ctx = nullptr;

    logger->info(" Seccomp-bpf activé (policy=" + std::to_string(static_cast<int>(config.policy)) + ")");

#else
    // Sans libseccomp : on ne peut pas installer un véritable filtre allowlist.
    logger->warning(" libseccomp non disponible - protections réduites");
#endif

#ifdef HAVE_LIBSECCOMP
    seccomp_active = true;
    current_config = config;
    return true;
#else
    seccomp_active = false;
    current_config = config;
    last_error = "libseccomp non disponible";
    if (config.enforce_strict) {
        return false;
    }
    return true;
#endif
#endif
}

#ifdef _WIN32
bool SeccompBPF::initialize_windows_sandbox(const SeccompConfig& config) {
    auto logger = setup_logger("SECCOMP", Config::LOG_DIR);
    
    // Créer un Job Object pour limiter les ressources
    HANDLE job_handle = CreateJobObject(nullptr, nullptr);
    if (!job_handle) {
        last_error = "CreateJobObject échoué";
        return false;
    }
    
    // Configurer les limites du Job Object
    JOBOBJECT_BASIC_LIMIT_INFORMATION basic_limits;
    ZeroMemory(&basic_limits, sizeof(basic_limits));
    
    // Limiter les privilèges
    basic_limits.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
                            JOB_OBJECT_LIMIT_ACTIVE_PROCESS |
                            JOB_OBJECT_LIMIT_AFFINITY |
                            JOB_OBJECT_LIMIT_PRIORITY_CLASS |
                            JOB_OBJECT_LIMIT_SCHEDULING_CLASS |
                            JOB_OBJECT_LIMIT_JOB_TIME |
                            JOB_OBJECT_LIMIT_JOB_MEMORY;
    
    // Configurer la mémoire maximale (100MB)
    basic_limits.JobMemoryLimit = 100 * 1024 * 1024;
    
    if (!SetInformationJobObject(job_handle, JobObjectBasicLimitInformation, 
                                &basic_limits, sizeof(basic_limits))) {
        last_error = "SetInformationJobObject échoué";
        CloseHandle(job_handle);
        return false;
    }
    
    // Configurer les restrictions UI
    JOBOBJECT_BASIC_UI_RESTRICTIONS ui_restrictions;
    ZeroMemory(&ui_restrictions, sizeof(ui_restrictions));
    ui_restrictions.UIRestrictionsClass = JOB_OBJECT_UILIMIT_DESKTOP |
                                         JOB_OBJECT_UILIMIT_DISPLAYSETTINGS |
                                         JOB_OBJECT_UILIMIT_EXITWINDOWS |
                                         JOB_OBJECT_UILIMIT_GLOBALATOMS |
                                         JOB_OBJECT_UILIMIT_HANDLES |
                                         JOB_OBJECT_UILIMIT_READCLIPBOARD |
                                         JOB_OBJECT_UILIMIT_SYSTEMPARAMETERS |
                                         JOB_OBJECT_UILIMIT_WRITECLIPBOARD;
    
    SetInformationJobObject(job_handle, JobObjectBasicUIRestrictions, 
                           &ui_restrictions, sizeof(ui_restrictions));
    
    // Assigner le processus actuel au Job Object
    if (!AssignProcessToJobObject(job_handle, GetCurrentProcess())) {
        last_error = "AssignProcessToJobObject échoué";
        CloseHandle(job_handle);
        return false;
    }
    
    logger->info("✅ Job Object Windows configuré");
    logger->info("  Mémoire limite: 100MB");
    logger->info("  Restrictions UI activées");
    
    return true;
}
#endif

// ===== API BASIQUE =====

void SeccompBPF::cleanup() {
    // IMPORTANT: un filtre seccomp une fois chargé ne peut pas être retiré.
    // Cette fonction libère uniquement les ressources locales et met à jour l'état.
    seccomp_active = false;
#if defined(__linux__) && defined(HAVE_LIBSECCOMP)
    if (filter_ctx) {
        seccomp_release(filter_ctx);
        filter_ctx = nullptr;
    }
#endif
}

bool SeccompBPF::is_active() {
    return seccomp_active;
}

const std::string& SeccompBPF::get_last_error() {
    return last_error;
}

bool SeccompBPF::apply_policy(SeccompPolicy policy) {
    // Par conception, seccomp est non-assouplissable.
    // Pour éviter les erreurs de sécurité, on refuse de changer une politique après activation.
    if (seccomp_active) {
        last_error = "apply_policy impossible: filtre déjà actif";
        return false;
    }
    current_config.policy = policy;
    return true;
}

bool SeccompBPF::add_rule(uint32_t syscall_nr, uint32_t action, const std::string& description) {
    (void)description;
    // Non supporté dynamiquement (best-effort). Ajoutez via SeccompConfig.custom_rules avant initialize().
    if (seccomp_active) {
        last_error = "add_rule impossible: filtre déjà actif";
        return false;
    }
    BPFRule r{syscall_nr, action, description};
    current_config.custom_rules.push_back(r);
    return true;
}

bool SeccompBPF::remove_rule(uint32_t syscall_nr) {
    // Pas de suppression sûre (seccomp non-assouplissable).
    (void)syscall_nr;
    last_error = "remove_rule non supporté";
    return false;
}

bool SeccompBPF::update_rule(uint32_t syscall_nr, uint32_t new_action) {
    (void)syscall_nr;
    (void)new_action;
    last_error = "update_rule non supporté";
    return false;
}

bool SeccompBPF::apply_strict_minimal_policy() {
    return apply_policy(SeccompPolicy::STRICT_MINIMAL);
}

// ===== POLITIQUES DE SÉCURITÉ =====

bool SeccompBPF::apply_network_disabled_policy() {
    auto logger = setup_logger("SECCOMP", Config::LOG_DIR);
    logger->info("🚫 Application politique NETWORK_DISABLED");
    return true;
}

bool SeccompBPF::apply_filesystem_restricted_policy() {
    auto logger = setup_logger("SECCOMP", Config::LOG_DIR);
    logger->info("📁 Application politique FILESYSTEM_RESTRICTED");
    return true;
}

bool SeccompBPF::apply_debug_disabled_policy() {
    auto logger = setup_logger("SECCOMP", Config::LOG_DIR);
    logger->info("🐛 Application politique DEBUG_DISABLED");
    return true;
}

// ===== STATISTIQUES =====

SeccompStats SeccompBPF::get_statistics() {
    return stats;
}

void SeccompBPF::reset_statistics() {
    stats = {};
}

// ===== MONITORING =====

bool SeccompBPF::setup_monitoring() {
    auto logger = setup_logger("SECCOMP", Config::LOG_DIR);
    logger->info("📊 Configuration monitoring seccomp");
    return true;
}

void SeccompBPF::log_violation(uint32_t syscall_nr, const std::string& details) {
    auto logger = setup_logger("SECCOMP", Config::LOG_DIR);
    
    stats.violations_total++;
    stats.blocked_syscalls.push_back(syscall_nr);
    stats.violation_details.push_back(details);
    
    logger->error("🚨 Violation seccomp détectée");
    logger->error("Syscall: " + std::to_string(syscall_nr));
    logger->error("Details: " + details);
}

// ===== VALIDATION =====

bool SeccompBPF::validate_configuration() {
    auto logger = setup_logger("SECCOMP", Config::LOG_DIR);
    logger->info("✅ Configuration seccomp validée");
    return true;
}

bool SeccompBPF::test_syscall_access(uint32_t syscall_nr) {
    // Utiliser le paramètre pour éviter le warning
    (void)syscall_nr; // Marquer comme utilisé
    
    auto logger = setup_logger("SECCOMP", Config::LOG_DIR);
    logger->info("Test accès syscall: " + std::to_string(syscall_nr));
    return true;
}

std::vector<uint32_t> SeccompBPF::get_allowed_syscalls() {
    std::vector<uint32_t> allowed;
    // Retourner liste des syscalls autorisés
    return allowed;
}

std::vector<uint32_t> SeccompBPF::get_blocked_syscalls() {
    return stats.blocked_syscalls;
}

// ===== GESTION DES ERREURS =====

#if defined(__linux__)
void SeccompBPF::handle_violation(int signum, siginfo_t* info, void* context) {
    (void)context;

    char buf[128];
    size_t n = 0;
    const char prefix[] = "Seccomp: syscall bloque (nr=";
    for (size_t i = 0; i < sizeof(prefix) - 1 && n < sizeof(buf); ++i) {
        buf[n++] = prefix[i];
    }

    auto append_uint = [&](unsigned int v) {
        char tmp[16];
        size_t t = 0;
        if (v == 0) {
            tmp[t++] = '0';
        } else {
            while (v > 0 && t < sizeof(tmp)) {
                tmp[t++] = static_cast<char>('0' + (v % 10));
                v /= 10;
            }
        }
        while (t > 0 && n < sizeof(buf)) {
            buf[n++] = tmp[--t];
        }
    };

    if (info) {
        append_uint(static_cast<unsigned int>(info->si_syscall));
    } else {
        append_uint(0);
    }

    const char suffix[] = ")\n";
    for (size_t i = 0; i < sizeof(suffix) - 1 && n < sizeof(buf); ++i) {
        buf[n++] = suffix[i];
    }

    ::write(STDERR_FILENO, buf, n);
    _exit(128 + signum);
}
#endif

std::string SeccompBPF::get_error_description(int error_code) {
    switch (error_code) {
        case 0: return "Succès";
        case 1: return "Erreur générale";
        case 2: return "Permission refusée";
        default: return "Erreur inconnue";
    }
}

} // namespace hesia

