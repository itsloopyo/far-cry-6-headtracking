// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/config_table.h"
#include "cameraunlock/config/head_tracking_config.h"
#include "cameraunlock/config/legacy_import.h"

#include <string>

namespace FarCry6HeadTracking {

namespace legacy {
struct Config;
enum class ReadStatus;
}  // namespace legacy

// The game's name as cameraunlock-core's data/games.json spells it. The settings file's
// first line names it.
constexpr char kGameDisplayName[] = "Far Cry 6";

// The settings file, beside the mod DLL in the game's bin folder.
constexpr wchar_t kConfigFileName[] = L"FarCry6HeadTracking.ini";

struct Config : cameraunlock::HeadTrackingConfig {
    // The game's co-op sessions leave the view under the mod's control the same way a
    // solo session does, and nothing about head tracking changes where a shot lands.
    // It is still switched off there by default so a session shared with another
    // player runs the stock camera.
    bool disable_in_coop = true;
};

// Every row of the settings file. WorldSpaceYaw and the tracking-mode pair are the rows
// the hotkeys save.
cameraunlock::config::ConfigTable<Config> ConfigTable();

// Reads a pre-canonical file through the frozen reader in legacy_config/, then maps it.
cameraunlock::config::LegacyImport<Config> LegacyConfigImport();

// The map from what the frozen reader read into the settings the mod runs on.
cameraunlock::config::ImportResult MapLegacyConfig(legacy::ReadStatus status, const legacy::Config& read,
                                                   Config& out);

cameraunlock::config::ConfigOwnerOptions<Config> ConfigOwnerOptionsFor(const std::wstring& path);

}  // namespace FarCry6HeadTracking
