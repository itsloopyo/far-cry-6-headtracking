// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <windows.h>

namespace FarCry6HeadTracking {

// Centres the game's window on the work area of the monitor the game places it on.
//
// Far Cry 6 holds its window at the WindowPosition saved in gamerprofile.xml. It
// places the window with SetWindowPos from its own module, and asks again every
// frame the window is anywhere else, so a window moved from outside is back within
// a frame. This therefore never moves the window itself: it rewrites the position
// in the game's own placement call, and answers the repeated requests without
// moving a window that is already centred. A placement as large as the work area
// (fullscreen or borderless) passes through unchanged.
//
// gameModule is the module whose SetWindowPos calls are the game placing its window.
// A failure to install is logged, and leaves the window where the game puts it.
void StartWindowCentering(HMODULE gameModule);

void StopWindowCentering();

}  // namespace FarCry6HeadTracking
