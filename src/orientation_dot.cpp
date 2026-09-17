// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "orientation_dot.h"
#include "logging.h"
#include "mod.h"

#include "cameraunlock/hooks/hook_manager.h"
#define CAMERAUNLOCK_DX12_OVERLAY_IMPLEMENTATION
#include "cameraunlock/rendering/dx12_overlay.h"

#include <array>
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

struct DotState {
    const void* reticle = nullptr;
    bool stock_visible = false;
    float x = 0.0f;
    float y = 0.0f;
    ULONGLONG position_stamp = 0;
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

void HookedPosition(void* weapon, float dt, void* player) {
    g_position(weapon, dt, player);
    float position[2];
    std::memcpy(position, static_cast<const uint8_t*>(weapon) + 0xb74, sizeof(position));
    std::lock_guard<std::mutex> lock(g_mutex);
    g_dot.x = position[0];
    g_dot.y = position[1];
    g_dot.position_stamp = GetTickCount64();
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
    if (!dot.reticle || dot.stock_visible || !Mod::Instance().TrackingAllowed() ||
        now - dot.position_stamp > 200 || now - dot.gameplay_stamp > 200 ||
        !std::isfinite(dot.x) || !std::isfinite(dot.y) ||
        std::abs(dot.x) > 1.0f || std::abs(dot.y) > 1.0f) return;

    const float x = (dot.x * 0.5f + 0.5f) * dc.Width();
    const float y = (dot.y * 0.5f + 0.5f) * dc.Height();
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
    Log::Line("Orientation dot active: hidden while the stock reticle is visible");
}

}  // namespace

void NoteOrientationDotGameplay() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_dot.gameplay_stamp = GetTickCount64();
}

bool StartOrientationDot(uintptr_t module) {
    struct Hook { uintptr_t rva; uint32_t hash; void* detour; void** original; };
    const std::array<Hook, 3> targets{{
        {0x30bb060, 0x8812c97c, reinterpret_cast<void*>(&HookedVisibility),
         reinterpret_cast<void**>(&g_visibility)},
        {0x27ee100, 0x9196e66a, reinterpret_cast<void*>(&HookedPosition),
         reinterpret_cast<void**>(&g_position)},
        {0x3089e50, 0x0683cfc9, reinterpret_cast<void*>(&HookedDestroy),
         reinterpret_cast<void**>(&g_destroy)},
    }};
    for (const auto& target : targets) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(module + target.rva);
        uint32_t hash = 2166136261u;
        for (size_t i = 0; i < 32; ++i) hash = (hash ^ bytes[i]) * 16777619u;
        if (hash != target.hash) {
            Log::Line("ERROR: reticle function verification failed at %llX",
                      static_cast<unsigned long long>(target.rva));
            return false;
        }
    }
    auto& hooks = HookManager::Instance();
    for (const auto& target : targets) {
        void* address = reinterpret_cast<void*>(module + target.rva);
        auto status = hooks.CreateHook(address, target.detour, target.original);
        if (status == HookStatus::Ok) status = hooks.EnableHook(address);
        if (status != HookStatus::Ok) {
            Log::Line("ERROR: reticle hook installation: %s", HookStatusToString(status));
            return false;
        }
    }
    std::thread(InstallOverlay).detach();
    return true;
}

}  // namespace FarCry6HeadTracking
