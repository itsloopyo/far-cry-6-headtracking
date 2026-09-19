// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "orientation_dot.h"
#include "logging.h"
#include "mod.h"

#include "cameraunlock/hooks/hook_manager.h"
#define CAMERAUNLOCK_DX12_OVERLAY_IMPLEMENTATION
#include "cameraunlock/rendering/dx12_overlay.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>

namespace FarCry6HeadTracking {
namespace {

using cameraunlock::hooks::HookManager;
using cameraunlock::hooks::HookStatus;
using cameraunlock::rendering::DX12DrawContext;
using cameraunlock::rendering::DX12Overlay;

using VisibilityFn = void (*)(void*);
using PositionFn = void (*)(void*, float, void*);
using DestroyFn = void (*)(void*);
VisibilityFn g_visibility = nullptr;
PositionFn g_position = nullptr;
DestroyFn g_destroy = nullptr;
using Present1Fn = HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT,
                                                const DXGI_PRESENT_PARAMETERS*);
Present1Fn g_present1 = nullptr;

constexpr ULONGLONG kFreshMs = 200;

// Screen positions are centred and normalised: x right, y down, the frame spanning -1..1.
struct DotState {
    const void* reticle = nullptr;
    bool stock_visible = false;
    float native_x = 0.0f;
    float native_y = 0.0f;
    ULONGLONG native_stamp = 0;
    float projected_x = 0.0f;
    float projected_y = 0.0f;
    ULONGLONG projected_stamp = 0;
    ULONGLONG gameplay_stamp = 0;
};
std::mutex g_mutex;
DotState g_dot;

void HookedVisibility(void* reticle) {
    g_visibility(reticle);
    const bool visible = *(static_cast<const uint8_t*>(reticle) + 0x14) != 0;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_dot.reticle != reticle || g_dot.stock_visible != visible) {
        Log::Line("Stock reticle %s", visible ? "visible" : "hidden");
    }
    g_dot.reticle = reticle;
    g_dot.stock_visible = visible;
}

// The weapon's own reticle position, which already carries the native extended view
// compensation. It is only updated while a weapon is out.
void HookedPosition(void* weapon, float dt, void* player) {
    g_position(weapon, dt, player);
    float position[2];
    std::memcpy(position, static_cast<const uint8_t*>(weapon) + 0xb74, sizeof(position));
    std::lock_guard<std::mutex> lock(g_mutex);
    g_dot.native_x = position[0];
    g_dot.native_y = position[1];
    g_dot.native_stamp = GetTickCount64();
}

void HookedDestroy(void* reticle) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_dot.reticle == reticle) g_dot.reticle = nullptr;
    }
    g_destroy(reticle);
}

void DrawDot(DX12DrawContext& dc) {
    DotState dot;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        dot = g_dot;
    }
    const auto now = GetTickCount64();
    if (dot.stock_visible || !Mod::Instance().TrackingAllowed() ||
        now - dot.gameplay_stamp > kFreshMs) {
        return;
    }
    float nx;
    float ny;
    if (now - dot.native_stamp <= kFreshMs) {
        nx = dot.native_x;
        ny = dot.native_y;
    } else if (now - dot.projected_stamp <= kFreshMs) {
        nx = dot.projected_x;
        ny = dot.projected_y;
    } else {
        return;
    }
    if (!std::isfinite(nx) || !std::isfinite(ny) || std::abs(nx) > 1.0f || std::abs(ny) > 1.0f) {
        return;
    }

    const float x = (nx * 0.5f + 0.5f) * dc.Width();
    const float y = (ny * 0.5f + 0.5f) * dc.Height();
    dc.DrawDot(x, y, 6.0f, 0x20000000);
    dc.DrawDot(x, y, 5.0f, 0x20ffffff);
    dc.DrawDot(x, y, 3.5f, 0x60ffffff);
    dc.DrawDot(x, y, 2.0f, 0xa0ffffff);
}

HRESULT STDMETHODCALLTYPE HookedPresent1(IDXGISwapChain1* swap, UINT sync, UINT flags,
                                         const DXGI_PRESENT_PARAMETERS* parameters) {
    if (flags & DXGI_PRESENT_TEST) return g_present1(swap, sync, flags, parameters);
    namespace backend = cameraunlock::rendering::detail12;
    auto& state = backend::State();
    if (!state.firstPresentLogged) {
        state.firstPresentLogged = true;
        Log::Line("Orientation dot: Present1 hook fired");
    }
    if (!state.initialized) backend::InitDeviceResources(swap);
    if (state.initialized) backend::RenderFrame(swap);
    return g_present1(swap, sync, flags, parameters);
}

void InstallOverlay() {
    cameraunlock::rendering::SetDX12OverlayLogger([](const char* message) {
        Log::Line("%s", message);
    });
    void** swapVtable = nullptr;
    void** queueVtable = nullptr;
    if (!cameraunlock::rendering::detail12::GetVTables(swapVtable, queueVtable)) {
        Log::Line("ERROR: orientation dot could not locate Present1");
        return;
    }
    // Present hooks outlive shutdown callbacks, so the overlay stays mapped with the mod.
    auto* overlay = new DX12Overlay();
    overlay->SetRenderCallback(DrawDot);
    if (!overlay->Install()) {
        Log::Line("ERROR: orientation dot overlay installation failed");
        return;
    }
    auto& hooks = HookManager::Instance();
    // The game presents through IDXGISwapChain1, bypassing core's Present hook.
    auto status = hooks.CreateHook(swapVtable[22], reinterpret_cast<void*>(&HookedPresent1),
                                   reinterpret_cast<void**>(&g_present1));
    if (status == HookStatus::Ok) status = hooks.EnableHook(swapVtable[22]);
    if (status != HookStatus::Ok) {
        Log::Line("ERROR: orientation dot Present1 hook: %s", HookStatusToString(status));
        return;
    }
    Log::Line("Orientation dot active: shown whenever the stock reticle is not");
}

// The overlay is installed on the first gameplay frame, not at startup. Its vtable
// probe creates a D3D12 device, and run while the game was still creating its own,
// that probe alone (no hooks, nothing drawn) left the Steam build rendering
// posterised with blank UI textures for the rest of the session.
std::atomic<bool> g_overlayStarted{false};

}  // namespace

void NoteOrientationDotGameplay() {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_dot.gameplay_stamp = GetTickCount64();
    }
    if (!g_overlayStarted.exchange(true, std::memory_order_relaxed)) {
        std::thread(InstallOverlay).detach();
    }
}

void NoteOrientationDotProjection(float x, float y) {
    DotState dot;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_dot.projected_x = x;
        g_dot.projected_y = y;
        g_dot.projected_stamp = GetTickCount64();
        dot = g_dot;
    }
    // Where both exist, the mod's own projection is checked against the game's.
    static ULONGLONG lastLog = 0;
    const ULONGLONG now = dot.projected_stamp;
    if (now - dot.native_stamp <= kFreshMs && now - lastLog > 5000) {
        lastLog = now;
        Log::Line("Dot check: native %.4f,%.4f projected %.4f,%.4f", dot.native_x, dot.native_y,
                  x, y);
    }
}

bool OrientationDotWeaponOut() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return GetTickCount64() - g_dot.native_stamp <= kFreshMs;
}

bool StartOrientationDot(uintptr_t module, const Offsets& offsets) {
    struct Hook { NativeFunction target; void* detour; void** original; };
    const std::array<Hook, 3> targets{{
        {offsets.reticle_visibility, reinterpret_cast<void*>(&HookedVisibility),
         reinterpret_cast<void**>(&g_visibility)},
        {offsets.reticle_position, reinterpret_cast<void*>(&HookedPosition),
         reinterpret_cast<void**>(&g_position)},
        {offsets.reticle_destroy, reinterpret_cast<void*>(&HookedDestroy),
         reinterpret_cast<void**>(&g_destroy)},
    }};
    for (const auto& target : targets) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(module + target.target.rva);
        uint32_t hash = 2166136261u;
        for (size_t i = 0; i < 32; ++i) hash = (hash ^ bytes[i]) * 16777619u;
        if (hash != target.target.prefix_hash) {
            Log::Line("ERROR: reticle function verification failed at %llX",
                      static_cast<unsigned long long>(target.target.rva));
            return false;
        }
    }
    auto& hooks = HookManager::Instance();
    for (const auto& target : targets) {
        void* address = reinterpret_cast<void*>(module + target.target.rva);
        auto status = hooks.CreateHook(address, target.detour, target.original);
        if (status == HookStatus::Ok) status = hooks.EnableHook(address);
        if (status != HookStatus::Ok) {
            Log::Line("ERROR: reticle hook installation: %s", HookStatusToString(status));
            return false;
        }
    }
    return true;
}

}  // namespace FarCry6HeadTracking
