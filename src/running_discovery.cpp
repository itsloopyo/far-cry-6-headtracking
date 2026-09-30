// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "runtime_discovery.h"
#include "logging.h"

#include <windows.h>

#include <algorithm>
#include <limits>

namespace FarCry6HeadTracking::discovery {

bool ResolveRunningBuild(void* module, Offsets& result) {
    result = {};
    cameraunlock::memory::PeFingerprint fingerprint{};
    if (!cameraunlock::memory::ReadPeFingerprint(module, fingerprint)) {
        Log::Line("ERROR: could not read FC_m64d3d12.dll fingerprint; tracking disabled");
        return false;
    }
    Log::Line("Build: FC_m64d3d12.dll TimeDateStamp=%08X SizeOfImage=%08X CheckSum=%08X",
              fingerprint.TimeDateStamp, fingerprint.SizeOfImage, fingerprint.CheckSum);
    const auto base = reinterpret_cast<uintptr_t>(module);
    if (!fingerprint.SizeOfImage || fingerprint.SizeOfImage > std::numeric_limits<uintptr_t>::max() - base) {
        Log::Line("ERROR: invalid engine image extent; tracking disabled");
        return false;
    }
    Image image{static_cast<const uint8_t*>(module), fingerprint.SizeOfImage, base, {}, {}};
    const auto append = [](std::vector<Range>& ranges, Range range) {
        if (!ranges.empty() && ranges.back().end == range.begin) ranges.back().end = range.end;
        else ranges.push_back(range);
    };
    for (uintptr_t at = base; at < base + image.size;) {
        MEMORY_BASIC_INFORMATION memory{};
        if (!VirtualQuery(reinterpret_cast<const void*>(at), &memory, sizeof(memory)) ||
            memory.RegionSize > std::numeric_limits<uintptr_t>::max() - at) {
            Log::Line("ERROR: could not inspect engine image pages; tracking disabled");
            return false;
        }
        const auto end = std::min(base + image.size,
                                 reinterpret_cast<uintptr_t>(memory.BaseAddress) + memory.RegionSize);
        if (end <= at) {
            Log::Line("ERROR: invalid engine page extent; tracking disabled");
            return false;
        }
        const DWORD access = memory.Protect & 0xff;
        const bool readable = access == PAGE_READONLY || access == PAGE_READWRITE ||
                              access == PAGE_WRITECOPY || access == PAGE_EXECUTE_READ ||
                              access == PAGE_EXECUTE_READWRITE || access == PAGE_EXECUTE_WRITECOPY;
        if (memory.State == MEM_COMMIT && memory.AllocationBase == module && readable &&
            !(memory.Protect & PAGE_GUARD)) {
            const Range range{static_cast<uint32_t>(at - base), static_cast<uint32_t>(end - base)};
            append(image.readable, range);
            if (access == PAGE_EXECUTE_READ || access == PAGE_EXECUTE_READWRITE ||
                access == PAGE_EXECUTE_WRITECOPY) append(image.executable, range);
        }
        at = end;
    }
    const auto start = GetTickCount64();
    std::string diagnostic;
    const bool resolved = Resolve(image, result, diagnostic);
    Log::Line("Discovery: %s (%llu ms)", diagnostic.c_str(), GetTickCount64() - start);
    if (!resolved) Log::Line("ERROR: native dependency validation failed; tracking disabled");
    return resolved;
}

}  // namespace FarCry6HeadTracking::discovery
