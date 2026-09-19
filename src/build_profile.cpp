// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "build_profile.h"
#include "logging.h"

namespace FarCry6HeadTracking {

const BuildProfile* const kKnownProfiles[] = {
    &kUbisoftProfile_20250514,
    &kSteamProfile_20230428,
};
const int kKnownProfileCount = static_cast<int>(sizeof(kKnownProfiles) / sizeof(kKnownProfiles[0]));

const BuildProfile* MatchRunningBuild(void* module) {
    using cameraunlock::memory::ClassifyMismatch;
    using cameraunlock::memory::FingerprintMismatch;
    using cameraunlock::memory::PeFingerprint;

    PeFingerprint running{};
    if (!module || !cameraunlock::memory::ReadPeFingerprint(module, running)) {
        Log::Line("ERROR: could not read the FC_m64d3d12.dll fingerprint; camera left alone");
        return nullptr;
    }
    // Logged whatever happens next: a user on an unknown build sends this file, and
    // these three numbers are what a new profile is keyed on.
    Log::Line("Build: FC_m64d3d12.dll TimeDateStamp=%08X SizeOfImage=%08X CheckSum=%08X",
              running.TimeDateStamp, running.SizeOfImage, running.CheckSum);

    for (const BuildProfile* profile : kKnownProfiles) {
        if (running.Matches(profile->fingerprint)) {
            Log::Line("Build: matched profile %s", profile->name);
            return profile;
        }
    }

    const BuildProfile& primary = *kKnownProfiles[0];
    switch (ClassifyMismatch(running, primary.fingerprint)) {
        case FingerprintMismatch::Newer:
            Log::Line("ERROR: this Far Cry 6 is newer than any build the mod knows (newest is %s); "
                      "camera left alone. Check the releases page for an updated mod.",
                      primary.name);
            break;
        case FingerprintMismatch::Older:
            Log::Line("ERROR: this Far Cry 6 is older than the newest build the mod knows (%s) "
                      "and matches no other; camera left alone. Let the store finish updating.",
                      primary.name);
            break;
        case FingerprintMismatch::Differs:
            Log::Line("ERROR: FC_m64d3d12.dll is repacked or modified; camera left alone.");
            break;
    }
    return nullptr;
}

}  // namespace FarCry6HeadTracking
