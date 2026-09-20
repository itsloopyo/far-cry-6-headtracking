// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "hotkeys.h"

#include "logging.h"
#include "mod.h"

#include "cameraunlock/input/chord_hotkeys.h"
#include "cameraunlock/input/hotkey_poller.h"

#include <cstdio>
#include <exception>

namespace FarCry6HeadTracking {

namespace {

// ~60Hz: fast enough that a deliberate keypress is never missed, slow enough to
// cost nothing.
constexpr int kPollIntervalMs = 16;

cameraunlock::input::HotkeyPoller g_poller;
bool g_started = false;

}  // namespace

void StartHotkeys(const Config& cfg, TrackingRuntime& runtime) {
    if (g_started) return;

    using cameraunlock::input::ChordGuarded;
    using cameraunlock::input::NavGuarded;

    auto onToggle = [&runtime]() { runtime.ToggleEnabled(); };
    auto onCycleMode = [&runtime]() { runtime.CycleTrackingMode(); };
    auto onCycleAds = []() { Mod::Instance().CycleAdsMode(); };
    auto onYawMode = []() { Mod::Instance().ToggleYawMode(); };

    const bool cycleKeyBound = cfg.vk_cycle_mode != cfg.vk_toggle;

    // Nav-cluster keys are suppressed while Ctrl+Shift is held so the chord path is
    // the sole trigger for Ctrl+Shift+<nav> combos - a single keypress never fires
    // an action twice.
    g_poller.SetToggleKey(cfg.vk_toggle, NavGuarded(onToggle));
    // A rebind that puts both actions on one key would fire both from a single
    // press, which reads in game as a stuck key: tracking toggles and the mode
    // cycles together. The chords below still reach the second action.
    if (!cycleKeyBound) {
        Log::Line("WARN: INI [Hotkeys] Toggle and CycleMode are both 0x%02X. Only the "
                  "toggle is bound to it; use Ctrl+Shift+G to cycle tracking mode.",
                  cfg.vk_toggle);
    } else {
        g_poller.AddHotkey(cfg.vk_cycle_mode, NavGuarded(onCycleMode));
    }

    const bool adsKeyBound =
        cfg.vk_ads_mode != cfg.vk_toggle && cfg.vk_ads_mode != cfg.vk_cycle_mode;
    if (!adsKeyBound) {
        Log::Line("WARN: INI [Hotkeys] AdsMode 0x%02X is already bound to another action, "
                  "so it is not bound to the ADS mode cycle; use Ctrl+Shift+U.",
                  cfg.vk_ads_mode);
    } else {
        g_poller.AddHotkey(cfg.vk_ads_mode, NavGuarded(onCycleAds));
    }

    // The chords are not an alternative the user picks between: both sets are live at
    // once, so a keyboard without a navigation cluster still reaches every action.
    g_poller.AddHotkey('Y', ChordGuarded(onToggle));
    g_poller.AddHotkey('G', ChordGuarded(onCycleMode));
    g_poller.AddHotkey('U', ChordGuarded(onCycleAds));
    g_poller.AddHotkey('H', ChordGuarded(onYawMode));
    if (cfg.vk_yaw_mode != cfg.vk_toggle && cfg.vk_yaw_mode != cfg.vk_cycle_mode &&
        cfg.vk_yaw_mode != cfg.vk_ads_mode) {
        g_poller.AddHotkey(cfg.vk_yaw_mode, NavGuarded(onYawMode));
        Log::Line("Yaw mode hotkey: 0x%02X (or Ctrl+Shift+H)", cfg.vk_yaw_mode);
    } else {
        Log::Line("WARN: yaw mode key 0x%02X is already bound; use Ctrl+Shift+H",
                  cfg.vk_yaw_mode);
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

    // The summary must not name a key a collision branch above declined to bind.
    char cycleKey[32] = "Ctrl+Shift+G only";
    if (cycleKeyBound) {
        std::snprintf(cycleKey, sizeof(cycleKey), "0x%02X (or Ctrl+Shift+G)",
                      cfg.vk_cycle_mode);
    }
    char adsKey[32] = "Ctrl+Shift+U only";
    if (adsKeyBound) {
        std::snprintf(adsKey, sizeof(adsKey), "0x%02X (or Ctrl+Shift+U)", cfg.vk_ads_mode);
    }
    Log::Line("Hotkeys: toggle=0x%02X (or Ctrl+Shift+Y), cycle mode=%s, ADS mode=%s",
              cfg.vk_toggle, cycleKey, adsKey);
    g_started = true;
}

void StopHotkeys() {
    if (!g_started) return;
    g_poller.Stop();
    g_started = false;
}

}  // namespace FarCry6HeadTracking
