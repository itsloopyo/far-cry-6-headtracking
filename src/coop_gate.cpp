// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "coop_gate.h"
#include "coop_session.h"

#include "logging.h"
#include "mod.h"

#include "cameraunlock/hooks/hook_manager.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <initializer_list>

namespace FarCry6HeadTracking {

namespace {

constexpr char kUpcModule[] = "upc_r2_loader64.dll";
constexpr char kSetName[] = "UPC_MultiplayerSessionSet";
constexpr char kClearName[] = "UPC_MultiplayerSessionClear";

using SetSessionFn = int32_t(__fastcall*)(void*, const void*);
using ClearSessionFn = int32_t(__fastcall*)(void*);

// One watched export: where the detour was installed, so it can be removed, and the
// trampoline back to the game's own implementation, which every detour must call.
struct WatchedExport {
    void* original = nullptr;
    void* target = nullptr;
};

WatchedExport g_set;
WatchedExport g_clear;

SessionCounts ReadCounts(const void* session) {
    if (!session) return {};
    uint8_t bytes[kSessionPopulationBytes];
    __try {
        std::memcpy(bytes, session, sizeof(bytes));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return {};
    }
    return DecodeSessionCounts(bytes, sizeof(bytes));
}

// Logged on every change of the player count rather than on every publish: the game
// republishes the same session repeatedly, and a line per publish would bury the
// transition that matters.
void ReportSessionCounts(const SessionCounts& counts) {
    // Both counts, packed into one atomic. Keying on the current count alone drops
    // a change to the session's size, and a plain variable here is written from
    // whichever game thread published the session, with no ordering against the
    // next one.
    const uint64_t pair =
        (static_cast<uint64_t>(counts.max_players) << 32) | counts.current_players;
    static std::atomic<uint64_t> s_lastReported{~uint64_t{0}};
    if (s_lastReported.exchange(pair) == pair) return;
    Log::Line("Multiplayer session published: %u of %u players", counts.current_players,
              counts.max_players);
}

// Nothing was readable where the counts should be. Say so once and leave head
// tracking alone: a session the game publishes for every solo game is not a reason
// to switch the mod off.
void ReportUnreadableSession() {
    static bool s_warned = false;
    if (s_warned) return;
    s_warned = true;
    Log::Line("WARN: the multiplayer session the game published could not be read, so "
              "head tracking cannot tell co-op from single player and stays on.");
}

int32_t __fastcall HookedSet(void* context, const void* session) {
    const SessionCounts counts = ReadCounts(session);
    if (counts.read) {
        ReportSessionCounts(counts);
        Mod::Instance().SetInCoopSession(counts.current_players > 1);
    } else {
        ReportUnreadableSession();
    }
    return reinterpret_cast<SetSessionFn>(g_set.original)(context, session);
}

int32_t __fastcall HookedClear(void* context) {
    Mod::Instance().SetInCoopSession(false);
    return reinterpret_cast<ClearSessionFn>(g_clear.original)(context);
}

bool Install(HMODULE module, const char* name, void* detour, WatchedExport& out) {
    void* address = reinterpret_cast<void*>(GetProcAddress(module, name));
    if (!address) {
        Log::Line("ERROR: %s does not export %s, so the co-op gate cannot watch for a "
                  "multiplayer session.", kUpcModule, name);
        return false;
    }
    using cameraunlock::hooks::HookManager;
    using cameraunlock::hooks::HookStatus;
    HookManager& hooks = HookManager::Instance();
    HookStatus status =
        hooks.CreateHook(address, detour, reinterpret_cast<void**>(&out.original));
    if (status != HookStatus::Ok) {
        Log::Line("ERROR: could not hook %s: %s", name,
                  cameraunlock::hooks::HookStatusToString(status));
        return false;
    }
    // Recorded before the enable rather than after it: a hook that was created
    // and not enabled still has to be removable, and StopCoopGate has nothing
    // but this to remove it by.
    out.target = address;
    status = hooks.EnableHook(address);
    if (status != HookStatus::Ok) {
        Log::Line("ERROR: could not enable the hook on %s: %s", name,
                  cameraunlock::hooks::HookStatusToString(status));
        return false;
    }
    return true;
}

}  // namespace

bool StartCoopGate() {
    // upc_r2_loader64.dll is a static import of the game's own module, so it is
    // already mapped by the time the game asks this DLL for a head tracking API.
    HMODULE module = GetModuleHandleA(kUpcModule);
    if (!module) {
        Log::Line("ERROR: %s is not loaded, so head tracking cannot tell a co-op "
                  "session from a single player one. It will stay on in co-op.",
                  kUpcModule);
        return false;
    }

    using cameraunlock::hooks::HookManager;
    using cameraunlock::hooks::HookStatus;
    HookManager& hooks = HookManager::Instance();
    const HookStatus init = hooks.Initialize();
    if (init != HookStatus::Ok && init != HookStatus::ErrorAlreadyInitialized) {
        Log::Line("ERROR: hook engine failed to start: %s",
                  cameraunlock::hooks::HookStatusToString(init));
        return false;
    }

    const bool set = Install(module, kSetName, reinterpret_cast<void*>(&HookedSet), g_set);
    const bool clear =
        Install(module, kClearName, reinterpret_cast<void*>(&HookedClear), g_clear);
    if (!set || !clear) {
        // Take the half that landed back out. Set without Clear is worse than
        // neither: it would latch head tracking off the first time a second
        // player joined and never see the session end, while this function has
        // already reported the gate as not installed.
        StopCoopGate();
        // And release the latch itself. The Set hook is live from the moment it
        // is enabled, so a session published between that and the failure below
        // has already been recorded - and with the hooks now gone nothing is
        // left that could ever clear it, which is head tracking off for the rest
        // of the session with the log saying the gate was never installed.
        Mod::Instance().SetInCoopSession(false);
        return false;
    }
    Log::Line("Co-op gate active: watching %s for a published multiplayer session",
              kUpcModule);
    return true;
}

void StopCoopGate() {
    cameraunlock::hooks::HookManager& hooks = cameraunlock::hooks::HookManager::Instance();
    // Both disabled before either is removed. Disabling restores the export, so no
    // NEW call can enter a detour while the other hook is still coming out.
    //
    // It does not evict a thread already running inside a detour body, which is why
    // `original` is deliberately left pointing at the trampoline: that thread
    // resumes into its tail call whatever the hook state is, MinHook does not
    // decommit the trampoline on removal, so leaving it set lets the call complete
    // where nulling it would guarantee a null dereference.
    for (WatchedExport* watched : {&g_set, &g_clear}) {
        if (watched->target) hooks.DisableHook(watched->target);
    }
    for (WatchedExport* watched : {&g_set, &g_clear}) {
        if (!watched->target) continue;
        hooks.RemoveHook(watched->target);
        watched->target = nullptr;
    }
}

}  // namespace FarCry6HeadTracking
