// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "coop_gate.h"

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

// More parameters than either function declares. On x64 the caller cleans up and
// the first four arrive in registers, so forwarding a fixed eight passes every
// real argument through untouched whatever the true arity is, and the surplus is
// read by nobody.
//
// The return is 64 bits wide for the same reason the parameter list is long. A
// narrower one would forward only the low half of rax, so if either export
// returns a handle or a pointer the game reads back a truncated value; declaring
// it wide is correct whatever the true return type is, including void.
using UpcFn = uint64_t(__fastcall*)(void*, void*, void*, void*, void*, void*, void*,
                                    void*);

// One watched export: where the detour was installed, so it can be removed, and the
// trampoline back to the game's own implementation, which every detour must call.
struct WatchedExport {
    UpcFn original = nullptr;
    void* target = nullptr;
};

WatchedExport g_set;
WatchedExport g_clear;

// The session descriptor the game hands to UPC_MultiplayerSessionSet: an id
// pointer, then the maximum and the current player count as 32-bit values.
//
// Reading the counts is what makes the gate usable at all. Far Cry 6's co-op is
// drop-in, so the game publishes a joinable session the moment the world loads
// and keeps it published for the whole session. Treating "a session exists" as
// "in co-op" switched head tracking off in ordinary single player, which is how
// these offsets came to be read: the first descriptor logged in a solo session
// was max 2, current 1.
struct SessionCounts {
    bool read = false;
    uint32_t max_players = 0;
    uint32_t current_players = 0;
};

constexpr size_t kMaxPlayerCountOffset = 0x08;
constexpr size_t kCurrentPlayerCountOffset = 0x0C;

SessionCounts ReadCounts(void* session) {
    SessionCounts counts;
    if (!session) return counts;
    const auto* bytes = static_cast<const uint8_t*>(session);
    // The pointer comes from the game and is only read, but it is still one this
    // code did not create.
    __try {
        counts.max_players = *reinterpret_cast<const uint32_t*>(bytes + kMaxPlayerCountOffset);
        counts.current_players =
            *reinterpret_cast<const uint32_t*>(bytes + kCurrentPlayerCountOffset);
        counts.read = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        counts.read = false;
    }
    return counts;
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

uint64_t __fastcall HookedSet(void* a1, void* a2, void* a3, void* a4, void* a5,
                              void* a6, void* a7, void* a8) {
    const SessionCounts counts = ReadCounts(a2);
    if (counts.read) {
        ReportSessionCounts(counts);
        Mod::Instance().SetInCoopSession(counts.current_players > 1);
    } else {
        ReportUnreadableSession();
    }
    return g_set.original(a1, a2, a3, a4, a5, a6, a7, a8);
}

uint64_t __fastcall HookedClear(void* a1, void* a2, void* a3, void* a4, void* a5,
                                void* a6, void* a7, void* a8) {
    Mod::Instance().SetInCoopSession(false);
    return g_clear.original(a1, a2, a3, a4, a5, a6, a7, a8);
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
