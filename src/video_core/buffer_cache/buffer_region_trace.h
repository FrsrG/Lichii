// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>

#include "common/common_types.h"
#include "common/logging.h"

namespace VideoCommon {

// CPU-side evidence only: never flushes guest memory or reads a Vulkan resource.
class BufferRegionTrace {
public:
    static BufferRegionTrace& Get() {
        static BufferRegionTrace trace;
        return trace;
    }

    bool Overlaps(DAddr begin, u64 size) const {
        return enabled && records.load(std::memory_order_relaxed) < Limit && size != 0 &&
               (begin <= address ? address - begin < size : begin - address < bytes);
    }

    void IndexBinding(DAddr begin, u32 first, u32 count, u32 element_bytes, u64 buffer_id) {
        if (!enabled || count != 6) {
            return;
        }
        const DAddr indices = begin + u64{first} * element_bytes;
        const bool covered = indices <= address ? address - indices < u64{count} * element_bytes
                                               : indices - address < bytes;
        std::scoped_lock lock{mutex};
        if (covered ? index_covered_logged : index_misses >= 3) {
            return;
        }
        if (covered) {
            index_covered_logged = true;
        } else {
            ++index_misses;
        }
        LOG_INFO(HW_GPU,
                 "Buffer region trace six-index candidate: device={:#x} first={} count={} "
                 "element_bytes={} buffer={} selected={:#x}+{:#x} overlaps={} "
                 "(coverage only, not UI identification; first 3 misses)",
                 indices, first, count, element_bytes, buffer_id, address, bytes, covered);
    }

    template <typename Memory, typename Tracker, typename Ranges>
    void Record(const char* site, DAddr begin, u64 size, Memory& memory, Tracker& tracker,
                Ranges& gpu_ranges, u64 detail0 = 0, u64 detail1 = 0, int cpu_upload = -1,
                std::span<const u8> staged = {}) {
        if (!Overlaps(begin, size)) {
            return;
        }
        std::array<u8, 64> snapshot{};
        memory.ReadBlockUnsafe(address, snapshot.data(), bytes);
        const bool tracker_gpu = tracker.IsRegionGpuModified(address, bytes);
        const bool range_gpu = gpu_ranges.Intersects(address, bytes);
        const DAddr first = (std::max)(address, begin);
        const size_t relative = static_cast<size_t>(first - begin);
        const size_t staged_size = staged.empty() || relative >= staged.size()
                                       ? 0
                                       : (std::min)(static_cast<size_t>(address + bytes - first),
                                                    staged.size() - relative);
        const auto staged_part = staged_size != 0 ? staged.subspan(relative, staged_size)
                                                 : std::span<const u8>{};
        const std::string cpu_hex = Hex(std::span<const u8>{snapshot.data(), bytes});
        const std::string staged_hex = Hex(staged_part);
        const std::string identity = fmt::format("{}:{}:{}", site, detail0, detail1);
        const std::string state = fmt::format("{}:{}:{}:{}:{}", cpu_upload, tracker_gpu,
                                             range_gpu, cpu_hex, staged_hex);
        std::scoped_lock lock{mutex};
        if (records.load(std::memory_order_relaxed) >= Limit) {
            return;
        }
        const auto previous = last_states.find(identity);
        if (previous != last_states.end() && previous->second == state) {
            return;
        }
        last_states.insert_or_assign(identity, state);
        const u32 number = records.fetch_add(1, std::memory_order_relaxed) + 1;
        LOG_INFO(HW_GPU,
                 "Buffer region trace #{} site={} selected={:#x}+{:#x} operation={:#x}+{:#x} "
                 "detail0={:#x} detail1={:#x} cpu_upload={} tracker_gpu={} range_gpu={} "
                 "guest_cpu={} staged_at={:#x} staged={} (CPU bytes, not GPU contents)",
                 number, site, address, bytes, begin, size, detail0, detail1, cpu_upload,
                 tracker_gpu, range_gpu, cpu_hex, first, staged_hex);
        if (number == Limit) {
            LOG_INFO(HW_GPU, "Buffer region trace reached {} state-change records; stopping", Limit);
        }
    }

private:
    static constexpr u32 Limit = 256;
    BufferRegionTrace() {
        const char* value = std::getenv("EDEN_NX2_BUFFER_REGION_TRACE");
        if (!value || !*value) {
            return;
        }
        char* end{};
        address = std::strtoull(value, &end, 16);
        const bool starts_hex = (*value >= '0' && *value <= '9') ||
                                (*value >= 'a' && *value <= 'f') ||
                                (*value >= 'A' && *value <= 'F');
        if (!starts_hex || end == value || *end != '\0' || address == 0 ||
            address >= (u64{1} << 36)) {
            LOG_WARNING(HW_GPU, "Buffer region trace disabled: invalid device address");
            return;
        }
        bytes = 64;
        if (const char* size = std::getenv("EDEN_NX2_BUFFER_REGION_TRACE_SIZE")) {
            char* size_end{};
            const u64 parsed = std::strtoull(size, &size_end, 0);
            if (*size < '0' || *size > '9' || size_end == size || *size_end != '\0' ||
                parsed == 0 || parsed > 64) {
                LOG_WARNING(HW_GPU, "Buffer region trace disabled: size must be 1..64");
                return;
            }
            bytes = static_cast<size_t>(parsed);
        }
        if (address + bytes > (u64{1} << 36)) {
            return;
        }
        enabled = true;
        LOG_INFO(HW_GPU,
                 "Buffer region trace enabled at {:#x}+{:#x}; max {} state-change records; "
                 "cpu_upload=-1 unknown, 0/1 from normal upload iterator; GPU marks are "
                 "ownership tracking, not proof of a shader store",
                 address, bytes, Limit);
    }

    static std::string Hex(std::span<const u8> data) {
        static constexpr char digits[] = "0123456789abcdef";
        std::string result(data.size() * 2, '0');
        for (size_t i = 0; i < data.size(); ++i) {
            result[i * 2] = digits[data[i] >> 4];
            result[i * 2 + 1] = digits[data[i] & 15];
        }
        return result;
    }

    DAddr address{};
    size_t bytes{};
    bool enabled{};
    bool index_covered_logged{};
    u32 index_misses{};
    std::atomic<u32> records{};
    std::mutex mutex;
    std::unordered_map<std::string, std::string> last_states;
};

} // namespace VideoCommon
