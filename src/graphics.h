#pragma once

#include <d3d11.h>
#include <toml.h>

// Plain, versioned counters also allow the local smoke check to inspect live behavior.
struct GraphicsStatus {
    unsigned version = 1;
    unsigned presentHooked = 0;
    unsigned samplerHooked = 0;
    unsigned vsync = 1;
    unsigned fpsLimit = 60;
    unsigned anisotropy = 16;
    unsigned width = 0;
    unsigned height = 0;
    volatile LONG64 frames = 0;
    volatile LONG64 samplerOverrides = 0;
    double measuredFps = 0;
    unsigned lastSyncInterval = 0;
    unsigned refreshRate = 0;
    unsigned lastPresentResult = 0;
    unsigned hookError = 0;
    unsigned softwareLimiter = 0;
    unsigned samplerFallbacks = 0;
};

extern "C" GraphicsStatus graphicsStatus;

void ConfigureGraphics(toml_table_t *root);
void ApplyGraphicsSwapChainOptions(DXGI_SWAP_CHAIN_DESC *description);
void AttachGraphicsHooks(IDXGISwapChain *chain, ID3D11Device *device);
