// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/common_types.h"
#include <array>
#include <filesystem>
#include <string>

namespace VideoCommon {

// Owned, immutable after publication. DAddr is the device-memory offset used by
// BufferBase::CpuAddr and Vulkan Buffer object names, not a Vulkan device address.
struct Nx2PreMmeBufferProbeRequest {
    static constexpr size_t MaxWords = 16;
    std::filesystem::path output_path;
    u64 request_id{1};
    u32 start{}, method{};
    u64 generation{}, address_space_id{};
    u64 queue_position{}; // Front queue prefix immediately before probe enqueue.
    GPUVAddr gpu_address{};
    DAddr device_address{};
    u32 word_count{};
    std::array<u32, MaxWords> selected_words{}, cpu_words{};
    std::string cpu_status{"unavailable"};
    u64 map_serial{}, host_pointer_serial{};
    u64 map_serial_after{}, host_pointer_serial_after{};
};

} // namespace VideoCommon
