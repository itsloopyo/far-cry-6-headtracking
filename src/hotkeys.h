// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "config.h"

namespace FarCry6HeadTracking {

// Registers the three key lists of the settings file on the hotkey thread.
void StartHotkeys(const Config& cfg);
void StopHotkeys();

}  // namespace FarCry6HeadTracking
