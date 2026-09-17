// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "tracking_runtime.h"

#include "logging.h"

#include <cmath>

namespace FarCry6HeadTracking {

namespace {

bool AllFinite(float a, float b, float c) {
    return std::isfinite(a) && std::isfinite(b) && std::isfinite(c);
}

// The pipeline finite-checks the wire and the INI bounds the sensitivities it
// multiplies by, so a Config that came from a file cannot overflow the product.
// This covers a Config built in code, which is what the overflow test drives, and
// it is the last thing between a non-finite pose and the transformation the game
// renders through. Drop the channel and say so once - a silently dropped pose reads
// in game exactly like a tracker that has stopped sending.
void ReportNonFinite(const char* channel) {
    static bool s_warned = false;
    if (s_warned) return;
    s_warned = true;
    Log::Line("WARN: the processed %s came out non-finite and is being dropped. Check "
              "the Sensitivity and Position values in the INI: a large enough one "
              "overflows the pose it multiplies.", channel);
}

}  // namespace

void TrackingRuntime::ConfigureRotation() {
    cameraunlock::SensitivitySettings sens;
    sens.yaw = m_cfg.sens_yaw;
    sens.pitch = m_cfg.sens_pitch;
    sens.roll = m_cfg.sens_roll;
    sens.invert_yaw = m_cfg.invert_yaw;
    sens.invert_pitch = m_cfg.invert_pitch;
    sens.invert_roll = m_cfg.invert_roll;
    m_session.GetProcessor().SetSensitivity(sens);
}

void TrackingRuntime::ConfigurePosition() {
    cameraunlock::PositionSettings pos;
    pos.sensitivity_x = m_cfg.pos_sens_x;
    pos.sensitivity_y = m_cfg.pos_sens_y;
    pos.sensitivity_z = m_cfg.pos_sens_z;
    pos.limit_x = m_cfg.pos_limit_x;
    pos.limit_y = m_cfg.pos_limit_y;
    // The INI exposes one vertical limit, so mirror it into the downward bound the
    // way PositionSettings::Symmetric does. Leaving limit_y_down at its struct
    // default would grow the upward budget only, silently.
    pos.limit_y_down = m_cfg.pos_limit_y;
    pos.limit_z = m_cfg.pos_limit_z;
    pos.limit_z_back = m_cfg.pos_limit_z_back;
    pos.invert_x = m_cfg.invert_pos_x;
    pos.invert_y = m_cfg.invert_pos_y;
    pos.invert_z = m_cfg.invert_pos_z;
    m_session.SetPositionSettings(pos);
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

    ConfigureRotation();
    ConfigurePosition();
    ConfigureSmoothing();

    m_enabled.store(m_cfg.enabled_on_startup, std::memory_order_relaxed);
    m_session.SetMode(m_cfg.position_enabled
                          ? cameraunlock::TrackingMode::RotationAndPosition
                          : cameraunlock::TrackingMode::RotationOnly);

    m_receiver.SetLog([](const std::string& msg) { Log::Line("UDP: %s", msg.c_str()); });

    if (m_receiver.Start(m_cfg.udp_port)) {
        Log::Line("UDP receiver listening on port %u", m_cfg.udp_port);
    } else {
        Log::Line("WARN: UDP receiver did not bind immediately on port %u; background "
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
        Log::Line("Tracker connected: pose packets are arriving on port %u",
                  m_cfg.udp_port);
    } else {
        // What the player sees is worth saying with it: the last pose is held
        // rather than zeroed, so the view stops moving where the head last was
        // and blends on from there when packets resume.
        Log::Line("Tracker silent: no pose packet on port %u for over %dms, so the "
                  "view is holding at the last head pose",
                  m_cfg.udp_port, cameraunlock::UdpReceiver::kConnectionTimeoutMs);
    }
}

void TrackingRuntime::ToggleEnabled() {
    const bool prev = m_enabled.load(std::memory_order_relaxed);
    m_enabled.store(!prev, std::memory_order_relaxed);
    Log::Line("Tracking %s", !prev ? "enabled" : "disabled");
}

void TrackingRuntime::CycleTrackingMode() {
    switch (m_session.CycleMode()) {
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
    if (out.has_rotation && !AllFinite(out.yaw, out.pitch, out.roll)) {
        out.has_rotation = false;
        ReportNonFinite("head rotation");
    }

    out.has_position = m_session.GetPositionOffset(out.pos_x, out.pos_y, out.pos_z);
    if (out.has_position && !AllFinite(out.pos_x, out.pos_y, out.pos_z)) {
        out.has_position = false;
        ReportNonFinite("head position");
    }
    m_lastSample = out;
    return out;
}

}  // namespace FarCry6HeadTracking
