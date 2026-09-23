// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "tobii_abi.h"
#include "tracking_runtime.h"

#include "cameraunlock/camera/zoom_compensation.h"
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

// Yaw, pitch and the lean move the picture across the frame, so a narrower field of
// view magnifies them; roll turns it about the view axis by the same angle at any
// field of view, so it is left alone.
inline tobii::Transformation ScaleForZoom(tobii::Transformation pose, float factor) {
    using cameraunlock::camera::ScaleAngleForZoom;
    pose.rotation.yaw_degrees = ScaleAngleForZoom(pose.rotation.yaw_degrees, factor);
    pose.rotation.pitch_degrees = ScaleAngleForZoom(pose.rotation.pitch_degrees, factor);
    pose.position.x *= factor;
    pose.position.y *= factor;
    pose.position.z *= factor;
    return pose;
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
