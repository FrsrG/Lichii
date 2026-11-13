// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"

namespace {
using Shader::Ampere::Instruction;
using Shader::Ampere::LowerToMaxwell;
using Shader::Ampere::NativeRegisterCopy;
using Shader::Ampere::PrepareRegisterPressure;
using Shader::Ampere::TextureRegisterSpans;

constexpr Instruction Exit{{0x794d, 0, 0x03800000, 0}};
constexpr std::array<Instruction, 3> CapturedTextures{{
    {{0x1c1c7f60, 0x500004eb, 0x089e07eb, 0x000f4200}},
    {{0x26267f60, 0x500004f3, 0x089e07f3, 0x000f4200}},
    {{0x16167f60, 0x500004ea, 0x089e07ea, 0x000f4200}},
}};

void Set(Instruction& instruction, u32 bit, u32 count, u32 value) {
    const u32 mask = ((1U << count) - 1) << (bit % 32);
    instruction.words[bit / 32] =
        (instruction.words[bit / 32] & ~mask) | ((value << (bit % 32)) & mask);
}

using Banks = std::array<std::array<u32, 256>, 3>;

void Copy(Banks& banks, u64 instruction) {
    banks[(instruction >> 8) & 3][instruction & 255] =
        banks[(instruction >> 10) & 3][(instruction >> 20) & 255];
}
} // namespace

TEST_CASE("Captured Kirby TEX shadows preserve coordinates results and uniform mirrors",
          "[ampere][texture_pressure]") {
    for (const Instruction& input : CapturedTextures) {
        INFO(input.words[0]);
        const auto pressure = PrepareRegisterPressure(input, 216);
        REQUIRE(pressure.valid);
        Banks banks{};
        for (u32 bank = 0; bank < banks.size(); ++bank)
            for (u32 reg = 0; reg < banks[bank].size(); ++reg)
                banks[bank][reg] = (bank << 24) | reg;
        const Banks before = banks;
        for (const u64 instruction : pressure.prefix)
            Copy(banks, instruction);
        const auto& tex = pressure.instruction;
        const u32 source_a = tex.Bits(24, 8), source_b = tex.Bits(32, 8);
        CHECK(banks[0][source_a] == before[0][input.Bits(24, 8)]);
        CHECK(banks[0][source_a + 1] == before[0][input.Bits(24, 8) + 1]);
        CHECK(banks[0][source_b] == before[1][input.Bits(32, 8)]);
        CHECK(tex.Bits(40, 6) == 4); // UR4 remains a uniform handle, not a remapped GPR.
        CHECK(source_b == tex.Bits(64, 8));
        const u32 destination = tex.Bits(16, 8);
        banks[0][destination] = 0x12345678;
        banks[0][destination + 1] = 0x87654321;
        banks[0][tex.Bits(64, 8)] = 0xabcdef01;
        for (const u64 instruction : pressure.suffix)
            Copy(banks, instruction);
        Banks expected = before;
        expected[0][input.Bits(16, 8)] = 0x12345678;
        expected[0][input.Bits(16, 8) + 1] = 0x87654321;
        expected[1][input.Bits(64, 8)] = 0xabcdef01;
        CHECK(banks[0] == expected[0]);
        CHECK(banks[1] == expected[1]);
        const auto lowered = LowerToMaxwell(std::array{input, Exit});
        INFO(lowered.error);
        CHECK(lowered);
    }
}

TEST_CASE("TEX shadow spans follow packing masks and half results", "[ampere][texture_pressure]") {
    struct Case {
        u32 opcode, dimension, lod, offsets, compare, half, second, packed, mask;
        std::array<u32, 4> counts;
    };
    constexpr std::array cases{
        Case{0xf60, 2, 1, 0, 0, 0, 235, 1, 7, {2, 2, 1, 1}},
        Case{0xb60, 5, 0, 0, 1, 0, 255, 1, 1, {1, 2, 2, 0}},
        Case{0xf60, 1, 3, 1, 0, 0, 255, 1, 15, {4, 2, 2, 0}},
        Case{0x361, 1, 3, 0, 0, 1, 235, 1, 7, {1, 2, 2, 1}},
        Case{0x361, 3, 3, 0, 0, 0, 255, 0, 15, {4, 3, 2, 0}},
        Case{0xf60, 2, 1, 0, 0, 1, 255, 1, 7, {2, 2, 1, 0}},
        Case{0xf60, 2, 1, 0, 0, 0, 235, 1, 3, {2, 2, 1, 0}},
    };
    for (const auto& test : cases) {
        Instruction input = CapturedTextures[0];
        Set(input, 0, 12, test.opcode);
        Set(input, 61, 3, test.dimension);
        Set(input, 87, 3, test.lod);
        Set(input, 76, 1, test.offsets);
        Set(input, 78, 1, test.compare);
        Set(input, 79, 1, test.half);
        Set(input, 64, 8, test.second);
        Set(input, 60, 1, test.packed);
        Set(input, 72, 4, test.mask);
        const auto spans = TextureRegisterSpans(input);
        for (size_t i = 0; i < spans.size(); ++i)
            CHECK(spans[i].count == test.counts[i]);
    }
}

TEST_CASE("TEX shadows protect complete spans and ignore unused destinations",
          "[ampere][texture_pressure]") {
    Instruction input = CapturedTextures[0];
    Set(input, 24, 8, 214); // The low coordinate pair occupies the otherwise borrowable quartet.
    auto pressure = PrepareRegisterPressure(input, 216);
    REQUIRE(pressure.valid);
    CHECK(pressure.instruction.Bits(32, 8) < 212);
    Set(input, 24, 8, 215); // A pair straddling the bank boundary cannot use one quartet.
    CHECK_FALSE(PrepareRegisterPressure(input, 216).valid);
    Set(input, 24, 8, 28);
    Set(input, 64, 8, 254);
    CHECK(PrepareRegisterPressure(input, 216).valid); // RGB writes only one secondary register.
    Set(input, 72, 4, 15);
    CHECK_FALSE(PrepareRegisterPressure(input, 216).valid); // RGBA would also write R255.
    Set(input, 72, 4, 3);
    Set(input, 32, 8, 40);
    Set(input, 64, 8, 235); // RG never writes the secondary destination.
    pressure = PrepareRegisterPressure(input, 216);
    REQUIRE(pressure.valid);
    CHECK(pressure.prefix.empty());
    CHECK(pressure.suffix.empty());
    Set(input, 72, 4, 0);
    CHECK_FALSE(PrepareRegisterPressure(input, 216).valid);
}

TEST_CASE("TEX lowering shadows a high source with no secondary destination",
          "[ampere][texture_pressure]") {
    Instruction input = CapturedTextures[0];
    Set(input, 64, 8, 255);
    const auto lowered = LowerToMaxwell(std::array{input, Exit});
    INFO(lowered.error);
    REQUIRE(lowered);
    bool reads_guest_shadow = false;
    for (u32 low = 0; low < 216; ++low)
        reads_guest_shadow |=
            std::ranges::find(lowered.instructions, NativeRegisterCopy(0, low, 1, 235)) !=
            lowered.instructions.end();
    CHECK(reads_guest_shadow);
}
