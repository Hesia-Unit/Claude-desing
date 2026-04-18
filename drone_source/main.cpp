#include "drone_network.hpp"
#include "logger.hpp"
#include "config.hpp"
#include "sentinel_bridge.hpp"
#include "security_utils.hpp"
#include <iostream>
#include <string>
#include <cstdlib>
#ifndef _WIN32
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <cstring>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#endif

using namespace hesia;

#ifndef _WIN32
static bool check_allowlist_lockdown(std::string& reason) {
    const char* dir_path = "/etc/hesia/sentinel";
    const char* file_path = "/etc/hesia/sentinel/allowlist.conf";

    auto check_node = [&](const char* path, bool expect_dir) -> bool {
        struct stat st{};
        if (lstat(path, &st) != 0) {
            reason = std::string("cannot stat ") + path + ": " + std::strerror(errno);
            return false;
        }
        if (S_ISLNK(st.st_mode)) {
            reason = std::string("path must not be symlink: ") + path;
            return false;
        }
        if (expect_dir) {
            if (!S_ISDIR(st.st_mode)) {
                reason = std::string("not a directory: ") + path;
                return false;
            }
        } else {
            if (!S_ISREG(st.st_mode)) {
                reason = std::string("not a regular file: ") + path;
                return false;
            }
        }
        if (st.st_uid != 0) {
            reason = std::string("not owned by root: ") + path;
            return false;
        }
        if (st.st_mode & (S_IWGRP | S_IWOTH)) {
            reason = std::string("group/other writable: ") + path;
            return false;
        }
        return true;
    };

    if (!check_node(dir_path, true)) return false;
    if (!check_node(file_path, false)) return false;
    return true;
}

static bool drop_privileges(const char* user_name,
                            const char* group_name,
                            std::string& reason) {
    if (!user_name || !*user_name) {
        reason = "HESIA_DROP_USER not set";
        return false;
    }
    if (geteuid() != 0) {
        return true;
    }

    errno = 0;
    struct passwd* pw = getpwnam(user_name);
    if (!pw) {
        reason = std::string("unknown user: ") + user_name;
        return false;
    }

    gid_t gid = pw->pw_gid;
    if (group_name && *group_name) {
        struct group* gr = getgrnam(group_name);
        if (!gr) {
            reason = std::string("unknown group: ") + group_name;
            return false;
        }
        gid = gr->gr_gid;
    }

    if (initgroups(pw->pw_name, gid) != 0) {
        reason = std::string("initgroups failed: ") + std::strerror(errno);
        return false;
    }
    if (setgid(gid) != 0) {
        reason = std::string("setgid failed: ") + std::strerror(errno);
        return false;
    }
    if (setuid(pw->pw_uid) != 0) {
        reason = std::string("setuid failed: ") + std::strerror(errno);
        return false;
    }
    if (geteuid() == 0) {
        reason = "drop privileges failed";
        return false;
    }
    return true;
}

static void maybe_drop_privileges(const std::shared_ptr<Logger>& logger) {
    const char* drop_user = std::getenv("HESIA_DROP_USER");
    if (!drop_user || !*drop_user) {
        return;
    }
    const char* drop_group = std::getenv("HESIA_DROP_GROUP");
    std::string reason;
    if (!drop_privileges(drop_user, drop_group, reason)) {
        const std::string msg = std::string("Drop privileges failed: ") + reason;
        if (logger) {
            logger->error(msg);
        } else {
            std::cerr << msg << std::endl;
        }
        throw std::runtime_error(msg);
    }
    if (logger) {
        logger->info(std::string("Privileges dropped to user ") + drop_user);
    } else {
        std::cerr << "Privileges dropped to user " << drop_user << std::endl;
    }
}
#endif

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    try {
#ifndef _WIN32
        std::string allowlist_reason;
        if (!check_allowlist_lockdown(allowlist_reason)) {
            std::cerr << "Erreur: allowlist non verrouillee: " << allowlist_reason << std::endl;
            return 1;
        }
#endif
        int sentinel_rc = hesia::sentinel_check();
        if (sentinel_rc != 0) {
            std::cerr << "Erreur: Sentinel check failed (code " << sentinel_rc << ")" << std::endl;
            return sentinel_rc;
        }

        Config::init();
        auto logger = setup_logger("HESIA-DRONE", Config::LOG_DIR);
#ifndef _WIN32
        // Drop privileges before enabling seccomp to avoid blocked setgroups/setuid.
        maybe_drop_privileges(logger);
#endif
        RuntimeProtection::setup_protection();
        logger->info("Demarrage du drone HESIA");
        
        DroneNetworkClient client("DRONE_001");
        client.main();
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Erreur: " << e.what() << std::endl;
        return 1;
    }
}
