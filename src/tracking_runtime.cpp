// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "tracking_runtime.h"

#include "logging.h"

namespace FarCry6HeadTracking {

void TrackingRuntime::ConfigurePosition() {
    m_session.SetPositionSettings(m_cfg.position);
}

void TrackingRuntime::ConfigureSmoothing() {
    // The session forwards both values to the rotation AND position processors and
    // re-reads the receiver's connection locality inside every Update() to pick the
    // one that applies. Without IsRemoteConnection() on the receiver that selection
    // silently pins to local, so assert the trait rather than trusting it.
    static_assert(decltype(m_session)::kHasRemoteConnection,
                  "receiver must expose IsRemoteConnection()");
    m_session.SetLocalSmoothing(m_cfg.local_smoothing);
    m_session.SetRemoteSmoothing(m_cfg.remote_smoothing);
}

void TrackingRuntime::Start(const Config& cfg) {
    m_cfg = cfg;

    ConfigurePosition();
    ConfigureSmoothing();

    m_enabled.store(m_cfg.enable_on_startup, std::memory_order_relaxed);
    // The config table reads a pair that names no mode as its defaults, so the pair
    // always decodes.
    m_session.SetMode(cameraunlock::DecodeTrackingMode(m_cfg.rotation_enabled, m_cfg.position_enabled).value());

    m_receiver.SetLog([](const std::string& msg) { Log::Line("UDP: %s", msg.c_str()); });

    if (m_receiver.Start(static_cast<uint16_t>(m_cfg.udp_port))) {
        Log::Line("UDP receiver listening on port %d", m_cfg.udp_port);
    } else {
        Log::Line("WARN: UDP receiver did not bind immediately on port %d; background "
                  "retry active", m_cfg.udp_port);
    }
}

void TrackingRuntime::Stop() {
    // Only the receiver, which is internally thread-safe. m_receiving and
    // m_lastSample beside it are render-thread-only, and Stop runs on whichever
    // thread called the game's Shutdown.
    m_receiver.Stop();
}

void TrackingRuntime::PollConnectionState() {
    const bool receiving = m_receiver.IsReceiving();
    if (receiving == m_receiving) return;
    m_receiving = receiving;
    if (receiving) {
        Log::Line("Tracker connected: pose packets are arriving on port %d",
                  m_cfg.udp_port);
    } else {
        // What the player sees is worth saying with it: the last pose is held
        // rather than zeroed, so the view stops moving where the head last was
        // and blends on from there when packets resume.
        Log::Line("Tracker silent: no pose packet on port %d for over %dms, so the "
                  "view is holding at the last head pose",
                  m_cfg.udp_port, cameraunlock::UdpReceiver::kConnectionTimeoutMs);
    }
}

void TrackingRuntime::ToggleEnabled() {
    const bool prev = m_enabled.load(std::memory_order_relaxed);
    m_enabled.store(!prev, std::memory_order_relaxed);
    Log::Line("Tracking %s", !prev ? "enabled" : "disabled");
}

cameraunlock::TrackingMode TrackingRuntime::CycleTrackingMode() {
    const cameraunlock::TrackingMode mode = m_session.CycleMode();
    switch (mode) {
        case cameraunlock::TrackingMode::RotationAndPosition:
            Log::Line("Tracking mode: rotation + position (6DOF)");
            break;
        case cameraunlock::TrackingMode::RotationOnly:
            Log::Line("Tracking mode: rotation only");
            break;
        case cameraunlock::TrackingMode::PositionOnly:
            Log::Line("Tracking mode: position only");
            break;
    }
    return mode;
}

FrameSample TrackingRuntime::SampleFrame() {
    // Ticked before the gates in THIS function, so a tracker that falls silent
    // does not leave the clock stale: a stale one hands the next live frame a dt
    // clamped to kMaxFrameDtSec, which drives the smoothing factor to ~1 and snaps
    // the view instead of blending it back.
    //
    // The gameplay gate in FramePump::Advance is outside this function and skips
    // the call entirely, so a pause still resumes on a clamped dt. That one is
    // wanted: the head has moved while the menu was up, and the view should arrive
    // where the head is rather than easing across the room.
    const float dt = m_clock.Tick();

    FrameSample out;

    if (!m_enabled.load(std::memory_order_relaxed)) {
        m_lastSample = out;
        return out;
    }
    // Tracking loss holds the last pose rather than zeroing it. A webcam tracker
    // that loses the face for half a second with the head turned would otherwise
    // snap the view to centre and snap back on re-acquisition.
    if (!m_receiver.IsReceiving()) {
        return m_lastSample;
    }
    if (!m_session.Update(dt)) {
        return m_lastSample;
    }

    // GetRotation reports success in PositionOnly mode too, handing back zeros.
    // Taking that as a rotation channel would let the mod hold the view at a
    // rotation the player has switched off.
    out.has_rotation = m_session.IsRotationActive() &&
                       m_session.GetRotation(out.yaw, out.pitch, out.roll);
    out.has_position = m_session.GetPositionOffset(out.pos_x, out.pos_y, out.pos_z);
    m_lastSample = out;
    return out;
}

}  // namespace FarCry6HeadTracking
