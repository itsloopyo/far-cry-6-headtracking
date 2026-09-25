// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "config.h"

#include "cameraunlock/input/key_bindings.h"

#include <vector>

namespace FarCry6HeadTracking {

enum class HotkeyAction { Toggle, CycleTrackingMode, YawMode };

struct HotkeyList {
    HotkeyAction action;
    std::vector<cameraunlock::input::KeyBinding> bindings;
};

// The three key lists of the settings file, parsed, as StartHotkeys puts them on the poller.
std::vector<HotkeyList> HotkeyLists(const Config& cfg);

}  // namespace FarCry6HeadTracking
