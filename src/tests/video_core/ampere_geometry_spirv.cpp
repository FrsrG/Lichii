// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/frontend/ir/ir_emitter.h"
#include "shader_recompiler/object_pool.h"

TEST_CASE("SPIR-V geometry stream zero needs no geometry streams capability",
          "[video_core][ampere][spirv]") {
    namespace IR = Shader::IR;
    Shader::ObjectPool<IR::Inst> inst_pool;
    IR::Block block{inst_pool};
    IR::IREmitter ir{block};
    ir.EmitVertex(ir.Imm32(0));
    ir.EndPrimitive(ir.Imm32(0));
    IR::Program program;
    program.stage = Shader::Stage::Geometry;
    program.output_topology = Shader::OutputTopology::PointList;
    program.output_vertices = 1;
    program.invocations = 1;
    program.blocks.push_back(&block);
    program.post_order_blocks.push_back(&block);
    IR::AbstractSyntaxNode block_node{};
    block_node.type = IR::AbstractSyntaxNode::Type::Block;
    block_node.data.block = &block;
    IR::AbstractSyntaxNode return_node{};
    return_node.type = IR::AbstractSyntaxNode::Type::Return;
    program.syntax_list = {block_node, return_node};
    Shader::Profile profile{};
    profile.supported_spirv = 0x00010300;
    profile.support_geometry_streams = false;
    const auto code = Shader::Backend::SPIRV::EmitSPIRV(profile, program);
    REQUIRE(code.size() > 5);
    constexpr u32 OpCapability = 17;
    constexpr u32 OpEmitVertex = 218;
    constexpr u32 OpEndPrimitive = 219;
    constexpr u32 OpEmitStreamVertex = 220;
    constexpr u32 OpEndStreamPrimitive = 221;
    constexpr u32 CapabilityGeometry = 2;
    constexpr u32 CapabilityGeometryStreams = 54;
    u32 emits = 0, ends = 0, geometry = 0;
    for (size_t offset = 5; offset < code.size();) {
        const u32 words = code[offset] >> 16;
        const u32 opcode = code[offset] & 0xffff;
        REQUIRE(words > 0);
        REQUIRE(offset + words <= code.size());
        if (opcode == OpCapability) {
            REQUIRE(words == 2);
            REQUIRE(code[offset + 1] != CapabilityGeometryStreams);
            geometry += code[offset + 1] == CapabilityGeometry;
        }
        emits += opcode == OpEmitVertex;
        ends += opcode == OpEndPrimitive;
        REQUIRE(opcode != OpEmitStreamVertex);
        REQUIRE(opcode != OpEndStreamPrimitive);
        offset += words;
    }
    REQUIRE(emits == 1);
    REQUIRE(ends == 1);
    REQUIRE(geometry == 1);
}
