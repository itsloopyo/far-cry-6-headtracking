// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "hotkey_bindings.h"

#include <stdexcept>
#include <string>
#include <utility>

namespace FarCry6HeadTracking {

namespace {

std::vector<cameraunlock::input::KeyBinding> Parse(const std::string& keys, const char* setting) {
    cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(keys);
    if (!parsed.ok()) {
        // The config table read the list with the same parser, so this is a bug.
        throw std::logic_error(std::string(setting) + "=" + keys + " is not a key list: " + parsed.error);
    }
    return std::move(parsed.bindings);
}

}  // namespace

std::vector<HotkeyList> HotkeyLists(const Config& cfg) {
    return {
        {HotkeyAction::Toggle, Parse(cfg.toggle_key_name, "ToggleKey")},
        {HotkeyAction::CycleTrackingMode, Parse(cfg.cycle_tracking_mode_key_name, "CycleTrackingModeKey")},
        {HotkeyAction::YawMode, Parse(cfg.yaw_mode_key_name, "YawModeKey")},
    };
}

}  // namespace FarCry6HeadTracking
