// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace FarCry6HeadTracking {

constexpr size_t kSessionPopulationBytes = 0x14;

struct SessionCounts {
    bool read = false;
    uint32_t max_players = 0;
    uint32_t current_players = 0;
};

inline SessionCounts DecodeSessionCounts(const void* bytes, size_t size) {
    SessionCounts counts;
    if (!bytes || size < kSessionPopulationBytes) return counts;
    const auto* data = static_cast<const uint8_t*>(bytes);
    // UPC R2: id pointer, joinability enum, current size, maximum size.
    std::memcpy(&counts.current_players, data + 0x0c, sizeof(uint32_t));
    std::memcpy(&counts.max_players, data + 0x10, sizeof(uint32_t));
    counts.read = true;
    return counts;
}

}  // namespace FarCry6HeadTracking
