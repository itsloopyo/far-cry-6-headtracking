// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "hotkeys.h"

#include "hotkey_bindings.h"
#include "logging.h"
#include "mod.h"

#include "cameraunlock/input/hotkey_poller.h"
#include "cameraunlock/input/key_binding_registration.h"

#include <exception>
#include <functional>
#include <stdexcept>

namespace FarCry6HeadTracking {

namespace {

// ~60Hz: fast enough that a deliberate keypress is never missed, slow enough to
// cost nothing.
constexpr int kPollIntervalMs = 16;

cameraunlock::input::HotkeyPoller g_poller;
bool g_started = false;

std::function<void()> ActionFor(HotkeyAction action) {
    switch (action) {
        case HotkeyAction::Toggle: return []() { Mod::Instance().Runtime().ToggleEnabled(); };
        case HotkeyAction::CycleTrackingMode: return []() { Mod::Instance().CycleTrackingMode(); };
        case HotkeyAction::YawMode: return []() { Mod::Instance().ToggleYawMode(); };
    }
    throw std::logic_error("unknown hotkey action");
}

}  // namespace

void StartHotkeys(const Config& cfg) {
    if (g_started) return;

    // A plain key does not fire while Ctrl and Shift are both held, so Ctrl+Shift with
    // that key reaches only a binding that names the chord.
    for (const HotkeyList& list : HotkeyLists(cfg)) {
        cameraunlock::input::RegisterKeyBindings(g_poller, list.bindings, ActionFor(list.action));
    }

    // The poller rethrows std::system_error when the process cannot spawn its
    // thread, deliberately, so the failure is not silent. Catch it here: this runs
    // under the game's own call into GetApi, where an escaping exception would take
    // the game down mid-startup.
    bool started = false;
    try {
        started = g_poller.Start(kPollIntervalMs);
    } catch (const std::exception& e) {
        Log::Line("ERROR: HotkeyPoller could not start its thread: %s", e.what());
        return;
    }
    if (!started) {
        Log::Line("ERROR: HotkeyPoller failed to start");
        return;
    }

    Log::Line("Hotkeys: ToggleKey=%s, CycleTrackingModeKey=%s, YawModeKey=%s",
              cfg.toggle_key_name.c_str(), cfg.cycle_tracking_mode_key_name.c_str(),
              cfg.yaw_mode_key_name.c_str());
    g_started = true;
}

void StopHotkeys() {
    if (!g_started) return;
    g_poller.Stop();
    g_started = false;
}

}  // namespace FarCry6HeadTracking
