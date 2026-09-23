// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "config.h"
#include "tracking_runtime.h"

#include <atomic>
#include <string>

namespace FarCry6HeadTracking {

// Process-wide mod state. Brought up lazily from the first GetApi call rather than
// from DllMain: the config read, the socket and the hotkey thread all need to run
// outside the loader lock.
class Mod {
public:
    static Mod& Instance();

    // Idempotent. Returns false when the mod could not start, in which case the
    // shim still answers the game but reports no tracker, so the game runs exactly
    // as it would with no eye tracker plugged in.
    bool EnsureStarted();
    void Shutdown();

    bool IsStarted() const { return m_started.load(std::memory_order_acquire); }

    TrackingRuntime& Runtime() { return m_runtime; }

    // The game pauses the extended view for itself when it leaves gameplay. Honour
    // it: while paused the shim hands back an identity transformation, so menus,
    // loading, the pause screen and cutscenes all hold the stock camera.
    void SetPaused(bool paused);
    bool IsPaused() const { return m_paused.load(std::memory_order_relaxed); }

    // True when the co-op watch is actually installed. A gate that could not be
    // installed is reported rather than assumed clear: "no co-op session seen"
    // and "nothing is watching for one" look identical from the camera.
    bool IsCoopGateActive() const { return m_coopGateActive.load(std::memory_order_relaxed); }

    // Set when the session has a second player in it. Head tracking holds still
    // there when DisableInCoop is on.
    void SetInCoopSession(bool in_coop);
    bool IsInCoopSession() const { return m_inCoop.load(std::memory_order_relaxed); }

    // True when the head pose may reach the camera this frame.
    bool TrackingAllowed() const;

    bool WorldSpaceYaw() const { return m_worldSpaceYaw.load(std::memory_order_relaxed); }
    void ToggleYawMode();

    void OpenLog();

private:
    Mod() = default;

    // Reads the INI beside this DLL, reporting what it found. False means the mod
    // cannot start at all, which is a state the shim still answers the game from.
    bool LoadConfiguration();

    // Brings up everything that outlives the call: the receiver, the hotkey thread,
    // the co-op watch and the window centring wait.
    void StartSubsystems();

    std::atomic<bool> m_started{false};
    std::atomic<bool> m_startAttempted{false};
    // Distinguishes "shut down" from "never started", which look identical from
    // m_started alone and need different reports.
    std::atomic<bool> m_shutdownSeen{false};
    std::atomic<bool> m_paused{false};
    std::atomic<bool> m_inCoop{false};
    std::atomic<bool> m_coopGateActive{false};
    std::atomic<bool> m_worldSpaceYaw{kDefaultWorldSpaceYaw};
    std::string m_iniPath;
    Config m_cfg{};
    TrackingRuntime m_runtime;
};

}  // namespace FarCry6HeadTracking
