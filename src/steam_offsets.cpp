// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "build_profile.h"

// The Steam depot's module is an older link than the Ubisoft Connect one, but every
// function below is instruction for instruction the same as its Ubisoft counterpart
// apart from relative addresses, so the structure offsets the mod reads are shared.
namespace FarCry6HeadTracking {
namespace {

constexpr Offsets kOffsets_20230428 = {
    {0x12eacb0, 0x88521f0e},  // camera_update
    {0x2979fe0, 0xfed8b6c2},  // extended_view_rotation
    {0x40dfa0, 0x1a9cc877},   // set_camera_angles
    {0x58f0f0, 0xc873c642},   // render_camera
    {0xaabc20, 0x8efa6e88},   // world_ray_query
    {0xa80c40, 0x340d4018},   // query_filter_construct
    {0xa85f90, 0x5b604b82},   // query_filter_destroy
    {0xa02ba0, 0x9c8504f5},   // query_hits_destroy
    {0x12b40e0, 0x89304dc0},  // camera_owner
    {0xe0afe0, 0x8ca36122},   // owner_body
    {0x2bf3f0, 0x7e1dd08c},   // body_collider
    {0x12eb6b4, 0xd157382e},  // sights_read
    {0x12eb6f4, 0x4eaa089f},  // binoculars_call
    {0x30cbb30, 0x50124a5a},  // reticle_visibility
    {0x27fff30, 0x9196e66a},  // reticle_position
    {0x309ab00, 0xfb600131},  // reticle_destroy
    {0x14cbd10, 0x85e2fed8},  // flashlight_spawn
    {0x14b9250, 0x57a5aac4},  // flashlight_destroy
    {0x1008970, 0x74840edc},  // set_world_matrix
    0x6e5f470,                // extended_view_instance
    0x6b5bf50,                // world
};

}  // namespace

extern const BuildProfile kSteamProfile_20230428 = {
    "steam-win64-20230428",
    {0x644bbd74, 0x1fe87000, 0x1f12fe67},
    &kOffsets_20230428,
};

}  // namespace FarCry6HeadTracking
