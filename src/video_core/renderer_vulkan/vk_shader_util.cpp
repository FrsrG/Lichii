// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstring>
#include <cstdlib>
#include <string_view>
#include <vector>

#include <spirv/unified1/spirv.hpp>

#include "common/common_types.h"
#include "common/logging.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

vk::ShaderModule BuildShader(const Device& device, std::span<const u32> code, u64 shader_hash) {
    std::vector<u32> probe_code;
    if (const char* probe = std::getenv("EDEN_NX2_DREF_PROBE_HASH");
        shader_hash && probe && std::string_view{probe} == fmt::format("{:016x}", shader_hash)) {
        u32 float_type = 0;
        u32 one = 0;
        bool valid = code.size() >= 5 && code[0] == spv::MagicNumber;
        for (size_t pos = 5; valid && pos < code.size();) {
            const size_t count = code[pos] >> 16;
            const auto op = static_cast<spv::Op>(code[pos] & 0xffff);
            if (!count || count > code.size() - pos) {
                valid = false;
                break;
            }
            if (op == spv::Op::OpTypeFloat && count == 3 && code[pos + 2] == 32) {
                float_type = code[pos + 1];
            }
            if (op == spv::Op::OpConstant && count == 4 && code[pos + 1] == float_type &&
                float_type && code[pos + 3] == 0x3f800000) {
                one = code[pos + 2];
            }
            pos += count;
        }
        if (valid && one) {
            probe_code.assign(code.begin(), code.end());
            size_t replaced = 0;
            for (size_t pos = 5; pos < code.size();) {
                const size_t count = code[pos] >> 16;
                const auto op = static_cast<spv::Op>(code[pos] & 0xffff);
                if ((op == spv::Op::OpImageSampleDrefImplicitLod ||
                     op == spv::Op::OpImageSampleDrefExplicitLod) &&
                    count >= 5 && code[pos + 1] == float_type) {
                    probe_code[pos] = (4U << 16) | static_cast<u32>(spv::Op::OpCopyObject);
                    probe_code[pos + 3] = one;
                    for (size_t offset = 4; offset < count; ++offset) {
                        probe_code[pos + offset] = (1U << 16) | static_cast<u32>(spv::Op::OpNop);
                    }
                    ++replaced;
                }
                pos += count;
            }
            LOG_WARNING(Render_Vulkan, "Dref probe {:016x}: replaced {} comparisons with 1",
                        shader_hash, replaced);
            code = probe_code;
        } else {
            LOG_ERROR(Render_Vulkan, "Dref probe {:016x}: no valid scalar float one", shader_hash);
        }
    }
    return device.GetLogical().CreateShaderModule({
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .codeSize = static_cast<u32>(code.size_bytes()),
        .pCode = code.data(),
    });
}

} // namespace Vulkan
