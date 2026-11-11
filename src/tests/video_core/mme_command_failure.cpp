// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <limits>
#include <catch2/catch_test_macros.hpp>

#include "video_core/mme_command_failure.h"

using namespace Tegra::MmeCommandFailure;

TEST_CASE("MME command windows retain the failing word within a fixed entry bound",
          "[video_core][native_macro][diagnostic]") {
    for (const std::size_t words : {0U, 1U, 65535U, 65536U, 65537U, 2097151U}) {
        for (const auto index : std::array<std::size_t, 3>{0, words / 2, words == 0 ? 0 : words - 1}) {
            const auto window = EntryWindow(words, index);
            CHECK(window.count <= MaxEntryWords);
            CHECK(window.begin <= words);
            CHECK(window.count <= words - window.begin);
            if (index < words) {
                CHECK(window.begin <= index);
                CHECK(index - window.begin < window.count);
                // The parser's current multi-call has at most 8191 parameters. Keep all
                // available triggering words, including a call close to the entry's end.
                CHECK(std::min<std::size_t>(8191, words - index) <=
                      window.count - (index - window.begin));
            }
        }
    }
    const auto huge = EntryWindow(std::numeric_limits<std::size_t>::max(),
                                  std::numeric_limits<std::size_t>::max() - 1);
    CHECK(huge.count == MaxEntryWords);
    CHECK(huge.begin == std::numeric_limits<std::size_t>::max() - MaxEntryWords);
}

TEST_CASE("MME command provenance never calls earlier or truncated parameters complete",
          "[video_core][native_macro][diagnostic]") {
    constexpr std::array<std::uint32_t, 5> parameters{4, 0, 0x3f800000, 0, 0};
    CHECK(AllParametersInCall(parameters.size(), parameters, parameters));
    CHECK_FALSE(AllParametersInCall(parameters.size(), parameters,
                                    std::span{parameters}.last(1)));
    CHECK_FALSE(AllParametersInCall(parameters.size(), std::span{parameters}.first(4),
                                    std::span{parameters}.first(4)));
    constexpr std::array<std::uint32_t, 5> different{4, 0, 0x3f800001, 0, 0};
    CHECK_FALSE(AllParametersInCall(parameters.size(), parameters, different));
}
