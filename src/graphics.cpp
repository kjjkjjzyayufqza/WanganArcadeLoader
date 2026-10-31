#include "helpers.h"
#include "graphics.h"

#include <algorithm>
#include <mutex>

extern HWND windowHandle;
extern "C" { GraphicsStatus graphicsStatus; }

namespace {
struct Options {
    bool enabled = true;
    bool vsync = true;
    bool windowed = true;
    bool borderless = true;
    bool diagnostics = true;
    unsigned fpsLimit = 60;
    unsigned anisotropy = 16;
} options;

using PresentFunction = HRESULT (STDMETHODCALLTYPE *)(IDXGISwapChain *, UINT, UINT);
using SamplerFunction = HRESULT (STDMETHODCALLTYPE *)(ID3D11Device *, const D3D11_SAMPLER_DESC *, ID3D11SamplerState **);
PresentFunction originalPresent = nullptr;
SamplerFunction originalSampler = nullptr;
IDXGISwapChain *mainChain = nullptr;
ID3D11Device *mainDevice = nullptr;
std::mutex installationMutex;
LARGE_INTEGER clockFrequency{};

void Log(const char *format, ...) {
    if (!options.diagnostics) return;
    va_list arguments;
    va_start(arguments, format);
    LogV("Graphics", "INFO", format, arguments);
    va_end(arguments);
}

bool BoolOption(toml_table_t *table, const char *key, bool fallback) {
    auto value = toml_bool_in(table, key);
    return value.ok ? value.u.b != 0 : fallback;
}

unsigned IntegerOption(toml_table_t *table, const char *key, unsigned fallback, unsigned maximum) {
    auto value = toml_int_in(table, key);
    if (!value.ok) return fallback;
    if (value.u.i < 0 || value.u.i > maximum) {
        Log("Ignoring invalid graphics.%s; expected 0..%u", key, maximum);
        return fallback;
    }
    return static_cast<unsigned>(value.u.i);
}

struct FrameLimiter {
    HANDLE timer = nullptr;
    LONGLONG nextFrame = 0;
    FrameLimiter() {
        timer = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002,
                                      TIMER_MODIFY_STATE | SYNCHRONIZE);
        if (!timer) timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    }
    ~FrameLimiter() { if (timer) CloseHandle(timer); }

    void Wait(unsigned fps) {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        const LONGLONG period = clockFrequency.QuadPart / fps;
        if (!nextFrame || now.QuadPart > nextFrame + period * 4) nextFrame = now.QuadPart;
        while (now.QuadPart < nextFrame) {
            const LONGLONG remaining = nextFrame - now.QuadPart;
            // Sleep most of the interval; leave a small margin to avoid missing the deadline.
            const LONGLONG margin = clockFrequency.QuadPart / 2000;
            if (remaining > margin * 2) {
                if (timer) {
                    LARGE_INTEGER due;
                    due.QuadPart = -(remaining - margin) * 10000000 / clockFrequency.QuadPart;
                    if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE))
                        WaitForSingleObject(timer, 100);
                    else Sleep(1);
                } else Sleep(1);
            } else YieldProcessor();
            QueryPerformanceCounter(&now);
        }
        nextFrame += period;
    }
};

HRESULT STDMETHODCALLTYPE Present(IDXGISwapChain *chain, UINT interval, UINT flags) {
    if (chain != mainChain) return originalPresent(chain, interval, flags);
    if (flags & DXGI_PRESENT_TEST) return originalPresent(chain, interval, flags);

    static thread_local FrameLimiter limiter;
    // At or above the monitor's refresh rate VSync is the limiter. Running both
    // clocks would risk missing a vblank and accidentally dropping to half refresh.
    const bool useLimiter = options.fpsLimit &&
        !(options.vsync && graphicsStatus.refreshRate && options.fpsLimit >= graphicsStatus.refreshRate);
    graphicsStatus.softwareLimiter = useLimiter;
    if (useLimiter) limiter.Wait(options.fpsLimit);
    else limiter.nextFrame = 0;

    interval = options.vsync ? 1 : 0;
    if (interval) flags &= ~0x00000200u; // DXGI_PRESENT_ALLOW_TEARING is invalid with VSync.
    graphicsStatus.lastSyncInterval = interval;
    const HRESULT result = originalPresent(chain, interval, flags);
    graphicsStatus.lastPresentResult = static_cast<unsigned>(result);
    if (result == DXGI_STATUS_OCCLUDED) {
        Sleep(50);
        limiter.nextFrame = 0;
        return result;
    }
    if (FAILED(result)) return result;

    InterlockedIncrement64(&graphicsStatus.frames);
    static thread_local LONGLONG sampleStart = 0;
    static thread_local unsigned sampleFrames = 0;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (!sampleStart) sampleStart = now.QuadPart;
    ++sampleFrames;
    const double seconds = double(now.QuadPart - sampleStart) / clockFrequency.QuadPart;
    if (seconds >= 2) {
        graphicsStatus.measuredFps = sampleFrames / seconds;
        Log("fps=%.3f vsync=%u cap=%u software_limiter=%u size=%ux%u af_overrides=%lld present=0x%08X",
            graphicsStatus.measuredFps, graphicsStatus.lastSyncInterval, options.fpsLimit,
            graphicsStatus.softwareLimiter, graphicsStatus.width, graphicsStatus.height,
            graphicsStatus.samplerOverrides, graphicsStatus.lastPresentResult);
        sampleStart = now.QuadPart;
        sampleFrames = 0;
    }
    return result;
}

HRESULT STDMETHODCALLTYPE CreateSampler(ID3D11Device *device, const D3D11_SAMPLER_DESC *description,
                                        ID3D11SamplerState **sampler) {
    if (device != mainDevice || !description || options.anisotropy < 2)
        return originalSampler(device, description, sampler);
    // Preserve point-filtered UI and comparison/shadow samplers.
    if (description->Filter != D3D11_FILTER_MIN_MAG_MIP_LINEAR &&
        description->Filter != D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT &&
        description->Filter != D3D11_FILTER_ANISOTROPIC)
        return originalSampler(device, description, sampler);
    auto replacement = *description;
    replacement.Filter = D3D11_FILTER_ANISOTROPIC;
    replacement.MaxAnisotropy = options.anisotropy;
    const HRESULT result = originalSampler(device, &replacement, sampler);
    if (SUCCEEDED(result)) {
        InterlockedIncrement64(&graphicsStatus.samplerOverrides);
        return result;
    }
    ++graphicsStatus.samplerFallbacks;
    return originalSampler(device, description, sampler);
}

bool Hook(void *address, void *replacement, void **original) {
    MH_STATUS status = MH_CreateHook(address, replacement, original);
    if (status == MH_OK) status = MH_EnableHook(address);
    if (status == MH_OK) return true;
    graphicsStatus.hookError = static_cast<unsigned>(status);
    Log("Graphics hook failed: %s", MH_StatusToString(status));
    return false;
}
}

void ConfigureGraphics(toml_table_t *root) {
    QueryPerformanceFrequency(&clockFrequency);
    if (root) {
        options.windowed = BoolOption(root, "windowed", true);
        if (auto table = toml_table_in(root, "graphics")) {
            options.enabled = BoolOption(table, "enabled", true);
            options.diagnostics = BoolOption(table, "diagnostics", true);
            options.vsync = BoolOption(table, "vsync", true);
            options.borderless = BoolOption(table, "borderless", true);
            options.fpsLimit = IntegerOption(table, "fps_limit", 60, 360);
            options.anisotropy = IntegerOption(table, "anisotropy", 16, 16);
        }
    }
    graphicsStatus.vsync = options.vsync;
    graphicsStatus.fpsLimit = options.fpsLimit;
    graphicsStatus.anisotropy = options.anisotropy;
    if (options.diagnostics) {
        Log("WAL graphics v1 enabled=%u vsync=%u fps_limit=%u anisotropy=%u borderless=%u",
            options.enabled, options.vsync, options.fpsLimit, options.anisotropy, options.borderless);
    }
}

void ApplyGraphicsSwapChainOptions(DXGI_SWAP_CHAIN_DESC *description) {
    if (description) description->Windowed = options.windowed;
}

void AttachGraphicsHooks(IDXGISwapChain *chain, ID3D11Device *device) {
    if (!options.enabled || !chain || !device) return;
    DXGI_SWAP_CHAIN_DESC description{};
    if (FAILED(chain->GetDesc(&description)) || description.OutputWindow != windowHandle) return;
    std::lock_guard<std::mutex> lock(installationMutex);
    if (mainChain) return;
    mainChain = chain;
    mainDevice = device;
    graphicsStatus.width = description.BufferDesc.Width;
    graphicsStatus.height = description.BufferDesc.Height;

    MONITORINFOEXW monitor{};
    monitor.cbSize = sizeof(monitor);
    if (GetMonitorInfoW(MonitorFromWindow(windowHandle, MONITOR_DEFAULTTONEAREST), &monitor)) {
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        if (EnumDisplaySettingsW(monitor.szDevice, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1)
            graphicsStatus.refreshRate = mode.dmDisplayFrequency;
        if (options.windowed && options.borderless) {
            LONG_PTR style = GetWindowLongPtrW(windowHandle, GWL_STYLE);
            style &= ~LONG_PTR(WS_OVERLAPPEDWINDOW);
            style |= WS_POPUP;
            SetWindowLongPtrW(windowHandle, GWL_STYLE, style);
            const int x = monitor.rcMonitor.left + std::max(0L, (monitor.rcMonitor.right - monitor.rcMonitor.left - LONG(description.BufferDesc.Width)) / 2);
            const int y = monitor.rcMonitor.top + std::max(0L, (monitor.rcMonitor.bottom - monitor.rcMonitor.top - LONG(description.BufferDesc.Height)) / 2);
            SetWindowPos(windowHandle, nullptr, x, y, description.BufferDesc.Width, description.BufferDesc.Height,
                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
    }
    auto chainMethods = *reinterpret_cast<void ***>(chain);
    auto deviceMethods = *reinterpret_cast<void ***>(device);
    graphicsStatus.presentHooked = Hook(chainMethods[8], reinterpret_cast<void *>(Present), reinterpret_cast<void **>(&originalPresent));
    graphicsStatus.samplerHooked = Hook(deviceMethods[23], reinterpret_cast<void *>(CreateSampler), reinterpret_cast<void **>(&originalSampler));
    Log("Hooks present=%u sampler=%u size=%ux%u refresh=%u Hz",
        graphicsStatus.presentHooked, graphicsStatus.samplerHooked, graphicsStatus.width,
        graphicsStatus.height, graphicsStatus.refreshRate);
}
