// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "config.h"
#include "tracking_runtime.h"

namespace FarCry6HeadTracking {

// Brings up the hotkey poller. There is no yaw-mode binding, because this mod never
// touches the camera: it hands the game's own extended view a yaw and a pitch angle
// and the game composes them, so there is no rotation order here to switch between.
// The camera-local mode is out of reach for a second reason as well - what
// distinguishes it at steep pitch is roll, and the game reads only the yaw and pitch
// fields of the transformation. Page Down / Ctrl+Shift+H are left unbound rather
// than given a second meaning.
void StartHotkeys(const Config& cfg, TrackingRuntime& runtime);
void StopHotkeys();

}  // namespace FarCry6HeadTracking
