// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "config.h"

#include "logging.h"

#include "cameraunlock/config/ini_reader.h"
#include "cameraunlock/config/value_guards.h"

#include <windows.h>

#include <cstring>
#include <string>

namespace FarCry6HeadTracking {

namespace {

namespace guards = cameraunlock::config;

// Every number the user can type reaches the camera through these. Core owns them
// because each guards a hazard that is invisible without it: strtod parses a
// PREFIX, so "0,15" written with a European decimal comma yields 0.15 -> 0.0, in
// range, silently; GetPrivateProfileStringA does not strip an inline comment, so
// "0.15 ; settle" reaches the parser with the comment attached; and "nan" and
// "1e400" both parse. ReadFloatChecked requires the whole token and says so when
// it will not.
constexpr guards::LogSink kLogSink = &cameraunlock::logging::Line;

bool FileExists(const char* path) {
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

void WriteGeneralSection(cameraunlock::IniWriter& w) {
    w.WriteSection("General");
    w.WriteBool("EnableOnStartup", kDefaultEnableOnStartup);
    w.WriteComment("UDP port the tracker sends OpenTrack packets to. Point your tracker");
    w.WriteComment("output at this port.");
    w.WriteInt("Port", kDefaultPort);
}

void WriteSensitivitySection(cameraunlock::IniWriter& w) {
    w.WriteSection("Sensitivity");
    w.WriteDouble("Yaw", kDefaultSensitivity);
    w.WriteDouble("Pitch", kDefaultSensitivity);
    w.WriteDouble("Roll", kDefaultSensitivity);
    w.WriteComment("Flip an axis only if your tracker reports it backwards. The sign");
    w.WriteComment("conversion the game needs is already applied; these three ship off.");
    w.WriteBool("InvertYaw", kDefaultInvert);
    w.WriteBool("InvertPitch", kDefaultInvert);
    w.WriteBool("InvertRoll", kDefaultInvert);
}

void WriteSmoothingSection(cameraunlock::IniWriter& w) {
    w.WriteSection("Smoothing");
    w.WriteComment("Chosen per connection from the source address; covers rotation and position.");
    w.WriteComment("Only a loopback sender counts as local. A tracker on this PC that sends to");
    w.WriteComment("this machine LAN address instead of 127.0.0.1 is classified as remote.");
    w.WriteDouble("LocalSmoothing", kDefaultLocalSmoothing);
    w.WriteDouble("RemoteSmoothing", kDefaultRemoteSmoothing);
}

void WritePositionSection(cameraunlock::IniWriter& w) {
    w.WriteSection("Position");
    w.WriteComment("Positional tracking. Limits are metres of head travel.");
    w.WriteComment("The camera's collision query shortens a lean near an obstruction.");
    w.WriteBool("Enabled", kDefaultPositionEnabled);
    w.WriteDouble("SensitivityX", kDefaultPosSens);
    w.WriteDouble("SensitivityY", kDefaultPosSens);
    w.WriteDouble("SensitivityZ", kDefaultPosSens);
    w.WriteDouble("LimitX", kDefaultPosLimitX);
    w.WriteDouble("LimitY", kDefaultPosLimitY);
    w.WriteDouble("LimitZ", kDefaultPosLimitZ);
    w.WriteDouble("LimitZBack", kDefaultPosLimitZBack);
    w.WriteComment("As above: only for a tracker that reports an axis backwards. Leaving these");
    w.WriteComment("off is what keeps LimitZ on leaning in and LimitZBack on pulling away.");
    w.WriteBool("InvertX", kDefaultInvert);
    w.WriteBool("InvertY", kDefaultInvert);
    w.WriteBool("InvertZ", kDefaultInvert);
}

void WriteGameplaySection(cameraunlock::IniWriter& w) {
    w.WriteSection("Gameplay");
    w.WriteComment("Hold the view still while another player is in the session, so co-op runs");
    w.WriteComment("the stock camera. Set to 0 to keep head tracking in co-op.");
    w.WriteBool("DisableInCoop", kDefaultDisableInCoop);
    w.WriteComment("What head tracking does with the sights or binoculars up. paused: the view");
    w.WriteComment("settles onto the sights and only a head tilt still rolls it. tracked: the");
    w.WriteComment("view settles onto the sights, then head tracking carries on from there.");
    w.WriteComment("The ADS mode key cycles this and saves it here.");
    w.WriteString("AdsMode", AdsModeValue(kDefaultAdsMode));
}

void WriteHotkeysSection(cameraunlock::IniWriter& w) {
    w.WriteSection("Hotkeys");
    w.WriteComment("Virtual-key codes. Defaults: End (toggle), Page Up (cycle tracking mode),");
    w.WriteComment("Insert (cycle ADS mode).");
    w.WriteHex("Toggle", kDefaultVkToggle);
    w.WriteHex("CycleMode", kDefaultVkCycleMode);
    w.WriteHex("AdsMode", kDefaultVkAdsMode);
    w.WriteComment("Ctrl+Shift+Y (toggle), Ctrl+Shift+G (cycle tracking mode) and Ctrl+Shift+U");
    w.WriteComment("(cycle ADS mode) fire the same actions on a keyboard with no navigation");
    w.WriteComment("cluster. They are always registered.");
}

bool WriteDefaultIni(const char* path) {
    cameraunlock::IniWriter w;
    if (!w.Open(path)) return false;
    w.WriteComment("Far Cry 6 - Head Tracking configuration");
    w.WriteComment("Lives next to tobii_gameintegration_x64.dll in the game bin folder.");
    w.WriteBlankLine();
    WriteGeneralSection(w);
    w.WriteBlankLine();
    WriteSensitivitySection(w);
    w.WriteBlankLine();
    WriteSmoothingSection(w);
    w.WriteBlankLine();
    WritePositionSection(w);
    w.WriteBlankLine();
    WriteGameplaySection(w);
    w.WriteBlankLine();
    WriteHotkeysSection(w);
    w.Close();
    return true;
}

// Sensitivity is bounded as well as finite-checked. A finite value can still
// overflow the pose it multiplies, and the product reaching the transformation as
// infinity leaves the view somewhere the player cannot recover from.
float ReadSensitivity(const cameraunlock::IniReader& ini, const char* section,
                      const char* key, float fallback) {
    return guards::ReadFloatChecked(ini, section, key, fallback, -guards::kMaxSensitivity,
                                    guards::kMaxSensitivity, kLogSink);
}

// A positional limit is a distance in metres, so it is finite and above zero.
// Core bounds it to its own kMaxPositionLimit, which is generous rather than
// tight: it exists to catch a mistyped 10000 for 0.10, not to police tuning.
//
// Zero is then refused rather than passed on. Core clamps a negative up to 0,
// which is safe - it cannot invert PositionProcessor's clamp bounds the way a
// negative does - but a limit of zero pins the axis shut, and a typo that silently
// disables leaning is a worse answer than the shipped default. The follow-on line
// is worded to continue core's clamp message rather than contradict it.
float ReadLimit(const cameraunlock::IniReader& ini, const char* key, float fallback) {
    const float value = guards::ReadFloatChecked(ini, "Position", key, fallback, 0.0f,
                                                 guards::kMaxPositionLimit, kLogSink);
    if (value > 0.0f) return value;
    Log::Line("WARN: INI [Position] %s is not a usable distance, so the shipped "
              "default %.2f is used instead", key, static_cast<double>(fallback));
    return fallback;
}

// Validation, never a floor: any value in [0,1] reaches the processor untouched,
// 0.0 included.
float ReadSmoothing(const cameraunlock::IniReader& ini, const char* key, float fallback) {
    return guards::ReadFloatChecked(ini, "Smoothing", key, fallback, 0.0f, 1.0f, kLogSink);
}

bool ReadGeneralSection(Config& cfg, const cameraunlock::IniReader& ini) {
    cfg.enabled_on_startup = ini.ReadBool("General", "EnableOnStartup", kDefaultEnableOnStartup);
    // ReadIntInRange rather than ReadInt: ReadInt answers 0 for a present but
    // unparseable value rather than the default, so "Port=abc" would be reported
    // to the user as a port of 0 that they never typed.
    int port = kDefaultPort;
    if (!ini.ReadIntInRange("General", "Port", port, kMinPort, kMaxPort, kDefaultPort)) {
        // ReadIntInRange writes what it parsed before refusing, so the number the
        // user has to go and correct is named. It reads 0 for a value that is not a
        // number at all, which the range message covers.
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
    cfg.disable_in_coop = ini.ReadBool("Gameplay", "DisableInCoop", kDefaultDisableInCoop);

    const std::string ads = guards::ReadRawValue(ini, "Gameplay", "AdsMode");
    cfg.ads_mode = ParseFarCry6AdsMode(ads.c_str());
    if (!ads.empty() && _stricmp(ads.c_str(), AdsModeValue(cfg.ads_mode)) != 0) {
        Log::Line("WARN: INI [Gameplay] AdsMode=%s is not paused or tracked; using %s",
                  ads.c_str(), AdsModeValue(cfg.ads_mode));
    }
}

// A rebind the poller cannot act on is the worst kind of config error: the key never
// fires and the log says the binding was accepted. GetAsyncKeyState answers 0 for
// anything outside [1, 254], so report the rejection and fall back to the default.
//
// IsBindableVirtualKey, not input::IsValidHotkeyCode. The two deliberately
// disagree: this one asks "can this binding ever fire", which is the config
// reader's question, while the other is an allow list of the keys the fleet's own
// convention offers and would reject a perfectly pollable rebind like 0xC0.
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
    cfg.vk_ads_mode   = ReadVirtualKey(ini, "AdsMode",   kDefaultVkAdsMode);
}

}  // namespace

bool Config::LoadOrCreate(const char* iniPath) {
    if (!iniPath || !*iniPath) {
        Log::Line("ERROR: could not resolve the directory this mod was loaded from, so "
                  "there is nowhere to read the INI from. The mod will not start.");
        return false;
    }
    if (!FileExists(iniPath) && !WriteDefaultIni(iniPath)) {
        Log::Line("ERROR: could not create the default INI at %s. The game directory is "
                  "not writable by this account.", iniPath);
        return false;
    }

    cameraunlock::IniReader ini;
    if (!ini.Open(iniPath)) {
        Log::Line("ERROR: Failed to open INI: %s", iniPath);
        return false;
    }

    if (!ReadGeneralSection(*this, ini)) {
        return false;
    }
    ReadSensitivitySection(*this, ini);
    ReadSmoothingSection(*this, ini);
    ReadPositionSection(*this, ini);
    ReadGameplaySection(*this, ini);
    ReadHotkeysSection(*this, ini);
    return true;
}

}  // namespace FarCry6HeadTracking
