#pragma once

#include <cstdarg>

// UTF-8, append-only, in the executable directory. Each write is immediately
// visible to readers; the handle permits shared reading, writing and renaming.
void InitializeLog ();
void InstallRuntimeDiagnostics ();
void ShutdownLog ();
void LogV (const char *source, const char *level, const char *format, va_list arguments);
void Log (const char *source, const char *level, const char *format, ...);
void LogWide (const char *source, const char *level, const wchar_t *message);
