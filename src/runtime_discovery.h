// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "build_profile.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace FarCry6HeadTracking::discovery {

struct Range {
    uint32_t begin;
    uint32_t end;
};

struct Image {
    const uint8_t* bytes;
    size_t size;
    uintptr_t base;
    std::vector<Range> readable;
    std::vector<Range> executable;
};

struct Shape {
    uint64_t prefix;
    uint64_t body;
    uint32_t instructions;
    uint8_t first[3];
};

struct FunctionShape {
    uint64_t prefix;
    uint64_t body;
    uint32_t instructions;
};

FunctionShape InspectFunction(const Image& image, uint32_t address);
uint32_t FindFunction(const Image& image, const Shape& shape, bool leaf);
bool Resolve(const Image& image, Offsets& result, std::string& diagnostic);
bool ResolveRunningBuild(void* module, Offsets& result);

}  // namespace FarCry6HeadTracking::discovery
