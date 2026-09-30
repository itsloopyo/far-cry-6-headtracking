// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "runtime_discovery.h"

#include "hde64.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace FarCry6HeadTracking::discovery {
namespace {

constexpr uint64_t kOffset = 14695981039346656037ull;

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void HashByte(uint64_t& hash, uint8_t value) {
    hash = (hash ^ value) * 1099511628211ull;
}

bool Contains(const std::vector<Range>& ranges, uint32_t address, size_t size) {
    const auto end = static_cast<uint64_t>(address) + size;
    auto it = std::upper_bound(ranges.begin(), ranges.end(), address,
                               [](uint32_t a, const Range& r) { return a < r.begin; });
    if (it == ranges.begin()) return false;
    --it;
    return address >= it->begin && end <= it->end;
}

struct Instruction {
    uint32_t address = 0;
    hde64s decoded{};
    std::array<uint8_t, 16> bytes{};
    uint32_t target = 0;
    uint32_t reference = 0;
    bool relative = false;
    bool call = false;
    bool jump = false;
    bool terminal = false;

    int32_t Displacement() const {
        if (decoded.flags & F_DISP8) return static_cast<int8_t>(decoded.disp.disp8);
        if (decoded.flags & F_DISP32) return static_cast<int32_t>(decoded.disp.disp32);
        return 0;
    }

    int Register() const { return decoded.modrm_reg + 8 * decoded.rex_r; }
    int Base() const { return decoded.modrm_rm + 8 * decoded.rex_b; }
};

struct Function {
    uint32_t address;
    uint64_t hash;
    std::vector<Instruction> instructions;
};

class Reader {
public:
    explicit Reader(const Image& image) : image_(image) {
        Require(image.size <= std::numeric_limits<uint32_t>::max(), "image exceeds RVA bounds");
        uint32_t previous = 0;
        Require(image.bytes != nullptr, "missing image bytes");
        for (const auto* ranges : {&image.readable, &image.executable}) {
            previous = 0;
            for (const auto& range : *ranges) {
                Require(range.begin >= previous && range.begin < range.end && range.end <= image.size,
                        "invalid image ranges");
                previous = range.end;
            }
        }
        Require(Read<uint16_t>(0) == 0x5a4d, "missing DOS header");
        const auto nt = Read<uint32_t>(0x3c);
        Bytes(nt, 0x108);
        Require(Read<uint32_t>(nt) == 0x4550 && Read<uint16_t>(nt + 4) == 0x8664 &&
                    Read<uint16_t>(nt + 24) == 0x20b,
                "expected x64 PE image");
        Require(Read<uint32_t>(nt + 24 + 56) == image.size, "inconsistent PE image size");
        fingerprint = {Read<uint32_t>(nt + 8), Read<uint32_t>(nt + 80), Read<uint32_t>(nt + 88)};
        const auto count = Read<uint16_t>(nt + 6);
        const auto optionalSize = Read<uint16_t>(nt + 20);
        Require(count > 0 && count <= 96 && optionalSize >= 0xf0, "invalid PE section table");
        Require(Read<uint32_t>(nt + 24 + 108) >= 4, "missing PE data directories");
        imports_ = Read<uint32_t>(nt + 24 + 120);
        importSize_ = Read<uint32_t>(nt + 24 + 124);
        const auto sectionTable = nt + 24 + optionalSize;
        Bytes(sectionTable, static_cast<size_t>(count) * 40);
        for (unsigned i = 0; i < count; ++i) {
            const auto row = sectionTable + i * 40;
            const auto begin = Read<uint32_t>(row + 12);
            const auto size = Read<uint32_t>(row + 8);
            Require(size <= image.size && begin <= image.size - size, "section outside image");
            if (!size) continue;
            const auto flags = Read<uint32_t>(row + 36);
            auto& regions = flags & 0x20000000 ? executable_ : data_;
            for (const auto& readable : image.readable) {
                const auto lo = std::max(begin, readable.begin);
                const auto hi = std::min(begin + size, readable.end);
                if (lo >= hi) continue;
                if (!(flags & 0x20000000)) regions.push_back({lo, hi});
                else for (const auto& code : image.executable) {
                    const auto codeLo = std::max(lo, code.begin);
                    const auto codeHi = std::min(hi, code.end);
                    if (codeLo < codeHi) regions.push_back({codeLo, codeHi});
                }
            }
        }
        auto order = [](const Range& a, const Range& b) { return a.begin < b.begin; };
        std::sort(executable_.begin(), executable_.end(), order);
        std::sort(data_.begin(), data_.end(), order);
        const auto table = Read<uint32_t>(nt + 24 + 112 + 24);
        const auto size = Read<uint32_t>(nt + 24 + 112 + 28);
        Require(size && size % 12 == 0, "missing or malformed unwind table");
        Bytes(table, size);
        for (uint32_t i = 0; i < size; i += 12) {
            const auto begin = Read<uint32_t>(table + i);
            const auto end = Read<uint32_t>(table + i + 4);
            const auto unwind = Read<uint32_t>(table + i + 8);
            Require(begin < end && Executable(begin) && Executable(end - 1), "unwind endpoints are not executable");
            const auto root = Primary(begin, unwind);
            functions_[root].push_back({begin, end});
        }
        for (auto& pair : functions_) std::sort(pair.second.begin(), pair.second.end(), order);
    }

    const uint8_t* Bytes(uint32_t address, size_t size) const {
        Require(size <= image_.size && address <= image_.size - size &&
                    Contains(image_.readable, address, size), "unreadable image range");
        return image_.bytes + address;
    }

    template<class T> T Read(uint32_t address) const {
        T value;
        std::memcpy(&value, Bytes(address, sizeof(T)), sizeof(T));
        return value;
    }

    bool Executable(uint32_t address, size_t size = 1) const {
        return Contains(executable_, address, size);
    }

    bool Data(uint32_t address, size_t size = 1) const {
        return Contains(data_, address, size);
    }

    uint32_t Pointer(uint32_t address) const {
        const auto value = Read<uint64_t>(address);
        Require(value >= image_.base && value - image_.base < image_.size, "pointer outside engine image");
        return static_cast<uint32_t>(value - image_.base);
    }

    Instruction Decode(uint32_t address) const {
        Require(Executable(address), "instruction is not executable");
        Instruction out;
        out.address = address;
        size_t available = 0;
        while (available < 15 && Executable(address, available + 1)) ++available;
        std::array<uint8_t, 32> buffer{};
        std::memcpy(buffer.data(), Bytes(address, available), available);
        hde64_disasm(buffer.data(), &out.decoded);
        // HDE predates SSE4.1. BLENDVPS has the same ModR/M addressing as MOVAPS,
        // with a third opcode byte and an implicit XMM0 mask.
        const size_t opcode = (buffer[1] >= 0x40 && buffer[1] <= 0x4f) ? 2 : 1;
        if ((out.decoded.flags & F_ERROR) && available >= opcode + 4 && buffer[0] == 0x66 &&
            buffer[opcode] == 0x0f && buffer[opcode + 1] == 0x38 && buffer[opcode + 2] == 0x14) {
            buffer[opcode + 1] = 0x28;
            std::memmove(buffer.data() + opcode + 2, buffer.data() + opcode + 3, buffer.size() - opcode - 3);
            hde64_disasm(buffer.data(), &out.decoded);
            ++out.decoded.len;
        }
        if ((out.decoded.flags & F_ERROR) || !out.decoded.len || out.decoded.len > available) {
            throw std::runtime_error("unsupported or truncated instruction at RVA " + std::to_string(address));
        }
        auto& d = out.decoded;
        std::memcpy(out.bytes.data(), Bytes(address, d.len), d.len);
        out.relative = (d.flags & F_RELATIVE) != 0;
        out.call = d.opcode == 0xe8 || (d.opcode == 0xff && d.modrm_reg == 2);
        out.jump = d.opcode == 0xe9 || d.opcode == 0xeb ||
                   (d.opcode >= 0x70 && d.opcode <= 0x7f) ||
                   (d.opcode == 0x0f && d.opcode2 >= 0x80 && d.opcode2 <= 0x8f) ||
                   (d.opcode >= 0xe0 && d.opcode <= 0xe3) ||
                   (d.opcode == 0xff && d.modrm_reg == 4);
        out.terminal = d.opcode == 0xc3 || d.opcode == 0xc2 || d.opcode == 0xe9 ||
                       d.opcode == 0xeb || (d.opcode == 0xff && d.modrm_reg == 4);
        Require(d.opcode != 0xcc, "control flow reaches padding");
        unsigned immediate = d.flags & F_IMM64 ? 8 : d.flags & F_IMM32 ? 4 :
                             d.flags & F_IMM16 ? 2 : d.flags & F_IMM8 ? 1 : 0;
        if (out.relative) {
            const auto displacement = immediate == 1 ? static_cast<int8_t>(d.imm.imm8) :
                                                       static_cast<int32_t>(d.imm.imm32);
            const int64_t target = static_cast<int64_t>(address) + d.len + displacement;
            Require(target >= 0 && static_cast<uint64_t>(target) < image_.size,
                    "relative branch outside image");
            out.target = static_cast<uint32_t>(target);
            Require(Executable(out.target), "relative branch target is not executable");
            std::fill(out.bytes.begin() + d.len - immediate, out.bytes.begin() + d.len, uint8_t{0});
        }
        if ((d.flags & F_MODRM) && d.modrm_mod == 0 && d.modrm_rm == 5 && !d.p_67) {
            const int64_t target = static_cast<int64_t>(address) + d.len + static_cast<int32_t>(d.disp.disp32);
            Require(target >= 0 && static_cast<uint64_t>(target) < image_.size,
                    "RIP-relative operand outside image");
            out.reference = static_cast<uint32_t>(target);
            Bytes(out.reference, 1);
            std::fill(out.bytes.begin() + d.len - immediate - 4, out.bytes.begin() + d.len - immediate, uint8_t{0});
        }
        return out;
    }

    uint64_t Prefix(uint32_t address, uint32_t count) const {
        uint64_t hash = kOffset;
        for (uint32_t i = 0; i < std::min(count, 12u); ++i) {
            const auto instruction = Decode(address);
            HashInstruction(hash, instruction);
            address += instruction.decoded.len;
        }
        return hash;
    }

    Function DecodeFunction(uint32_t address) const {
        std::vector<Range> spans;
        const auto found = functions_.find(address);
        if (found != functions_.end()) spans = found->second;
        else spans.push_back({address, static_cast<uint32_t>(std::min<uint64_t>(image_.size,
                                                       static_cast<uint64_t>(address) + 4096))});
        std::vector<uint32_t> pending{address};
        std::map<uint32_t, Instruction> decoded;
        while (!pending.empty()) {
            auto at = pending.back();
            pending.pop_back();
            while (!decoded.count(at)) {
                Require(Contains(spans, at, 1), "control flow leaves function bounds");
                Require(decoded.size() < 32768, "function exceeds instruction bound");
                auto instruction = Decode(at);
                Require(Contains(spans, at, instruction.decoded.len), "instruction crosses unwind boundary");
                decoded.emplace(at, instruction);
                if (instruction.jump && instruction.relative && Contains(spans, instruction.target, 1)) {
                    pending.push_back(instruction.target);
                }
                if (instruction.terminal) break;
                at += instruction.decoded.len;
            }
        }
        Function result{address, kOffset, {}};
        std::map<uint32_t, uint32_t> ordinals;
        uint32_t previousEnd = 0;
        for (const auto& pair : decoded) {
            Require(pair.first >= previousEnd, "branch enters the middle of an instruction");
            previousEnd = pair.first + pair.second.decoded.len;
            ordinals.emplace(pair.first, static_cast<uint32_t>(result.instructions.size()));
            result.instructions.push_back(pair.second);
        }
        for (const auto& instruction : result.instructions) {
            HashInstruction(result.hash, instruction);
            const auto target = ordinals.find(instruction.relative ? instruction.target : 0);
            const uint32_t ordinal = target == ordinals.end() ? UINT32_MAX : target->second;
            for (unsigned shift = 0; shift < 32; shift += 8) HashByte(result.hash, static_cast<uint8_t>(ordinal >> shift));
        }
        return result;
    }

    bool Matches(uint32_t address, const Shape& shape) const {
        if (!Executable(address, 3) || std::memcmp(Bytes(address, 3), shape.first, 3) != 0) return false;
        // The three-byte prefilter also admits unrelated leaves and padding.
        // Only a complete prefix establishes a candidate whose body must decode.
        try {
            if (Prefix(address, shape.instructions) != shape.prefix) return false;
        } catch (const std::runtime_error&) {
            return false;
        }
        const auto function = DecodeFunction(address);
        return function.hash == shape.body && function.instructions.size() == shape.instructions;
    }

    uint32_t Find(const Shape& shape, bool leaf) const {
        std::set<uint32_t> matches;
        if (leaf) {
            for (const auto& range : executable_) {
                for (uint32_t at = range.begin; static_cast<uint64_t>(at) + 3 <= range.end; ++at) {
                    if (std::memcmp(image_.bytes + at, shape.first, 3) == 0 && Matches(at, shape)) matches.insert(at);
                }
            }
        } else {
            for (const auto& pair : functions_) if (Matches(pair.first, shape)) matches.insert(pair.first);
        }
        Require(matches.size() == 1, matches.empty() ? "missing instruction anchor" : "ambiguous instruction anchor");
        return *matches.begin();
    }

    uint32_t Callee(const Function& caller, const Shape& shape) const {
        std::set<uint32_t> targets;
        for (const auto& instruction : caller.instructions) {
            if (instruction.call && instruction.relative && Matches(instruction.target, shape)) targets.insert(instruction.target);
        }
        Require(targets.size() == 1, targets.empty() ? "missing native call relationship" : "ambiguous native call relationship");
        return *targets.begin();
    }

    uint32_t Follow(uint32_t address) const {
        std::set<uint32_t> visited;
        for (;;) {
            Require(visited.size() < 8 && visited.insert(address).second, "cyclic or excessive jump thunks");
            const auto instruction = Decode(address);
            if (instruction.decoded.opcode != 0xe9 && instruction.decoded.opcode != 0xeb) return address;
            address = instruction.target;
        }
    }

    bool Imported(uint32_t thunk, const char* name) const {
        const auto branch = Decode(thunk);
        if (branch.decoded.opcode != 0xff || branch.decoded.modrm_reg != 4 || !branch.reference) return false;
        Require(Data(branch.reference, 8), "math import slot outside image data");
        const auto target = Read<uint64_t>(branch.reference);
        if (!target) return false;
        Bytes(imports_, importSize_);
        for (uint32_t i = 0; static_cast<uint64_t>(i) + 20 <= importSize_; i += 20) {
            const auto lookup = Read<uint32_t>(imports_ + i);
            const auto iat = Read<uint32_t>(imports_ + i + 16);
            if (!lookup && !iat) break;
            Require(lookup && iat, "missing import lookup table");
            for (uint32_t index = 0; index < 32768; index += 8) {
                Bytes(lookup, static_cast<size_t>(index) + 8);
                Bytes(iat, static_cast<size_t>(index) + 8);
                const auto entry = Read<uint64_t>(lookup + index);
                if (!entry) break;
                if (entry & (1ull << 63)) continue;
                Require(entry <= UINT32_MAX - 2, "import name exceeds RVA bounds");
                if (std::memcmp(Bytes(static_cast<uint32_t>(entry) + 2, std::strlen(name) + 1),
                                name, std::strlen(name) + 1) == 0 && Read<uint64_t>(iat + index) == target) return true;
            }
        }
        return false;
    }

    void SharedCameraArgument(uint32_t render, uint32_t zoom) const {
        std::set<uint32_t> callers;
        for (const auto& pair : functions_) {
            bool candidate = false;
            for (const auto& span : pair.second) {
                for (uint32_t at = span.begin; static_cast<uint64_t>(at) + 5 <= span.end; ++at) {
                    if (!Executable(at, 5) || Read<uint8_t>(at) != 0xe8) continue;
                    if (static_cast<int64_t>(at) + 5 + Read<int32_t>(at + 1) == zoom) candidate = true;
                }
            }
            if (!candidate) continue;
            const auto caller = DecodeFunction(pair.first);
            for (size_t i = 3; i < caller.instructions.size(); ++i) {
                const auto& call = caller.instructions[i];
                const auto& first = caller.instructions[i - 3];
                const auto& second = caller.instructions[i - 1];
                const auto& draw = caller.instructions[i - 2];
                if (!call.call || call.target != zoom || !draw.call || draw.target != render) continue;
                const auto base = first.Base();
                const bool preserved = base == 3 || base == 5 || base == 6 || base == 7 || base >= 12;
                Require(first.decoded.opcode == 0x8d && first.decoded.rex_w && first.Register() == 1 &&
                            preserved && !first.reference && !(first.decoded.flags & F_SIB) &&
                            first.decoded.modrm_mod != 3 && first.decoded.len == second.decoded.len &&
                            first.bytes == second.bytes && first.address + first.decoded.len == draw.address &&
                            draw.address + draw.decoded.len == second.address &&
                            second.address + second.decoded.len == call.address,
                        "FOV ratio and renderer do not receive the same camera object");
                callers.insert(call.address);
            }
        }
        Require(callers.size() == 1, "missing or ambiguous renderer/FOV object relationship");
    }

    cameraunlock::memory::PeFingerprint fingerprint{};

private:
    static void HashInstruction(uint64_t& hash, const Instruction& instruction) {
        HashByte(hash, instruction.decoded.len);
        for (unsigned i = 0; i < instruction.decoded.len; ++i) HashByte(hash, instruction.bytes[i]);
    }

    uint32_t Primary(uint32_t begin, uint32_t unwind) const {
        std::set<uint32_t> visited;
        for (;;) {
            Require(visited.size() < 32 && visited.insert(unwind).second, "cyclic or excessive chained unwind");
            Bytes(unwind, 4);
            const auto versionFlags = Read<uint8_t>(unwind);
            Require((versionFlags & 7) == 1 || (versionFlags & 7) == 2, "unsupported unwind version");
            const auto count = Read<uint8_t>(unwind + 2);
            Bytes(unwind + 4, ((count + 1u) & ~1u) * 2);
            if (!(versionFlags & 0x20)) return begin;
            const auto parent = unwind + 4 + ((count + 1u) & ~1u) * 2;
            Bytes(parent, 12);
            begin = Read<uint32_t>(parent);
            unwind = Read<uint32_t>(parent + 8);
            // Embedded parents identify the prologue. Their EndAddress can cross
            // sections or precede BeginAddress in the relocated engine; only the
            // actual exception-table entries supply instruction bounds.
            Require(Executable(begin), "chained unwind prologue is not executable");
        }
    }

    const Image& image_;
    std::vector<Range> executable_;
    std::vector<Range> data_;
    std::map<uint32_t, std::vector<Range>> functions_;
    uint32_t imports_ = 0;
    uint32_t importSize_ = 0;
};

// These describe decoded control flow and operand widths, not image locations.
// RIP references and direct branch destinations are resolved separately; member
// displacements remain in the contract so an unrecognised layout cannot pass.
constexpr Shape kCameraUpdate{0x38eb1c5b2f8682feull, 0x1feb8a5d228e700eull, 1200, {0x40, 0x55, 0x56}};
constexpr Shape kRotation{0xb8fafcd80f6e06c6ull, 0x0b549e95f4ba9cc0ull, 345, {0x48, 0x8b, 0xc4}};
constexpr Shape kAngles{0x4469a4a700910b18ull, 0x9137f482e3a7925full, 93, {0x48, 0x8b, 0xc4}};
constexpr Shape kRender{0xc0506b798514b123ull, 0x4e427232eb7851a4ull, 158, {0x48, 0x8b, 0xc4}};
constexpr Shape kRay{0xf7ce72abd185fc15ull, 0x73f7891aa03d0325ull, 31, {0x48, 0x89, 0x5c}};
constexpr Shape kFilterConstruct{0x8cb05684727715dfull, 0x6554b138681d13d7ull, 6, {0x48, 0x8d, 0x05}};
constexpr Shape kFilterDestroy{0x9d06cf6217fbbb05ull, 0x4cd5241a6e7e7569ull, 3, {0x48, 0x8d, 0x05}};
constexpr Shape kHitsDestroy{0xf3477fb44211302full, 0x314bf65ef9bfb0f7ull, 12, {0x48, 0x83, 0xec}};
constexpr Shape kOwner{0x7a875449f2b145a3ull, 0x86c24874c52f7021ull, 60, {0x48, 0x89, 0x5c}};
constexpr Shape kBody{0x0edf801abd67b957ull, 0x4433986f089bec58ull, 21, {0x40, 0x53, 0x48}};
constexpr Shape kCollider{0x77c6bec3b931d5b5ull, 0x1d734d32c65202adull, 2, {0x48, 0x8d, 0x41}};
constexpr Shape kReticle{0xe92ba51a8bab3a27ull, 0x8595312e34450f8bull, 436, {0x48, 0x8b, 0xc4}};
constexpr Shape kPublish{0x9fdfe7f1db4ab8f3ull, 0x393a8b6f29631b81ull, 83, {0x48, 0x89, 0x5c}};
constexpr Shape kSpawn{0x3d248b4972c6eb41ull, 0x3b819ce10bc8040aull, 90, {0x48, 0x89, 0x5c}};
constexpr Shape kSpawnAlternative{0x3d248b4972c6eb41ull, 0x83c9b4bd158d93b5ull, 89, {0x48, 0x89, 0x5c}};
constexpr Shape kLightDestroy{0x24f1268e09fd4d9full, 0xb320450492fa104full, 24, {0x48, 0x89, 0x5c}};
constexpr Shape kLightRelease{0x945121f8159c3822ull, 0xa1caaa9a3db54558ull, 99, {0x48, 0x89, 0x5c}};
constexpr Shape kMatrix{0xc202f96d85450d74ull, 0x8d855de6326a2b0dull, 32, {0x0f, 0x10, 0x41}};
constexpr Shape kZoom{0x347b6e063fc7f96dull, 0x6ebc404967fe380full, 17, {0x40, 0x53, 0x48}};

constexpr std::array<NativeFunction Offsets::*, 18> kFunctions{{
    &Offsets::camera_update, &Offsets::extended_view_rotation, &Offsets::set_camera_angles,
    &Offsets::render_camera, &Offsets::world_ray_query, &Offsets::query_filter_construct,
    &Offsets::query_filter_destroy, &Offsets::query_hits_destroy, &Offsets::camera_owner,
    &Offsets::owner_body, &Offsets::body_collider, &Offsets::sights_read,
    &Offsets::binoculars_call, &Offsets::reticle_position, &Offsets::reticle_publish_position,
    &Offsets::flashlight_spawn, &Offsets::flashlight_destroy, &Offsets::set_world_matrix,
}};

NativeFunction Native(const Reader& reader, uint32_t address) {
    Require(reader.Executable(address, 32), "native verification bytes are not executable");
    uint32_t hash = 2166136261u;
    for (unsigned i = 0; i < 32; ++i) hash = (hash ^ reader.Read<uint8_t>(address + i)) * 16777619u;
    return {address, hash};
}

template<class Predicate>
const Instruction& UniqueInstruction(const Function& function, Predicate matches) {
    const Instruction* result = nullptr;
    for (const auto& instruction : function.instructions) {
        if (!matches(instruction)) continue;
        Require(!result, "ambiguous instruction relationship");
        result = &instruction;
    }
    Require(result != nullptr, "missing instruction relationship");
    return *result;
}

bool Memory(const Instruction& i, uint8_t opcode, int reg, int base, int32_t displacement) {
    return i.decoded.opcode == opcode && i.decoded.modrm_mod != 3 &&
           !(i.decoded.flags & F_SIB) && !i.reference && i.Register() == reg &&
           i.Base() == base && i.Displacement() == displacement;
}

uint32_t GlobalForCall(const Function& function, uint32_t target, int reg) {
    uint32_t result = 0;
    uint32_t candidate = 0;
    for (const auto& i : function.instructions) {
        if (i.reference && i.decoded.opcode == 0x8b && i.decoded.rex_w && i.Register() == reg) {
            candidate = i.reference;
        }
        if (i.call) {
            if (i.target == target) {
                Require(candidate && (!result || result == candidate), "native call has no unique global argument");
                result = candidate;
            }
            candidate = 0;
        }
    }
    Require(result != 0, "native global call relationship is absent");
    return result;
}

uint32_t Vtable(const Reader& reader, const Function& release, int32_t member) {
    uint32_t result = 0;
    for (size_t i = 0; i < release.instructions.size(); ++i) {
        const auto& load = release.instructions[i];
        if (!load.reference || load.decoded.opcode != 0x8d || load.Register() != 0) continue;
        for (size_t j = i + 1; j < std::min(i + 4, release.instructions.size()); ++j) {
            const auto& store = release.instructions[j];
            if (!Memory(store, 0x89, 0, 1, member) || !store.decoded.rex_w) continue;
            Require(!result && reader.Data(load.reference, 8) && load.reference % 8 == 0,
                    "invalid or ambiguous component vtable");
            result = load.reference;
        }
    }
    Require(result != 0, "component destructor does not bind its vtable");
    return result;
}

Offsets Discover(const Reader& reader, std::string& stage) {
    Offsets result{};
    stage = "camera update";
    result.camera_update = Native(reader, reader.Find(kCameraUpdate, false));
    const auto update = reader.DecodeFunction(static_cast<uint32_t>(result.camera_update.rva));
    struct Dependency { const char* name; NativeFunction Offsets::*field; const Shape* shape; };
    for (const auto& dependency : {
             Dependency{"extended-view rotation", &Offsets::extended_view_rotation, &kRotation},
             Dependency{"camera angle setter", &Offsets::set_camera_angles, &kAngles},
             Dependency{"world ray query", &Offsets::world_ray_query, &kRay},
             Dependency{"query filter constructor", &Offsets::query_filter_construct, &kFilterConstruct},
             Dependency{"query filter destructor", &Offsets::query_filter_destroy, &kFilterDestroy},
             Dependency{"query hit destructor", &Offsets::query_hits_destroy, &kHitsDestroy},
             Dependency{"camera owner", &Offsets::camera_owner, &kOwner},
             Dependency{"owner body", &Offsets::owner_body, &kBody},
             Dependency{"body collider", &Offsets::body_collider, &kCollider}}) {
        stage = dependency.name;
        result.*(dependency.field) = Native(reader, reader.Callee(update, *dependency.shape));
    }
    stage = "camera globals and aiming ABI";
    result.extended_view_instance = UniqueInstruction(update, [](const Instruction& i) {
        return i.reference && i.decoded.opcode == 0x8b && i.decoded.rex_w && i.Register() == 3;
    }).reference;
    result.world = GlobalForCall(update, static_cast<uint32_t>(result.world_ray_query.rva), 1);
    for (const auto global : {result.extended_view_instance, result.world}) {
        Require(global % 8 == 0 && reader.Data(static_cast<uint32_t>(global), 8), "unaligned or unreadable camera global");
    }
    result.sights_read = Native(reader, UniqueInstruction(update, [](const Instruction& i) {
        return Memory(i, 0x0f, 0, 14, 0x2afa) && i.decoded.opcode2 == 0xb6 && !i.decoded.rex_w;
    }).address);
    const auto& phone = UniqueInstruction(update, [&update](const Instruction& i) {
        if (!i.call || !Memory(i, 0xff, 2, 0, 0x230)) return false;
        const auto index = &i - update.instructions.data();
        if (static_cast<size_t>(index) + 1 >= update.instructions.size()) return false;
        const auto& save = update.instructions[static_cast<size_t>(index) + 1];
        return index >= 2 && Memory(update.instructions[index - 2], 0x8b, 0, 14, 0) &&
               save.decoded.opcode == 0x88 && save.Register() == 0 &&
               save.decoded.sib_base == 4 && save.Displacement() == 0x42;
    });
    const auto phoneIt = std::lower_bound(update.instructions.begin(), update.instructions.end(), phone.address,
                                        [](const Instruction& i, uint32_t address) { return i.address < address; });
    Require(phoneIt - update.instructions.begin() >= 2, "missing phone dispatch receiver");
    const auto& vtableLoad = *(phoneIt - 2);
    Require(Memory(vtableLoad, 0x8b, 0, 14, 0) && vtableLoad.decoded.rex_w,
            "phone dispatch is not owned by the camera owner");
    result.binoculars_call = Native(reader, vtableLoad.address);
    const auto filterConstruct = reader.DecodeFunction(static_cast<uint32_t>(result.query_filter_construct.rva));
    const auto filterDestroy = reader.DecodeFunction(static_cast<uint32_t>(result.query_filter_destroy.rva));
    Require(filterConstruct.instructions.front().reference == filterDestroy.instructions.front().reference &&
                reader.Data(filterConstruct.instructions.front().reference, 8),
            "query filter constructor/destructor type disagreement");

    stage = "render camera and reticle";
    result.render_camera = Native(reader, reader.Find(kRender, false));
    stage = "camera FOV layout";
    const auto zoom = reader.DecodeFunction(reader.Find(kZoom, false));
    uint32_t sine = 0;
    unsigned calls = 0;
    unsigned halves = 0;
    for (const auto& i : zoom.instructions) {
        if (i.call) {
            Require(i.relative && (!sine || sine == i.target), "inconsistent FOV math helper");
            sine = i.target;
            ++calls;
        }
        if (i.reference) {
            Require(reader.Data(i.reference, 4) && reader.Read<uint32_t>(i.reference) == 0x3f000000,
                    "FOV calculation does not use half angles");
            ++halves;
        }
    }
    Require(calls == 2 && halves == 2 && reader.Imported(sine, "sinf"), "FOV calculation is not a half-angle sine ratio");
    reader.SharedCameraArgument(static_cast<uint32_t>(result.render_camera.rva), zoom.address);
    stage = "reticle";
    result.reticle_position = Native(reader, reader.Find(kReticle, false));
    const auto reticle = reader.DecodeFunction(static_cast<uint32_t>(result.reticle_position.rva));
    result.reticle_publish_position = Native(reader, reader.Callee(reticle, kPublish));
    stage = "entity matrix setter";
    result.set_world_matrix = Native(reader, reader.Find(kMatrix, true));

    stage = "flashlight component and virtual dispatch";
    const auto releaseAddress = reader.Find(kLightRelease, false);
    const auto release = reader.DecodeFunction(releaseAddress);
    result.flashlight_vtable = Vtable(reader, release, 0);
    const auto endVtable = Vtable(reader, release, 0x18);
    Require(endVtable > result.flashlight_vtable && endVtable - result.flashlight_vtable <= 1024,
            "component vtable extent is inconsistent");
    const auto destroyThunk = reader.Pointer(static_cast<uint32_t>(result.flashlight_vtable));
    const auto destroyBody = reader.Follow(destroyThunk);
    Require(reader.Matches(destroyBody, kLightDestroy), "component deleting-destructor ABI differs");
    Require(reader.Callee(reader.DecodeFunction(destroyBody), kLightRelease) == releaseAddress,
            "component deleting destructor has a different owner");
    result.flashlight_destroy = Native(reader, destroyThunk);
    uint32_t spawn = 0;
    for (uint32_t at = static_cast<uint32_t>(result.flashlight_vtable); at < endVtable; at += 8) {
        const auto target = reader.Pointer(at);
        Require(reader.Executable(target), "non-executable component vtable entry");
        const auto body = reader.Follow(target);
        if (!reader.Matches(body, kSpawn) && !reader.Matches(body, kSpawnAlternative)) continue;
        Require(!spawn, "ambiguous flashlight spawn slot");
        spawn = target;
    }
    Require(spawn != 0, "missing flashlight spawn slot");
    result.flashlight_spawn = Native(reader, spawn);
    return result;
}

}  // namespace

FunctionShape InspectFunction(const Image& image, uint32_t address) {
    Reader reader(image);
    const auto function = reader.DecodeFunction(address);
    return {reader.Prefix(address, static_cast<uint32_t>(function.instructions.size())), function.hash,
            static_cast<uint32_t>(function.instructions.size())};
}

uint32_t FindFunction(const Image& image, const Shape& shape, bool leaf) {
    return Reader(image).Find(shape, leaf);
}

bool Resolve(const Image& image, Offsets& result, std::string& diagnostic) {
    result = {};
    std::string stage = "PE and unwind tables";
    try {
        const Reader reader(image);
        auto resolved = Discover(reader, stage);
        stage = "historical profile cross-check";
        const BuildProfile* known = nullptr;
        for (int i = 0; i < kKnownProfileCount; ++i) {
            const auto* profile = kKnownProfiles[i];
            if (!reader.fingerprint.Matches(profile->fingerprint)) continue;
            known = profile;
            for (const auto member : kFunctions) {
                const auto& expected = profile->offsets->*member;
                const auto& actual = resolved.*member;
                Require(actual.rva == expected.rva && actual.prefix_hash == expected.prefix_hash,
                        "discovered native target disagrees with exact profile");
            }
            Require(resolved.extended_view_instance == profile->offsets->extended_view_instance &&
                        resolved.world == profile->offsets->world,
                    "discovered global disagrees with exact profile");
        }
        result = resolved;
        diagnostic = known ? std::string("validated discovery; cross-checked ") + known->name :
                             "validated discovery; unlisted fingerprint";
        return true;
    } catch (const std::runtime_error& error) {
        diagnostic = stage + ": " + error.what();
        return false;
    }
}

}  // namespace FarCry6HeadTracking::discovery
