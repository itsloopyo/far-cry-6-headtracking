// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "tracker_info.h"

#include "logging.h"

#include <windows.h>

#include <atomic>

namespace FarCry6HeadTracking {

namespace {

constexpr char kTrackerUrl[] = "opentrack://far-cry-6-headtracking";
constexpr char kTrackerName[] = "OpenTrack Head Tracking";
constexpr char kTrackerModel[] = "OpenTrack";
constexpr char kTrackerGeneration[] = "OpenTrack";
constexpr char kTrackerSerial[] = "far-cry-6-headtracking";
constexpr char kTrackerFirmware[] = "1";

// A screen the OS would not describe leaves the corresponding field at zero, and
// the game is then told the tracker watches a display with no extent. Say so
// once rather than letting it pass: a degenerate rect looks exactly like a
// tracker that is working, and there is nothing else in the log to read it from.
//
// A latch per call site, not one shared between them: with a single flag a
// failure of the second query after the first had already failed goes unreported,
// which is the case where the description is most wrong. Atomic because
// MakeTrackerInfo is reachable from TrackWindow, which the game may call from a
// different thread than the one that acquired the API.
void ReportDisplayQueryFailure(std::atomic<bool>& warned, const char* what, DWORD error) {
    if (warned.exchange(true)) return;
    Log::Line("WARN: %s failed (%lu), so the tracker the game is told about carries no "
              "screen size. Head tracking still runs.", what, error);
}

// The monitor the game window sits on, so the tracker the game is told about
// describes the screen the player is actually looking at.
void FillDisplay(tobii::TrackerInfo& info, HWND tracked_window) {
    // MONITOR_DEFAULTTOPRIMARY never answers null, so the handle is used directly
    // and only the description below can fail.
    HMONITOR monitor = tracked_window
                           ? MonitorFromWindow(tracked_window, MONITOR_DEFAULTTOPRIMARY)
                           : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    // Returning here leaves the physical size at zero as well as the rect. The two
    // queries used to be independent, but the DC below is opened by the device name
    // this call supplies, and gluing the primary monitor's millimetres onto a
    // missing rect is a worse answer than an empty one.
    static std::atomic<bool> s_monitorWarned{false};
    if (!GetMonitorInfoW(monitor, reinterpret_cast<MONITORINFO*>(&mi))) {
        ReportDisplayQueryFailure(s_monitorWarned, "GetMonitorInfoW", GetLastError());
        return;
    }
    info.display_rect_in_os_coordinates = {mi.rcMonitor.left, mi.rcMonitor.top,
                                           mi.rcMonitor.right, mi.rcMonitor.bottom};

    // A DC for THIS monitor, not the desktop DC. GetDC(nullptr) reports the
    // primary display's physical size, so on a second monitor the game would be
    // handed one screen's pixel rect glued to another screen's millimetres.
    static std::atomic<bool> s_dcWarned{false};
    HDC dc = CreateDCW(nullptr, mi.szDevice, nullptr, nullptr);
    if (!dc) {
        ReportDisplayQueryFailure(s_dcWarned, "CreateDCW", GetLastError());
        return;
    }
    info.display_size_mm = {GetDeviceCaps(dc, HORZSIZE), GetDeviceCaps(dc, VERTSIZE)};
    DeleteDC(dc);
}

}  // namespace

tobii::TrackerInfo MakeTrackerInfo(void* tracked_window, bool attached) {
    tobii::TrackerInfo info{};
    info.type = tobii::kTrackerTypeEyeTracker;
    info.capabilities = tobii::kStreamHeadPose | tobii::kStreamPresence;
    info.url = kTrackerUrl;
    info.friendly_name = kTrackerName;
    info.monitor_name_in_os = kTrackerName;
    info.model_name = kTrackerModel;
    info.generation = kTrackerGeneration;
    info.serial_number = kTrackerSerial;
    info.firmware_version = kTrackerFirmware;
    info.is_attached = attached;
    FillDisplay(info, static_cast<HWND>(tracked_window));
    return info;
}

}  // namespace FarCry6HeadTracking
