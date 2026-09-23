// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "frame_pump.h"

#include "camera_adapter.h"
#include "logging.h"
#include "mod.h"
#include "pose_bridge.h"

#include <windows.h>

namespace FarCry6HeadTracking {

namespace {

// Microseconds off the performance counter. Whole seconds first, remainder
// second: the counter runs at 10MHz on a current machine, so multiplying the raw
// tick count by a million overflows int64 after about eleven days of uptime and
// hands the game a timestamp that has run backwards.
int64_t NowMicroseconds() {
    static const int64_t frequency = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f.QuadPart;
    }();
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return (now.QuadPart / frequency) * 1000000 +
           (now.QuadPart % frequency) * 1000000 / frequency;
}

// One line every ~10 seconds at 60fps, carrying the numbers that separate "the
// tracker is not sending" from "the pose is arriving and the game is ignoring it".
// listening is separate from receiving because receiving alone cannot tell them
// apart: a port another program is still holding reads receiving = 0 exactly like
// a bound port nobody is sending to, and the two need opposite fixes.
constexpr uint64_t kHeartbeatFrames = 600;

}  // namespace

FramePump::FramePump() : m_timestampMicroseconds(NowMicroseconds()) {}

FramePump& FramePump::Instance() {
    static FramePump instance;
    return instance;
}

void FramePump::Advance() {
    ++m_updates;

    Mod& mod = Mod::Instance();
    // Ahead of the gate: a tracker that drops while the player is in a menu is
    // still a tracker that dropped, and the log has to carry when it happened.
    mod.Runtime().PollConnectionState();

    const int64_t now = NowMicroseconds();
    if (!mod.TrackingAllowed()) {
        m_adsLean.Reset();
        m_aiming = false;
        m_transformation = tobii::Transformation{};
    } else {
        const FrameSample sample = mod.Runtime().SampleFrame();
        m_aiming = CameraAiming();
        if (!mod.Runtime().IsEnabled()) {
            m_adsLean.Reset();
            m_transformation = ToTransformation(sample);
        } else {
            // Here, ahead of the game, so the native extended view, its HUD
            // compensation, the render lean and the reticle all see one pose.
            m_transformation = ScaleForZoom(ToTransformation(sample), CameraZoomFactor());
            m_transformation = m_adsLean.Apply(m_aiming, m_transformation,
                                               static_cast<unsigned long long>(now / 1000));
        }
    }
    LogAdsEdge(m_aiming);
    m_timestampMicroseconds = now;

    LogHeartbeat();
}

// One line each way, with the zoom factor the sights brought with them.
void FramePump::LogAdsEdge(bool aiming) {
    if (aiming == m_loggedAiming) return;
    m_loggedAiming = aiming;
    Log::Line("ADS: sights %s, zoom factor %.4f", aiming ? "up" : "down", CameraZoomFactor());
}

void FramePump::LogHeartbeat() const {
    if (m_updates % kHeartbeatFrames != 0) return;
    Mod& mod = Mod::Instance();
    Log::Line("update=%llu transform=%llu listening=%d receiving=%d enabled=%d paused=%d "
              "coop=%d/%d ads=%d zoom=%.4f yaw=%.2f pitch=%.2f roll=%.2f x=%.1f y=%.1f z=%.1f",
              static_cast<unsigned long long>(m_updates),
              static_cast<unsigned long long>(m_transformationReads),
              mod.Runtime().IsListening() ? 1 : 0,
              mod.Runtime().IsReceiving() ? 1 : 0,
              mod.Runtime().IsEnabled() ? 1 : 0,
              mod.IsPaused() ? 1 : 0,
              mod.IsInCoopSession() ? 1 : 0,
              mod.IsCoopGateActive() ? 1 : 0,
              m_aiming ? 1 : 0,
              CameraZoomFactor(),
              m_transformation.rotation.yaw_degrees,
              m_transformation.rotation.pitch_degrees,
              m_transformation.rotation.roll_degrees,
              m_transformation.position.x,
              m_transformation.position.y,
              m_transformation.position.z);
}

}  // namespace FarCry6HeadTracking
