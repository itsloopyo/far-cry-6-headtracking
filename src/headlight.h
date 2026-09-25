// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "build_profile.h"

#include "cameraunlock/effects/head_follow_light.h"
#include "cameraunlock/math/quat4.h"

#include <cstdint>

namespace FarCry6HeadTracking {

// Installs nothing when light.follows_head is false, so the beam stays on the aim.
bool StartHeadlight(uintptr_t module, const Offsets& offsets,
                    const cameraunlock::effects::HeadFollowLightSettings& light);

// The extended view's clean aim and the tracked view the game renders, both world
// rotations. Their difference is the head turn the beam follows.
void NoteHeadDelta(const cameraunlock::math::Quat4& clean,
                   const cameraunlock::math::Quat4& tracked);

}  // namespace FarCry6HeadTracking
