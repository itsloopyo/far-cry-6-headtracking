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
using cameraunlock::input::KeyModifiers;

// A legacy action's key list: its own key where the old build bound it, then the
// Ctrl+Shift chord it always registered beside it.
std::string KeyList(int vk, const char* key, char chordLetter, std::vector<cfg::DroppedValue>& dropped) {
    const std::string own = cfg::LegacyVirtualKeyToBindings(vk, "Hotkeys", key, dropped);
    const std::string chord = FormatKeyBindings({{KeyModifiers::kCtrl | KeyModifiers::kShift, chordLetter}});
    return own.empty() ? chord : own + ", " + chord;
}

// The key each action registered, 0 where the old build left it unbound because an
// earlier action already had that key.
struct LegacyHotkeyCodes {
    int toggle;
    int cycle_mode;
    int yaw_mode;
};

constexpr int kVkInsert = 0x2D;

LegacyHotkeyCodes EffectiveHotkeys(const legacy::Config& read) {
    const bool cycleKeyBound = read.vk_cycle_mode != read.vk_toggle;
    const bool yawKeyBound = read.vk_yaw_mode != read.vk_toggle && read.vk_yaw_mode != read.vk_cycle_mode;
    return {read.vk_toggle, cycleKeyBound ? read.vk_cycle_mode : 0, yawKeyBound ? read.vk_yaw_mode : 0};
}

// The legacy build had no true free look. Its key list comes after the other three, so where
// one of them already has Insert the list keeps only its chord, as the old build left a later
// action's key unbound.
bool InsertTaken(const LegacyHotkeyCodes& keys) {
    return keys.toggle == kVkInsert || keys.cycle_mode == kVkInsert || keys.yaw_mode == kVkInsert;
}

}  // namespace

cfg::ConfigTable<Config> ConfigTable() {
    using C = cfg::schema::Concept;
    cfg::ConfigTable<Config> table = cfg::HeadTrackingConfigTable<Config>(
        {C::UdpPort, C::EnableOnStartup, C::WorldSpaceYaw, C::RotationEnabled, C::LocalSmoothing,
         C::RemoteSmoothing, C::PositionEnabled, C::PositionLimitX, C::PositionLimitY, C::PositionLimitYDown,
         C::PositionLimitZ, C::PositionLimitZBack, C::TrueFreeLook, C::CollisionEnabled, C::CollisionMargin, C::CollisionChannel,
         C::CollisionReleaseSmoothing, C::ToggleKey, C::CycleTrackingModeKey, C::YawModeKey, C::TrueFreeLookKey, C::LightFollowsHead,
         C::LightMultiplier});
    table.Select(C::CollisionMargin)
        .Comment("How far, in metres, the view is held off a wall when you lean into it.\n"
                 "The mod holds it at least 0.05 metres past the camera's near clip distance.")
        .Select(C::CollisionChannel)
        .Comment("The game's collision layers the wall check tests against, as a bit mask written in decimal.");
    table.Select(C::WorldSpaceYaw).Writable()
        .Select(C::RotationEnabled).Writable()
        .Select(C::PositionEnabled).Writable()
        .Select(C::TrueFreeLook).Writable();
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

    // No key reached the lean's wall check: it always ran, on these values.
    out.collision_enabled = true;
    out.lean_clamp.skin = 0.10f;
    out.lean_clamp.release_smoothing = 0.9f;
    out.collision_channel = kCollisionLayerMask;

    const LegacyHotkeyCodes keys = EffectiveHotkeys(read);
    out.toggle_key_name = KeyList(keys.toggle, "Toggle", 'Y', dropped);
    out.cycle_tracking_mode_key_name = KeyList(keys.cycle_mode, "CycleMode", 'G', dropped);
    out.yaw_mode_key_name = KeyList(keys.yaw_mode, "YawMode", 'H', dropped);
    if (InsertTaken(keys)) {
        out.true_free_look_key_name = FormatKeyBindings({{KeyModifiers::kCtrl | KeyModifiers::kShift, 'U'}});
    }

    // A setting the player never changed from what the old build wrote on its first start
    // follows Defaults.ini. LimitY stood for both vertical bounds, and the lean's wall check
    // and the flashlight had no setting at all.
    using C = cfg::schema::Concept;
    const legacy::Config shipped;
    const LegacyHotkeyCodes shippedKeys = EffectiveHotkeys(shipped);
    cfg::LegacyFollowsDefaultsIni follows;
    follows.Setting(C::UdpPort, read.udp_port, shipped.udp_port);
    follows.Setting(C::EnableOnStartup, read.enabled_on_startup, shipped.enabled_on_startup);
    follows.Setting(C::WorldSpaceYaw, read.world_space_yaw, shipped.world_space_yaw);
    follows.TrackingMode(read.position_enabled, shipped.position_enabled);
    follows.Setting(C::LocalSmoothing, read.local_smoothing, shipped.local_smoothing);
    follows.Setting(C::RemoteSmoothing, read.remote_smoothing, shipped.remote_smoothing);
    follows.Setting(C::PositionLimitX, read.pos_limit_x, shipped.pos_limit_x);
    follows.Setting(C::PositionLimitY, read.pos_limit_y, shipped.pos_limit_y);
    follows.Setting(C::PositionLimitYDown, read.pos_limit_y, shipped.pos_limit_y);
    follows.Setting(C::PositionLimitZ, read.pos_limit_z, shipped.pos_limit_z);
    follows.Setting(C::PositionLimitZBack, read.pos_limit_z_back, shipped.pos_limit_z_back);
    follows.NotInLegacy(C::TrueFreeLook);
    follows.NotInLegacy(C::CollisionEnabled);
    follows.NotInLegacy(C::CollisionReleaseSmoothing);
    follows.Setting(C::ToggleKey, keys.toggle, shippedKeys.toggle);
    follows.Setting(C::CycleTrackingModeKey, keys.cycle_mode, shippedKeys.cycle_mode);
    follows.Setting(C::YawModeKey, keys.yaw_mode, shippedKeys.yaw_mode);
    follows.Setting(C::TrueFreeLookKey, !InsertTaken(keys));
    follows.NotInLegacy(C::LightFollowsHead);
    follows.NotInLegacy(C::LightMultiplier);

    return status == legacy::ReadStatus::Absent
               ? cfg::ImportResult::Absent(std::move(dropped), std::move(shaping), follows.Concepts())
               : cfg::ImportResult::Imported(std::move(dropped), std::move(shaping), follows.Concepts());
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
