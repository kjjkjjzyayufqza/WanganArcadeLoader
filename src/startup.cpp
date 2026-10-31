#include "startup.h"
#include "helpers.h"

#include <atomic>
#include <cstring>

namespace {
bool ignoreOfflineTimeLimit = true;
bool offlineMode = true;
bool fastBoot = true;

HOOK (bool, HasOfflineUptimeExpired, ASLR (0x1407312D0), void *self) {
    static std::atomic_flag logged = ATOMIC_FLAG_INIT;
    if (!logged.test_and_set (std::memory_order_relaxed))
        Log ("Startup", "INFO", "Offline uptime limit bypassed; E0552 predicate returned false");
    return false;
}

HOOK (void *, CreateStartupViewReference, ASLR (0x1404A5A10), void **self) {
    // This base constructor owns only a shared pointer to the diagnostic view.
    // Its derived constructor still starts native initialization. Native update
    // and destruction already handle an empty reference, so no live renderer is
    // torn down while asynchronous initialization is using it.
    self[0] = nullptr;
    self[1] = nullptr;
    Log ("Startup", "INFO", "Fast startup: initial diagnostic view omitted; native initialization continues");
    return self;
}

HOOK (bool, UpdateInitialLogo, ASLR (0x1403D79E0), void *self, unsigned char *context) {
    static bool skipped = false;
    if (skipped || !context) return originalUpdateInitialLogo (self, context);
    auto *logo = *reinterpret_cast<void **> (context + 0x168);
    if (!logo) return originalUpdateInitialLogo (self, context);
    // Preserve the native close-and-transition sequence and skip only its first
    // animation wait. All construction and readiness work remains native.
    skipped = true;
    using CloseLogo = void (*) (void *);
    auto table = *reinterpret_cast<void ***>(logo);
    reinterpret_cast<CloseLogo> (table[0xA0 / sizeof (void *)]) (logo);
    using Transition = void (*) (void *, void *);
    reinterpret_cast<Transition> (ASLR (0x1402D1980)) (context + 8, context + 0x78);
    Log ("Startup", "INFO", "Fast startup: initial logo animation skipped through native transition");
    return false;
}
}

void ConfigureStartup (toml_table_t *config, bool offline) {
    offlineMode = offline;
    if (config) {
        ignoreOfflineTimeLimit = readConfigBool (config, "ignoreOfflineTimeLimit", true);
        fastBoot = readConfigBool (config, "fastBoot", true);
    }
}

void InstallStartupHooks () {
    if (offlineMode && ignoreOfflineTimeLimit) {
        // This non-leaf predicate compares time() with the last offline timestamp.
        // Preserve the clock, startup state table, and every other error check.
        const unsigned char expected[] = {
            0x40, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x83, 0xB9,
            0xD0, 0x07, 0x00, 0x00, 0x03, 0x48, 0x8B, 0xF1,
        };
        if (std::memcmp (reinterpret_cast<void *> (ASLR (0x1407312D0)), expected, sizeof (expected)) != 0) {
            Log ("Startup", "ERROR", "Offline uptime hook signature mismatch; leaving native check intact");
            return;
        }
        INSTALL_HOOK (HasOfflineUptimeExpired);
    }
    if (fastBoot) {
        const unsigned char menuSignature[] = {
            0x48, 0x89, 0x4C, 0x24, 0x08, 0x53, 0x57, 0x48,
            0x83, 0xEC, 0x38, 0x48, 0xC7, 0x44, 0x24, 0x20,
        };
        const unsigned char logoSignature[] = {
            0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
            0x8A, 0x68, 0x01, 0x00, 0x00, 0x48, 0x8B, 0xDA,
        };
        const unsigned char warningModeSignature[] = {0x0F, 0x45, 0xD9};
        if (std::memcmp (reinterpret_cast<void *> (ASLR (0x1404A5A10)), menuSignature, sizeof (menuSignature)) != 0 ||
            std::memcmp (reinterpret_cast<void *> (ASLR (0x1403C8373)), warningModeSignature, sizeof (warningModeSignature)) != 0 ||
            std::memcmp (reinterpret_cast<void *> (ASLR (0x1403D79E0)), logoSignature, sizeof (logoSignature)) != 0) {
            Log ("Startup", "ERROR", "Fast startup hook signature mismatch; leaving native sequence intact");
            return;
        }
        INSTALL_HOOK (CreateStartupViewReference);
        INSTALL_HOOK (UpdateInitialLogo);
        // Select the native logo_anime_mode flag for the initial presentation.
        WRITE_MEMORY (ASLR (0x1403C8374), unsigned char, 0x44);
    }
}
