#include "logging.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <string>

int main () {
    SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    InitializeLog ();
    InstallRuntimeDiagnostics ();
    Log ("Smoke", "INFO", "Native logging handshake");
    OutputDebugStringA ("ANSI debug output handshake");
    OutputDebugStringW (L"Wide debug output handshake");
    std::printf ("Standard output handshake\n");
    std::fprintf (stderr, "Standard error handshake\n");
    std::fflush (stdout);
    std::fflush (stderr);
    const std::string syntheticIdentifier (20, '1');
    Log ("Smoke", "INFO", "accessCode=%s", syntheticIdentifier.c_str ());
    Log ("Smoke", "INFO", "%s", (std::string ("fixture") + "@" + "example.com").c_str ());
    if (std::getenv ("WAL_SMOKE_WAIT")) Sleep (30000);
    if (std::getenv ("WAL_SMOKE_CRASH"))
        RaiseException (EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    ShutdownLog ();
    return 37;
}
