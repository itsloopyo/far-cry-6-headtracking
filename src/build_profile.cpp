// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "build_profile.h"


namespace FarCry6HeadTracking {

const BuildProfile* const kKnownProfiles[] = {
    &kUbisoftProfile_20250514,
    &kSteamProfile_20230428,
};
const int kKnownProfileCount = static_cast<int>(sizeof(kKnownProfiles) / sizeof(kKnownProfiles[0]));

}  // namespace FarCry6HeadTracking
