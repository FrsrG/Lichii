// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "shader_recompiler/environment.h"
#include "shader_recompiler/frontend/ir/basic_block.h"
#include "shader_recompiler/frontend/ir/program.h"
#include "shader_recompiler/frontend/ir/value.h"
#include "shader_recompiler/host_translate_info.h"
#include "shader_recompiler/ir_opt/passes.h"
#include "shader_recompiler/object_pool.h"

namespace {
namespace IR = Shader::IR;

class EmptyEnvironment final : public Shader::Environment {
public:
    u64 ReadInstruction(u32) override {
        return 0;
    }
    u32 ReadCbufValue(u32, u32) override {
        return 0;
    }
    Shader::TextureType ReadTextureType(u32) override {
        return {};
    }
    Shader::TexturePixelFormat ReadTexturePixelFormat(u32) override {
        return {};
    }
    bool IsTexturePixelFormatInteger(u32) override {
        return false;
    }
    u32 ReadViewportTransformState() override {
        return 0;
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
        return {};
    }
    bool HasHLEMacroState() const override {
        return false;
    }
    std::optional<Shader::ReplaceConstant> GetReplaceConstBuffer(u32, u32) override {
        return std::nullopt;
    }
    void Dump(u64, u64) override {}
};
} // namespace

TEST_CASE("Storage iterator analysis handles deep graphs", "[shader_stack]") {
    constexpr size_t depth = 32768;
    const bool cyclic = GENERATE(false, true);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    block.AppendNewInst(IR::Opcode::Phi, {});
    IR::Inst& phi = block.Instructions().back();
    phi.SetFlags(IR::Type::U32);
    IR::Value value = cyclic ? IR::Value{&phi} : IR::Value{u32{0}};
    for (size_t i = 0; i < depth; ++i) {
        block.AppendNewInst(IR::Opcode::IAdd32, {value, IR::Value{u32{1}}});
        value = IR::Value{&block.Instructions().back()};
    }
    phi.AddPhiOperand(&block, value);
    // Force the public storage pass to run its iterator analysis, without rebasing any load.
    block.AppendNewInst(IR::Opcode::LoadGlobal32, {IR::Value{u64{0}}});
    IR::Program program;
    program.blocks.push_back(&block);
    program.post_order_blocks.push_back(&block);
    EmptyEnvironment env;
    Shader::HostTranslateInfo host{};
    host.support_int64 = true;
    REQUIRE_NOTHROW(Shader::Optimization::GlobalMemoryToStorageBufferPass(env, program, host));
    CHECK(program.info.storage_buffers_descriptors.empty());
    CHECK(block.Instructions().size() == depth + 2);
    CHECK(block.Instructions().back().GetOpcode() == IR::Opcode::LoadGlobal32);
    CHECK(phi.Arg(0).InstRecursive() == value.Inst());
}

TEST_CASE("Identity accessors handle long chains", "[shader_stack]") {
    constexpr size_t depth = 65536;
    Shader::ObjectPool<IR::Inst> pool;
    IR::Inst* terminal = pool.Create(IR::Opcode::IAdd32, 0);
    std::vector<IR::Inst*> chain;
    chain.reserve(depth);
    for (size_t i = 0; i < depth; ++i) {
        IR::Inst* identity = pool.Create(IR::Opcode::Identity, 0);
        identity->SetArg(0, IR::Value{terminal});
        chain.push_back(identity);
    }
    // Link from the outside in so building the test itself does not repeatedly walk the chain.
    for (size_t i = 0; i + 1 < depth; ++i) {
        chain[i]->SetArg(0, IR::Value{chain[i + 1]});
    }
    const IR::Value value{chain.front()};
    CHECK_FALSE(value.IsImmediate());
    CHECK(value.InstRecursive() == terminal);
    CHECK(value.TryInstRecursive() == terminal);
    CHECK(value.Resolve().Inst() == terminal);
    chain.back()->SetArg(0, IR::Value{u32{42}});
    CHECK(value.IsImmediate());
    CHECK(value.TryInstRecursive() == nullptr);
    CHECK(value.Resolve().U32() == 42);
    CHECK(IR::Value{u32{7}}.TryInstRecursive() == nullptr);
    CHECK(IR::Value{u32{7}}.Resolve().U32() == 7);
}
