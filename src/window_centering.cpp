// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "window_centering.h"

#include "logging.h"

#include "cameraunlock/hooks/hook_manager.h"

#include <intrin.h>

#include <atomic>
#include <cwchar>

namespace FarCry6HeadTracking {

namespace {

using SetWindowPosFn = BOOL(WINAPI*)(HWND, HWND, int, int, int, int, UINT);

// The class of the game's main window. The 868x488 splash the game shows first is
// "NomadSplash", and is never placed through SetWindowPos.
constexpr wchar_t kGameWindowClass[] = L"Nomad";

void* g_target = nullptr;
SetWindowPosFn g_original = nullptr;
uintptr_t g_gameStart = 0;
uintptr_t g_gameEnd = 0;

// Placements are logged on counts 1, 2, 4, 8... The game places its window once at
// startup and once per outside move, so a count that keeps climbing is a real
// placement loop, and still readable in the log.
std::atomic<long> g_centred{0};
std::atomic<long> g_filling{0};
std::atomic<bool> g_skipReported{false};

bool ShouldLog(std::atomic<long>& counter, long& count) {
    count = ++counter;
    return (count & (count - 1)) == 0;
}

bool IsGameWindow(HWND window) {
    wchar_t cls[16];
    return GetClassNameW(window, cls, 16) > 0 && std::wcscmp(cls, kGameWindowClass) == 0;
}

int CenteredOrigin(int areaStart, int areaExtent, int windowExtent) {
    return areaStart + (areaExtent - windowExtent) / 2;
}

struct Centred {
    bool apply = false;
    int x = 0;
    int y = 0;
    int cx = 0;
    int cy = 0;
};

// Where the game's placement of its window should put it instead. apply is false
// when the placement is left as the game asked.
Centred CentreFor(HWND window, int x, int y, int cx, int cy, UINT flags) {
    Centred out;
    if (flags & SWP_NOSIZE) {
        RECT current{};
        if (!GetWindowRect(window, &current)) {
            Log::Line("WARN: window: GetWindowRect failed (%lu), leaving the game's "
                      "placement as it is", GetLastError());
            return out;
        }
        cx = current.right - current.left;
        cy = current.bottom - current.top;
    }

    const RECT requested{x, y, x + cx, y + cy};
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(MonitorFromRect(&requested, MONITOR_DEFAULTTONEAREST), &info)) {
        Log::Line("WARN: window: GetMonitorInfoW failed (%lu), leaving the game's "
                  "placement as it is", GetLastError());
        return out;
    }
    // The work area, not the monitor: centring against the full monitor puts the
    // title bar behind a top-docked taskbar, where it cannot be dragged back.
    const RECT& work = info.rcWork;
    const int workWidth = work.right - work.left;
    const int workHeight = work.bottom - work.top;

    if (cx >= workWidth || cy >= workHeight) {
        long count = 0;
        if (ShouldLog(g_filling, count)) {
            Log::Line("window: the game placed its %dx%d window at (%d, %d), filling the "
                      "%dx%d work area, so it is left there (placement %ld)", cx, cy, x, y,
                      workWidth, workHeight, count);
        }
        return out;
    }

    out.x = CenteredOrigin(work.left, workWidth, cx);
    out.y = CenteredOrigin(work.top, workHeight, cy);
    out.cx = cx;
    out.cy = cy;
    out.apply = out.x != x || out.y != y;
    return out;
}

BOOL WINAPI HookedSetWindowPos(HWND window, HWND insertAfter, int x, int y, int cx, int cy,
                               UINT flags) {
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    if ((flags & SWP_NOMOVE) || caller < g_gameStart || caller >= g_gameEnd ||
        !IsGameWindow(window)) {
        return g_original(window, insertAfter, x, y, cx, cy, flags);
    }

    const Centred centred = CentreFor(window, x, y, cx, cy, flags);
    if (!centred.apply) {
        return g_original(window, insertAfter, x, y, cx, cy, flags);
    }

    // The game compares its window against the saved position every frame and asks
    // again whenever they differ, which once centred is every frame. Moving the
    // window to where it already is would send the frame change and position
    // messages each time for nothing, so those requests are answered here.
    RECT current{};
    if (GetWindowRect(window, &current) && current.left == centred.x &&
        current.top == centred.y && current.right - current.left == centred.cx &&
        current.bottom - current.top == centred.cy) {
        if (!g_skipReported.exchange(true)) {
            Log::Line("window: the game keeps asking for (%d, %d); the window is already "
                      "centred at (%d, %d), so those requests are answered without moving "
                      "it", x, y, centred.x, centred.y);
        }
        return TRUE;
    }

    long count = 0;
    if (ShouldLog(g_centred, count)) {
        Log::Line("window: the game placed its %dx%d window at (%d, %d); centred to "
                  "(%d, %d) (placement %ld)", centred.cx, centred.cy, x, y, centred.x,
                  centred.y, count);
    }
    return g_original(window, insertAfter, centred.x, centred.y, cx, cy, flags);
}

}  // namespace

void StartWindowCentering(HMODULE gameModule) {
    if (g_target) return;

    if (!gameModule) {
        Log::Line("ERROR: window: the game module is not loaded, so the window is left "
                  "where the game puts it");
        return;
    }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(gameModule);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        reinterpret_cast<const BYTE*>(gameModule) + dos->e_lfanew);
    g_gameStart = reinterpret_cast<uintptr_t>(gameModule);
    g_gameEnd = g_gameStart + nt->OptionalHeader.SizeOfImage;

    void* address = reinterpret_cast<void*>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowPos"));
    if (!address) {
        Log::Line("ERROR: window: user32.dll does not export SetWindowPos, so the window is "
                  "left where the game puts it");
        return;
    }

    using cameraunlock::hooks::HookManager;
    using cameraunlock::hooks::HookStatus;
    using cameraunlock::hooks::HookStatusToString;
    HookManager& hooks = HookManager::Instance();
    HookStatus status = hooks.Initialize();
    if (status != HookStatus::Ok && status != HookStatus::ErrorAlreadyInitialized) {
        Log::Line("ERROR: window: hook engine failed to start: %s", HookStatusToString(status));
        return;
    }
    status = hooks.CreateHook(address, reinterpret_cast<void*>(&HookedSetWindowPos),
                              reinterpret_cast<void**>(&g_original));
    if (status != HookStatus::Ok) {
        Log::Line("ERROR: window: could not hook SetWindowPos: %s", HookStatusToString(status));
        return;
    }
    g_target = address;
    status = hooks.EnableHook(address);
    if (status != HookStatus::Ok) {
        Log::Line("ERROR: window: could not enable the SetWindowPos hook: %s",
                  HookStatusToString(status));
        StopWindowCentering();
        return;
    }
    Log::Line("window: centring the game's own window placement");
}

void StopWindowCentering() {
    if (!g_target) return;
    // g_original stays pointing at the trampoline, which MinHook does not free on
    // removal, so a thread already inside the detour still completes its call.
    cameraunlock::hooks::HookManager& hooks = cameraunlock::hooks::HookManager::Instance();
    hooks.DisableHook(g_target);
    hooks.RemoveHook(g_target);
    g_target = nullptr;
}

}  // namespace FarCry6HeadTracking
