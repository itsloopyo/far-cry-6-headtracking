// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "tobii_abi.h"

#include "cameraunlock/ads/ads_fade.h"

namespace FarCry6HeadTracking {

// Head tracking carries straight on through the aim. The one thing the sights change
// is the lean: the render hook moves the eye, and a leaned eye is no longer on the
// sight line. The lean eases out while the sights are up and back in when they come
// down. Rotation, roll included, is never touched.
//
// Units are the Tobii transformation's millimetres; scaling is linear, so nothing is
// converted.
class AdsLean {
public:
    // Once per frame, with the sights as the game reports them this frame.
    tobii::Transformation Apply(bool aiming, const tobii::Transformation& pose,
                                unsigned long long nowMs) {
        const float scale = m_fade.Update(aiming, nowMs);
        tobii::Transformation out = pose;
        out.position.x *= scale;
        out.position.y *= scale;
        out.position.z *= scale;
        return out;
    }

    // Wherever tracking is suppressed, so the next aim starts clean.
    void Reset() { m_fade.Reset(); }

private:
    cameraunlock::ads::AdsFade m_fade;
};

}  // namespace FarCry6HeadTracking
