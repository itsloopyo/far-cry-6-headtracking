// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "ads.h"

#include "cameraunlock/data/position_settings.h"
#include "cameraunlock/math/smoothing_utils.h"

#include <cstdint>

namespace FarCry6HeadTracking {

// The shipped defaults, in one place, so the INI writer, the reader's per-key
// fallback and the Config member initialisers cannot drift apart.
constexpr bool  kDefaultEnableOnStartup = true;
constexpr int   kDefaultPort            = 4242;
constexpr int   kMinPort                = 1024;
constexpr int   kMaxPort                = 65535;
constexpr float kDefaultSensitivity     = 1.0f;
constexpr bool  kDefaultInvert          = false;
constexpr float kDefaultLocalSmoothing  = static_cast<float>(cameraunlock::math::kDefaultLocalSmoothing);
constexpr float kDefaultRemoteSmoothing = static_cast<float>(cameraunlock::math::kDefaultRemoteSmoothing);

constexpr bool  kDefaultPositionEnabled = true;
constexpr float kDefaultPosSens         = 1.0f;
constexpr float kDefaultPosLimitX       = cameraunlock::PositionSettings{}.limit_x;
constexpr float kDefaultPosLimitY       = cameraunlock::PositionSettings{}.limit_y;
constexpr float kDefaultPosLimitZ       = cameraunlock::PositionSettings{}.limit_z;
constexpr float kDefaultPosLimitZBack   = cameraunlock::PositionSettings{}.limit_z_back;

// The game's co-op sessions leave the view under the mod's control the same way a
// solo session does, and nothing about head tracking changes where a shot lands.
// It is still switched off there by default so a session shared with another
// player runs the stock camera.
constexpr bool  kDefaultDisableInCoop   = true;

constexpr int   kDefaultVkToggle        = 0x23; // VK_END
constexpr int   kDefaultVkCycleMode     = 0x21; // VK_PRIOR (Page Up)

struct Config {
    bool enabled_on_startup = kDefaultEnableOnStartup;
    uint16_t udp_port = static_cast<uint16_t>(kDefaultPort);

    float sens_yaw = kDefaultSensitivity;
    float sens_pitch = kDefaultSensitivity;
    float sens_roll = kDefaultSensitivity;
    // Per-axis user inversion, for a tracker that genuinely reports an axis
    // backwards. The protocol-to-Tobii sign conversion is not carried here: it
    // happens once at the boundary in pose_bridge.h, after the asymmetric
    // position clamp has already been applied in the pipeline's own basis.
    bool invert_yaw = kDefaultInvert;
    bool invert_pitch = kDefaultInvert;
    bool invert_roll = kDefaultInvert;

    // Picked per connection from the packet source address: loopback senders get
    // local_smoothing, remote network devices get remote_smoothing. Both cover
    // rotation and position.
    float local_smoothing = kDefaultLocalSmoothing;
    float remote_smoothing = kDefaultRemoteSmoothing;

    bool position_enabled = kDefaultPositionEnabled;
    float pos_sens_x = kDefaultPosSens;
    float pos_sens_y = kDefaultPosSens;
    float pos_sens_z = kDefaultPosSens;
    float pos_limit_x = kDefaultPosLimitX;
    float pos_limit_y = kDefaultPosLimitY;
    float pos_limit_z = kDefaultPosLimitZ;
    float pos_limit_z_back = kDefaultPosLimitZBack;
    bool invert_pos_x = kDefaultInvert;
    bool invert_pos_y = kDefaultInvert;
    bool invert_pos_z = kDefaultInvert;

    bool disable_in_coop = kDefaultDisableInCoop;
    AdsMode ads_mode = kDefaultAdsMode;

    int vk_toggle = kDefaultVkToggle;
    int vk_cycle_mode = kDefaultVkCycleMode;
    int vk_ads_mode = kDefaultVkAdsMode;

    bool LoadOrCreate(const char* iniPath);
};

}  // namespace FarCry6HeadTracking
