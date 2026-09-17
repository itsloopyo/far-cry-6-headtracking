// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "tobii_abi.h"
#include "tracking_runtime.h"

#include "cameraunlock/math/angle_utils.h"

namespace FarCry6HeadTracking {

constexpr float kMillimetresPerMetre = 1000.0f;

// Far Cry 6 negates extended-view yaw internally. Passing tracker yaw through
// preserves the requested look direction. Core's z is already negative-forward.
inline tobii::Transformation ToTransformation(const FrameSample& sample) {
    tobii::Transformation t{};
    if (sample.has_rotation) {
        t.rotation.yaw_degrees = sample.yaw;
        t.rotation.pitch_degrees = sample.pitch;
        t.rotation.roll_degrees = sample.roll;
    }
    if (sample.has_position) {
        t.position.x = -sample.pos_x * kMillimetresPerMetre;
        t.position.y = sample.pos_y * kMillimetresPerMetre;
        t.position.z = sample.pos_z * kMillimetresPerMetre;
    }
    return t;
}

inline tobii::ExtendedViewTransformation ToExtendedViewTransformation(
    const tobii::Transformation& pose) {
    constexpr float radiansPerDegree = static_cast<float>(cameraunlock::math::kDegToRad);
    return {{pose.rotation.yaw_degrees * radiansPerDegree,
             pose.rotation.pitch_degrees * radiansPerDegree,
             pose.rotation.roll_degrees * radiansPerDegree},
            pose.position};
}

}  // namespace FarCry6HeadTracking
