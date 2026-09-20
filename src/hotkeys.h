// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "config.h"
#include "tracking_runtime.h"

namespace FarCry6HeadTracking {

void StartHotkeys(const Config& cfg, TrackingRuntime& runtime);
void StopHotkeys();

}  // namespace FarCry6HeadTracking
