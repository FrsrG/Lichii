// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>

#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"

namespace {
using Shader::Ampere::Instruction;
using Shader::Ampere::LowerToMaxwell;
constexpr Instruction EXIT{{0x0000794d, 0, 0x03800000, 0}};

bool Contains(const Shader::Ampere::LoweredProgram& lowered, u64 instruction) {
    return std::find(lowered.instructions.begin(), lowered.instructions.end(), instruction) !=
           lowered.instructions.end();
}

constexpr std::array<Instruction, 16> GEOMETRY_PROGRAM{{
    {{0x0000734e, 0, 0, 0}},
    {{0x00037802, 0xd0, 0xf00, 0}},
    {{0x00007210, 3, 0x07f1e0ff, 0}},
    {{0xff017224, 0xff, 0x000e0401, 0}},
    {{0x00007357, 0, 0, 0}},
    {{0x00007802, 0, 0xf00, 0}},
    {{0x00017802, 0, 0xf00, 0}},
    {{0x00037802, 0, 0xf00, 0}},
    {{0x0100720c, 0xff, 0x03f03070, 0}},
    {{0x00017923, 0, 0, 0}},          // ISBERD R1, [R0]
    {{0x00027324, 0xff, 0x4000, 0}},  // OUT.EMIT R2, R0, RZ
    {{0x02037924, 0, 0x8000, 0}},     // OUT.CUT R3, R2, 0
    EXIT,
    {{0x00ff7324, 0xff, 0, 0}},
    {{0x00007957, 0, 0, 0}},
    EXIT,
}};
} // namespace

TEST_CASE("Ampere geometry program counter uses the native address", "[video_core][ampere]") {
    const Instruction nop{{0x00007918, 0, 0, 0}};
    const Instruction lepc{{0x0014734e, 0, 0, 0}};
    const std::array code{nop, lepc, EXIT};
    REQUIRE_FALSE(static_cast<bool>(LowerToMaxwell(code)));
    REQUIRE_FALSE(LowerToMaxwell(code, true));
    const auto result = LowerToMaxwell(code, {.geometry = true,
                                            .native_code_address = 0x00000012fffffff0ULL});
    REQUIRE(static_cast<bool>(result));
    REQUIRE(result.instructions.size() == 4);
    REQUIRE((result.instructions[1] & 0xff) == 20);
    REQUIRE(((result.instructions[1] >> 20) & 0xffffffffULL) == 0);
    REQUIRE((result.instructions[2] & 0xff) == 21);
    REQUIRE(((result.instructions[2] >> 20) & 0xffffffffULL) == 0x13);

    Instruction unsupported = lepc;
    unsupported.words[1] = 1;
    REQUIRE_FALSE(LowerToMaxwell(std::array{nop, unsupported, EXIT},
                                 {.geometry = true, .native_code_address = 0x00000012fffffff0ULL}));
    unsupported = lepc;
    unsupported.words[0] = (unsupported.words[0] & ~0xff0000U) | (245U << 16);
    REQUIRE_FALSE(LowerToMaxwell(std::array{nop, unsupported, EXIT},
                                 {.geometry = true, .native_code_address = 0x00000012fffffff0ULL}));
}

TEST_CASE("Ampere geometry stage map and exit setup are stage specific", "[video_core][ampere]") {
    const Instruction map_read{{0x05127923, 0, 0, 0}};
    const Instruction exit_setup{{0x03007357, 0, 0, 0}};
    auto code = GEOMETRY_PROGRAM;
    code[9] = map_read;
    REQUIRE_FALSE(static_cast<bool>(LowerToMaxwell(code)));
    const auto result = LowerToMaxwell(code, true);
    REQUIRE(static_cast<bool>(result));
    const u64 map = result.instructions[result.source_instruction_offsets[9]];
    REQUIRE((map & 0xff) == 18);
    REQUIRE(((map >> 20) & 0xff) == 5);
    REQUIRE_FALSE(LowerToMaxwell(std::array{map_read, exit_setup, EXIT}, true));

    Instruction unsupported = exit_setup;
    unsupported.words[1] = 1;
    REQUIRE_FALSE(LowerToMaxwell(std::array{unsupported, EXIT}, true));
}

TEST_CASE("Ampere F2I supports 16-bit truncation", "[video_core][ampere]") {
    for (const bool is_signed : {false, true}) {
        const Instruction convert{{0x00067305, 9, 0x0021e800U | (is_signed ? 0x100U : 0), 0}};
        const auto result = LowerToMaxwell(std::array{convert, EXIT});
        REQUIRE(static_cast<bool>(result));
        const auto word = std::ranges::find_if(result.instructions, [](u64 value) {
            return (value & 0xfff0000000000000ULL) == 0x5cb0000000000000ULL;
        });
        REQUIRE(word != result.instructions.end());
        REQUIRE(((*word >> 8) & 3) == 1);
        REQUIRE(((*word >> 12) & 1) == static_cast<u64>(is_signed));
        REQUIRE(((*word >> 39) & 3) == 3);
        REQUIRE(((*word >> 44) & 1) == 1);
    }
    const Instruction unsupported{{0x00067305, 9, 0x0021e801, 0}};
    REQUIRE_FALSE(static_cast<bool>(LowerToMaxwell(std::array{unsupported, EXIT})));
}

TEST_CASE("Ampere geometry programs lower their output instructions", "[video_core][ampere]") {
    const auto lowered = LowerToMaxwell(GEOMETRY_PROGRAM, true);
    INFO(lowered.error << " at " << lowered.error_offset);
    REQUIRE(static_cast<bool>(lowered));
    REQUIRE(lowered.source_size == sizeof(GEOMETRY_PROGRAM));
    constexpr std::array<u32, 16> expected_offsets{
        0, 0, 1, 1, 1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10,
    };
    const auto& offsets = lowered.source_instruction_offsets;
    REQUIRE(offsets.size() == expected_offsets.size());
    for (size_t index = 0; index < offsets.size(); ++index) {
        CAPTURE(index);
        REQUIRE(offsets[index] == expected_offsets[index]);
    }
    // The retained setup MOV keeps the original native handler value, not its lowered address.
    const u64 handler_value = lowered.instructions[offsets[1]];
    REQUIRE((handler_value & 0xfff0000000000000ULL) == 0x0100000000000000ULL);
    REQUIRE((handler_value & 0xff) == 3);
    REQUIRE(((handler_value >> 16) & 0xf) == 7);
    REQUIRE(((handler_value >> 20) & 0xffffffffULL) == 0xd0);

    const u64 branch = lowered.instructions[offsets[12]];
    REQUIRE((branch & ~(0xffffffULL << 20)) == 0xe24000000007000fULL);
    const u32 encoded_offset = static_cast<u32>((branch >> 20) & 0xffffff);
    const s64 displacement = encoded_offset & 0x800000 ? s64{encoded_offset} - 0x1000000
                                                     : s64{encoded_offset};
    const auto address_of = [](u32 index) { return 0x50U + index * 8 + ((index + 1) / 3) * 8; };
    REQUIRE(s64{address_of(offsets[12])} + 8 + displacement == address_of(offsets[13]));
    REQUIRE(lowered.instructions[offsets[13]] == 0xfbe000000ff700ffULL);
    REQUIRE(Contains(lowered, 0x5c98078000070001ULL)); // MOV R1, R0
    REQUIRE(Contains(lowered, 0xfbe000800ff70002ULL)); // OUT.EMIT R2, R0, RZ
    REQUIRE(Contains(lowered, 0xf6e0010000070203ULL)); // OUT.CUT R3, R2, 0
    REQUIRE(Contains(lowered, 0xfbe000000ff700ffULL));
    REQUIRE(lowered.instructions.back() == 0xe30000000007000fULL);
}

TEST_CASE("Ampere output instructions stay rejected outside geometry programs",
          "[video_core][ampere]") {
    const auto lowered = LowerToMaxwell(GEOMETRY_PROGRAM);
    REQUIRE(!static_cast<bool>(lowered));
    REQUIRE(lowered.error == "native program counter load outside geometry stage");
    for (const auto& output : {GEOMETRY_PROGRAM[10], GEOMETRY_PROGRAM[11]}) {
        const auto rejected = LowerToMaxwell(std::array{output, EXIT});
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error == "native geometry output outside geometry stage");
    }
}

TEST_CASE("Ampere geometry rejects an unvalidated program counter load", "[video_core][ampere]") {
    const Instruction lepc{{0x0004734e, 0, 0, 0}};
    const auto truncated = LowerToMaxwell(std::array{lepc, EXIT}, true);
    REQUIRE_FALSE(truncated);
    REQUIRE(truncated.error == "native exit envelope truncated");
    auto invalid = GEOMETRY_PROGRAM;
    invalid[0] = lepc;
    const auto rejected = LowerToMaxwell(invalid, true);
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error == "native exit handler installation");
}
