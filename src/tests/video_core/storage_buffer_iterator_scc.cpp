// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <optional>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "shader_recompiler/environment.h"
#include "shader_recompiler/frontend/ir/ir_emitter.h"
#include "shader_recompiler/host_translate_info.h"
#include "shader_recompiler/ir_opt/passes.h"

namespace {
namespace IR = Shader::IR;

class TestEnvironment final : public Shader::Environment {
public:
    bool native{true};
    TestEnvironment() {
        stage = Shader::Stage::Compute;
    }
    bool IsNativeProgram() const override {
        return native;
    }
    bool IsCbufStorageBufferPointer(u32, u32) override {
        return true;
    }
    u64 ReadInstruction(u32) override {
        return 0;
    }
    u32 ReadCbufValue(u32, u32) override {
        return 0;
    }
    Shader::TextureType ReadTextureType(u32) override {
        return Shader::TextureType::Color2D;
    }
    Shader::TexturePixelFormat ReadTexturePixelFormat(u32) override {
        return Shader::TexturePixelFormat::A8B8G8R8_UNORM;
    }
    bool IsTexturePixelFormatInteger(u32) override {
        return false;
    }
    u32 ReadViewportTransformState() override {
        return 1;
    }
    u32 TextureBoundBuffer() const override {
        return 0;
    }
    u32 LocalMemorySize() const override {
        return 0;
    }
    u32 SharedMemorySize() const override {
        return 0;
    }
    std::array<u32, 3> WorkgroupSize() const override {
        return {32, 1, 1};
    }
    bool HasHLEMacroState() const override {
        return false;
    }
    std::optional<Shader::ReplaceConstant> GetReplaceConstBuffer(u32, u32) override {
        return std::nullopt;
    }
    void Dump(u64, u64) override {}
};

struct IteratorProgram {
    Shader::ObjectPool<IR::Inst> pool{256};
    IR::Block entry{pool};
    IR::Block loop{pool};
    IR::Program program;
    TestEnvironment env;

    IteratorProgram() {
        program.stage = Shader::Stage::Compute;
        entry.AddBranch(&loop);
        loop.AddBranch(&loop);
        program.blocks = {&entry, &loop};
        program.post_order_blocks = {&loop, &entry};
    }

    IR::Inst* Phi() {
        loop.AppendNewInst(IR::Opcode::Phi, {});
        auto* phi = &loop.back();
        phi->SetFlags(IR::Type::U32);
        phi->AddPhiOperand(&entry, IR::Value{u32{0}});
        return phi;
    }

    static IR::U32 Word(IR::IREmitter& ir, u32 offset) {
        return ir.GetCbuf(ir.Imm32(0), ir.Imm32(offset));
    }

    // Both words look like pointer pairs. Only a word proved to step an iterator may be
    // excluded; otherwise the legacy nearest-word selection keeps the closer one.
    void Load(u32 near_word, u32 base_word) {
        IR::IREmitter ir{loop};
        const auto low =
            ir.IAdd(Word(ir, near_word), ir.IAdd(Word(ir, base_word), ir.LocalInvocationIdX()));
        const auto high = ir.IAdd(Word(ir, near_word + 4), Word(ir, base_word + 4));
        static_cast<void>(ir.LoadGlobal32(ir.PackUint2x32(ir.CompositeConstruct(low, high))));
    }

    std::vector<u32> Offsets() {
        Shader::HostTranslateInfo host{};
        host.min_ssbo_alignment = 16;
        Shader::Optimization::GlobalMemoryToStorageBufferPass(env, program, host);
        std::vector<u32> result;
        for (const auto& descriptor : program.info.storage_buffers_descriptors) {
            result.push_back(descriptor.cbuf_offset);
        }
        return result;
    }
};
} // Anonymous namespace

TEST_CASE("Iterator SCC traversal handles deep cyclic and acyclic shader graphs",
          "[shader][storage_buffer][iterator_scc]") {
    const u32 depth = GENERATE(1U, 32768U);
    const bool cyclic = GENERATE(false, true);
    const bool native = GENERATE(false, true);
    CAPTURE(depth, cyclic, native);
    IteratorProgram fixture;
    fixture.env.native = native;
    IR::Inst* const phi = fixture.Phi();
    IR::IREmitter ir{fixture.loop};
    const auto condition = ir.ILessThan(ir.LocalInvocationIdX(), ir.Imm32(16), false);
    IR::U32 value = ir.IAdd(cyclic ? IR::U32{IR::Value{phi}} : ir.LocalInvocationIdX(),
                            fixture.Word(ir, 0x200));
    for (u32 index = 0; index < depth; ++index) {
        value = IR::U32{ir.Select(condition, value, ir.Imm32(0))};
    }
    phi->AddPhiOperand(&fixture.loop, value);
    fixture.Load(0x200, 0x160);
    const std::vector<u32> expected = cyclic   ? std::vector<u32>{0x160}
                                      : native ? std::vector<u32>{0x160, 0x200}
                                               : std::vector<u32>{0x200};
    CHECK(fixture.Offsets() == expected);
}

TEST_CASE("Iterator SCC boundaries keep acyclic addends as pointer candidates",
          "[shader][storage_buffer][iterator_scc]") {
    const bool reverse_roots = GENERATE(false, true);
    IteratorProgram fixture;
    IR::Inst* first = fixture.Phi();
    IR::Inst* second = fixture.Phi();
    if (reverse_roots) {
        std::swap(first, second);
    }
    IR::IREmitter ir{fixture.loop};
    const auto condition = ir.ILessThan(ir.LocalInvocationIdX(), ir.Imm32(16), false);
    const auto next = ir.IAdd(IR::U32{IR::Value{first}}, fixture.Word(ir, 0x200));
    first->AddPhiOperand(&fixture.loop, ir.Select(condition, next, ir.Imm32(0)));
    // This add can reach the cycle, but the cycle cannot reach it. Its constant is a base,
    // not an iterator step, regardless of which Phi starts the depth-first walk.
    second->AddPhiOperand(&fixture.loop,
                          ir.IAdd(IR::U32{IR::Value{first}}, fixture.Word(ir, 0x168)));
    fixture.Load(0x200, 0x168);
    CHECK(fixture.Offsets() == std::vector<u32>{0x168});
}

TEST_CASE("Connected iterator SCCs retain both stride words",
          "[shader][storage_buffer][iterator_scc]") {
    const bool reverse_edges = GENERATE(false, true);
    const bool joined_cycle = GENERATE(false, true);
    IteratorProgram fixture;
    IR::Inst* const outer = fixture.Phi();
    IR::Inst* const inner = fixture.Phi();
    IR::IREmitter ir{fixture.loop};
    const auto condition = ir.ILessThan(ir.LocalInvocationIdX(), ir.Imm32(16), false);
    const auto inner_next = ir.IAdd(IR::U32{IR::Value{inner}}, fixture.Word(ir, 0x210));
    inner->AddPhiOperand(&fixture.loop, ir.Select(condition, inner_next,
                                                  joined_cycle ? IR::Value{outer} : ir.Imm32(0)));
    const auto outer_next = ir.IAdd(IR::U32{IR::Value{outer}}, fixture.Word(ir, 0x200));
    outer->AddPhiOperand(&fixture.loop,
                         ir.Select(condition, reverse_edges ? IR::Value{inner} : outer_next,
                                   reverse_edges ? outer_next : IR::Value{inner}));
    fixture.Load(0x200, 0x160);
    fixture.Load(0x210, 0x168);
    CHECK(fixture.Offsets() == std::vector<u32>{0x160, 0x168});
}
