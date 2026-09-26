// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "config.h"

namespace FarCry6HeadTracking {
bool StartCameraAdapter(const Config& config);

// Whether the player has a weapon's sights up, as of the last camera update.
bool CameraAiming();

// How far a tracker degree or millimetre has to shrink so it moves the picture as
// far as it would at the game's un-zoomed field of view. 1.0 when nothing is zoomed.
float CameraZoomFactor();
}
