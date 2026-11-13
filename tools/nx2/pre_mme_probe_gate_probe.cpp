// SPDX-License-Identifier: GPL-3.0-or-later
// Standalone production-header probe. Input captures stay local, no GPU/emulator.
#include "video_core/nx2_macro_failure_dump.h"
#include <iostream>
#include <stdexcept>
namespace Common::Log {
void FmtLogMessageImpl(Class, Level, const char *, unsigned int, const char *, fmt::string_view,
                       const fmt::format_args &) {}
} // namespace Common::Log
namespace C = VideoCommon::Nx2MacroFailure;
void Check(bool value, const char *what) {
    if (!value)
        throw std::runtime_error(what);
}
int main(int argc, char **argv) {
    try {
        Check(argc == 3, "usage: probe capture.json enabled|disabled");
        C::Json saved;
        std::ifstream(argv[1]) >> saved;
        auto code = saved.at("code").get<std::vector<C::Word>>();
        auto fifo = saved.at("fifo_before").get<std::vector<C::Word>>();
        auto origins = saved.at("parameter_origins");
        const auto start = saved.at("start").get<C::Word>();
        const auto method = saved.at("method").get<C::Word>();
        const auto generation = saved.at("generation").get<std::uint64_t>();
        int owner, other;
        Check(!C::Begin(&owner, start, method, generation, code, fifo, {}), "unarmed capture");
        C::NotifyFailure(&owner, start, generation);
        Check(!C::Begin(&other, start, method, generation, code, fifo, {}), "wrong owner");
        Check(!C::Begin(&owner, start, method, generation + 1, code, fifo, {}), "wrong generation");
        auto capture = C::Begin(&owner, start, method, generation, code, fifo, {});
        Check(bool(capture), "matching armed capture absent");
        auto freeze = [&](auto &words, auto &rows, bool complete = true) {
            return C::FreezePreMmeProbe(*capture, code, words, rows, complete);
        };
        if (std::string_view(argv[2]) == "disabled") {
            Check(!freeze(fifo, origins), "disabled probe queued");
            std::cout << "disabled passed\n";
            return 0;
        }
        Check(C::PreMmeProbeCodeMatches(start, code), "real macro fingerprint rejected");
        code.at(start * 3) ^= 1;
        Check(!freeze(fifo, origins), "modified code accepted");
        code.at(start * 3) ^= 1;
        auto valid_fifo = fifo;
        fifo.pop_back();
        Check(!freeze(fifo, origins), "incomplete FIFO selected");
        fifo = valid_fifo;
        fifo.at(2) = 1;
        Check(!freeze(fifo, origins), "small count selected");
        fifo = valid_fifo;
        Check(!freeze(fifo, origins, false), "incomplete trace selected");
        auto rows = origins;
        rows.at(2)["source_device_address"] = nullptr;
        Check(!freeze(fifo, rows), "unknown DAddr selected");
        rows = origins;
        rows.at(2)["source_device_address"] =
            rows.at(2)["source_device_address"].get<std::uint64_t>() + 4;
        Check(!freeze(fifo, rows), "noncontiguous DAddr selected");
        rows = origins;
        rows.at(2)["source_gpu_address"] =
            rows.at(2)["source_gpu_address"].get<std::uint64_t>() + 4;
        Check(!freeze(fifo, rows), "noncontiguous GPUVA selected");
        rows = origins;
        rows.at(2)["address_space_id"] = rows.at(2)["address_space_id"].get<std::uint64_t>() + 1;
        Check(!freeze(fifo, rows), "mixed ASID selected");
        rows = origins;
        rows.at(2)["argument_source"] = "header-inline-bitfield";
        Check(!freeze(fifo, rows), "inline parameter selected");
        rows = origins;
        rows.at(2)["argument"] = 0;
        Check(!freeze(fifo, rows), "different selected word accepted");
        capture->document["generation"] = generation + 1;
        Check(!freeze(fifo, origins), "capture generation changed");
        capture->document["generation"] = generation;
        auto request = freeze(fifo, origins);
        Check(bool(request), "valid request rejected after rejected attempts");
        Check(request->word_count == fifo.size() - 1 && request->word_count <= 16,
              "body cap/count");
        Check(request->selected_words[1] == fifo[2], "wrong body offset");
        const auto frozen = request->selected_words;
        fifo[2] = 0;
        origins.at(2)["argument"] = 0;
        Check(request->selected_words == frozen, "request borrowed mutable state");
        rows = saved.at("parameter_origins");
        Check(!freeze(valid_fifo, rows), "second request queued");
        Check(capture->document.at("pre_mme_buffer_probe").at("request_id") == request->request_id,
              "capture/request link missing");
        std::cout << "passed freeze, rejection, immutable ownership and one-shot cap start="
                  << start << '\n';
        return 0;
    } catch (const std::exception &ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
