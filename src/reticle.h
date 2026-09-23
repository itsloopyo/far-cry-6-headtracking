// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "build_profile.h"

#include <cstdint>

namespace FarCry6HeadTracking {

bool StartReticle(uintptr_t module, const Offsets& offsets);

// How far the shot's landing point sits from where the weapon's own projection puts
// it, centred and normalised (x right, y down), the space the weapon reports its
// reticle in. Covers the lean and the yaw-axis switch.
void NoteReticleParallax(float x, float y);

// True while a weapon is updating its reticle position.
bool ReticleWeaponOut();

}  // namespace FarCry6HeadTracking
