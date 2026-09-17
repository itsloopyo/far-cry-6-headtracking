// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "tobii_abi.h"

#include "cameraunlock/ads/ads_blend.h"
#include "cameraunlock/ads/ads_fade.h"
#include "cameraunlock/ads/ads_mode.h"
#include "cameraunlock/ads/entry_pose.h"

namespace FarCry6HeadTracking {

// Far Cry 6 ships the two-slot ADS cycle, paused and tracked. The orientation dot
// already marks the aim point whenever the game hides its own reticle, which it
// does with iron sights up, so a separate marker slot would draw a second mark on
// the same point. `marker` is not a value here: ParseAdsMode is always called with
// allowMarker false, and the cycle is the two-slot one.
using cameraunlock::ads::AdsMode;
using cameraunlock::ads::AdsModeValue;
using cameraunlock::ads::kDefaultAdsMode;

constexpr int kDefaultVkAdsMode = 0x2D;  // VK_INSERT

inline AdsMode ParseFarCry6AdsMode(const char* text) {
    return cameraunlock::ads::ParseAdsMode(text, false);
}

inline AdsMode NextFarCry6AdsMode(AdsMode mode) {
    return cameraunlock::ads::NextAdsModeTwoSlot(mode);
}

// What the mode does, for the log. Core's toast wording does not fit this game:
// roll stays live in paused, and the orientation dot is drawn with the sights up
// in tracked.
inline const char* FarCry6AdsModeDescription(AdsMode mode) {
    return mode == AdsMode::Tracked
               ? "ADS mode: tracked - the view settles onto the sights, then head tracking "
                 "carries on from there"
               : "ADS mode: paused - the view settles onto the sights and only a head tilt "
                 "still rolls it";
}

// The head pose the game and the render hook are handed while the sights come up,
// stay up and come down.
//
// paused   yaw, pitch and the lean fade onto the sights and stay there; roll stays
//          live, since a head tilt moves neither the eye off the barrel nor the
//          aim point off the middle of the frame.
// tracked  the same swing onto the sights, then head tracking carries on measured
//          from the pose the sights came up on.
//
// Units are the Tobii transformation's: degrees and millimetres. Blending is
// linear in both, so nothing is converted.
class AdsPose {
public:
    // Once per frame. `live` says this frame's rotation is a real sample, which is
    // what the entry pose may be captured from.
    tobii::Transformation Apply(AdsMode mode, bool aiming, bool live,
                                const tobii::Transformation& absolute,
                                unsigned long long nowMs) {
        using Pose = cameraunlock::ads::AdsEntryPose::Pose;
        const Pose abs = ToPose(absolute);
        const float scale = m_fade.Update(aiming, nowMs);
        // Kept for the length of the ride back down: dropping it when aiming ends
        // would make the relative pose the absolute one, and the return would step
        // the view by the whole entry offset in one frame. Only an entry that was
        // captured is held; capturing one on the way down would step the view the
        // other way.
        const bool holdEntry = aiming || (scale < 1.0f && m_entry.HasEntry());
        const Pose rel = m_entry.Relative(holdEntry, live, abs);
        return ToTransformation(cameraunlock::ads::BlendAdsPose(mode, scale, abs, rel));
    }

    // Wherever tracking is suppressed, so the next aim starts clean.
    void Reset() {
        m_fade.Reset();
        m_entry.Reset();
    }

private:
    static cameraunlock::ads::AdsEntryPose::Pose ToPose(const tobii::Transformation& t) {
        cameraunlock::ads::AdsEntryPose::Pose p;
        p.yaw = t.rotation.yaw_degrees;
        p.pitch = t.rotation.pitch_degrees;
        p.roll = t.rotation.roll_degrees;
        p.x = t.position.x;
        p.y = t.position.y;
        p.z = t.position.z;
        return p;
    }

    static tobii::Transformation ToTransformation(const cameraunlock::ads::AdsEntryPose::Pose& p) {
        tobii::Transformation t{};
        t.rotation.yaw_degrees = p.yaw;
        t.rotation.pitch_degrees = p.pitch;
        t.rotation.roll_degrees = p.roll;
        t.position.x = p.x;
        t.position.y = p.y;
        t.position.z = p.z;
        return t;
    }

    cameraunlock::ads::AdsFade m_fade;
    cameraunlock::ads::AdsEntryPose m_entry;
};

}  // namespace FarCry6HeadTracking
