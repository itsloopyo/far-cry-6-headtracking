// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "tobii_abi.h"

namespace FarCry6HeadTracking {

// Builds the TrackerInfo the game is told about: a screen-mounted eye tracker that
// reports head pose and presence and nothing else, described against the monitor
// the game window sits on.
//
// @p tracked_window is the handle the game passed to ITrackerController::TrackWindow,
// or nullptr before it has passed one, in which case the primary monitor is
// described instead. It is taken as void* because that is the type the ABI carries
// it in, which keeps windows.h out of every translation unit that needs a tracker.
//
// @p attached is what the game is told about the device being plugged in. It has
// to be the same answer ITrackerController::IsConnected gives, or the game gets
// two different replies to one question depending which it asks.
//
// The returned strings point at storage that lives for the process, so a copy of
// the struct stays valid for as long as the game holds it.
tobii::TrackerInfo MakeTrackerInfo(void* tracked_window, bool attached);

}  // namespace FarCry6HeadTracking
