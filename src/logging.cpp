#include "logging.h"
#include "helpers.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

namespace {
HANDLE logFile = INVALID_HANDLE_VALUE;
SRWLOCK logLock = SRWLOCK_INIT;
std::atomic<LPTOP_LEVEL_EXCEPTION_FILTER> nextExceptionFilter{nullptr};
thread_local bool forwardingDebugOutput = false;

struct PreserveLastError {
    DWORD value = GetLastError ();
    ~PreserveLastError () { SetLastError (value); }
};

std::string Redact (const char *message, size_t length) {
    std::string text (message, length);
    auto lower = [] (std::string value) {
        std::transform (value.begin (), value.end (), value.begin (),
            [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
        return value;
    };
    for (const char *key : {"accesscode", "access_code", "chipid", "chip_id",
                          "pcb_serial", "password", "api_key", "token", "cookie", "authorization"}) {
        size_t at = 0;
        while ((at = lower (text).find (key, at)) != std::string::npos) {
            size_t value = at + std::strlen (key);
            while (value < text.size () && (text[value] == ' ' || text[value] == '\t' || text[value] == '"')) ++value;
            if (value >= text.size () || (text[value] != '=' && text[value] != ':')) { ++at; continue; }
            ++value;
            while (value < text.size () && (text[value] == ' ' || text[value] == '\t' || text[value] == '"' || text[value] == '\'')) ++value;
            size_t end = text.find_first_of ("\r\n\t ,;\"'", value);
            if (end == std::string::npos) end = text.size ();
            text.replace (value, end - value, "<redacted>");
            at = value + 10;
        }
    }
    for (size_t at = 0; at < text.size ();) {
        const unsigned char c = text[at];
        if (std::isalpha (c) && at + 2 < text.size () && text[at + 1] == ':' &&
            (text[at + 2] == '\\' || text[at + 2] == '/') &&
            (!at || !std::isalnum (static_cast<unsigned char> (text[at - 1])))) {
            size_t end = text.find_first_of ("\r\n\"'<>\t", at);
            if (end == std::string::npos) end = text.size ();
            text.replace (at, end - at, "<local-path>");
            at += 12;
        } else if (std::isdigit (c)) {
            size_t end = at;
            while (end < text.size () && std::isdigit (static_cast<unsigned char> (text[end]))) ++end;
            if (end - at >= 12) { text.replace (at, end - at, "<redacted-id>"); at += 13; }
            else at = end;
        } else if (c == '@') {
            size_t begin = at, end = at + 1;
            while (begin && (std::isalnum (static_cast<unsigned char> (text[begin - 1])) ||
                   std::strchr ("._%+-", text[begin - 1]))) --begin;
            while (end < text.size () && (std::isalnum (static_cast<unsigned char> (text[end])) ||
                   text[end] == '.' || text[end] == '-')) ++end;
            if (begin < at && text.substr (at + 1, end - at - 1).find ('.') != std::string::npos) {
                text.replace (begin, end - begin, "<redacted-email>");
                at = begin + 16;
            } else ++at;
        } else ++at;
    }
    return text;
}

void ExceptionRecord (const char *kind, EXCEPTION_POINTERS *exception) {
    // No allocation or logger lock: a fault may interrupt the normal writer.
    if (!exception || !exception->ExceptionRecord || logFile == INVALID_HANDLE_VALUE) return;
    PreserveLastError preserve;
    char entry[512];
    auto *record = exception->ExceptionRecord;
    HMODULE module = nullptr;
    GetModuleHandleExA (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR> (record->ExceptionAddress), &module);
    const auto offset = reinterpret_cast<uintptr_t> (record->ExceptionAddress) - reinterpret_cast<uintptr_t> (module);
    int count = snprintf (entry, sizeof (entry),
        "[Exception] [%s] code=0x%08lX ip=%p module_base=%p offset=0x%llX thread=%lu\r\n",
        kind, record->ExceptionCode, record->ExceptionAddress, module,
        static_cast<unsigned long long> (offset), GetCurrentThreadId ());
    if (count > 0) {
        DWORD written = 0;
        WriteFile (logFile, entry, static_cast<DWORD> (std::min<int> (count, sizeof (entry) - 1)), &written, nullptr);
    }
}

LONG CALLBACK ObserveException (EXCEPTION_POINTERS *exception) {
    static thread_local bool reporting = false;
    if (!exception || !exception->ExceptionRecord || reporting) return EXCEPTION_CONTINUE_SEARCH;
    const DWORD code = exception->ExceptionRecord->ExceptionCode;
    if (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION ||
        code == EXCEPTION_INT_DIVIDE_BY_ZERO || code == EXCEPTION_STACK_OVERFLOW ||
        code == 0xC0000374 || code == 0xC0000409 || code == 0xC0000420) {
        reporting = true;
        ExceptionRecord ("FIRST_CHANCE", exception);
        reporting = false;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI ReportUnhandledException (EXCEPTION_POINTERS *exception) {
    ExceptionRecord ("UNHANDLED", exception);
    if (logFile != INVALID_HANDLE_VALUE) FlushFileBuffers (logFile);
    auto next = nextExceptionFilter.load (std::memory_order_acquire);
    if (next && next != ReportUnhandledException) return next (exception);
    return EXCEPTION_CONTINUE_SEARCH;
}

HOOK (LPTOP_LEVEL_EXCEPTION_FILTER, SetExceptionFilter,
      PROC_ADDRESS ("kernel32.dll", "SetUnhandledExceptionFilter"), LPTOP_LEVEL_EXCEPTION_FILTER filter) {
    auto previous = nextExceptionFilter.load (std::memory_order_acquire);
    if (filter != ReportUnhandledException)
        previous = nextExceptionFilter.exchange (filter, std::memory_order_acq_rel);
    originalSetExceptionFilter (ReportUnhandledException);
    return previous;
}

HOOK (void, DebugOutputA, PROC_ADDRESS ("kernel32.dll", "OutputDebugStringA"), LPCSTR message) {
    PreserveLastError preserve;
    const bool nested = forwardingDebugOutput;
    forwardingDebugOutput = true;
    if (!nested && message) Log ("DebugOutput", "INFO", "%s", message);
    originalDebugOutputA (message);
    forwardingDebugOutput = nested;
}

HOOK (void, DebugOutputW, PROC_ADDRESS ("kernel32.dll", "OutputDebugStringW"), LPCWSTR message) {
    PreserveLastError preserve;
    const bool nested = forwardingDebugOutput;
    forwardingDebugOutput = true;
    if (!nested && message) LogWide ("DebugOutput", "INFO", message);
    originalDebugOutputW (message);
    forwardingDebugOutput = nested;
}

void Write (const char *source, const char *level, const char *message, size_t length) {
    if (!length) return;
    const std::string sanitized = Redact (message, length);
    message = sanitized.data ();
    length = sanitized.size ();
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
}

void InstallRuntimeDiagnostics () {
    if (logFile == INVALID_HANDLE_VALUE) return;
    ULONG reserve = 64 * 1024;
    SetThreadStackGuarantee (&reserve);
    AddVectoredExceptionHandler (0, ObserveException);
    nextExceptionFilter.store (SetUnhandledExceptionFilter (ReportUnhandledException), std::memory_order_release);
    INSTALL_HOOK (SetExceptionFilter);
    INSTALL_HOOK (DebugOutputA);
    INSTALL_HOOK (DebugOutputW);
    Log ("Loader", "INFO", "Runtime diagnostics enabled: native debug output, fatal exceptions and unhandled exceptions");
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
