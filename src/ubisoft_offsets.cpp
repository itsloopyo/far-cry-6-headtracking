// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "build_profile.h"

namespace FarCry6HeadTracking {
namespace {

constexpr Offsets kOffsets_20250514 = {
    {0x12d8d10, 0xd4da43f5},  // camera_update
    {0x29681b0, 0xfed8b6c2},  // extended_view_rotation
    {0x4087e0, 0x1a9cc877},   // set_camera_angles
    {0x5874d0, 0xc873c642},   // render_camera
    {0xa97520, 0xfed2a23f},   // world_ray_query
    {0xa6c4b0, 0xd3825632},   // query_filter_construct
    {0xa71800, 0xafc3f51f},   // query_filter_destroy
    {0x9edf70, 0x3aaf89f3},   // query_hits_destroy
    {0x12a2140, 0xee726436},  // camera_owner
    {0xdf7ad0, 0x8ca36122},   // owner_body
    {0x2b9ee0, 0x66cd8ae6},   // body_collider
    {0x12d9714, 0xd157382e},  // sights_read
    {0x12d9754, 0x4eaa089f},  // binoculars_call
    {0x30bb060, 0x8812c97c},  // reticle_visibility
    {0x27ee100, 0x9196e66a},  // reticle_position
    {0x3089e50, 0x0683cfc9},  // reticle_destroy
    {0x2777930, 0xcb643b5c},  // reticle_publish_position
    {0x14b9c10, 0x95cb8be8},  // flashlight_spawn
    {0x14a7150, 0xbed0683d},  // flashlight_destroy
    {0xff5b80, 0x74840edc},   // set_world_matrix
    0x6dc8960,                // extended_view_instance
    0x6ac5798,                // world
};

}  // namespace

extern const BuildProfile kUbisoftProfile_20250514 = {
    "ubisoft-win64-20250514",
    {0x6824d119, 0x1fb64000, 0x1ee4dd4b},
    &kOffsets_20250514,
};

}  // namespace FarCry6HeadTracking
