// SPDX-License-Identifier: GPL-3.0-or-later
// Standalone lifecycle regression; supply a new EDEN_NX2_MME_FAILURE_DUMP directory.
// Uses production capture code with synthetic results, no emulator/GPU state.
#include "video_core/nx2_macro_failure_dump.h"
#include <array>
#include <iostream>
#include <stdexcept>

namespace Common::Log {
void FmtLogMessageImpl(Class, Level, const char*, unsigned int, const char*, fmt::string_view,
                       const fmt::format_args&) {}
}
namespace Capture = VideoCommon::Nx2MacroFailure;
struct Result {
    bool success;
    std::uint32_t pc{};
    size_t consumed{};
    std::string_view error;
};
struct Checkpoint {
    std::array<std::uint32_t, 8> regs{};
    std::uint32_t carry{}, method{}, increment{}, loop_count{}, loop_start{}, loop_end{}, next_pc{}, steps{};
    bool end_next{}, stopped{}, cancel_end{};
};
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main() {
    try {
        auto* gate = Capture::GetGate();
        Check(gate && !std::filesystem::exists(gate->directory), "supply a new capture output directory");
        int owner, other;
        constexpr std::uint32_t start = 0x1d2, later = 0x1e7;
        constexpr std::uint64_t generation = 7;
        const std::array<std::uint32_t, 3> words{};
        Check(!Capture::Begin(&owner, start, 0, generation, words, words, words), "unarmed call captured");
        Capture::NotifyFailure(&owner, start, generation);
        Check(!Capture::Begin(&other, start, 0, generation, words, words, words), "wrong engine captured");
        Check(!Capture::Begin(&owner, start, 0, generation + 1, words, words, words), "wrong generation captured");
        for (size_t i = 0; i < 32; ++i) {
            auto capture = Capture::Begin(&owner, start, 0, generation, words, words, words);
            Check(bool(capture), "successful invocation stopped the capture wait");
            capture->Finish(Result{true}, Checkpoint{}, words, words);
            capture.reset();
            Check(gate->phase == Capture::Armed, "successful capture did not rearm");
        }
        Check(gate->attempts == 32 && !std::filesystem::exists(gate->directory), "success wrote an output");
        Capture::NotifyFailure(&other, later, generation + 1);
        Check(!Capture::ArmedFor(&owner, start, generation) && Capture::ArmedFor(&other, later, generation + 1),
              "later actual failure did not retarget");
        auto capture = Capture::Begin(&other, later, 0xe4e, generation + 1, words, words, words);
        Check(bool(capture), "retargeted invocation not captured");
        capture->Finish(Result{false, later + 6, 0, "synthetic failure"}, Checkpoint{}, words, words);
        capture.reset();
        size_t outputs{};
        for (const auto& entry : std::filesystem::directory_iterator(gate->directory)) {
            if (entry.path().extension() != ".json") continue;
            ++outputs;
            std::ifstream input(entry.path());
            Capture::Json saved; input >> saved;
            Check(saved["start"] == later && saved["generation"] == generation + 1 && saved["attempt"] == 33,
                  "saved retarget metadata differs");
        }
        Capture::NotifyFailure(&owner, start, generation);
        Check(outputs == 1 && gate->phase == Capture::Done &&
              !Capture::Begin(&other, later, 0, generation + 1, words, words, words), "one-shot capture revived");
        std::cout << "PASS: 32 successes, target/generation retarget, one failed save, terminal disarm\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
