// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/logging.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <vector>

namespace VideoCommon::Nx2MacroFailure {
using Word = std::uint32_t;
using Json = nlohmann::json;
constexpr size_t MaxWords = 65536;
constexpr size_t MaxReads = 4096;
constexpr size_t MaxEmits = 4096;
constexpr size_t MaxDmaSnapshots = 8;

enum Phase { Waiting, Claiming, Armed, Capturing, Done };
struct Gate {
  std::filesystem::path directory;
  std::atomic<Phase> phase{Waiting};
  const void *owner{};
  Word start{};
  std::uint64_t generation{};
  size_t attempts{};
};

inline Gate *GetGate() noexcept {
  static const auto gate = []() noexcept -> std::unique_ptr<Gate> {
    try {
      const char *path = std::getenv("EDEN_NX2_MME_FAILURE_DUMP");
      if (!path || !*path)
        return {};
      auto result = std::make_unique<Gate>();
      result->directory = std::filesystem::u8path(path);
      return result;
    } catch (...) {
      return {};
    }
  }();
  return gate.get();
}

inline bool IsTarget(Word start) { return start == 0x1d2 || start == 0x1e7; }

inline bool ArmedFor(const void* owner, Word start, std::uint64_t generation) noexcept {
  const auto* gate = GetGate();
  return gate && gate->phase.load(std::memory_order_acquire) == Armed &&
         gate->owner == owner && gate->start == start && gate->generation == generation;
}

// An actual failure arms a following invocation: no state is copied before the
// first failure. A different failing target may retarget an armed capture; a saved
// failed invocation remains terminal. These transitions run on the GPU front thread.
inline void NotifyFailure(const void *owner, Word start,
                          std::uint64_t generation) {
  auto *gate = GetGate();
  if (!gate || !IsTarget(start))
    return;
  Phase expected = Waiting;
  if (!gate->phase.compare_exchange_strong(expected, Claiming)) {
    if (expected != Armed ||
        !gate->phase.compare_exchange_strong(expected, Claiming))
      return;
  }
  gate->owner = owner;
  gate->start = start;
  gate->generation = generation;
  gate->phase.store(Armed, std::memory_order_release);
}

struct Capture {
  Gate &gate;
  Json document;
  size_t reads{};
  size_t emits{};
  size_t dma_snapshots{};
  bool finished{};
  bool inputs_complete{true};
  bool emits_complete{true};

  explicit Capture(Gate &gate_) : gate{gate_} {}
  ~Capture() {
    // Allocation or serialization errors must not leave the capture armed
    // indefinitely.
    if (!finished)
      gate.phase.store(Done, std::memory_order_release);
  }
  Json Words(std::span<const Word> words) {
    if (words.size() > MaxWords)
      inputs_complete = false;
    Json result = Json::array();
    for (Word value : words.first((std::min)(words.size(), MaxWords)))
      result.push_back(value);
    return result;
  }
  void Read(Word address, Word value) noexcept {
    try {
      if (reads++ < MaxReads)
        document["register_reads"].push_back({address, value});
      else
        inputs_complete = false;
    } catch (...) {
      inputs_complete = false;
      reads = MaxReads;
    }
  }
  void Emit(Word method, Word value, size_t fifo_before,
            std::span<const Word> fifo_after,
            std::span<const Word> dram_after) noexcept {
    try {
      const size_t index = emits++;
      if (index < MaxEmits)
        document["emits"].push_back({method, value});
      else
        emits_complete = false;
      if (method != 0x157 && method != 0x158)
        return;
      if (dma_snapshots++ >= MaxDmaSnapshots) {
        inputs_complete = false;
        return;
      }
      document["dma_callbacks"].push_back({
          {"emit_index", index},
          {"method", method},
          {"value", value},
          {"fifo_before", fifo_before},
          {"fifo_after", fifo_after.size()},
          {"dram_after", Words(dram_after)},
      });
    } catch (...) {
      inputs_complete = emits_complete = false;
      emits = MaxEmits;
      dma_snapshots = MaxDmaSnapshots;
    }
  }

  template <class Result, class Checkpoint>
  void Finish(const Result &result, const Checkpoint &checkpoint,
              std::span<const Word> fifo_after,
              std::span<const Word> dram_after) noexcept {
    finished = true;
    if (result.success) {
      // Release this bounded temporary snapshot and keep waiting. Successful calls
      // are not evidence that the intermittent failure will not recur.
      gate.phase.store(Armed, std::memory_order_release);
      return;
    }
    gate.phase.store(Done, std::memory_order_release);
    try {
      document["fifo_after"] = Words(fifo_after);
      document["dram_after"] = Words(dram_after);
      document["result"] = {{"error", std::string{result.error}},
                            {"pc", result.pc},
                            {"consumed", result.consumed}};
      document["pc_kind"] =
          result.error == "instruction limit exceeded"
              ? "next dispatch target; no instruction at this PC ran"
              : "current instruction, or outside-code target";
      document["checkpoint"] = {
          {"regs", checkpoint.regs},
          {"carry", checkpoint.carry},
          {"method", checkpoint.method},
          {"increment", checkpoint.increment},
          {"loop_count", checkpoint.loop_count},
          {"loop_start", checkpoint.loop_start},
          {"loop_end", checkpoint.loop_end},
          {"next_pc", checkpoint.next_pc},
          {"steps_remaining", checkpoint.steps},
          {"end_next", checkpoint.end_next},
          {"stopped", checkpoint.stopped},
          {"cancel_end", checkpoint.cancel_end},
      };
      document["input_trace_complete"] = inputs_complete;
      document["emit_trace_complete"] = emits_complete;
      document["register_read_count"] = reads;
      document["emit_count"] = emits;
      document["dma_callback_count"] = dma_snapshots;
      std::filesystem::create_directories(gate.directory);
      const auto stamp =
          std::chrono::steady_clock::now().time_since_epoch().count();
      const auto path =
          gate.directory / ("macro-failure-" + std::to_string(gate.start) +
                            "-" + std::to_string(stamp) + ".json");
      std::ofstream file(path, std::ios::binary | std::ios::trunc);
      file.exceptions(std::ios::failbit | std::ios::badbit);
      file << document.dump();
      file.close();
      LOG_INFO(HW_GPU,
               "Saved bounded MME failure capture to {} (inputs complete {}, "
               "emits complete {})",
               path.generic_string(), inputs_complete, emits_complete);
    } catch (const std::exception &error) {
      LOG_ERROR(HW_GPU, "MME failure capture could not be saved: {}",
                error.what());
    }
  }
};

inline std::unique_ptr<Capture> Begin(const void *owner, Word start,
                                      Word method, std::uint64_t generation,
                                      std::span<const Word> code,
                                      std::span<const Word> fifo,
                                      std::span<const Word> dram) noexcept {
  auto *gate = GetGate();
  if (!gate || gate->phase.load(std::memory_order_acquire) != Armed ||
      gate->owner != owner || gate->start != start ||
      gate->generation != generation)
    return {};
  Phase expected = Armed;
  if (!gate->phase.compare_exchange_strong(expected, Capturing))
    return {};
  ++gate->attempts;
  try {
    auto result = std::make_unique<Capture>(*gate);
    result->document = {
        {"version", 1},
        {"kind", "maxwell-split-front"},
        {"start", start},
        {"method", method},
        {"generation", generation},
        {"attempt", gate->attempts},
        {"code", result->Words(code)},
        {"fifo_before", result->Words(fifo)},
        {"dram_before", result->Words(dram)},
        {"register_reads", Json::array()},
        {"emits", Json::array()},
        {"dma_callbacks", Json::array()},
    };
    return result;
  } catch (...) {
    gate->phase.store(Done, std::memory_order_release);
    return {};
  }
}
} // namespace VideoCommon::Nx2MacroFailure
