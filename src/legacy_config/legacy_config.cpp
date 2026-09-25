// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "legacy_config.h"

#include "cameraunlock/config/ini_reader.h"
#include "cameraunlock/config/value_guards.h"
#include "cameraunlock/logging/file_log.h"

#include <windows.h>

#include <string>

namespace FarCry6HeadTracking::legacy {

namespace {

namespace Log = ::cameraunlock::logging;
namespace guards = cameraunlock::config;

constexpr guards::LogSink kLogSink = &cameraunlock::logging::Line;

bool FileExists(const char* path) {
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

float ReadSensitivity(const cameraunlock::IniReader& ini, const char* section,
                      const char* key, float fallback) {
    return guards::ReadFloatChecked(ini, section, key, fallback, -guards::kMaxSensitivity,
                                    guards::kMaxSensitivity, kLogSink);
}

float ReadLimit(const cameraunlock::IniReader& ini, const char* key, float fallback) {
    const float value = guards::ReadFloatChecked(ini, "Position", key, fallback, 0.0f,
                                                 guards::kMaxPositionLimit, kLogSink);
    if (value > 0.0f) return value;
    Log::Line("WARN: INI [Position] %s is not a usable distance, so the shipped "
              "default %.2f is used instead", key, static_cast<double>(fallback));
    return fallback;
}

float ReadSmoothing(const cameraunlock::IniReader& ini, const char* key, float fallback) {
    return guards::ReadFloatChecked(ini, "Smoothing", key, fallback, 0.0f, 1.0f, kLogSink);
}

bool ReadGeneralSection(Config& cfg, const cameraunlock::IniReader& ini) {
    cfg.enabled_on_startup = ini.ReadBool("General", "EnableOnStartup", kDefaultEnableOnStartup);
    int port = kDefaultPort;
    if (!ini.ReadIntInRange("General", "Port", port, kMinPort, kMaxPort, kDefaultPort)) {
        Log::Line("ERROR: INI [General] Port=%d is not a whole number between %d and "
                  "%d. The mod will not start.", port, kMinPort, kMaxPort);
        return false;
    }
    cfg.udp_port = static_cast<uint16_t>(port);
    return true;
}

void ReadSensitivitySection(Config& cfg, const cameraunlock::IniReader& ini) {
    cfg.sens_yaw   = ReadSensitivity(ini, "Sensitivity", "Yaw",   kDefaultSensitivity);
    cfg.sens_pitch = ReadSensitivity(ini, "Sensitivity", "Pitch", kDefaultSensitivity);
    cfg.sens_roll  = ReadSensitivity(ini, "Sensitivity", "Roll",  kDefaultSensitivity);
    cfg.invert_yaw   = ini.ReadBool("Sensitivity", "InvertYaw",   kDefaultInvert);
    cfg.invert_pitch = ini.ReadBool("Sensitivity", "InvertPitch", kDefaultInvert);
    cfg.invert_roll  = ini.ReadBool("Sensitivity", "InvertRoll",  kDefaultInvert);
}

void ReadSmoothingSection(Config& cfg, const cameraunlock::IniReader& ini) {
    cfg.local_smoothing  = ReadSmoothing(ini, "LocalSmoothing",  kDefaultLocalSmoothing);
    cfg.remote_smoothing = ReadSmoothing(ini, "RemoteSmoothing", kDefaultRemoteSmoothing);
}

void ReadPositionSection(Config& cfg, const cameraunlock::IniReader& ini) {
    cfg.position_enabled = ini.ReadBool("Position", "Enabled", kDefaultPositionEnabled);
    cfg.pos_sens_x = ReadSensitivity(ini, "Position", "SensitivityX", kDefaultPosSens);
    cfg.pos_sens_y = ReadSensitivity(ini, "Position", "SensitivityY", kDefaultPosSens);
    cfg.pos_sens_z = ReadSensitivity(ini, "Position", "SensitivityZ", kDefaultPosSens);
    cfg.pos_limit_x = ReadLimit(ini, "LimitX", kDefaultPosLimitX);
    cfg.pos_limit_y = ReadLimit(ini, "LimitY", kDefaultPosLimitY);
    cfg.pos_limit_z = ReadLimit(ini, "LimitZ", kDefaultPosLimitZ);
    cfg.pos_limit_z_back = ReadLimit(ini, "LimitZBack", kDefaultPosLimitZBack);
    cfg.invert_pos_x = ini.ReadBool("Position", "InvertX", kDefaultInvert);
    cfg.invert_pos_y = ini.ReadBool("Position", "InvertY", kDefaultInvert);
    cfg.invert_pos_z = ini.ReadBool("Position", "InvertZ", kDefaultInvert);
}

void ReadGameplaySection(Config& cfg, const cameraunlock::IniReader& ini) {
    cfg.world_space_yaw = ini.ReadBool("Gameplay", "WorldSpaceYaw", kDefaultWorldSpaceYaw);
    cfg.disable_in_coop = ini.ReadBool("Gameplay", "DisableInCoop", kDefaultDisableInCoop);
}

int ReadVirtualKey(const cameraunlock::IniReader& ini, const char* key, int fallback) {
    const int raw = ini.ReadHex("Hotkeys", key, fallback);
    if (guards::IsBindableVirtualKey(raw)) {
        return raw;
    }
    Log::Line("WARN: INI [Hotkeys] %s value 0x%02X is not a usable virtual-key code; "
              "using the default 0x%02X", key, raw, fallback);
    return fallback;
}

void ReadHotkeysSection(Config& cfg, const cameraunlock::IniReader& ini) {
    cfg.vk_toggle     = ReadVirtualKey(ini, "Toggle",    kDefaultVkToggle);
    cfg.vk_cycle_mode = ReadVirtualKey(ini, "CycleMode", kDefaultVkCycleMode);
    cfg.vk_yaw_mode   = ReadVirtualKey(ini, "YawMode",   kDefaultVkYawMode);
}

}  // namespace

ReadStatus Read(const char* iniPath, Config& cfg) {
    if (!FileExists(iniPath)) {
        return ReadStatus::Absent;
    }

    cameraunlock::IniReader ini;
    if (!ini.Open(iniPath)) {
        Log::Line("ERROR: Failed to open INI: %s", iniPath);
        return ReadStatus::OpenFailed;
    }

    if (!ReadGeneralSection(cfg, ini)) {
        return ReadStatus::PortRefused;
    }
    ReadSensitivitySection(cfg, ini);
    ReadSmoothingSection(cfg, ini);
    ReadPositionSection(cfg, ini);
    ReadGameplaySection(cfg, ini);
    ReadHotkeysSection(cfg, ini);
    return ReadStatus::Read;
}

}  // namespace FarCry6HeadTracking::legacy
