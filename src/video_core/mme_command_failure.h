// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string_view>
#include <vector>

namespace Tegra::MmeCommandFailure {

constexpr std::size_t MaxEntryWords = 65536;
constexpr std::size_t MaxParameters = 4096;

inline const char* Prefix() {
    static const char* const prefix = std::getenv("EDEN_NX2_MME_COMMAND_FAILURE");
    return prefix;
}

inline std::atomic<bool> captured{};
inline std::atomic<std::uint64_t> sequence{};

inline bool Armed() {
    const char* const prefix = Prefix();
    return prefix != nullptr && *prefix && !captured.load(std::memory_order_relaxed);
}

// Front-thread-only observation of the actual failed invocation, before its FIFO is cleared.
struct Failure {
    std::uint64_t serial{}, generation{}, final_generation{}, parameter_count{}, consumed{};
    std::uint32_t method{}, start{}, pc{};
    std::string_view error;
    std::vector<std::uint32_t> parameters;
};

struct Window {
    std::size_t begin{}, count{};
};

// Prefer surrounding context, but always include the triggering word. No source reads here.
constexpr Window EntryWindow(std::size_t words, std::size_t index) {
    const auto count = std::min(words, MaxEntryWords);
    if (index >= words) {
        return {0, count};
    }
    const auto begin = index > MaxEntryWords / 2 ? index - MaxEntryWords / 2 : 0;
    return {std::min(begin, words - count), count};
}

inline bool AllParametersInCall(std::uint64_t parameter_count,
                                std::span<const std::uint32_t> parameters,
                                std::span<const std::uint32_t> call) {
    return parameter_count == parameters.size() && parameter_count == call.size() &&
           std::equal(parameters.begin(), parameters.end(), call.begin(), call.end());
}

} // namespace Tegra::MmeCommandFailure
