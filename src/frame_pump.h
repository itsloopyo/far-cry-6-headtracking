// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "ads.h"
#include "tobii_abi.h"

#include <cstdint>

namespace FarCry6HeadTracking {

// The mod's frame boundary, and the one owner of the pose the game reads back.
//
// The game's own per-frame pump drives Advance() and its extended view then reads
// Current() as many times as it likes within that frame. Sampling here rather than
// in the read is what keeps one frame of head motion to one advance of the
// interpolator.
//
// Render thread only: no member is atomic, because nothing else touches them. The
// atomics elsewhere in the mod exist for the hotkey thread.
class FramePump {
public:
    static FramePump& Instance();

    // Polls the tracker connection, applies the gameplay gate and republishes the
    // transformation. A gated frame publishes an identity transformation and does
    // NOT advance the pipeline, so the frame clock and the interpolator stay where
    // the last live frame left them.
    void Advance();

    const tobii::Transformation& Current() const { return m_transformation; }

    // When the transformation above was published, which is what the head pose
    // stream has to be stamped with. Sampling it per read instead would put two
    // times on one frame's pose.
    int64_t CurrentTimestampMicroseconds() const { return m_timestampMicroseconds; }

    // Counted for the heartbeat only. The game reading the transformation back a
    // different number of times than it pumps is the first sign the shape of its
    // call into this DLL has changed.
    void NoteTransformationRead() { ++m_transformationReads; }

private:
    // Seeds the timestamp, so a head pose read before the first Advance carries a
    // real time rather than the epoch.
    FramePump();

    void LogHeartbeat() const;
    void LogAdsEdge(bool aiming);

    tobii::Transformation m_transformation{};
    AdsLean m_adsLean;
    bool m_aiming = false;
    bool m_loggedAiming = false;
    int64_t m_timestampMicroseconds = 0;
    uint64_t m_updates = 0;
    uint64_t m_transformationReads = 0;
};

}  // namespace FarCry6HeadTracking
