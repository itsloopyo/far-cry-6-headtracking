// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The config differential test. Every input is read three ways:
//
//   oracle  v0.1.0's reader (oracle/), the newest published build, and v0.1.0's startup code
//   import  the frozen reader in src/legacy_config/, and the startup code it ran under
//
// Comparison 1, oracle against import, finds what a player updating from v0.1.0 sees change
// that the conversion did not cause. Every difference it may find is listed in
// kComparisonOneDifferences with the commit that made it; any other fails the test.
//
// Inputs: no file, an empty file, the first-run output of each published build (dev at
// dec1791 and v0.1.0, extracted once into inputs/), a few hand-written files for the listed
// differences, and core's corpus over v0.1.0's first-run output. Neither published build
// shipped a config file or a launcher seed.

#include "legacy_config/legacy_config.h"
#include "oracle/oracle_config.h"

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
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace {

namespace legacy = FarCry6HeadTracking::legacy;
namespace oracle = FarCry6HeadTracking::oracle;
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

std::vector<LegacyKey> ReadKeys() {
    std::vector<LegacyKey> keys;
    for (const MutationKey& k : CorpusKeys()) keys.push_back({k.section, k.key});
    return keys;
}

// ---------------------------------------------------------------------------
// The run
// ---------------------------------------------------------------------------

struct Folders {
    std::wstring oracle;
    std::wstring import;
};

const wchar_t kIniName[] = L"FarCry6HeadTracking.ini";

void RunInput(const Folders& f, const std::string& name, const std::optional<std::string>& bytes) {
    OracleRun o;
    {
        EmptyFolder(f.oracle);
        const std::wstring path = f.oracle + L"\\" + kIniName;
        if (bytes) WriteBytes(path, *bytes);
        o.usable = o.cfg.LoadOrCreate(Narrow(path).c_str());
    }

    ImportRun i;
    {
        EmptyFolder(f.import);
        const std::wstring path = f.import + L"\\" + kIniName;
        if (bytes) {
            WriteBytes(path, *bytes);
            SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY);
        }
        const auto before = Snapshot(f.import);
        i.status = legacy::Read(Narrow(path).c_str(), i.cfg);
        if (Snapshot(f.import) != before) Fail(name, "the import changed the folder it read from");
        if (!bytes && i.status != legacy::ReadStatus::Absent) Fail(name, "the import read a file that is not there");
    }

    CompareOracleWithImport(name, o, i);
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
        const Folders folders{MakeFolder(root, L"oracle"), MakeFolder(root, L"import")};

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

        for (const auto& [name, bytes] : inputs) RunInput(folders, name, bytes);

        const std::vector<IniMutation> corpus = GenerateIniMutations(firstRun, ReadKeys(), CorpusKeys());
        for (const IniMutation& m : corpus) RunInput(folders, "corpus: " + m.name, m.bytes);

        std::printf("%zu inputs, %zu of them from the corpus\n", inputs.size() + corpus.size(), corpus.size());
        std::printf("comparison 1, v0.1.0 against the frozen reader:\n");
        for (const ListedDifference& d : kComparisonOneDifferences) {
            std::printf("  %s (%s): %d inputs\n    %s\n", d.id, d.commit, d.seen, d.what);
            if (d.seen == 0) Fail(d.id, "a listed difference no input shows");
        }

        EmptyFolder(folders.oracle);
        EmptyFolder(folders.import);
        RemoveDirectoryW(folders.oracle.c_str());
        RemoveDirectoryW(folders.import.c_str());
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
