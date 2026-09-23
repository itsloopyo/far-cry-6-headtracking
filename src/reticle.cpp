// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "reticle.h"
#include "logging.h"
#include "mod.h"

#include "cameraunlock/hooks/hook_manager.h"

#include <array>
#include <cstring>
#include <mutex>

namespace FarCry6HeadTracking {
namespace {

using cameraunlock::hooks::HookManager;
using cameraunlock::hooks::HookStatus;

using PositionFn = void (*)(void*, float, void*);
using PublishPositionFn = void (*)(void*, const float*);
PositionFn g_position = nullptr;
PublishPositionFn g_publishPosition = nullptr;

constexpr ULONGLONG kFreshMs = 200;

struct ReticleState {
    float parallax_x = 0.0f;
    float parallax_y = 0.0f;
    ULONGLONG parallax_stamp = 0;
    ULONGLONG weapon_stamp = 0;
};
std::mutex g_mutex;
ReticleState g_state;

uint32_t PrefixHash(uintptr_t address) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(address);
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < 32; ++i) hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}

// +0xb74 is also the weapon's smoothing history. Publish the render correction
// through its HUD property (+0x1340), without feeding it into the next update.
void HookedPosition(void* weapon, float dt, void* player) {
    g_position(weapon, dt, player);
    float position[2];
    std::memcpy(position, static_cast<const uint8_t*>(weapon) + 0xb74, sizeof(position));
    const float nativeX = position[0];
    const float nativeY = position[1];
    const ULONGLONG now = GetTickCount64();
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (now - g_state.parallax_stamp <= kFreshMs && Mod::Instance().TrackingAllowed() &&
            Mod::Instance().Runtime().IsEnabled()) {
            position[0] += g_state.parallax_x;
            position[1] += g_state.parallax_y;
        }
        g_state.weapon_stamp = now;
    }
    g_publishPosition(static_cast<uint8_t*>(weapon) + 0x1340, position);
    static thread_local ULONGLONG lastLog = 0;
    if (now - lastLog >= 1000) {
        lastLog = now;
        Log::Line("Reticle: weapon=%.4f,%.4f HUD=%.4f,%.4f", nativeX, nativeY,
                  position[0], position[1]);
    }
}

}  // namespace

void NoteReticleParallax(float x, float y) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_state.parallax_x = x;
    g_state.parallax_y = y;
    g_state.parallax_stamp = GetTickCount64();
}

bool ReticleWeaponOut() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return GetTickCount64() - g_state.weapon_stamp <= kFreshMs;
}

bool StartReticle(uintptr_t module, const Offsets& offsets) {
    const std::array<NativeFunction, 2> targets{{offsets.reticle_position,
                                                  offsets.reticle_publish_position}};
    for (const auto& target : targets) {
        if (PrefixHash(module + target.rva) != target.prefix_hash) {
            Log::Line("ERROR: reticle function verification failed at %llX",
                      static_cast<unsigned long long>(target.rva));
            return false;
        }
    }
    g_publishPosition = reinterpret_cast<PublishPositionFn>(module + offsets.reticle_publish_position.rva);
    void* address = reinterpret_cast<void*>(module + offsets.reticle_position.rva);
    auto& hooks = HookManager::Instance();
    auto status = hooks.CreateHook(address, reinterpret_cast<void*>(&HookedPosition),
                                   reinterpret_cast<void**>(&g_position));
    if (status == HookStatus::Ok) status = hooks.EnableHook(address);
    if (status != HookStatus::Ok) {
        Log::Line("ERROR: reticle hook installation: %s", HookStatusToString(status));
        return false;
    }
    return true;
}

}  // namespace FarCry6HeadTracking
