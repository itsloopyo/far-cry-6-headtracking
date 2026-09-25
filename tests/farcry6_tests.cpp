// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Covers the pure logic between the tracking pipeline and the game: the sign and
// unit conversion the pose crosses on its way out, the settings file and what its
// hotkeys save, and the tracker description the game is answered with, which is what
// decides which of its eye tracking features come up. Plus one test that is not
// pure logic at all - the mod reclaiming a tracker port another program was
// holding - because that is a property of this mod's wiring rather than of the
// receiver core tests already cover.

#include "ads.h"
#include "config.h"
#include "camera_pose.h"
#include "pose_bridge.h"
#include "tracker_info.h"
#include "tracking_runtime.h"
#include "window_centering.h"

#include "legacy_config/legacy_config.h"

#include "cameraunlock/config/value_guards.h"
#include "cameraunlock/tracking/tracking_mode.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace FarCry6HeadTracking;

namespace {

int g_failures = 0;

void Check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

void CheckNear(float actual, float expected, const char* what) {
    if (std::fabs(actual - expected) > 1e-4f) {
        std::printf("FAIL: %s (got %.6f, wanted %.6f)\n", what, actual, expected);
        ++g_failures;
    }
}

template <class Path>
std::string ReadBytes(const Path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteBytes(const std::wstring& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

FrameSample Rotation(float yaw, float pitch, float roll) {
    FrameSample s;
    s.has_rotation = true;
    s.yaw = yaw;
    s.pitch = pitch;
    s.roll = roll;
    return s;
}

FrameSample PositionMetres(float x, float y, float z) {
    FrameSample s;
    s.has_position = true;
    s.pos_x = x;
    s.pos_y = y;
    s.pos_z = z;
    return s;
}

void TestRotationSigns() {
    const auto right = ToTransformation(Rotation(25.0f, 0.0f, 0.0f));
    CheckNear(right.rotation.yaw_degrees, 25.0f, "head right yaw");
    const auto left = ToTransformation(Rotation(-25.0f, 0.0f, 0.0f));
    CheckNear(left.rotation.yaw_degrees, -25.0f, "head left yaw");

    const auto up = ToTransformation(Rotation(0.0f, 20.0f, 0.0f));
    CheckNear(up.rotation.pitch_degrees, 20.0f, "head up pitch");

    const auto roll = ToTransformation(Rotation(0.0f, 0.0f, 15.0f));
    CheckNear(roll.rotation.roll_degrees, 15.0f, "head roll");
}

void TestExtendedViewRadians() {
    static_assert(sizeof(tobii::ExtendedViewTransformation) == 24);
    const tobii::Transformation head{{90.0f, -45.0f, 180.0f}, {10.0f, 20.0f, 30.0f}};
    for (int frame = 0; frame < 600; ++frame) {
        const auto view = ToExtendedViewTransformation(head);
        CheckNear(view.rotation.yaw_radians, 1.57079633f, "extended view yaw uses radians");
        CheckNear(view.rotation.pitch_radians, -0.78539816f, "extended view pitch uses radians");
        CheckNear(view.rotation.roll_radians, 3.14159265f, "extended view roll uses radians");
        CheckNear(view.position.x, 10.0f, "extended view preserves position x");
        CheckNear(view.position.y, 20.0f, "extended view preserves position y");
        CheckNear(view.position.z, 30.0f, "extended view preserves position z");
    }
    CheckNear(head.rotation.yaw_degrees, 90.0f, "head stream remains in degrees");
    const auto identity = ToExtendedViewTransformation({});
    CheckNear(identity.rotation.yaw_radians, 0.0f, "disabled extended view remains centred");
}

void TestRenderAxes() {
    CameraParameters clean{};
    clean.eye = {12.0f, 34.0f, 56.0f};
    clean.forward = {0.0f, 1.0f, 0.0f};
    clean.up = {0.0f, 0.0f, 1.0f};
    clean.right = {1.0f, 0.0f, 0.0f};
    const auto position = ToTransformation(PositionMetres(0.25f, 0.20f, -0.40f)).position;
    const auto lean = CameraLean(clean, position);
    CheckNear(lean.x, -0.25f, "positive tracker X leans left in world metres");
    CheckNear(lean.y, 0.40f, "negative tracker Z leans forward");
    CheckNear(lean.z, 0.20f, "Y raises the view in a Z-up world");

    for (int frame = 0; frame < 600; ++frame) {
        auto tracked = clean;
        const auto roll = ToTransformation(Rotation(0.0f, 0.0f, 90.0f));
        ApplyCameraRoll(tracked, roll.rotation.roll_degrees);
        CheckNear(tracked.up.x, -1.0f, "positive tracker roll tips camera up toward camera left");
        CheckNear(tracked.right.z, 1.0f, "positive tracker roll tips camera right upward");
        CheckNear(tracked.forward.y, 1.0f, "roll preserves look direction");
        CheckNear(Vec3::Dot(tracked.right, tracked.up), 0.0f, "rolled basis stays orthogonal");
        CheckNear(tracked.up.Magnitude(), 1.0f, "rolled basis stays unit length");
    }
    CheckNear(clean.up.z, 1.0f, "render copies leave game rotation untouched");
    CheckNear(clean.eye.x, 12.0f, "render copies leave game position untouched");

    CameraParameters turned = clean;
    turned.forward = {1.0f, 0.0f, 0.0f};
    turned.right = {0.0f, -1.0f, 0.0f};
    const auto turnedLean = CameraLean(turned, position);
    CheckNear(turnedLean.x, 0.40f, "forward lean follows camera heading");
    CheckNear(turnedLean.y, 0.25f, "side lean follows camera heading");
}

void TestLocalYaw() {
    using cameraunlock::math::Quat4;
    const Quat4 clean(0.5f, 0.0f, 0.0f, std::sqrt(0.75f));
    const Quat4 yaw(0.0f, 0.0f, -std::sqrt(0.5f), std::sqrt(0.5f));
    CameraParameters camera{};
    camera.eye = {1.0f, 2.0f, 3.0f};
    const Quat4 native = yaw * clean;
    camera.forward = native.Rotate({0.0f, 1.0f, 0.0f});
    camera.up = native.Rotate({0.0f, 0.0f, 1.0f});
    camera.right = native.Rotate({1.0f, 0.0f, 0.0f});
    CheckNear(camera.forward.z, std::sqrt(0.75f), "world yaw preserves elevation");
    RotateCameraBasis(camera, LocalYawCorrection(clean, Quat4::Identity(),
                                                 -1.57079632679f));
    CheckNear(camera.forward.x, 1.0f, "local quarter turn looks along camera right");
    CheckNear(camera.forward.y, 0.0f, "local quarter turn removes forward component");
    CheckNear(camera.forward.z, 0.0f, "local yaw follows tilted camera up");
    CheckNear(camera.up.y, -std::sqrt(0.75f), "local yaw preserves tilted up axis");
    CheckNear(camera.up.z, 0.5f, "local yaw preserves tilted up elevation");
    CheckNear(camera.eye.x, 1.0f, "yaw correction leaves eye position alone");
    CheckNear(LocalYawCorrection(clean, clean, -1.0f).w, 1.0f,
              "equal reference and camera axes need no correction");
    CheckNear(LocalYawCorrection(clean, Quat4::Identity(), 0.0f).w, 1.0f,
              "zero yaw needs no correction");
}

// Metres in, millimetres out, because that is the unit the extended view axis
// limits are named in (XRightMm, YUpMm, ZBackMm).
void TestPositionUnitsAndSigns() {
    const auto x = ToTransformation(PositionMetres(0.10f, 0.0f, 0.0f));
    CheckNear(x.position.x, -100.0f, "x metres to millimetres, mirrored");

    const auto y = ToTransformation(PositionMetres(0.0f, 0.10f, 0.0f));
    CheckNear(y.position.y, 100.0f, "y metres to millimetres");

    // The pipeline calls a forward lean NEGATIVE z and the game calls moving away
    // from the screen POSITIVE, so the two agree and z crosses unnegated. Negating
    // it here would hand the generous forward budget to leaning back.
    const auto forward = ToTransformation(PositionMetres(0.0f, 0.0f, -0.40f));
    CheckNear(forward.position.z, -400.0f, "forward lean stays negative");
    const auto back = ToTransformation(PositionMetres(0.0f, 0.0f, 0.10f));
    CheckNear(back.position.z, 100.0f, "backward lean stays positive");
}

// A frame with no fresh data must produce an identity transformation, not the
// previous one: the game applies whatever it is handed every frame.
void TestEmptySampleIsIdentity() {
    const auto t = ToTransformation(FrameSample{});
    Check(t.rotation.yaw_degrees == 0.0f && t.rotation.pitch_degrees == 0.0f &&
              t.rotation.roll_degrees == 0.0f && t.position.x == 0.0f &&
              t.position.y == 0.0f && t.position.z == 0.0f,
          "an empty sample is identity");
}

// Rotation and position are reported independently, so one channel dropping out
// must not zero the other.
void TestChannelsAreIndependent() {
    FrameSample s = Rotation(10.0f, 0.0f, 0.0f);
    s.has_position = true;
    s.pos_x = 0.20f;
    const auto t = ToTransformation(s);
    CheckNear(t.rotation.yaw_degrees, 10.0f, "rotation survives a position channel");
    CheckNear(t.position.x, -200.0f, "position survives a rotation channel");

    FrameSample rotationOnly = Rotation(10.0f, 0.0f, 0.0f);
    rotationOnly.pos_x = 99.0f;  // stale, and has_position is false
    CheckNear(ToTransformation(rotationOnly).position.x, 0.0f,
              "a position the sample did not claim is not sent");
}

// The tracker the game is told about decides which of its eye tracking features
// come up. Head pose and presence and nothing else is what keeps aim at gaze,
// enemy tagging and dynamic light adaptation off, and is_attached is what stops
// the game treating the device as unplugged.
void TestTrackerDescription() {
    const tobii::TrackerInfo info = MakeTrackerInfo(nullptr, true);
    Check(info.type == tobii::kTrackerTypeEyeTracker, "the tracker is an eye tracker");
    // The NUMBER, not the symbols that build it. Each flag is 1 << the stream id,
    // so presence (id 0) and head pose (id 1) are 0x03. The symbolic expression
    // only ever agrees with itself, whatever the enum says, and a dense
    // from-zero numbering puts 0x0C on the wire - OS gaze and gaze, the two
    // streams this mod exists to leave switched off.
    Check(info.capabilities == 0x03, "the wire capabilities are presence and head pose");
    Check(info.capabilities == (tobii::kStreamHeadPose | tobii::kStreamPresence),
          "head pose and presence are the only streams offered");
    Check(info.is_attached, "the tracker reports itself attached when the mod started");
    Check(!MakeTrackerInfo(nullptr, false).is_attached,
          "a mod that did not start reports the tracker unattached");
    Check(info.url != nullptr && std::strcmp(info.url, "opentrack://far-cry-6-headtracking") == 0,
          "the tracker url is the mod's own");
    Check(info.friendly_name != nullptr && *info.friendly_name != '\0',
          "the tracker has a name to show");
    Check(info.serial_number != nullptr && *info.serial_number != '\0',
          "the tracker has a serial number");

    // With no window yet the primary monitor is described, and a monitor the OS
    // reports as empty would have the game placing gaze in a zero-sized rect.
    const tobii::Rectangle& rect = info.display_rect_in_os_coordinates;
    Check(rect.right > rect.left && rect.bottom > rect.top,
          "a display rect is filled in before the game has passed a window");
}

void TestSanitizers() {
    namespace guards = cameraunlock::config;

    CheckNear(guards::SanitizeSmoothing("s", 1.5f, 0.15f, nullptr), 1.0f,
              "smoothing clamps at 1");
    CheckNear(guards::SanitizeSmoothing("s", -0.5f, 0.15f, nullptr), 0.0f,
              "smoothing clamps at 0");
    CheckNear(guards::SanitizeSmoothing("s", 0.0f, 0.15f, nullptr), 0.0f,
              "zero smoothing is honoured, never floored");
    CheckNear(guards::SanitizeSmoothing("s", std::nanf(""), 0.15f, nullptr), 0.15f,
              "NaN smoothing falls back");

    CheckNear(guards::SanitizePositionLimit("l", -0.4f, 0.3f, nullptr), 0.0f,
              "core clamps a negative limit up to zero, so it cannot invert the bounds");
    CheckNear(guards::SanitizePositionLimit("l", 0.9f, 0.3f, nullptr), 0.9f,
              "a wider limit is tuning");
    CheckNear(guards::SanitizePositionLimit("l", 40.0f, 0.3f, nullptr), 10.0f,
              "a mistyped 40 for 0.40 is bounded");

    CheckNear(guards::SanitizeSensitivity("y", HUGE_VALF, 1.0f, nullptr), 1.0f,
              "an infinite sensitivity falls back");
    CheckNear(guards::SanitizeSensitivity("y", 3.0e38f, 1.0f, nullptr),
              guards::kMaxSensitivity,
              "a sensitivity that would overflow the pose is bounded");

    // The one input class that produces a wrong number in silence. A European
    // decimal comma is a PREFIX parse: "0,15" reads as 0, which is inside every
    // valid range, so nothing downstream can tell it from a deliberate 0.
    float parsed = -1.0f;
    Check(!guards::ParseFloatStrict("0,15", parsed), "a decimal comma is refused");
    Check(!guards::ParseFloatStrict("0.15 scale", parsed), "trailing text is refused");
    Check(guards::ParseFloatStrict("0.15", parsed) && std::fabs(parsed - 0.15f) < 1e-6f,
          "a plain decimal parses");
}

// The settings file the repo commits is what the table renders from its defaults, byte
// for byte. `pixi run render-config` rewrites it after a change to a row.
void TestCommittedConfigIsRendered() {
    const auto table = ConfigTable();
    const std::string rendered =
        cameraunlock::config::RenderCanonical(table, table.defaults(), {kGameDisplayName});
    Check(ReadBytes(FARCRY6_COMMITTED_CONFIG) == rendered,
          "config/FarCry6HeadTracking.ini is the table rendered from its defaults (pixi run render-config)");
}

// A fresh install and an upgrade from the defaults of the last pre-canonical build start
// the same: the map of that build's defaults holds every row at the table's default.
void TestLegacyDefaultsMapToTheDefaults() {
    const auto table = ConfigTable();
    Config mapped = table.defaults();
    const auto result = MapLegacyConfig(legacy::ReadStatus::Absent, legacy::Config{}, mapped);
    Check(result.status == cameraunlock::config::ImportStatus::Absent && result.dropped.empty(),
          "the old defaults drop nothing");
    Check(result.pose_shaping.size() == 12, "every sensitivity and inversion is recorded");
    for (const auto& value : result.pose_shaping) {
        Check(value.folded, "every shipped sensitivity and inversion is identity");
    }
    Check(cameraunlock::config::RenderCanonical(table, mapped, {kGameDisplayName}) ==
              cameraunlock::config::RenderCanonical(table, table.defaults(), {kGameDisplayName}),
          "the old defaults map to the defaults");
    Check(mapped.toggle_key_name == "End, Ctrl+Shift+Y" &&
              mapped.cycle_tracking_mode_key_name == "PageUp, Ctrl+Shift+G" &&
              mapped.yaw_mode_key_name == "PageDown, Ctrl+Shift+H",
          "the old hotkeys and their always-on chords become the fleet's key lists");
}

std::vector<std::string> Lines(const std::string& bytes) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < bytes.size()) {
        const size_t end = bytes.find("\r\n", start);
        lines.push_back(bytes.substr(start, end - start));
        start = end + 2;
    }
    return lines;
}

// The lines of `after` that differ from `before`, which must have as many lines.
std::vector<std::string> ChangedLines(const std::string& before, const std::string& after) {
    const std::vector<std::string> a = Lines(before);
    const std::vector<std::string> b = Lines(after);
    if (a.size() != b.size()) return {"a line was added or removed"};
    std::vector<std::string> changed;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) changed.push_back(b[i]);
    }
    return changed;
}

// A save changes the lines of its rows and no other byte, the yaw mode and the tracking
// mode persist, and End's row cannot be saved at all.
void TestTogglesSave() {
    using cameraunlock::config::ConfigLoadStatus;
    using cameraunlock::config::ConfigOwner;
    using cameraunlock::config::ConfigSaveStatus;

    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    const std::wstring dir = std::wstring(temp) + L"farcry6-config-save-" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(dir.c_str(), nullptr);
    const std::wstring path = dir + L"\\" + kConfigFileName;
    const std::string committed = ReadBytes(FARCRY6_COMMITTED_CONFIG);
    WriteBytes(path, committed);

    {
        ConfigOwner<Config> owner(ConfigOwnerOptionsFor(path));
        Check(owner.Load().status == ConfigLoadStatus::Canonical, "the committed file loads as canonical");

        Check(owner.Save([](Config& c) { c.world_space_yaw = false; }).status == ConfigSaveStatus::Saved,
              "the yaw mode saves");
        const std::string afterYaw = ReadBytes(path);
        Check(ChangedLines(committed, afterYaw) == std::vector<std::string>{"WorldSpaceYaw=false"},
              "saving the yaw mode changes its line and nothing else");

        const auto channels = cameraunlock::EncodeTrackingMode(cameraunlock::TrackingMode::RotationOnly);
        Check(owner.Save([channels](Config& c) {
                  c.rotation_enabled = channels.rotation_enabled;
                  c.position_enabled = channels.position_enabled;
              }).status == ConfigSaveStatus::Saved,
              "the tracking mode saves");
        Check(ChangedLines(afterYaw, ReadBytes(path)) == std::vector<std::string>{"PositionEnabled=false"},
              "saving rotation only changes PositionEnabled and nothing else");

        bool refused = false;
        try {
            owner.Save([](Config& c) { c.enable_on_startup = false; });
        } catch (const std::logic_error&) {
            refused = true;
        }
        Check(refused, "EnableOnStartup is not Writable, so the End toggle cannot persist");
    }

    ConfigOwner<Config> reopened(ConfigOwnerOptionsFor(path));
    const auto again = reopened.Load();
    Check(again.status == ConfigLoadStatus::Canonical && again.diagnostics.empty() &&
              !again.config.world_space_yaw && again.config.rotation_enabled && !again.config.position_enabled &&
              again.config.enable_on_startup,
          "the saved yaw and tracking mode come back at the next start");

    DeleteFileW(path.c_str());
    RemoveDirectoryW(dir.c_str());
}

// A user who launches this game while the last one is still running finds the
// tracker port taken. The receiver core retries the bind on its own, but nothing
// there says the MOD survives it: a runtime that latched the failed start, or a
// frame path that cached "not receiving", would leave the player with no head
// tracking for the rest of the session and no way back but a relaunch.
//
// So this drives the real thing. A raw socket holds the port, a sender is already
// streaming into it, and the clock starts the moment the port is freed. The
// measured recovery is printed rather than only asserted, because a number that
// has crept from 300 ms to 3 s still passes a generous bound and is worth seeing.
void TestUdpPortRecovery() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        Check(false, "winsock starts up for the port recovery test");
        return;
    }

    // Walk a range rather than pinning one port: a real tracker, or a previous
    // run of this suite, may be sitting on any single choice.
    SOCKET occupier = INVALID_SOCKET;
    uint16_t port = 0;
    for (uint16_t candidate = 51701; candidate < 51733 && occupier == INVALID_SOCKET; ++candidate) {
        SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == INVALID_SOCKET) break;
        sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(candidate);
        addr.sin_addr.s_addr = INADDR_ANY;
        if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
            closesocket(s);
            continue;
        }
        occupier = s;
        port = candidate;
    }
    if (occupier == INVALID_SOCKET) {
        Check(false, "a free loopback port is available to hold");
        WSACleanup();
        return;
    }

    // The tracker app is already sending, the way it would be. Each packet
    // differs from the last: the receiver holds a pose that repeats bit for bit,
    // because that is what a tracker which has lost the head looks like.
    std::atomic<bool> senderStop(false);
    std::thread sender([&senderStop, port] {
        SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == INVALID_SOCKET) return;
        sockaddr_in to;
        std::memset(&to, 0, sizeof(to));
        to.sin_family = AF_INET;
        to.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &to.sin_addr);
        int n = 0;
        while (!senderStop.load(std::memory_order_relaxed)) {
            double pose[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
            pose[3] = 2.0 + 0.01 * static_cast<double>(n % 50);
            sendto(s, reinterpret_cast<const char*>(pose), sizeof(pose), 0,
                   reinterpret_cast<sockaddr*>(&to), sizeof(to));
            ++n;
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        closesocket(s);
    });

    Config cfg;
    cfg.udp_port = port;
    TrackingRuntime runtime;
    runtime.Start(cfg);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    Check(!runtime.IsReceiving(), "no tracking while another program holds the port");
    Check(!runtime.SampleFrame().has_rotation, "no pose reaches the game while the port is held");

    const auto freed = std::chrono::steady_clock::now();
    closesocket(occupier);            // the user remembers, and closes the other game

    double elapsedMs = 0.0;
    bool recovered = false;
    while (elapsedMs < 5000.0) {
        recovered = runtime.SampleFrame().has_rotation;
        elapsedMs = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - freed).count();
        if (recovered) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    std::printf("  head tracking live %.0f ms after the port was freed\n", elapsedMs);
    Check(recovered, "head tracking comes back once the port is free, with no restart");
    // The receiver retries every 500 ms off a 100 ms supervisor tick, so the worst
    // honest case is about 600 ms plus one tracker packet. Two seconds fails a
    // cadence that has quietly grown, without failing on a loaded CI runner.
    Check(elapsedMs < 2000.0, "recovery is prompt rather than eventual");

    runtime.Stop();
    senderStop.store(true);
    sender.join();
    WSACleanup();
}

// The vtable layouts in tobii_abi.h are the 7.3 ones the shipped DLL enforces.
// GetApi has to refuse anything else rather than answer with them: a 9.x caller
// routed through a 7.3 table calls whatever function happens to sit in the slot
// it asked for, which crashes the player's game instead of quietly doing
// nothing. The game itself asks for 7.3.
void TestApiVersionGate() {
    Check(tobii::IsSupportedApiVersion(7, 3), "the version the game asks for is served");
    Check(tobii::IsSupportedApiVersion(7, 0), "an older 7.x minor is served");
    Check(!tobii::IsSupportedApiVersion(7, 4), "a newer 7.x minor is refused");
    Check(!tobii::IsSupportedApiVersion(9, 0), "the published 9.x layout is refused");
    Check(!tobii::IsSupportedApiVersion(6, 3), "an older major is refused");
    Check(!tobii::IsSupportedApiVersion(0, 0), "an unset version is refused");
}

}  // namespace


int g_gameWindowPosChanged = 0;

LRESULT CALLBACK CountingWndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_WINDOWPOSCHANGED) ++g_gameWindowPosChanged;
    return DefWindowProcW(window, message, wParam, lParam);
}

// The game places its "Nomad" window with SetWindowPos from its own module, and
// repeats the request every frame the window is not at its saved position. The
// placement is centred on the work area and the repeats leave a centred window
// untouched; a placement filling the work area, a call on any other window, and any
// call once centring is stopped all pass through as made. The test executable
// stands in for the game module.
void TestWindowCentring() {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW gameClass{};
    gameClass.lpfnWndProc = CountingWndProc;
    gameClass.hInstance = instance;
    gameClass.lpszClassName = L"Nomad";
    WNDCLASSW otherClass{};
    otherClass.lpfnWndProc = DefWindowProcW;
    otherClass.hInstance = instance;
    otherClass.lpszClassName = L"NomadSplash";
    Check(RegisterClassW(&gameClass) != 0 && RegisterClassW(&otherClass) != 0,
          "the test window classes register");

    MONITORINFO info{};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &info);
    const RECT work = info.rcWork;
    const int workW = work.right - work.left;
    const int workH = work.bottom - work.top;
    const int savedX = work.left + 10;
    const int savedY = work.top + 10;

    const HWND game = CreateWindowExW(WS_EX_NOACTIVATE, L"Nomad", L"FarCry6 test",
                                      WS_OVERLAPPEDWINDOW, savedX, savedY, 640, 400, nullptr,
                                      nullptr, instance, nullptr);
    const HWND other = CreateWindowExW(WS_EX_NOACTIVATE, L"NomadSplash", L"", WS_POPUP,
                                       savedX, savedY, 400, 300, nullptr, nullptr, instance,
                                       nullptr);
    Check(game != nullptr && other != nullptr, "the test windows are created");
    if (!game || !other) return;
    ShowWindow(game, SW_SHOWNOACTIVATE);
    ShowWindow(other, SW_SHOWNOACTIVATE);

    auto at = [](HWND w, int x, int y) {
        RECT r{};
        GetWindowRect(w, &r);
        return r.left == x && r.top == y;
    };
    const UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED;
    const int centredX = work.left + (workW - 640) / 2;
    const int centredY = work.top + (workH - 400) / 2;

    StartWindowCentering(instance);

    SetWindowPos(game, nullptr, savedX, savedY, 640, 400, flags);
    Check(at(game, centredX, centredY), "the game's placement of its window is centred");

    g_gameWindowPosChanged = 0;
    for (int frame = 0; frame < 5; ++frame) {
        Check(SetWindowPos(game, nullptr, savedX, savedY, 640, 400, flags) != FALSE,
              "a repeated placement request reports success");
    }
    Check(at(game, centredX, centredY) && g_gameWindowPosChanged == 0,
          "repeated requests for the saved position leave a centred window untouched");

    SetWindowPos(game, nullptr, savedX, savedY, 0, 0, flags | SWP_NOSIZE);
    Check(at(game, centredX, centredY),
          "a placement that keeps the size is centred at the window's current size");

    SetWindowPos(other, nullptr, savedX, savedY, 0, 0, flags | SWP_NOSIZE);
    Check(at(other, savedX, savedY), "a window of another class is placed where asked");

    SetWindowPos(game, nullptr, work.left, work.top, workW, workH, flags);
    Check(at(game, work.left, work.top), "a placement filling the work area is left there");

    StopWindowCentering();
    SetWindowPos(game, nullptr, savedX, savedY, 640, 400, flags);
    Check(at(game, savedX, savedY), "once stopped, the game's placement passes through");

    DestroyWindow(game);
    DestroyWindow(other);
    UnregisterClassW(L"Nomad", instance);
    UnregisterClassW(L"NomadSplash", instance);
}

tobii::Transformation HeadPose(float yaw, float pitch, float roll, float x, float y, float z) {
    tobii::Transformation t{};
    t.rotation.yaw_degrees = yaw;
    t.rotation.pitch_degrees = pitch;
    t.rotation.roll_degrees = roll;
    t.position.x = x;
    t.position.y = y;
    t.position.z = z;
    return t;
}

// Hip fire passes the pose through; with the sights up the lean eases out while
// rotation, roll included, stays absolute and unscaled.
void TestAdsLeanEasing() {
    using cameraunlock::ads::AdsFade;
    AdsLean ads;
    const tobii::Transformation head = HeadPose(20.0f, -10.0f, 15.0f, 30.0f, -20.0f, 40.0f);

    const tobii::Transformation hip = ads.Apply(false, head, 1000);
    CheckNear(hip.rotation.yaw_degrees, 20.0f, "at the hip yaw passes through");
    CheckNear(hip.rotation.pitch_degrees, -10.0f, "at the hip pitch passes through");
    CheckNear(hip.rotation.roll_degrees, 15.0f, "at the hip roll passes through");
    CheckNear(hip.position.x, 30.0f, "at the hip the lean passes through (x)");
    CheckNear(hip.position.y, -20.0f, "at the hip the lean passes through (y)");
    CheckNear(hip.position.z, 40.0f, "at the hip the lean passes through (z)");

    ads.Apply(true, head, 1001);
    const unsigned long long up = 1001 + AdsFade::kLowerMs + 1;
    const tobii::Transformation aimed = ads.Apply(true, head, up);
    CheckNear(aimed.rotation.yaw_degrees, 20.0f, "with the sights up yaw is untouched");
    CheckNear(aimed.rotation.pitch_degrees, -10.0f, "with the sights up pitch is untouched");
    CheckNear(aimed.rotation.roll_degrees, 15.0f, "with the sights up roll is untouched");
    CheckNear(aimed.position.x, 0.0f, "with the sights up the lean is out (x)");
    CheckNear(aimed.position.y, 0.0f, "with the sights up the lean is out (y)");
    CheckNear(aimed.position.z, 0.0f, "with the sights up the lean is out (z)");

    AdsLean mid;
    mid.Apply(false, head, 0);
    mid.Apply(true, head, 1);
    const tobii::Transformation half = mid.Apply(true, head, 1 + AdsFade::kLowerMs / 2);
    Check(half.position.z > 0.0f && half.position.z < 40.0f,
          "mid-transition the lean is part way out");
    CheckNear(half.position.x / 30.0f, half.position.z / 40.0f,
              "mid-transition every lean axis is scaled by the same fade");
    CheckNear(half.rotation.yaw_degrees, 20.0f, "mid-transition yaw is untouched");
    CheckNear(half.rotation.roll_degrees, 15.0f, "mid-transition roll is untouched");

    // Lowering the sights halfway down continues from where the lean is.
    const tobii::Transformation reversed =
        mid.Apply(false, head, 2 + AdsFade::kLowerMs / 2);
    Check(std::fabs(reversed.position.z - half.position.z) < 2.0f,
          "a reversal mid-transition does not step the lean");
    const tobii::Transformation back =
        mid.Apply(false, head, 2 + AdsFade::kLowerMs / 2 + AdsFade::kRaiseMs + 1);
    CheckNear(back.position.z, 40.0f, "lowering the sights returns the lean");
}

// The zoom factor shrinks what moves the picture across the frame and leaves roll.
void TestZoomScaling() {
    const tobii::Transformation head = HeadPose(20.0f, -10.0f, 15.0f, 30.0f, -20.0f, 40.0f);

    const tobii::Transformation hip = ScaleForZoom(head, 1.0f);
    CheckNear(hip.rotation.yaw_degrees, 20.0f, "factor 1 leaves yaw alone");
    CheckNear(hip.rotation.pitch_degrees, -10.0f, "factor 1 leaves pitch alone");
    CheckNear(hip.position.z, 40.0f, "factor 1 leaves the lean alone");

    const float factor = 0.5f;
    const tobii::Transformation zoomed = ScaleForZoom(head, factor);
    const float kDeg = 3.14159265f / 180.0f;
    CheckNear(std::tan(zoomed.rotation.yaw_degrees * kDeg), std::tan(20.0f * kDeg) * factor,
              "yaw scales so its screen displacement matches the un-zoomed view");
    CheckNear(std::tan(zoomed.rotation.pitch_degrees * kDeg), std::tan(-10.0f * kDeg) * factor,
              "pitch scales so its screen displacement matches the un-zoomed view");
    CheckNear(zoomed.rotation.roll_degrees, 15.0f, "roll does not scale with the zoom");
    CheckNear(zoomed.position.x, 15.0f, "the lean scales with the zoom (x)");
    CheckNear(zoomed.position.y, -10.0f, "the lean scales with the zoom (y)");
    CheckNear(zoomed.position.z, 20.0f, "the lean scales with the zoom (z)");
}

int main(int argc, char** argv) {
    // `pixi run render-config`: write the committed settings file and run nothing else.
    if (argc == 3 && std::strcmp(argv[1], "--render-config") == 0) {
        const auto table = ConfigTable();
        const std::string rendered =
            cameraunlock::config::RenderCanonical(table, table.defaults(), {kGameDisplayName});
        std::ofstream out(argv[2], std::ios::binary | std::ios::trunc);
        out.write(rendered.data(), static_cast<std::streamsize>(rendered.size()));
        if (!out) {
            std::printf("could not write %s\n", argv[2]);
            return 1;
        }
        return 0;
    }

    TestRotationSigns();
    TestExtendedViewRadians();
    TestRenderAxes();
    TestLocalYaw();
    TestPositionUnitsAndSigns();
    TestEmptySampleIsIdentity();
    TestChannelsAreIndependent();
    TestTrackerDescription();
    TestSanitizers();
    TestCommittedConfigIsRendered();
    TestLegacyDefaultsMapToTheDefaults();
    TestTogglesSave();
    TestApiVersionGate();
    TestUdpPortRecovery();
    TestWindowCentring();
    TestAdsLeanEasing();
    TestZoomScaling();

    if (g_failures == 0) {
        std::printf("all tests passed\n");
        return 0;
    }
    std::printf("%d test(s) failed\n", g_failures);
    return 1;
}
