// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "runtime_discovery.h"
#include "coop_session.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {
using namespace FarCry6HeadTracking;
using namespace FarCry6HeadTracking::discovery;

struct Fixture {
    std::vector<uint8_t> bytes = std::vector<uint8_t>(0x6000);

    template<class T> void Put(uint32_t at, T value) { std::memcpy(bytes.data() + at, &value, sizeof(value)); }
    void Code(uint32_t at, std::initializer_list<uint8_t> code) {
        std::copy(code.begin(), code.end(), bytes.begin() + at);
    }
    Fixture() {
        Put<uint16_t>(0, 0x5a4d);
        Put<uint32_t>(0x3c, 0x80);
        Put<uint32_t>(0x80, 0x4550);
        Put<uint16_t>(0x84, 0x8664);
        Put<uint16_t>(0x86, 2);
        Put<uint16_t>(0x94, 0xf0);
        Put<uint16_t>(0x98, 0x20b);
        Put<uint32_t>(0xd0, static_cast<uint32_t>(bytes.size()));
        Put<uint32_t>(0x104, 16);
        Put<uint32_t>(0x120, 0x4000);
        Put<uint32_t>(0x124, 12);
        Put<uint32_t>(0x190, 0x2000);
        Put<uint32_t>(0x194, 0x1000);
        Put<uint32_t>(0x1ac, 0x60000020);
        Put<uint32_t>(0x1b8, 0x2000);
        Put<uint32_t>(0x1bc, 0x4000);
        Put<uint32_t>(0x1d4, 0x40000040);
        Put<uint8_t>(0x4100, 1);
        Function(0x1000, 0x4800, 0x2000);
        Entry(0x4000, 0x1000, 0x1017, 0x4100);
    }
    void Function(uint32_t at, uint32_t global, uint32_t callee) {
        Code(at, {0xf3, 0x0f, 0x10, 0x41, 0x24, 0xf3, 0x0f, 0x59, 0x05,
                  0, 0, 0, 0, 0x85, 0xd2, 0x74, 0x05, 0xe8, 0, 0, 0, 0, 0xc3});
        Put<int32_t>(at + 9, static_cast<int32_t>(global) - static_cast<int32_t>(at + 13));
        Put<int32_t>(at + 18, static_cast<int32_t>(callee) - static_cast<int32_t>(at + 22));
        Code(callee, {0xc3});
    }
    void Entry(uint32_t at, uint32_t begin, uint32_t end, uint32_t unwind) {
        Put(at, begin); Put(at + 4, end); Put(at + 8, unwind);
    }
    Image View() const {
        return {bytes.data(), bytes.size(), 0x140000000ull, {{0, 0x6000}}, {{0x1000, 0x3000}}};
    }
};

int failures = 0;
void Check(bool condition, const char* name) {
    if (!condition) { ++failures; std::printf("FAIL discovery: %s\n", name); }
}

template<class Action> void Reject(Action action, const char* name) {
    try { action(); Check(false, name); }
    catch (const std::runtime_error&) {}
}
}  // namespace

int TestDiscovery() {
    const uint8_t session[]{
        0, 0, 0, 0, 0, 0, 0, 0,
        4, 0, 0, 0,
        1, 0, 0, 0,
        2, 0, 0, 0,
    };
    const auto population = DecodeSessionCounts(session, sizeof(session));
    Check(population.read && population.current_players == 1 && population.max_players == 2,
          "UPC joinability is distinct from the maximum player count");
    Check(!DecodeSessionCounts(session, sizeof(session) - 1).read, "truncated UPC descriptor");
    Check(!DecodeSessionCounts(nullptr, sizeof(session)).read, "missing UPC descriptor");
    Fixture original;
    const auto signature = InspectFunction(original.View(), 0x1000);
    const Shape shape{signature.prefix, signature.body, signature.instructions, {0xf3, 0x0f, 0x10}};
    Check(signature.instructions == 6, "independent fixture instruction count");
    Check(FindFunction(original.View(), shape, false) == 0x1000, "unique unwind anchor");

    Fixture moved;
    std::fill(moved.bytes.begin() + 0x1000, moved.bytes.begin() + 0x1017, uint8_t{0xcc});
    moved.Function(0x1800, 0x4a00, 0x2300);
    moved.Entry(0x4000, 0x1800, 0x1817, 0x4100);
    auto relocated = moved.View();
    relocated.base += 0x70000000;
    Check(FindFunction(relocated, shape, false) == 0x1800, "moved code, global, call and image base");
    Check(FindFunction(relocated, shape, true) == 0x1800, "leaf relocation");

    struct Mutation { uint32_t at; uint8_t value; };
    for (const auto mutation : {Mutation{0x1004, 0x28}, {0x1003, 0x42},
                                {0x1000, 0xf2}, {0x1010, 0x04}}) {
        Fixture bad;
        bad.Put(mutation.at, mutation.value);
        Reject([&] { FindFunction(bad.View(), shape, false); }, "changed field, owner, width or branch boundary");
    }
    Fixture duplicate;
    duplicate.Function(0x1800, 0x4a00, 0x2300);
    duplicate.Entry(0x400c, 0x1800, 0x1817, 0x4100);
    duplicate.Put<uint32_t>(0x124, 24);
    Reject([&] { FindFunction(duplicate.View(), shape, false); }, "duplicate anchors");
    Reject([&] { FindFunction(duplicate.View(), shape, true); }, "duplicate leaf anchors");

    Fixture malformed;
    malformed.Put<uint32_t>(0x4004, 0x1015);
    Reject([&] { InspectFunction(malformed.View(), 0x1000); }, "truncated instruction at unwind boundary");
    auto unreadable = original.View();
    unreadable.readable = {{0, 0x1008}, {0x1010, 0x6000}};
    Reject([&] { InspectFunction(unreadable, 0x1000); }, "unreadable instruction tail");
    auto noExecute = original.View();
    noExecute.executable.clear();
    Reject([&] { FindFunction(noExecute, shape, false); }, "non-executable image pages");
    auto truncated = original.View();
    truncated.size = 0x100;
    truncated.readable = {{0, 0x100}};
    truncated.executable.clear();
    Reject([&] { FindFunction(truncated, shape, false); }, "truncated PE headers");

    Fixture chained;
    chained.Put<uint32_t>(0x4008, 0x4120);
    chained.Put<uint8_t>(0x4120, 0x21);
    chained.Entry(0x4124, 0x1000, 0x800, 0x4100);
    Check(FindFunction(chained.View(), shape, false) == 0x1000, "chained prologue with nonbounding embedded end");
    chained.Put<uint32_t>(0x412c, 0x4120);
    Reject([&] { FindFunction(chained.View(), shape, false); }, "cyclic unwind");

    Fixture sse;
    sse.Code(0x1000, {0x66, 0x44, 0x0f, 0x38, 0x14, 0x1d, 0, 0, 0, 0, 0xc3});
    sse.Put<int32_t>(0x1006, 0x4800 - 0x100a);
    sse.Entry(0x4000, 0x1000, 0x100b, 0x4100);
    Check(InspectFunction(sse.View(), 0x1000).instructions == 2, "SSE4.1 with REX and RIP displacement");

    Offsets result = *kKnownProfiles[0]->offsets;
    std::string diagnostic;
    Check(!Resolve(original.View(), result, diagnostic), "unknown registry route rejects missing dependency set");
    Check(result.camera_update.rva == 0 && result.world == 0 && result.flashlight_spawn.rva == 0,
          "failed selection clears previously populated result");
    Check(diagnostic.find("camera update") != std::string::npos, "failure identifies discovery stage");
    std::printf("discovery tests: %d failure(s)\n", failures);
    return failures;
}
