// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "headlight.h"
#include "logging.h"

#include "cameraunlock/hooks/hook_manager.h"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>

namespace FarCry6HeadTracking {
namespace {

using cameraunlock::hooks::HookManager;
using cameraunlock::hooks::HookStatus;
using cameraunlock::math::Quat4;
using cameraunlock::math::Vec3;

// The beam leads the view: it turns through this multiple of the head's yaw and pitch.
constexpr float kHeadlightHeadScale = 1.5f;
constexpr ULONGLONG kHeadDeltaFreshMs = 200;

using SpawnFn = void (*)(void*);
using DestroyFn = void* (*)(void*, unsigned);
using SetWorldMatrixFn = void (*)(void*, const float*, void*);
SpawnFn g_spawn = nullptr;
DestroyFn g_destroy = nullptr;
SetWorldMatrixFn g_setWorldMatrix = nullptr;

// The flashlight component, seen when the game spawns its light. Cleared by the
// component's own destructor, so SetWorldMatrix never follows a freed pointer.
std::atomic<uint8_t*> g_component{nullptr};

std::mutex g_deltaMutex;
Quat4 g_delta;
ULONGLONG g_deltaStamp = 0;

// The component keeps its light entity behind a handle at +0x1e0; the entity is the
// handle's +8, null until the game has created it.
const void* LightEntity(const uint8_t* component) {
    const auto* handle = *reinterpret_cast<const uint8_t* const*>(component + 0x1e0);
    return handle ? *reinterpret_cast<const void* const*>(handle + 8) : nullptr;
}

Quat4 ScaleAngle(Quat4 q, float scale) {
    if (q.w < 0.0f) q = q.Negated();
    const float half = std::acos(std::fmin(q.w, 1.0f));
    const float s = std::sin(half);
    if (s < 1e-6f) return Quat4::Identity();
    const float scaled = half * scale;
    const float k = std::sin(scaled) / s;
    return Quat4(q.x * k, q.y * k, q.z * k, std::cos(scaled));
}

void HookedSpawn(void* component) {
    g_spawn(component);
    g_component.store(static_cast<uint8_t*>(component), std::memory_order_release);
    Log::Line("Headlight: flashlight component %p", component);
}

void* HookedDestroy(void* component, unsigned flags) {
    uint8_t* expected = static_cast<uint8_t*>(component);
    g_component.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
    return g_destroy(component, flags);
}

// Called for every entity whose transform changes. Rows 0-2 of the matrix are the
// entity's right, forward and up axes in world space, row 3 its position.
void HookedSetWorldMatrix(void* entity, const float* matrix, void* extra) {
    const uint8_t* component = g_component.load(std::memory_order_acquire);
    if (!component || entity != LightEntity(component)) {
        g_setWorldMatrix(entity, matrix, extra);
        return;
    }
    Quat4 delta;
    {
        std::lock_guard<std::mutex> lock(g_deltaMutex);
        if (GetTickCount64() - g_deltaStamp > kHeadDeltaFreshMs) {
            g_setWorldMatrix(entity, matrix, extra);
            return;
        }
        delta = g_delta;
    }
    const Quat4 turn = ScaleAngle(delta, kHeadlightHeadScale);
    float turned[16];
    std::memcpy(turned, matrix, sizeof(turned));
    for (int row = 0; row < 3; ++row) {
        const Vec3 axis = turn.Rotate(Vec3(matrix[row * 4], matrix[row * 4 + 1], matrix[row * 4 + 2]));
        turned[row * 4] = axis.x;
        turned[row * 4 + 1] = axis.y;
        turned[row * 4 + 2] = axis.z;
    }
    g_setWorldMatrix(entity, turned, extra);

    static ULONGLONG lastLog = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - lastLog > 10000) {
        lastLog = now;
        Log::Line("Headlight: beam turned %.1f deg for a head turn of %.1f deg",
                  2.0f * std::acos(std::fmin(std::fabs(turn.w), 1.0f)) * 57.29578f,
                  2.0f * std::acos(std::fmin(std::fabs(delta.w), 1.0f)) * 57.29578f);
    }
}

bool Hook(uintptr_t module, const NativeFunction& target, void* detour, void** original,
          const char* name) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(module + target.rva);
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < 32; ++i) hash = (hash ^ bytes[i]) * 16777619u;
    if (hash != target.prefix_hash) {
        Log::Line("ERROR: headlight %s verification failed at %llX", name,
                  static_cast<unsigned long long>(target.rva));
        return false;
    }
    void* address = reinterpret_cast<void*>(module + target.rva);
    auto& hooks = HookManager::Instance();
    auto status = hooks.CreateHook(address, detour, original);
    if (status == HookStatus::Ok) status = hooks.EnableHook(address);
    if (status != HookStatus::Ok) {
        Log::Line("ERROR: headlight %s hook: %s", name, HookStatusToString(status));
        return false;
    }
    return true;
}

}  // namespace

void NoteHeadDelta(const Quat4& clean, const Quat4& tracked) {
    std::lock_guard<std::mutex> lock(g_deltaMutex);
    g_delta = (tracked * clean.Inverse()).Normalized();
    g_deltaStamp = GetTickCount64();
}

bool StartHeadlight(uintptr_t module, const Offsets& offsets) {
    // Destroy first: once the spawn hook can record a component, its destructor must
    // already be able to clear it.
    if (!Hook(module, offsets.flashlight_destroy, reinterpret_cast<void*>(&HookedDestroy),
              reinterpret_cast<void**>(&g_destroy), "destroy") ||
        !Hook(module, offsets.flashlight_spawn, reinterpret_cast<void*>(&HookedSpawn),
              reinterpret_cast<void**>(&g_spawn), "spawn") ||
        !Hook(module, offsets.set_world_matrix, reinterpret_cast<void*>(&HookedSetWorldMatrix),
              reinterpret_cast<void**>(&g_setWorldMatrix), "transform")) {
        return false;
    }
    Log::Line("Headlight: the flashlight beam follows head yaw and pitch at %.1fx",
              kHeadlightHeadScale);
    return true;
}

}  // namespace FarCry6HeadTracking
