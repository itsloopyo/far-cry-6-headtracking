// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "cameraunlock/memory/pe_fingerprint.h"

#include <cstdint>

// One shipped FC_m64d3d12.dll: its PE fingerprint and where the camera functions
// sit in it.
//
// Append-only. A patch or a store build that moves a function gets a NEW profile
// added to the top of kKnownProfiles, never an in-place edit, so a user who has not
// taken the patch keeps matching the profile their module was built with. The top
// entry is the diagnostic primary: when nothing matches, the running fingerprint is
// compared against it to say whether the game is newer, older, or repacked.
namespace FarCry6HeadTracking {

// A function the mod hooks or calls, with the FNV-1a hash of its first 32 bytes in
// the running (unpacked) module. The hash is checked before anything is hooked.
struct NativeFunction {
    uintptr_t rva;
    uint32_t prefix_hash;
};

struct Offsets {
    NativeFunction camera_update;
    NativeFunction extended_view_rotation;
    NativeFunction set_camera_angles;
    NativeFunction render_camera;
    NativeFunction world_ray_query;
    NativeFunction query_filter_construct;
    NativeFunction query_filter_destroy;
    NativeFunction query_hits_destroy;
    NativeFunction camera_owner;
    NativeFunction owner_body;
    NativeFunction body_collider;
    // Not called. These are the camera update's own reads of the sights byte and the
    // sight-device slot, checked so the aim state the mod reads is the one it uses.
    NativeFunction sights_read;
    NativeFunction binoculars_call;
    NativeFunction reticle_position;
    NativeFunction reticle_publish_position;
    // Vtable slots 0xf8 (spawn its light) and 0 (scalar deleting destructor) of the
    // flashlight component. Both are jump thunks into the packed code.
    NativeFunction flashlight_spawn;
    NativeFunction flashlight_destroy;
    // The engine-wide entity transform setter: (entity, 4x4 row matrix, extra).
    NativeFunction set_world_matrix;
    uintptr_t extended_view_instance;
    uintptr_t world;
};

struct BuildProfile {
    const char* name;
    cameraunlock::memory::PeFingerprint fingerprint;
    const Offsets* offsets;
};

extern const BuildProfile kUbisoftProfile_20250514;
extern const BuildProfile kSteamProfile_20230428;

// Most recent build first.
extern const BuildProfile* const kKnownProfiles[];
extern const int kKnownProfileCount;

// Fingerprints the module, logs the result either way, and returns the matching
// profile, or nullptr when nothing matches and the camera must be left alone.
const BuildProfile* MatchRunningBuild(void* module);

}  // namespace FarCry6HeadTracking
