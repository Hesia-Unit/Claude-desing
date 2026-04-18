#include "sentinel_bridge.hpp"

#include <mutex>
#include <cstdlib>

#if defined(__GNUC__)
#define HESIA_WEAK __attribute__((weak))
#else
#define HESIA_WEAK
#endif

extern "C" {
void HESIA_WEAK adainit(void);
void HESIA_WEAK adafinal(void);
void HESIA_WEAK hesia_sentinel_init(void);
void HESIA_WEAK hesia_sentinel_finalize(void);
void HESIA_WEAK hesia_sentinel__init(void);
void HESIA_WEAK hesia_sentinel__final(void);
void HESIA_WEAK hesia_sentinelinit(void);
int hesia_sentinel_run(void);
}

namespace hesia {
namespace {
std::once_flag g_ada_once;
bool g_ada_ready = false;

void init_ada_runtime() {
    if (hesia_sentinel_init) {
        hesia_sentinel_init();
        if (hesia_sentinel_finalize) {
            std::atexit(hesia_sentinel_finalize);
        }
        g_ada_ready = true;
        return;
    }
    if (hesia_sentinel__init) {
        hesia_sentinel__init();
        if (hesia_sentinel__final) {
            std::atexit(hesia_sentinel__final);
        }
        g_ada_ready = true;
        return;
    }
    if (hesia_sentinelinit) {
        hesia_sentinelinit();
        g_ada_ready = true;
        return;
    }
    if (adainit) {
        adainit();
        if (adafinal) {
            std::atexit(adafinal);
        }
        g_ada_ready = true;
        return;
    }
}
}  // namespace

int sentinel_check() {
    std::call_once(g_ada_once, init_ada_runtime);
    if (!g_ada_ready) {
        return 10;
    }
    return hesia_sentinel_run();
}
}  // namespace hesia
