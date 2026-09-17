// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Covers the pure logic between the tracking pipeline and the game: the sign and
// unit conversion the pose crosses on its way out, the INI guards that decide what
// reaches it, and the tracker description the game is answered with, which is what
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

#include "cameraunlock/config/value_guards.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

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

// The INI is a system boundary, so the guards above are reached through a real
// file here rather than only called directly.
void TestConfigRejectsMalformedNumbers() {
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") +
                             "\\farcry6_ht_malformed.ini";
    std::remove(path.c_str());

    FILE* f = std::fopen(path.c_str(), "w");
    Check(f != nullptr, "the malformed test INI can be written");
    if (f) {
        // A decimal comma, an inline comment, and a value that would overflow the
        // pose it multiplies.
        //
        // The comma goes on RemoteSmoothing, whose default is 0.15, and NOT on
        // LocalSmoothing, whose default is 0.0. A prefix parse of "0,15" yields
        // 0.0, so asserting the LocalSmoothing default would pass whether the
        // strict parse ran or not: it is the bug's own output.
        std::fputs("[General]\nPort=4242\n"
                   "[Smoothing]\nRemoteSmoothing=0,15\nLocalSmoothing=0.25 ; settle\n"
                   "[Sensitivity]\nYaw=3e38\n",
                   f);
        std::fclose(f);
    }

    Config cfg;
    Check(cfg.LoadOrCreate(path.c_str()), "a malformed value still loads the file");
    CheckNear(cfg.remote_smoothing, kDefaultRemoteSmoothing,
              "a decimal comma falls back instead of silently reading as zero");
    CheckNear(cfg.local_smoothing, 0.25f, "an inline comment is stripped, not parsed");
    CheckNear(cfg.sens_yaw, cameraunlock::config::kMaxSensitivity,
              "a sensitivity that would overflow the pose is bounded at the INI");
    std::remove(path.c_str());
}

// The port is the one setting a user can get wrong in a way the mod cannot work
// around, so it is the one config error that refuses to start.
void TestConfigPort() {
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") +
                             "\\farcry6_ht_test.ini";
    std::remove(path.c_str());

    Config created;
    Check(created.LoadOrCreate(path.c_str()), "a missing INI is created and read");
    Check(created.udp_port == kDefaultPort, "the created INI carries the default port");

    FILE* f = std::fopen(path.c_str(), "w");
    Check(f != nullptr, "the test INI can be rewritten");
    if (f) {
        std::fputs("[General]\nPort=70000\n", f);
        std::fclose(f);
    }
    Config bad;
    Check(!bad.LoadOrCreate(path.c_str()), "an out-of-range port refuses to load");

    f = std::fopen(path.c_str(), "w");
    Check(f != nullptr, "the test INI can be rewritten for the limit case");
    if (f) {
        std::fputs("[General]\nPort=4242\n[Position]\nLimitZ=-1\n", f);
        std::fclose(f);
    }
    Config recovered;
    Check(recovered.LoadOrCreate(path.c_str()), "a bad limit still loads");
    CheckNear(recovered.pos_limit_z, kDefaultPosLimitZ, "a negative LimitZ falls back");

    // ReadInt answers 0 for a present but unparseable value rather than the
    // default, so this reached the user as a reported port of 0 they never typed.
    f = std::fopen(path.c_str(), "w");
    Check(f != nullptr, "the test INI can be rewritten for the unparseable port");
    if (f) {
        std::fputs("[General]\nPort=abc\n", f);
        std::fclose(f);
    }
    Config unparseable;
    Check(!unparseable.LoadOrCreate(path.c_str()),
          "a port that is not a number refuses to load");
    std::remove(path.c_str());
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

// Binds a UDP port, notes it and gives it straight back, so the runtime under
// test has a port nothing else on the machine is sitting on.
uint16_t FindFreeUdpPort(uint16_t first, uint16_t last) {
    for (uint16_t candidate = first; candidate < last; ++candidate) {
        SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == INVALID_SOCKET) return 0;
        sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(candidate);
        addr.sin_addr.s_addr = INADDR_ANY;
        const bool bound = bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
        closesocket(s);
        if (bound) return candidate;
    }
    return 0;
}

// The pipeline finite-checks the wire, and the INI guards check what the user
// typed, but neither covers their PRODUCT: a sensitivity that is finite on its
// own overflows the pose it multiplies, and infinity in the transformation
// leaves the view somewhere the player cannot recover from. The guard has to
// drop the poisoned channel and only that channel, so a bad Sensitivity value
// does not also take positional tracking down with it.
void TestNonFiniteRotationIsDropped() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        Check(false, "winsock starts up for the overflow test");
        return;
    }
    const uint16_t port = FindFreeUdpPort(51733, 51765);
    if (port == 0) {
        Check(false, "a free loopback port is available for the overflow test");
        WSACleanup();
        return;
    }

    Config cfg;
    cfg.udp_port = port;
    // Set on the struct directly, NOT through the INI: ReadSensitivity would bound
    // this to kMaxSensitivity. Large enough that any real head angle multiplied by
    // it leaves float range, which is what the runtime guard has to catch.
    cfg.sens_yaw = 3.0e38f;

    TrackingRuntime runtime;
    runtime.Start(cfg);

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
            // Position in centimetres, rotation in degrees. Both jitter: the
            // receiver holds a pose that repeats bit for bit, because that is
            // what a tracker which has lost the head looks like.
            double pose[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
            const double jitter = 0.01 * static_cast<double>(n % 50);
            pose[0] = 5.0 + jitter;   // x, 5cm of lean
            pose[3] = 2.0 + jitter;   // yaw
            sendto(s, reinterpret_cast<const char*>(pose), sizeof(pose), 0,
                   reinterpret_cast<sockaddr*>(&to), sizeof(to));
            ++n;
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        closesocket(s);
    });

    FrameSample sample;
    const auto started = std::chrono::steady_clock::now();
    while (std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now() - started).count() < 3000.0) {
        sample = runtime.SampleFrame();
        if (sample.has_position) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }

    // Asserted inside the success branch. A timed-out wait leaves `sample` default
    // constructed, whose has_rotation is already false, so a bare assertion here
    // would report a pass off the very failure the line above reports.
    Check(sample.has_position, "the tracker reaches the runtime for the overflow test");
    if (sample.has_position) {
        Check(!sample.has_rotation, "a rotation that overflowed to non-finite is dropped");
        Check(std::isfinite(sample.pos_x) && std::isfinite(sample.pos_y) &&
                  std::isfinite(sample.pos_z),
              "the position channel survives a poisoned rotation channel");
    }

    runtime.Stop();
    senderStop.store(true);
    sender.join();
    WSACleanup();
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

// Far Cry 6 has two ADS slots. `marker` is another mod's slot, so a file carrying it
// lands on the default rather than on a mode this game does not have.
void TestAdsModeSetting() {
    Check(kDefaultAdsMode == AdsMode::Paused, "ADS mode defaults to paused");
    Check(ParseFarCry6AdsMode("tracked") == AdsMode::Tracked, "tracked parses");
    Check(ParseFarCry6AdsMode(" Tracked ") == AdsMode::Tracked,
          "tracked parses whatever its case and surrounding spaces");
    Check(ParseFarCry6AdsMode("paused") == AdsMode::Paused, "paused parses");
    Check(ParseFarCry6AdsMode("marker") == AdsMode::Paused, "marker is not a mode here");
    Check(ParseFarCry6AdsMode("") == AdsMode::Paused, "an empty AdsMode is the default");
    Check(ParseFarCry6AdsMode("free") == AdsMode::Paused, "an unknown AdsMode is the default");
    Check(NextFarCry6AdsMode(AdsMode::Paused) == AdsMode::Tracked &&
              NextFarCry6AdsMode(AdsMode::Tracked) == AdsMode::Paused,
          "the ADS cycle is paused, tracked, paused");
}

// paused: yaw, pitch and the lean settle onto the sights; a head tilt keeps rolling
// the view throughout.
void TestAdsPausedKeepsRoll() {
    using cameraunlock::ads::AdsFade;
    AdsPose ads;
    const tobii::Transformation head = HeadPose(20.0f, -10.0f, 15.0f, 30.0f, -20.0f, 40.0f);

    const tobii::Transformation hip = ads.Apply(AdsMode::Paused, false, true, head, 1000);
    CheckNear(hip.rotation.yaw_degrees, 20.0f, "at the hip the pose passes through");
    CheckNear(hip.position.z, 40.0f, "at the hip the lean passes through");

    const tobii::Transformation start = ads.Apply(AdsMode::Paused, true, true, head, 1001);
    CheckNear(start.rotation.roll_degrees, 15.0f, "roll is untouched as the sights come up");

    const unsigned long long up = 1001 + AdsFade::kLowerMs + 1;
    const tobii::Transformation aimed = ads.Apply(AdsMode::Paused, true, true, head, up);
    CheckNear(aimed.rotation.yaw_degrees, 0.0f, "paused settles yaw onto the sights");
    CheckNear(aimed.rotation.pitch_degrees, 0.0f, "paused settles pitch onto the sights");
    CheckNear(aimed.position.x, 0.0f, "paused settles the lean onto the sights (x)");
    CheckNear(aimed.position.y, 0.0f, "paused settles the lean onto the sights (y)");
    CheckNear(aimed.position.z, 0.0f, "paused settles the lean onto the sights (z)");
    CheckNear(aimed.rotation.roll_degrees, 15.0f, "paused keeps roll live");

    const tobii::Transformation tilted =
        ads.Apply(AdsMode::Paused, true, true, HeadPose(40.0f, 5.0f, -8.0f, 0, 0, 0), up + 16);
    CheckNear(tilted.rotation.yaw_degrees, 0.0f, "head movement does not move paused aim");
    CheckNear(tilted.rotation.roll_degrees, -8.0f, "a new head tilt still rolls a paused aim");

    ads.Apply(AdsMode::Paused, false, true, head, up + 32);
    const tobii::Transformation back =
        ads.Apply(AdsMode::Paused, false, true, head, up + 32 + AdsFade::kRaiseMs + 1);
    CheckNear(back.rotation.yaw_degrees, 20.0f, "lowering the sights returns to the head pose");
    CheckNear(back.position.z, 40.0f, "lowering the sights returns the lean");
}

// tracked: the same settle onto the sights, then tracking measured from the pose the
// sights came up on, with roll absolute and no step when they come back down.
void TestAdsTrackedFromEntry() {
    using cameraunlock::ads::AdsFade;
    AdsPose ads;
    const tobii::Transformation entry = HeadPose(20.0f, -10.0f, 15.0f, 30.0f, -20.0f, 40.0f);
    ads.Apply(AdsMode::Tracked, false, true, entry, 0);
    ads.Apply(AdsMode::Tracked, true, true, entry, 1);

    const unsigned long long up = 1 + AdsFade::kLowerMs + 1;
    const tobii::Transformation moved = HeadPose(30.0f, -4.0f, 5.0f, 35.0f, -20.0f, 50.0f);
    const tobii::Transformation aimed = ads.Apply(AdsMode::Tracked, true, true, moved, up);
    CheckNear(aimed.rotation.yaw_degrees, 10.0f, "tracked yaw is measured from entry");
    CheckNear(aimed.rotation.pitch_degrees, 6.0f, "tracked pitch is measured from entry");
    CheckNear(aimed.rotation.roll_degrees, 5.0f, "tracked roll stays absolute");
    CheckNear(aimed.position.x, 5.0f, "tracked lean is measured from entry (x)");
    CheckNear(aimed.position.z, 10.0f, "tracked lean is measured from entry (z)");

    const tobii::Transformation released =
        ads.Apply(AdsMode::Tracked, false, true, moved, up + 1);
    Check(std::fabs(released.rotation.yaw_degrees - aimed.rotation.yaw_degrees) < 0.5f,
          "lowering the sights in tracked does not step the view");
    const tobii::Transformation back =
        ads.Apply(AdsMode::Tracked, false, true, moved, up + 1 + AdsFade::kRaiseMs + 1);
    CheckNear(back.rotation.yaw_degrees, 30.0f, "tracked returns to the head pose after lowering");

    AdsPose seam;
    seam.Apply(AdsMode::Tracked, true, true, HeadPose(175.0f, 0, 0, 0, 0, 0), 0);
    const tobii::Transformation across = seam.Apply(
        AdsMode::Tracked, true, true, HeadPose(-175.0f, 0, 0, 0, 0, 0), AdsFade::kLowerMs + 1);
    CheckNear(across.rotation.yaw_degrees, 10.0f, "tracked yaw crosses the seam the short way");

    AdsPose suppressed;
    suppressed.Apply(AdsMode::Tracked, true, true, entry, 0);
    suppressed.Reset();
    suppressed.Apply(AdsMode::Tracked, true, true, moved, 1);
    const tobii::Transformation fresh =
        suppressed.Apply(AdsMode::Tracked, true, true, moved, 2 + AdsFade::kLowerMs);
    CheckNear(fresh.rotation.yaw_degrees, 0.0f,
              "after a suppression the next aim measures from its own entry");
}

int main() {
    TestRotationSigns();
    TestExtendedViewRadians();
    TestRenderAxes();
    TestPositionUnitsAndSigns();
    TestEmptySampleIsIdentity();
    TestChannelsAreIndependent();
    TestTrackerDescription();
    TestSanitizers();
    TestConfigRejectsMalformedNumbers();
    TestConfigPort();
    TestApiVersionGate();
    TestUdpPortRecovery();
    TestNonFiniteRotationIsDropped();
    TestWindowCentring();
    TestAdsModeSetting();
    TestAdsPausedKeepsRoll();
    TestAdsTrackedFromEntry();

    if (g_failures == 0) {
        std::printf("all tests passed\n");
        return 0;
    }
    std::printf("%d test(s) failed\n", g_failures);
    return 1;
}
