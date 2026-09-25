// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "config.h"

#include "logging.h"
#include "legacy_config/legacy_config.h"

#include "cameraunlock/config/ini_reader.h"

#include <windows.h>

#include <cstring>
#include <string>

namespace FarCry6HeadTracking {

namespace {

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
    w.WriteComment("1: yaw about the game's reference up axis; 0: yaw about camera up.");
    w.WriteBool("WorldSpaceYaw", kDefaultWorldSpaceYaw);
}

void WriteHotkeysSection(cameraunlock::IniWriter& w) {
    w.WriteSection("Hotkeys");
    w.WriteComment("Virtual-key codes. Defaults: End (toggle), Page Up (cycle tracking mode),");
    w.WriteComment("Page Down (world/local yaw).");
    w.WriteHex("Toggle", kDefaultVkToggle);
    w.WriteHex("CycleMode", kDefaultVkCycleMode);
    w.WriteHex("YawMode", kDefaultVkYawMode);
    w.WriteComment("Ctrl+Shift+H also switches world/local yaw.");
    w.WriteComment("Ctrl+Shift+Y (toggle) and Ctrl+Shift+G (cycle tracking mode) fire the same");
    w.WriteComment("actions on a keyboard with no navigation cluster. They are always registered.");
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

    legacy::Config read;
    const legacy::ReadStatus status = legacy::Read(iniPath, read);
    if (status == legacy::ReadStatus::Absent) {
        Log::Line("ERROR: Failed to open INI: %s", iniPath);
        return false;
    }
    enabled_on_startup = read.enabled_on_startup;
    udp_port = read.udp_port;
    sens_yaw = read.sens_yaw;
    sens_pitch = read.sens_pitch;
    sens_roll = read.sens_roll;
    invert_yaw = read.invert_yaw;
    invert_pitch = read.invert_pitch;
    invert_roll = read.invert_roll;
    local_smoothing = read.local_smoothing;
    remote_smoothing = read.remote_smoothing;
    position_enabled = read.position_enabled;
    pos_sens_x = read.pos_sens_x;
    pos_sens_y = read.pos_sens_y;
    pos_sens_z = read.pos_sens_z;
    pos_limit_x = read.pos_limit_x;
    pos_limit_y = read.pos_limit_y;
    pos_limit_z = read.pos_limit_z;
    pos_limit_z_back = read.pos_limit_z_back;
    invert_pos_x = read.invert_pos_x;
    invert_pos_y = read.invert_pos_y;
    invert_pos_z = read.invert_pos_z;
    disable_in_coop = read.disable_in_coop;
    world_space_yaw = read.world_space_yaw;
    vk_toggle = read.vk_toggle;
    vk_cycle_mode = read.vk_cycle_mode;
    vk_yaw_mode = read.vk_yaw_mode;
    return status == legacy::ReadStatus::Read;
}

}  // namespace FarCry6HeadTracking
