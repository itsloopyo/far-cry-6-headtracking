// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "config.h"

#include "legacy_config/legacy_config.h"

#include "cameraunlock/config/head_tracking_config_table.h"
#include "cameraunlock/config/value_codecs.h"
#include "cameraunlock/input/key_bindings.h"

#include <utility>
#include <vector>

namespace FarCry6HeadTracking {

namespace {

namespace cfg = cameraunlock::config;
using cameraunlock::input::FormatKeyBindings;
using cameraunlock::input::KeyBinding;
using cameraunlock::input::KeyModifiers;

// A legacy action's key list: its own key where the old build bound it, then the
// Ctrl+Shift chord it always registered beside it.
std::string KeyList(const int* vk, char chordLetter) {
    std::vector<KeyBinding> bindings;
    if (vk != nullptr) bindings.push_back({KeyModifiers::kNone, *vk});
    bindings.push_back({KeyModifiers::kCtrl | KeyModifiers::kShift, chordLetter});
    return FormatKeyBindings(bindings);
}

}  // namespace

cfg::ConfigTable<Config> ConfigTable() {
    using C = cfg::schema::Concept;
    cfg::ConfigTable<Config> table = cfg::HeadTrackingConfigTable<Config>(
        {C::UdpPort, C::EnableOnStartup, C::WorldSpaceYaw, C::RotationEnabled, C::LocalSmoothing,
         C::RemoteSmoothing, C::PositionEnabled, C::PositionLimitX, C::PositionLimitY, C::PositionLimitYDown,
         C::PositionLimitZ, C::PositionLimitZBack, C::ToggleKey, C::CycleTrackingModeKey, C::YawModeKey,
         C::LightFollowsHead, C::LightMultiplier});
    table.Select(C::WorldSpaceYaw).Writable()
        .Select(C::RotationEnabled).Writable()
        .Select(C::PositionEnabled).Writable();
    table.Local("Gameplay", "DisableInCoop", &Config::disable_in_coop, cfg::BoolCodec(),
                "true: head tracking holds the view still while another player is in the session,\n"
                "so co-op runs the game's own camera.");
    return table;
}

cfg::ImportResult MapLegacyConfig(legacy::ReadStatus status, const legacy::Config& read, Config& out) {
    switch (status) {
        case legacy::ReadStatus::OpenFailed:
            return cfg::ImportResult::Refused("the file could not be opened");
        case legacy::ReadStatus::PortRefused:
            return cfg::ImportResult::Refused("[General] Port is not a whole number from 1024 to 65535");
        case legacy::ReadStatus::Read:
        case legacy::ReadStatus::Absent:
            break;
    }

    std::vector<cfg::DroppedValue> dropped;
    std::vector<cfg::PoseShapingValue> shaping;
    const auto shape = [&](auto value, auto shipped, const char* section, const char* key) {
        cfg::LegacyPoseShaping(value, shipped, section, key, shaping, dropped);
    };
    shape(read.sens_yaw, legacy::kDefaultSensitivity, "Sensitivity", "Yaw");
    shape(read.sens_pitch, legacy::kDefaultSensitivity, "Sensitivity", "Pitch");
    shape(read.sens_roll, legacy::kDefaultSensitivity, "Sensitivity", "Roll");
    shape(read.invert_yaw, legacy::kDefaultInvert, "Sensitivity", "InvertYaw");
    shape(read.invert_pitch, legacy::kDefaultInvert, "Sensitivity", "InvertPitch");
    shape(read.invert_roll, legacy::kDefaultInvert, "Sensitivity", "InvertRoll");
    shape(read.pos_sens_x, legacy::kDefaultPosSens, "Position", "SensitivityX");
    shape(read.pos_sens_y, legacy::kDefaultPosSens, "Position", "SensitivityY");
    shape(read.pos_sens_z, legacy::kDefaultPosSens, "Position", "SensitivityZ");
    shape(read.invert_pos_x, legacy::kDefaultInvert, "Position", "InvertX");
    shape(read.invert_pos_y, legacy::kDefaultInvert, "Position", "InvertY");
    shape(read.invert_pos_z, legacy::kDefaultInvert, "Position", "InvertZ");

    out.enable_on_startup = read.enabled_on_startup;
    out.udp_port = read.udp_port;
    out.local_smoothing = read.local_smoothing;
    out.position.local_smoothing = read.local_smoothing;
    out.remote_smoothing = read.remote_smoothing;
    out.position.remote_smoothing = read.remote_smoothing;

    // [Position] Enabled chose the startup mode and nothing else: the cycle still
    // reached every mode.
    out.rotation_enabled = true;
    out.position_enabled = read.position_enabled;
    out.position.limit_x = read.pos_limit_x;
    out.position.limit_y = read.pos_limit_y;
    out.position.limit_y_down = read.pos_limit_y;
    out.position.limit_z = read.pos_limit_z;
    out.position.limit_z_back = read.pos_limit_z_back;

    out.disable_in_coop = read.disable_in_coop;
    out.world_space_yaw = read.world_space_yaw;

    // The old build left a key another action already had unbound for the later
    // action, so the list carries only its chord there.
    out.toggle_key_name = KeyList(&read.vk_toggle, 'Y');
    const bool cycleKeyBound = read.vk_cycle_mode != read.vk_toggle;
    out.cycle_tracking_mode_key_name = KeyList(cycleKeyBound ? &read.vk_cycle_mode : nullptr, 'G');
    const bool yawKeyBound = read.vk_yaw_mode != read.vk_toggle && read.vk_yaw_mode != read.vk_cycle_mode;
    out.yaw_mode_key_name = KeyList(yawKeyBound ? &read.vk_yaw_mode : nullptr, 'H');

    return status == legacy::ReadStatus::Absent ? cfg::ImportResult::Absent(std::move(dropped), std::move(shaping))
                                                : cfg::ImportResult::Imported(std::move(dropped), std::move(shaping));
}

cfg::LegacyImport<Config> LegacyConfigImport() {
    cfg::LegacyImport<Config> import;
    import.run = [](const cfg::LegacyInput& input, Config& out) {
        legacy::Config read;
        const legacy::ReadStatus status = legacy::Read(input.ansi_path.c_str(), read);
        return MapLegacyConfig(status, read, out);
    };
    import.keys = {
        {"General", "EnableOnStartup"}, {"General", "Port"},
        {"Sensitivity", "Yaw"},         {"Sensitivity", "Pitch"},        {"Sensitivity", "Roll"},
        {"Sensitivity", "InvertYaw"},   {"Sensitivity", "InvertPitch"},  {"Sensitivity", "InvertRoll"},
        {"Smoothing", "LocalSmoothing"}, {"Smoothing", "RemoteSmoothing"},
        {"Position", "Enabled"},
        {"Position", "SensitivityX"},   {"Position", "SensitivityY"},    {"Position", "SensitivityZ"},
        {"Position", "LimitX"},         {"Position", "LimitY"},          {"Position", "LimitZ"},
        {"Position", "LimitZBack"},
        {"Position", "InvertX"},        {"Position", "InvertY"},         {"Position", "InvertZ"},
        {"Gameplay", "WorldSpaceYaw"},  {"Gameplay", "DisableInCoop"},
        {"Hotkeys", "Toggle"},          {"Hotkeys", "CycleMode"},        {"Hotkeys", "YawMode"},
    };
    return import;
}

cfg::ConfigOwnerOptions<Config> ConfigOwnerOptionsFor(const std::wstring& folder, cfg::DefaultsFile defaults) {
    cfg::ConfigOwnerOptions<Config> options;
    options.path = folder + L"\\" + kConfigFileName;
    options.table = ConfigTable();
    options.import = LegacyConfigImport();
    options.legacy_path = folder + L"\\" + kLegacyConfigFileName;
    options.header.display_name = kGameDisplayName;
    options.defaults = std::move(defaults);
    return options;
}

}  // namespace FarCry6HeadTracking
