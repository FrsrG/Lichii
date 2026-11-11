// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <span>
#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"

namespace {
using Shader::Ampere::Instruction;
using Shader::Ampere::LowerToMaxwell;
constexpr Instruction Exit{{0x0000794d, 0, 0x03800000, 0}};
constexpr std::array<Instruction, 2> Logged{{
    {{0x26267f60, 0x500004f3, 0x089e07f3, 0x000f4200}},
    {{0x1c1c7f60, 0x500004eb, 0x089e07eb, 0x000f4200}},
}};

// A texture callback records its actual coordinate/handle inputs before returning distinct
// channel values. MOV bank semantics follow the production Maxwell frontend.
struct TextureMachine {
    std::array<std::array<u32, 256>, 3> registers{};
    bool predicate{};
    u32 samples{};
    std::array<u32, 3> coordinates{};
    u32 handle{};
    u32 Read(u32 reg, u32 bank = 0) const { return reg == 255 ? 0 : registers.at(bank).at(reg); }
    u32& Native(u32 reg) { return registers.at(reg >= 216 ? 1 : 0).at(reg); }
    void Run(std::span<const u64> code) {
        for (const u64 word : code) {
            const auto bits = [word](u32 bit, u32 count) {
                return static_cast<u32>((word >> bit) & ((u64{1} << count) - 1));
            };
            const u32 guard = bits(16, 4);
            REQUIRE(((guard & 7) == 7 || (guard & 7) == 2));
            const bool enabled = (guard & 7) == 7 || predicate;
            if (enabled == bool(guard & 8)) continue;
            if (word == 0xe30000000007000fULL) return;
            const u32 dest = bits(0, 8), source = bits(20, 8);
            if ((word >> 48) == 0x5c98) {
                if (bits(44, 4) == 10 && bits(39, 4) == 0) {
                    registers.at(bits(8, 2)).at(dest) = Read(source, bits(10, 2));
                } else if (dest != 255) {
                    REQUIRE(bits(39, 4) == 15);
                    registers[0][dest] = Read(source);
                }
            } else if ((word >> 56) == 0xde) {
                REQUIRE(bits(28, 3) == 4); // 3D
                REQUIRE(bits(31, 4) == 7); // RGB
                REQUIRE(bits(37, 3) == 1); // LZ
                ++samples;
                const u32 a = bits(8, 8);
                coordinates = {Read(a), Read(a + 1), Read(a + 2)};
                handle = Read(source);
                for (u32 index = 0; index < 3; ++index)
                    registers[0][dest + index] = 0x10000000U + index;
            } else {
                FAIL("Unexpected instruction in uniform TEX lowering");
            }
        }
        FAIL("Texture lowering did not reach EXIT");
    }
};

void CheckTexture(const Instruction& instruction, bool predicate) {
    const auto lowered = LowerToMaxwell(std::array{instruction, Exit});
    INFO(lowered.error << " at " << lowered.error_offset);
    REQUIRE(lowered);
    TextureMachine machine;
    for (u32 reg = 0; reg < 256; ++reg) {
        machine.registers[0][reg] = 0xa0000000U + reg;
        machine.registers[1][reg] = 0xb0000000U + reg;
    }
    const u32 a = instruction.Bits(24, 8), b = instruction.Bits(32, 8);
    const u32 dest = instruction.Bits(16, 8), second = instruction.Bits(64, 8);
    machine.Native(a) = 0x3f000000;
    machine.Native(a + 1) = 0x3f800000;
    machine.Native(b) = 0x40000000;
    machine.registers[0][220] = 0x12345678; // UR4, distinct from ordinary R220.
    const auto before = machine.registers;
    machine.predicate = predicate;
    const bool execute = (instruction.Bits(12, 3) == 7 || predicate) !=
                         bool(instruction.Bits(15, 1));
    machine.Run(lowered.instructions);
    CHECK(machine.samples == (execute ? 1 : 0));
    if (execute) {
        CHECK(machine.coordinates == std::array<u32, 3>{0x3f000000, 0x3f800000, 0x40000000});
        CHECK(machine.handle == 0x12345678);
    }
    for (u32 reg = 0; reg < 255; ++reg) {
        u32 expected = before[reg >= 216 ? 1 : 0][reg];
        if (execute) {
            if (reg == dest) expected = 0x10000000;
            if (reg == dest + 1) expected = 0x10000001;
            if (reg == second) expected = 0x10000002;
        }
        CHECK(machine.Native(reg) == expected);
    }
    for (u32 reg = 216; reg < 246; ++reg)
        CHECK(machine.registers[0][reg] == before[0][reg]); // Every uniform mirror survives.
}

TEST_CASE("Kirby logged uniform TEX preserves aliased high Z coordinates and split RGB results",
          "[video_core][ampere][kirby][tex]") {
    for (const auto& logged : Logged) {
        CheckTexture(logged, false);
        for (const u32 guard : {2U, 10U}) {
            auto instruction = logged;
            instruction.words[0] = (instruction.words[0] & ~0xf000U) | (guard << 12);
            for (const bool predicate : {false, true}) CheckTexture(instruction, predicate);
        }
    }
}

TEST_CASE("Uniform TEX pressure accounts for source and destination span boundaries",
          "[video_core][ampere][kirby][tex]") {
    for (const u32 field : {16U, 24U}) {
        for (const u32 reg : {214U, 216U, 220U}) {
            auto instruction = Logged[0];
            instruction.words[0] = (instruction.words[0] & ~(255U << field)) | (reg << field);
            CheckTexture(instruction, false);
        }
        for (const u32 reg : {215U, 219U, 254U}) {
            auto instruction = Logged[0];
            instruction.words[0] = (instruction.words[0] & ~(255U << field)) | (reg << field);
            CHECK_FALSE(LowerToMaxwell(std::array{instruction, Exit}));
        }
    }
    // A low second destination must not hide a high, read-only Z coordinate in operand b.
    auto input_only = Logged[0];
    input_only.words[2] = (input_only.words[2] & ~255U) | 60;
    CheckTexture(input_only, false);
    // Ra=215 spans R215/R216 and needs pressure handling even with every starting field low.
    auto crossing = input_only;
    crossing.words[0] = (215U << 24) | (38U << 16) | 0x7f60;
    crossing.words[1] = (crossing.words[1] & ~255U) | 60;
    CHECK_FALSE(LowerToMaxwell(std::array{crossing, Exit}));
}
} // namespace
