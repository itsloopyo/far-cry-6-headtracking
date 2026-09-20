// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "build_profile.h"

#include <cstdint>

namespace FarCry6HeadTracking {

bool StartOrientationDot(uintptr_t module, const Offsets& offsets);
void NoteOrientationDotGameplay();

// Where the shot lands, projected into the frame the player sees: centred and
// normalised (x right, y down), the same space the weapon reports its reticle in.
// `parallaxX/Y` correct the native projection for lean and the yaw-axis switch.
void NoteOrientationDotAim(float x, float y, float parallaxX, float parallaxY);

// True while a weapon is updating its reticle position.
bool OrientationDotWeaponOut();

}  // namespace FarCry6HeadTracking
