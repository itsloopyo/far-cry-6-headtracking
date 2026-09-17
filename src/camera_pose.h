// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "tobii_abi.h"
#include "cameraunlock/math/angle_utils.h"
#include "cameraunlock/math/vec3.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace FarCry6HeadTracking {

using cameraunlock::math::Vec3;

struct CameraParameters {
    uint8_t settings[0x74];
    Vec3 eye;
    Vec3 offset;
    Vec3 forward;
    Vec3 up;
    Vec3 right;
    Vec3 euler;
};

static_assert(offsetof(CameraParameters, eye) == 0x74);
static_assert(offsetof(CameraParameters, forward) == 0x8c);
static_assert(sizeof(CameraParameters) == 0xbc);

inline Vec3 CameraLean(const CameraParameters& camera, const tobii::Position& position) {
    return camera.right * (position.x * 0.001f) +
           camera.up * (position.y * 0.001f) -
           camera.forward * (position.z * 0.001f);
}

inline void ApplyCameraRoll(CameraParameters& camera, float tobiiRollDegrees) {
    const float radians = -tobiiRollDegrees *
                          static_cast<float>(cameraunlock::math::kDegToRad);
    const float sine = std::sin(radians);
    const float cosine = std::cos(radians);
    const Vec3 up = camera.up;
    camera.up = up * cosine + camera.right * sine;
    camera.right = camera.right * cosine - up * sine;
}

}  // namespace FarCry6HeadTracking
