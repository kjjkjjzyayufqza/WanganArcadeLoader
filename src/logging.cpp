#include "logging.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>

namespace {
HANDLE logFile = INVALID_HANDLE_VALUE;
SRWLOCK logLock = SRWLOCK_INIT;

struct PreserveLastError {
    DWORD value = GetLastError ();
    ~PreserveLastError () { SetLastError (value); }
};

void Write (const char *source, const char *level, const char *message, size_t length) {
    if (!length) return;
    SYSTEMTIME now{};
    GetLocalTime (&now);
    char prefix[192];
    int prefixLength = snprintf (prefix, sizeof (prefix),
        "%04u-%02u-%02u %02u:%02u:%02u.%03u [pid=%lu tid=%lu] [%s] [%s] ",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
        now.wMilliseconds, GetCurrentProcessId (), GetCurrentThreadId (), source, level);
    std::string entry;
    size_t start = 0;
    while (start < length) {
        size_t end = start;
        while (end < length && message[end] != '\n') ++end;
        size_t contentEnd = end;
        if (contentEnd > start && message[contentEnd - 1] == '\r') --contentEnd;
        entry.append (prefix, prefixLength);
        entry.append (message + start, contentEnd - start);
        entry.append ("\r\n");
        start = end + 1;
    }
    AcquireSRWLockExclusive (&logLock);
    if (logFile != INVALID_HANDLE_VALUE) {
        size_t offset = 0;
        while (offset < entry.size ()) {
            DWORD written = 0;
            DWORD count = static_cast<DWORD> ((entry.size () - offset > MAXDWORD) ? MAXDWORD : entry.size () - offset);
            if (!WriteFile (logFile, entry.data () + offset, count, &written, nullptr) || !written) break;
            offset += written;
        }
    }
    ReleaseSRWLockExclusive (&logLock);
}
}

void InitializeLog () {
    PreserveLastError preserve;
    std::vector<wchar_t> path (32768);
    DWORD length = GetModuleFileNameW (nullptr, path.data (), static_cast<DWORD> (path.size ()));
    if (!length || length >= path.size ()) return;
    std::wstring filename (path.data (), length);
    filename.resize (filename.find_last_of (L"\\/") + 1);
    filename += L"WAL.log";
    logFile = CreateFileW (filename.c_str (), FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (logFile == INVALID_HANDLE_VALUE) {
        OutputDebugStringW (L"WanganArcadeLoader: cannot open WAL.log in the game directory.\n");
        return;
    }
    Log ("Loader", "INFO", "========== Session started: WanganArcadeLoader 6RR ========== log_version=1");
    LogWide ("Loader", "INFO", path.data ());
}

void ShutdownLog () {
    PreserveLastError preserve;
    // ExitProcess may already have terminated a thread holding the lock.
    if (!TryAcquireSRWLockExclusive (&logLock)) return;
    if (logFile != INVALID_HANDLE_VALUE) {
        const char end[] = "========== Session ended ==========\r\n";
        DWORD written = 0;
        WriteFile (logFile, end, sizeof (end) - 1, &written, nullptr);
        CloseHandle (logFile);
        logFile = INVALID_HANDLE_VALUE;
    }
    ReleaseSRWLockExclusive (&logLock);
}

void LogV (const char *source, const char *level, const char *format, va_list arguments) {
    PreserveLastError preserve;
    va_list countArguments;
    va_copy (countArguments, arguments);
    int length = vsnprintf (nullptr, 0, format, countArguments);
    va_end (countArguments);
    if (length <= 0) return;
    std::vector<char> message (static_cast<size_t> (length) + 1);
    vsnprintf (message.data (), message.size (), format, arguments);
    Write (source, level, message.data (), static_cast<size_t> (length));
}

void Log (const char *source, const char *level, const char *format, ...) {
    va_list arguments;
    va_start (arguments, format);
    LogV (source, level, format, arguments);
    va_end (arguments);
}

void LogWide (const char *source, const char *level, const wchar_t *message) {
    PreserveLastError preserve;
    if (!message) return;
    int length = WideCharToMultiByte (CP_UTF8, 0, message, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1) return;
    std::vector<char> converted (length);
    if (WideCharToMultiByte (CP_UTF8, 0, message, -1, converted.data (), length, nullptr, nullptr))
        Write (source, level, converted.data (), static_cast<size_t> (length - 1));
}
