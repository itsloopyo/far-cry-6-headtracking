// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The config differential test. Every input is read three ways:
//
//   oracle     v0.1.0's reader (oracle/), the newest published build, and v0.1.0's startup code
//   import     the frozen reader in src/legacy_config/, and the startup code it ran under
//   migration  the config owner converting the file, then the canonical reader and table on
//              the result, and the startup code of this build
//
// Comparison 1, oracle against import, finds what a player updating from v0.1.0 sees change
// that the conversion did not cause. Every difference it may find is listed in
// kComparisonOneDifferences with the commit that made it; any other fails the test.
//
// Comparison 2, import against migration, is the proof for the migration: no difference but
// a sensitivity or axis inversion the player set away from its shipped identity value, which
// the import must list as pose shaping and drop (approved change pose_shaping). No default
// moved, so the no-file input has no difference either.
//
// The distinct migrated files are written beside the executable under migrated\, for
// lint-migrated.mjs to run core's canonical config lint over.
//
// Inputs: no file, an empty file, the first-run output of each published build (dev at
// dec1791 and v0.1.0, extracted once into inputs/), a few hand-written files for the listed
// differences, and core's corpus over v0.1.0's first-run output. Neither published build
// shipped a config file or a launcher seed.

#include "config.h"
#include "legacy_config/legacy_config.h"
#include "oracle/oracle_config.h"

#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/legacy_import.h"
#include "cameraunlock/config/testing/ini_mutations.h"
#include "cameraunlock/input/key_bindings.h"
#include "cameraunlock/tracking/tracking_mode.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace {

namespace legacy = FarCry6HeadTracking::legacy;
namespace oracle = FarCry6HeadTracking::oracle;
namespace cfg = cameraunlock::config;
using FarCry6HeadTracking::Config;
using cameraunlock::TrackingMode;
using cameraunlock::config::LegacyKey;
using cameraunlock::config::testing::GenerateIniMutations;
using cameraunlock::config::testing::IniMutation;
using cameraunlock::config::testing::MutationKey;
using cameraunlock::input::KeyModifiers;

int g_failures = 0;

void Fail(const std::string& input, const std::string& what) {
    if (g_failures < 50) std::printf("FAIL [%s]: %s\n", input.c_str(), what.c_str());
    ++g_failures;
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

std::wstring Widen(const std::string& s) {
    return std::wstring(s.begin(), s.end());
}

std::string Narrow(const std::wstring& path) {
    const int size = WideCharToMultiByte(CP_ACP, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(size), 'x');
    WideCharToMultiByte(CP_ACP, 0, path.c_str(), -1, out.data(), size, nullptr, nullptr);
    out.resize(static_cast<size_t>(size) - 1);
    return out;
}

std::string ReadBytes(const std::wstring& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + Narrow(path));
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteBytes(const std::wstring& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write " + Narrow(path));
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// Every file in the folder, name and bytes, for "the import changed nothing".
std::map<std::wstring, std::string> Snapshot(const std::wstring& dir) {
    std::map<std::wstring, std::string> files;
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot list the test folder");
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        files[data.cFileName] = ReadBytes(dir + L"\\" + data.cFileName);
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return files;
}

void EmptyFolder(const std::wstring& dir) {
    for (const auto& [name, bytes] : Snapshot(dir)) {
        const std::wstring path = dir + L"\\" + name;
        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
        if (!DeleteFileW(path.c_str())) throw std::runtime_error("cannot empty the test folder");
    }
}

std::wstring MakeFolder(const std::wstring& parent, const wchar_t* name) {
    const std::wstring dir = parent + L"\\" + name;
    if (!CreateDirectoryW(dir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        throw std::runtime_error("cannot create the test folder");
    }
    EmptyFolder(dir);
    return dir;
}

// ---------------------------------------------------------------------------
// Startup state: what each build does with its Config
// ---------------------------------------------------------------------------

enum class Action { Toggle, CycleMode, YawMode, AdsMode };

const char* ActionName(Action a) {
    switch (a) {
        case Action::Toggle: return "toggle";
        case Action::CycleMode: return "cycle mode";
        case Action::YawMode: return "yaw mode";
        case Action::AdsMode: return "ADS mode";
    }
    throw std::logic_error("action");
}

// One key the poller watches for an action, and the modifiers it fires with: none is
// NavGuarded (not while Ctrl and Shift are both held), Ctrl+Shift is ChordGuarded.
struct Registration {
    Action action;
    int vk;
    unsigned modifiers;

    bool operator<(const Registration& o) const {
        return std::tie(action, vk, modifiers) < std::tie(o.action, o.vk, o.modifiers);
    }
    bool operator==(const Registration& o) const {
        return action == o.action && vk == o.vk && modifiers == o.modifiers;
    }
};

constexpr unsigned kNav = static_cast<unsigned>(KeyModifiers::kNone);
constexpr unsigned kChord = static_cast<unsigned>(KeyModifiers::kCtrl | KeyModifiers::kShift);

std::string Describe(const std::vector<Registration>& regs) {
    std::string out;
    for (const Registration& r : regs) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s%s:%s0x%02X", out.empty() ? "" : " ", ActionName(r.action),
                      r.modifiers == kChord ? "Ctrl+Shift+" : "", r.vk);
        out += buf;
    }
    return out;
}

// v0.1.0's StartHotkeys (src/hotkeys.cpp at faf077b), with the poller calls recorded.
std::vector<Registration> OracleHotkeys(const oracle::Config& cfg) {
    std::vector<Registration> regs;
    regs.push_back({Action::Toggle, cfg.vk_toggle, kNav});
    const bool cycleKeyBound = cfg.vk_cycle_mode != cfg.vk_toggle;
    if (cycleKeyBound) regs.push_back({Action::CycleMode, cfg.vk_cycle_mode, kNav});
    const bool adsKeyBound = cfg.vk_ads_mode != cfg.vk_toggle && cfg.vk_ads_mode != cfg.vk_cycle_mode;
    if (adsKeyBound) regs.push_back({Action::AdsMode, cfg.vk_ads_mode, kNav});
    regs.push_back({Action::Toggle, 'Y', kChord});
    regs.push_back({Action::CycleMode, 'G', kChord});
    regs.push_back({Action::AdsMode, 'U', kChord});
    regs.push_back({Action::YawMode, 'H', kChord});
    if (cfg.vk_yaw_mode != cfg.vk_toggle && cfg.vk_yaw_mode != cfg.vk_cycle_mode &&
        cfg.vk_yaw_mode != cfg.vk_ads_mode) {
        regs.push_back({Action::YawMode, cfg.vk_yaw_mode, kNav});
    }
    std::sort(regs.begin(), regs.end());
    return regs;
}

// StartHotkeys as the build that carries the frozen reader runs it (src/hotkeys.cpp at
// 5ea3ff9), with the poller calls recorded.
std::vector<Registration> ImportHotkeys(const legacy::Config& cfg) {
    std::vector<Registration> regs;
    regs.push_back({Action::Toggle, cfg.vk_toggle, kNav});
    if (cfg.vk_cycle_mode != cfg.vk_toggle) regs.push_back({Action::CycleMode, cfg.vk_cycle_mode, kNav});
    regs.push_back({Action::Toggle, 'Y', kChord});
    regs.push_back({Action::CycleMode, 'G', kChord});
    regs.push_back({Action::YawMode, 'H', kChord});
    if (cfg.vk_yaw_mode != cfg.vk_toggle && cfg.vk_yaw_mode != cfg.vk_cycle_mode) {
        regs.push_back({Action::YawMode, cfg.vk_yaw_mode, kNav});
    }
    std::sort(regs.begin(), regs.end());
    return regs;
}

uint32_t Bits(float f) {
    uint32_t b;
    std::memcpy(&b, &f, sizeof(b));
    return b;
}

// Every field the two Configs share, floats bit for bit. The oracle's ADS fields have no
// counterpart; comparison 1 lists them.
template <class A, class B>
std::vector<std::string> SharedFieldDifferences(const A& a, const B& b) {
    std::vector<std::string> out;
    const auto check = [&out](bool same, const char* name) {
        if (!same) out.push_back(name);
    };
#define SAME(f) check(a.f == b.f, #f)
#define SAME_BITS(f) check(Bits(a.f) == Bits(b.f), #f)
    SAME(enabled_on_startup);
    SAME(udp_port);
    SAME_BITS(sens_yaw);
    SAME_BITS(sens_pitch);
    SAME_BITS(sens_roll);
    SAME(invert_yaw);
    SAME(invert_pitch);
    SAME(invert_roll);
    SAME_BITS(local_smoothing);
    SAME_BITS(remote_smoothing);
    SAME(position_enabled);
    SAME_BITS(pos_sens_x);
    SAME_BITS(pos_sens_y);
    SAME_BITS(pos_sens_z);
    SAME_BITS(pos_limit_x);
    SAME_BITS(pos_limit_y);
    SAME_BITS(pos_limit_z);
    SAME_BITS(pos_limit_z_back);
    SAME(invert_pos_x);
    SAME(invert_pos_y);
    SAME(invert_pos_z);
    SAME(disable_in_coop);
    SAME(world_space_yaw);
    SAME(vk_toggle);
    SAME(vk_cycle_mode);
    SAME(vk_yaw_mode);
#undef SAME
#undef SAME_BITS
    return out;
}

// ---------------------------------------------------------------------------
// Comparison 1: v0.1.0 against the frozen reader
// ---------------------------------------------------------------------------

// What a player updating from v0.1.0 sees change, and the commit that made each change.
// The changelog carries the same list.
struct ListedDifference {
    const char* id;
    const char* commit;
    const char* what;
    int seen = 0;
};

ListedDifference kComparisonOneDifferences[] = {
    {"ads-mode", "faefd67",
     "[Gameplay] AdsMode is no longer read: head tracking carries on through the sights in every "
     "case, and the lean eases out while they are up"},
    {"ads-key", "faefd67",
     "[Hotkeys] AdsMode is no longer read, and neither it nor Ctrl+Shift+U cycles an ADS mode"},
    {"yaw-key-on-ads-key", "faefd67",
     "a [Hotkeys] YawMode on the same key as [Hotkeys] AdsMode was left unbound, and now switches "
     "the yaw mode"},
};

ListedDifference& Listed(const char* id) {
    for (ListedDifference& d : kComparisonOneDifferences) {
        if (std::strcmp(d.id, id) == 0) return d;
    }
    throw std::logic_error(id);
}

struct OracleRun {
    bool usable = false;
    oracle::Config cfg;
};

struct ImportRun {
    legacy::ReadStatus status = legacy::ReadStatus::Read;
    legacy::Config cfg;
};

bool ImportUsable(legacy::ReadStatus s) {
    return s == legacy::ReadStatus::Read || s == legacy::ReadStatus::Absent;
}

void CompareOracleWithImport(const std::string& name, const OracleRun& o, const ImportRun& i) {
    if (o.usable != ImportUsable(i.status)) {
        Fail(name, std::string("v0.1.0 ") + (o.usable ? "starts" : "does not start") + ", the import " +
                       (ImportUsable(i.status) ? "starts" : "does not start"));
        return;
    }
    if (!o.usable) return;

    for (const std::string& field : SharedFieldDifferences(o.cfg, i.cfg)) {
        Fail(name, "comparison 1: " + field + " differs from v0.1.0 with no listed reason");
    }

    if (o.cfg.ads_mode != oracle::kDefaultAdsMode) ++Listed("ads-mode").seen;
    ++Listed("ads-key").seen;

    // v0.1.0's registrations with the listed differences applied give the import's.
    std::vector<Registration> expected;
    for (const Registration& r : OracleHotkeys(o.cfg)) {
        if (r.action != Action::AdsMode) expected.push_back(r);
    }
    const bool yawOnlyOnAds = o.cfg.vk_yaw_mode == o.cfg.vk_ads_mode &&
                              o.cfg.vk_yaw_mode != o.cfg.vk_toggle && o.cfg.vk_yaw_mode != o.cfg.vk_cycle_mode;
    if (yawOnlyOnAds) {
        expected.push_back({Action::YawMode, o.cfg.vk_yaw_mode, kNav});
        ++Listed("yaw-key-on-ads-key").seen;
    }
    std::sort(expected.begin(), expected.end());
    const std::vector<Registration> actual = ImportHotkeys(i.cfg);
    if (expected != actual) {
        Fail(name, "comparison 1: hotkeys " + Describe(actual) + ", v0.1.0 less the listed differences " +
                       Describe(expected));
    }
}

// ---------------------------------------------------------------------------
// The keys the frozen reader reads, and the corpus descriptors
// ---------------------------------------------------------------------------

std::vector<MutationKey> CorpusKeys() {
    const auto boolean = [](const char* s, const char* k) { return MutationKey{s, k, "0", {}, false, {}}; };
    const auto sens = [](const char* s, const char* k) { return MutationKey{s, k, "0.5", {"150"}, false, {}}; };
    const auto limit = [](const char* k) { return MutationKey{"Position", k, "0.25", {"10.5", "0"}, false, {}}; };
    const auto smooth = [](const char* k) { return MutationKey{"Smoothing", k, "0.3", {"1.5"}, false, {}}; };
    const auto hotkey = [](const char* k, const char* alt) { return MutationKey{"Hotkeys", k, alt, {"0x10"}, true, {}}; };
    const auto invert = [](const char* s, const char* k) { return MutationKey{s, k, "1", {}, false, {}}; };
    return {
        boolean("General", "EnableOnStartup"),
        MutationKey{"General", "Port", "5000", {"1023", "65536"}, false, {}},
        sens("Sensitivity", "Yaw"),
        sens("Sensitivity", "Pitch"),
        sens("Sensitivity", "Roll"),
        invert("Sensitivity", "InvertYaw"),
        invert("Sensitivity", "InvertPitch"),
        invert("Sensitivity", "InvertRoll"),
        smooth("LocalSmoothing"),
        smooth("RemoteSmoothing"),
        boolean("Position", "Enabled"),
        sens("Position", "SensitivityX"),
        sens("Position", "SensitivityY"),
        sens("Position", "SensitivityZ"),
        limit("LimitX"),
        limit("LimitY"),
        limit("LimitZ"),
        limit("LimitZBack"),
        invert("Position", "InvertX"),
        invert("Position", "InvertY"),
        invert("Position", "InvertZ"),
        boolean("Gameplay", "WorldSpaceYaw"),
        boolean("Gameplay", "DisableInCoop"),
        hotkey("Toggle", "0x70"),
        hotkey("CycleMode", "0x71"),
        hotkey("YawMode", "0x72"),
    };
}

// ---------------------------------------------------------------------------
// Comparison 2: the frozen reader against the migration
// ---------------------------------------------------------------------------

// What the mod starts with. Pose shaping is not here: the migrated build applies none, and
// CheckPoseShaping holds the import to listing every value it leaves out.
struct Startup {
    int port = 0;
    bool enabled = false;
    TrackingMode mode = TrackingMode::RotationAndPosition;
    bool world_yaw = false;
    bool disable_in_coop = false;
    uint32_t local_smoothing = 0;
    uint32_t remote_smoothing = 0;
    uint32_t limit_x = 0;
    uint32_t limit_y = 0;
    uint32_t limit_y_down = 0;
    uint32_t limit_z = 0;
    uint32_t limit_z_back = 0;
    std::vector<Registration> hotkeys;
};

// The frozen reader's build: [Position] Enabled chose between the first two modes, and its
// one vertical limit bounded both directions (TrackingRuntime::ConfigurePosition at 5ea3ff9).
Startup FromImport(const legacy::Config& c) {
    Startup s;
    s.port = c.udp_port;
    s.enabled = c.enabled_on_startup;
    s.mode = c.position_enabled ? TrackingMode::RotationAndPosition : TrackingMode::RotationOnly;
    s.world_yaw = c.world_space_yaw;
    s.disable_in_coop = c.disable_in_coop;
    s.local_smoothing = Bits(c.local_smoothing);
    s.remote_smoothing = Bits(c.remote_smoothing);
    s.limit_x = Bits(c.pos_limit_x);
    s.limit_y = Bits(c.pos_limit_y);
    s.limit_y_down = Bits(c.pos_limit_y);
    s.limit_z = Bits(c.pos_limit_z);
    s.limit_z_back = Bits(c.pos_limit_z_back);
    s.hotkeys = ImportHotkeys(c);
    return s;
}

void AddKeyList(std::vector<Registration>& regs, Action action, const std::string& list) {
    const cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(list);
    if (!parsed.ok()) throw std::logic_error("'" + list + "' is not a key list: " + parsed.error);
    for (const cameraunlock::input::KeyBinding& b : parsed.bindings) {
        regs.push_back({action, b.vk, static_cast<unsigned>(b.modifiers)});
    }
}

// This build: TrackingRuntime::Start and StartHotkeys.
Startup FromMigration(const Config& c) {
    Startup s;
    s.port = c.udp_port;
    s.enabled = c.enable_on_startup;
    s.mode = cameraunlock::DecodeTrackingMode(c.rotation_enabled, c.position_enabled).value();
    s.world_yaw = c.world_space_yaw;
    s.disable_in_coop = c.disable_in_coop;
    s.local_smoothing = Bits(c.local_smoothing);
    s.remote_smoothing = Bits(c.remote_smoothing);
    s.limit_x = Bits(c.position.limit_x);
    s.limit_y = Bits(c.position.limit_y);
    s.limit_y_down = Bits(c.position.limit_y_down);
    s.limit_z = Bits(c.position.limit_z);
    s.limit_z_back = Bits(c.position.limit_z_back);
    AddKeyList(s.hotkeys, Action::Toggle, c.toggle_key_name);
    AddKeyList(s.hotkeys, Action::CycleMode, c.cycle_tracking_mode_key_name);
    AddKeyList(s.hotkeys, Action::YawMode, c.yaw_mode_key_name);
    std::sort(s.hotkeys.begin(), s.hotkeys.end());
    return s;
}

std::vector<std::string> StartupDifferences(const Startup& a, const Startup& b) {
    std::vector<std::string> out;
#define SAME(f) \
    if (a.f != b.f) out.push_back(#f)
    SAME(port);
    SAME(enabled);
    SAME(mode);
    SAME(world_yaw);
    SAME(disable_in_coop);
    SAME(local_smoothing);
    SAME(remote_smoothing);
    SAME(limit_x);
    SAME(limit_y);
    SAME(limit_y_down);
    SAME(limit_z);
    SAME(limit_z_back);
#undef SAME
    if (a.hotkeys != b.hotkeys) out.push_back("hotkeys " + Describe(a.hotkeys) + " against " + Describe(b.hotkeys));
    return out;
}

// The only difference comparison 2 allows: every sensitivity and inversion the frozen
// reader read is listed, folded where it holds the shipped identity value, and dropped as
// PoseShaping where it does not. Nothing else is dropped.
int CheckPoseShaping(const std::string& name, const legacy::Config& c, const cfg::ImportResult& result) {
    struct Read {
        const char* section;
        const char* key;
        bool shipped;
    };
    const Read reads[] = {
        {"Sensitivity", "Yaw", c.sens_yaw == legacy::kDefaultSensitivity},
        {"Sensitivity", "Pitch", c.sens_pitch == legacy::kDefaultSensitivity},
        {"Sensitivity", "Roll", c.sens_roll == legacy::kDefaultSensitivity},
        {"Sensitivity", "InvertYaw", c.invert_yaw == legacy::kDefaultInvert},
        {"Sensitivity", "InvertPitch", c.invert_pitch == legacy::kDefaultInvert},
        {"Sensitivity", "InvertRoll", c.invert_roll == legacy::kDefaultInvert},
        {"Position", "SensitivityX", c.pos_sens_x == legacy::kDefaultPosSens},
        {"Position", "SensitivityY", c.pos_sens_y == legacy::kDefaultPosSens},
        {"Position", "SensitivityZ", c.pos_sens_z == legacy::kDefaultPosSens},
        {"Position", "InvertX", c.invert_pos_x == legacy::kDefaultInvert},
        {"Position", "InvertY", c.invert_pos_y == legacy::kDefaultInvert},
        {"Position", "InvertZ", c.invert_pos_z == legacy::kDefaultInvert},
    };
    if (result.pose_shaping.size() != std::size(reads)) {
        Fail(name, "the import lists " + std::to_string(result.pose_shaping.size()) + " pose-shaping values, not 12");
        return 0;
    }
    int dropped = 0;
    for (size_t k = 0; k < std::size(reads); ++k) {
        const cfg::PoseShapingValue& v = result.pose_shaping[k];
        const std::string label = std::string("[") + reads[k].section + "] " + reads[k].key;
        if (v.section != reads[k].section || v.key != reads[k].key) Fail(name, label + " is not listed in its place");
        if (v.folded != reads[k].shipped) Fail(name, label + " is " + (v.folded ? "folded" : "dropped") + " wrongly");
        const bool listed = std::any_of(result.dropped.begin(), result.dropped.end(), [&](const cfg::DroppedValue& d) {
            return d.rule == cfg::DropRule::PoseShaping && d.section == reads[k].section && d.key == reads[k].key;
        });
        if (listed == reads[k].shipped) Fail(name, label + (listed ? " is dropped at its shipped value" : " is changed and not dropped"));
        if (!reads[k].shipped) ++dropped;
    }
    if (static_cast<int>(result.dropped.size()) != dropped) Fail(name, "the import drops a value no approved change names");
    return dropped;
}

struct MigrationTally {
    std::string committed;
    std::set<std::string> migrated;
    int created = 0;
    int converted = 0;
    int refused = 0;
    int with_pose_shaping_dropped = 0;
};

std::string Render(const Config& c) {
    return cfg::RenderCanonical(FarCry6HeadTracking::ConfigTable(), c, {FarCry6HeadTracking::kGameDisplayName});
}

// ---------------------------------------------------------------------------
// The run
// ---------------------------------------------------------------------------

struct Folders {
    std::wstring oracle;
    std::wstring import;
    std::wstring migration;
};

const wchar_t kIniName[] = L"FarCry6HeadTracking.ini";

void MigrateInput(const Folders& f, const std::string& name, const std::optional<std::string>& bytes,
                  const ImportRun& i, const cfg::ImportResult* result, MigrationTally& tally) {
    using cfg::ConfigLoadStatus;
    EmptyFolder(f.migration);
    const std::wstring path = f.migration + L"\\" + kIniName;
    if (bytes) WriteBytes(path, *bytes);
    cfg::ConfigOwner<Config> owner(FarCry6HeadTracking::ConfigOwnerOptionsFor(path));
    const cfg::ConfigLoadResult<Config> loaded = owner.Load();
    const std::map<std::wstring, std::string> after = Snapshot(f.migration);

    if (!bytes) {
        ++tally.created;
        if (loaded.status != ConfigLoadStatus::Created) Fail(name, "no file is not Created");
        if (ReadBytes(path) != tally.committed) Fail(name, "the created file is not config/FarCry6HeadTracking.ini");
    } else if (!ImportUsable(i.status)) {
        ++tally.refused;
        if (loaded.status != ConfigLoadStatus::LegacyRefused) Fail(name, "a file the import refuses is not LegacyRefused");
        if (after != std::map<std::wstring, std::string>{{kIniName, *bytes}}) {
            Fail(name, "a refused file did not keep its bytes, or got a copy");
        }
        return;
    } else {
        ++tally.converted;
        if (loaded.status != ConfigLoadStatus::Migrated) {
            Fail(name, std::string("the migration is ") + cfg::ConfigLoadStatusName(loaded.status) + ": " + loaded.reason);
            return;
        }
        const auto copy = after.find(std::wstring(kIniName) + L".pre-canonical");
        if (copy == after.end() || copy->second != *bytes) Fail(name, ".pre-canonical is not the input");
        if (after.size() != 2) Fail(name, "the migration left files other than the config and its copy");
        tally.with_pose_shaping_dropped += CheckPoseShaping(name, i.cfg, *result) > 0 ? 1 : 0;
    }

    const std::string migrated = ReadBytes(path);
    if (Render(loaded.config) != migrated) Fail(name, "rendering the re-read Config does not give the migrated bytes");
    for (const std::string& d : StartupDifferences(FromImport(i.cfg), FromMigration(loaded.config))) {
        Fail(name, "comparison 2: " + d);
    }
    tally.migrated.insert(migrated);

    cfg::ConfigOwner<Config> again(FarCry6HeadTracking::ConfigOwnerOptionsFor(path));
    const cfg::ConfigLoadResult<Config> reread = again.Load();
    if (reread.status != ConfigLoadStatus::Canonical || !reread.diagnostics.empty() ||
        Render(reread.config) != migrated || Snapshot(f.migration) != after ||
        ReadBytes(path) != migrated) {
        Fail(name, "migrating the migrated file does something");
    }
}

void RunInput(const Folders& f, const std::string& name, const std::optional<std::string>& bytes,
              MigrationTally& tally) {
    OracleRun o;
    {
        EmptyFolder(f.oracle);
        const std::wstring path = f.oracle + L"\\" + kIniName;
        if (bytes) WriteBytes(path, *bytes);
        o.usable = o.cfg.LoadOrCreate(Narrow(path).c_str());
    }

    ImportRun i;
    std::optional<cfg::ImportResult> result;
    {
        EmptyFolder(f.import);
        const std::wstring path = f.import + L"\\" + kIniName;
        if (bytes) {
            WriteBytes(path, *bytes);
            SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY);
        }
        const auto before = Snapshot(f.import);
        i.status = legacy::Read(Narrow(path).c_str(), i.cfg);
        if (bytes) {
            Config mapped = FarCry6HeadTracking::ConfigTable().defaults();
            result = FarCry6HeadTracking::LegacyConfigImport().run(cfg::LegacyInput{path, Narrow(path), false}, mapped);
        }
        if (Snapshot(f.import) != before) Fail(name, "the import changed the folder it read from");
        if (!bytes && i.status != legacy::ReadStatus::Absent) Fail(name, "the import read a file that is not there");
    }

    CompareOracleWithImport(name, o, i);
    MigrateInput(f, name, bytes, i, result ? &*result : nullptr, tally);
}

std::string ReadInput(const std::string& file) {
    return ReadBytes(Widen(std::string(FARCRY6_DIFFERENTIAL_INPUTS) + "/" + file));
}

std::string WithLine(std::string base, const std::string& after, const std::string& line) {
    const size_t at = base.find(after);
    if (at == std::string::npos) throw std::logic_error("no " + after + " in the base file");
    base.insert(base.find('\n', at) + 1, line + "\r\n");
    return base;
}

std::string Replaced(std::string base, const std::string& from, const std::string& to) {
    const size_t at = base.find(from);
    if (at == std::string::npos) throw std::logic_error("no " + from + " in the base file");
    return base.replace(at, from.size(), to);
}

void TestFrozenDefaults() {
    const oracle::Config o;
    const legacy::Config l;
    for (const std::string& field : SharedFieldDifferences(o, l)) {
        Fail("defaults", field + ": the frozen default differs from v0.1.0's");
    }
}

}  // namespace

int main() {
    try {
        wchar_t temp[MAX_PATH];
        GetTempPathW(MAX_PATH, temp);
        const std::wstring root = std::wstring(temp) + L"farcry6-config-differential-" +
                                  std::to_wstring(GetCurrentProcessId());
        CreateDirectoryW(root.c_str(), nullptr);
        const Folders folders{MakeFolder(root, L"oracle"), MakeFolder(root, L"import"),
                              MakeFolder(root, L"migration")};
        MigrationTally tally;
        tally.committed = ReadBytes(Widen(FARCRY6_COMMITTED_CONFIG));

        TestFrozenDefaults();

        const std::string firstRunDev = ReadInput("first-run-dec1791.ini");
        const std::string firstRun = ReadInput("first-run-v0.1.0.ini");
        {
            EmptyFolder(folders.oracle);
            const std::wstring path = folders.oracle + L"\\" + kIniName;
            oracle::Config created;
            if (!created.LoadOrCreate(Narrow(path).c_str()) || ReadBytes(path) != firstRun) {
                Fail("first run", "the oracle's first-run output is not inputs/first-run-v0.1.0.ini");
            }
        }

        std::vector<std::pair<std::string, std::optional<std::string>>> inputs = {
            {"no file", std::nullopt},
            {"empty file", std::string()},
            {"first run, dev dec1791", firstRunDev},
            {"first run, v0.1.0", firstRun},
            {"AdsMode=tracked", Replaced(firstRun, "AdsMode=paused", "AdsMode=tracked")},
            {"YawMode on the AdsMode key", Replaced(firstRun, "YawMode=0x22", "YawMode=0x2D")},
            {"YawMode on the Toggle key", Replaced(firstRun, "YawMode=0x22", "YawMode=0x23")},
            {"CycleMode on the Toggle key", Replaced(firstRun, "CycleMode=0x21", "CycleMode=0x23")},
            {"AdsMode key on the CycleMode key, YawMode on it too",
             Replaced(Replaced(firstRun, "AdsMode=0x2D", "AdsMode=0x21"), "YawMode=0x22", "YawMode=0x21")},
            {"dev file after a v0.1.0 yaw save",
             WithLine(firstRunDev, "AdsMode=paused", "WorldSpaceYaw=0")},
        };

        for (const auto& [name, bytes] : inputs) RunInput(folders, name, bytes, tally);

        // Fresh equals upgrade: each published build's first-run output converts to the
        // committed file, as no file is created as it.
        if (tally.migrated.count(tally.committed) == 0) Fail("first run", "no input migrated to the committed file");
        for (const std::string& file : {firstRunDev, firstRun}) {
            EmptyFolder(folders.migration);
            const std::wstring path = folders.migration + L"\\" + kIniName;
            WriteBytes(path, file);
            cfg::ConfigOwner<Config> owner(FarCry6HeadTracking::ConfigOwnerOptionsFor(path));
            owner.Load();
            if (ReadBytes(path) != tally.committed) Fail("first run", "a first-run file does not convert to the committed file");
        }

        const std::vector<IniMutation> corpus =
            GenerateIniMutations(firstRun, FarCry6HeadTracking::LegacyConfigImport().keys, CorpusKeys());
        for (const IniMutation& m : corpus) RunInput(folders, "corpus: " + m.name, m.bytes, tally);

        std::printf("%zu inputs, %zu of them from the corpus\n", inputs.size() + corpus.size(), corpus.size());
        std::printf("comparison 1, v0.1.0 against the frozen reader:\n");
        for (const ListedDifference& d : kComparisonOneDifferences) {
            std::printf("  %s (%s): %d inputs\n    %s\n", d.id, d.commit, d.seen, d.what);
            if (d.seen == 0) Fail(d.id, "a listed difference no input shows");
        }
        std::printf("comparison 2, the frozen reader against the migration: %d created, %d converted "
                    "(%d with a changed sensitivity or inversion dropped), %d refused as v0.1.0 refused them, "
                    "%zu distinct files\n",
                    tally.created, tally.converted, tally.with_pose_shaping_dropped, tally.refused,
                    tally.migrated.size());
        if (tally.with_pose_shaping_dropped == 0) Fail("pose shaping", "no input drops a changed value");

        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring lintDir(exe);
        lintDir = lintDir.substr(0, lintDir.find_last_of(L'\\'));
        lintDir = MakeFolder(lintDir, L"migrated");
        int n = 0;
        for (const std::string& file : tally.migrated) {
            WriteBytes(lintDir + L"\\" + std::to_wstring(n++) + L".ini", file);
        }

        for (const std::wstring& dir : {folders.oracle, folders.import, folders.migration}) {
            EmptyFolder(dir);
            RemoveDirectoryW(dir.c_str());
        }
        RemoveDirectoryW(root.c_str());
    } catch (const std::exception& e) {
        std::printf("FAIL: %s\n", e.what());
        return 1;
    }

    if (g_failures == 0) {
        std::printf("config differential: all passed\n");
        return 0;
    }
    std::printf("config differential: %d failure(s)\n", g_failures);
    return 1;
}
