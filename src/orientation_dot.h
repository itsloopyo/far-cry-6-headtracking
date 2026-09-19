// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "build_profile.h"

#include <cstdint>

namespace FarCry6HeadTracking {

bool StartOrientationDot(uintptr_t module, const Offsets& offsets);
void NoteOrientationDotGameplay();

// Where the clean aim lands in the head-tracked view, in centred normalised screen
// space (x right, y down). Used when no weapon is out to report its own reticle.
void NoteOrientationDotProjection(float x, float y);

// True while a weapon is updating its reticle position.
bool OrientationDotWeaponOut();

}  // namespace FarCry6HeadTracking
