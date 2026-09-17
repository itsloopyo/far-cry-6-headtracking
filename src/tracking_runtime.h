// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "config.h"

#include "cameraunlock/protocol/udp_receiver.h"
#include "cameraunlock/time/frame_clock.h"
#include "cameraunlock/tracking/head_tracking_session.h"

#include <atomic>

namespace FarCry6HeadTracking {

// One frame's processed head pose in the CORE basis: rotation in degrees (YPR) and
// position in metres with x = right, y = up, and NEGATIVE z as the forward lean.
// That z sign is what puts the generous LimitZ on leaning in and the restricted
// LimitZBack on pulling away, and it is why pose_bridge.h converts signs at the
// boundary rather than anywhere earlier.
//
// has_* report whether each channel produced fresh data this frame.
struct FrameSample {
    bool  has_rotation = false;
    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    bool  has_position = false;
    float pos_x = 0.0f, pos_y = 0.0f, pos_z = 0.0f;
};

class TrackingRuntime {
public:
    TrackingRuntime() : m_session(m_receiver) {}

    // Applies @p cfg and brings the UDP receiver up. A port that will not bind yet is
    // not a failure: the receiver retries in the background.
    void Start(const Config& cfg);
    void Stop();

    // Runs the per-frame pipeline once and returns the processed pose.
    //
    // Called from the game's render thread, once per frame, from the one place that
    // asks for a transformation. The frame clock and the session are stateful and not
    // reentrant, which is what makes that restriction load-bearing.
    FrameSample SampleFrame();

    bool IsReceiving() const { return m_receiver.IsReceiving(); }

    // Logs a tracker connection gained or lost, once per transition. Call once per
    // frame from the game's pump, outside the gameplay gate: a tracker that drops
    // while the player is in a menu still has to be reported at the moment it
    // happened. The heartbeat's `receiving` field cannot serve as that report - it
    // is a ten-second sample - and the receiver announces only the FIRST packet of
    // the process, never a resumption.
    void PollConnectionState();

    // Whether the socket is bound at all, which IsReceiving cannot say: a port
    // another program is holding and a bound port nobody is sending to both
    // report receiving = false, and they need opposite fixes.
    bool IsListening() const { return m_receiver.IsRunning(); }

    bool IsEnabled() const { return m_enabled.load(std::memory_order_relaxed); }

    void ToggleEnabled();
    void CycleTrackingMode();

private:
    static constexpr float kMaxFrameDtSec = 0.25f;

    void ConfigureRotation();
    void ConfigurePosition();
    void ConfigureSmoothing();

    Config m_cfg{};
    cameraunlock::UdpReceiver m_receiver;
    cameraunlock::HeadTrackingSession<cameraunlock::UdpReceiver> m_session;
    cameraunlock::time::FrameClock m_clock{kMaxFrameDtSec};

    std::atomic<bool> m_enabled{false};

    // Render thread only, like the frame clock and the session beside it.
    bool m_receiving = false;

    // The last pose the pipeline produced, republished while the tracker is
    // silent so the view holds where the head was rather than snapping to centre.
    FrameSample m_lastSample{};
};

}  // namespace FarCry6HeadTracking
