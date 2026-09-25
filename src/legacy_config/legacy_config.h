// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

// The FarCry6HeadTracking.ini reader as the last build before the canonical config format
// (5ea3ff9) ran it, frozen so an old file converts exactly as that build read it. Never
// edited: a change here changes what a player's old file means.
//
// The defaults are literals rather than core's constants, which is what they held at
// cameraunlock-core 2eded2c and befb88e, so a later core default cannot move what an old
// file without the key means.

namespace FarCry6HeadTracking::legacy {

constexpr bool  kDefaultEnableOnStartup = true;
constexpr int   kDefaultPort            = 4242;
constexpr int   kMinPort                = 1024;
constexpr int   kMaxPort                = 65535;
constexpr float kDefaultSensitivity     = 1.0f;
constexpr bool  kDefaultInvert          = false;
constexpr float kDefaultLocalSmoothing  = static_cast<float>(0.0);
constexpr float kDefaultRemoteSmoothing = static_cast<float>(0.15);

constexpr bool  kDefaultPositionEnabled = true;
constexpr float kDefaultPosSens         = 1.0f;
constexpr float kDefaultPosLimitX       = 0.30f;
constexpr float kDefaultPosLimitY       = 0.20f;
constexpr float kDefaultPosLimitZ       = 0.40f;
constexpr float kDefaultPosLimitZBack   = 0.10f;

constexpr bool  kDefaultDisableInCoop   = true;

constexpr int   kDefaultVkToggle        = 0x23;
constexpr int   kDefaultVkCycleMode     = 0x21;
constexpr int   kDefaultVkYawMode       = 0x22;
constexpr bool  kDefaultWorldSpaceYaw   = true;

struct Config {
    bool enabled_on_startup = kDefaultEnableOnStartup;
    uint16_t udp_port = static_cast<uint16_t>(kDefaultPort);

    float sens_yaw = kDefaultSensitivity;
    float sens_pitch = kDefaultSensitivity;
    float sens_roll = kDefaultSensitivity;
    bool invert_yaw = kDefaultInvert;
    bool invert_pitch = kDefaultInvert;
    bool invert_roll = kDefaultInvert;

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
    bool world_space_yaw = kDefaultWorldSpaceYaw;

    int vk_toggle = kDefaultVkToggle;
    int vk_cycle_mode = kDefaultVkCycleMode;
    int vk_yaw_mode = kDefaultVkYawMode;
};

enum class ReadStatus {
    // The file was read into the Config.
    Read,
    // There is no file at the path. The builds created one holding the defaults and read
    // that, so the Config holds the defaults.
    Absent,
    // The file vanished between the existence check and the open. The builds did not start.
    OpenFailed,
    // [General] Port is not a whole number from kMinPort to kMaxPort. The builds did not
    // start. The Config holds what was read before the port.
    PortRefused,
};

// Reads the file at the ANSI path through GetPrivateProfileStringA, as the builds did.
// Writes nothing. Logs through cameraunlock::logging as the builds did.
ReadStatus Read(const char* iniPath, Config& cfg);

}  // namespace FarCry6HeadTracking::legacy
