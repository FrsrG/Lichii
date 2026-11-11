// SPDX-License-Identifier: GPL-3.0-or-later

// Offline comparison against a split-front-mme-failure-v1 capture. No
// emulator/GPU calls.
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "video_core/macro_turing.h"

namespace {
using u32 = std::uint32_t;
using u64 = std::uint64_t;
struct Event {
  u32 kind, address, value, count, word_offset, word_count;
  u64 source;
};
static_assert(sizeof(Event) == 32);

std::string Json(const std::string &text) {
  constexpr char hex[] = "0123456789abcdef";
  std::string result{"\""};
  for (const unsigned char c : text) {
    if (c == '"' || c == '\\') {
      result += '\\';
      result += static_cast<char>(c);
    } else if (c < 32) {
      result += "\\u00";
      result += hex[c >> 4];
      result += hex[c & 15];
    } else {
      result += static_cast<char>(c);
    }
  }
  return result + '"';
}

template <typename T>
std::vector<T> Read(const std::string &path, size_t maximum) {
  std::ifstream file{path, std::ios::binary | std::ios::ate};
  if (!file)
    throw std::runtime_error("cannot open " + path);
  const auto bytes = file.tellg();
  if (bytes < 0 || static_cast<u64>(bytes) > maximum * sizeof(T) ||
      bytes % sizeof(T)) {
    throw std::runtime_error("invalid or excessive file size: " + path);
  }
  std::vector<T> data(static_cast<size_t>(bytes) / sizeof(T));
  file.seekg(0);
  file.read(reinterpret_cast<char *>(data.data()), bytes);
  if (!file)
    throw std::runtime_error("cannot read " + path);
  return data;
}

struct Stop {
  std::string status, detail;
  size_t event;
};

int Replay(const std::string &prefix) {
  if constexpr (std::endian::native != std::endian::little) {
    throw std::runtime_error("this helper requires a little-endian host");
  }
  std::ifstream input{prefix + ".txt"};
  if (!input)
    throw std::runtime_error("cannot open capture metadata");
  std::map<std::string, std::string> meta;
  for (std::string line; std::getline(input, line);) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    const auto equals = line.find('=');
    if (equals != std::string::npos)
      meta[line.substr(0, equals)] = line.substr(equals + 1);
  }
  if (meta.at("format") != "split-front-mme-failure-v1" ||
      meta.at("byte_order") != "little") {
    throw std::runtime_error("unsupported capture format/byte order");
  }
  const auto number = [&](const char *key) {
    return std::stoull(meta.at(key));
  };
  const auto code = Read<u32>(prefix + ".code", 0x8000);
  auto parameters = Read<u32>(prefix + ".parameters", 4096);
  const auto before = Read<u32>(prefix + ".dram-before", 0xc00);
  const auto after = Read<u32>(prefix + ".dram-after", 0xc00);
  const auto events = Read<Event>(prefix + ".events", 4096);
  const auto dma = Read<u32>(prefix + ".dma-words", 65536);
  if (code.size() != 0x8000 || before.size() != 0xc00 ||
      after.size() != 0xc00 || number("start") >= code.size() / 3 ||
      number("events_captured") != events.size() ||
      number("dma_words_captured") != dma.size() ||
      number("events_total") < events.size() ||
      number("dma_words_total") < dma.size()) {
    throw std::runtime_error("capture shape/count metadata mismatch");
  }
  for (const Event &event : events) {
    if (event.kind < 1 || event.kind > 3 ||
        (event.kind == 2 && event.count > 1) ||
        (event.kind == 3 &&
         (event.count > 0x1000 || event.word_count > event.count ||
          u64{event.word_offset} + event.word_count > dma.size()))) {
      throw std::runtime_error("invalid callback or DMA event fields");
    }
  }
  std::array<u32, 0xc00> ram;
  std::copy(before.begin(), before.end(), ram.begin());
  const bool truncated = number("events_total") != events.size() ||
                         number("dma_words_total") != dma.size() ||
                         number("parameter_count_before") != parameters.size();
  size_t cursor{};
  std::string status, detail;
  std::optional<Tegra::TuringMacroInterpreter::Result> result;
  size_t difference = ram.size();
  const auto next = [&](u32 kind) -> const Event & {
    if (cursor == events.size()) {
      throw Stop{
          number("events_total") > cursor ? "missing_context" : "mismatch",
          "interpreter requested a callback past the captured event stream",
          cursor};
    }
    if (events[cursor].kind != kind) {
      throw Stop{"mismatch", "different callback kind at the recorded event",
                 cursor};
    }
    return events[cursor];
  };
  try {
    if (number("parameter_count_before") != parameters.size()) {
      throw Stop{"missing_context", "original parameter FIFO was truncated",
                 cursor};
    }
    if (number("code_generation_before") != number("code_generation_after")) {
      throw Stop{"missing_context",
                 "macro code changed during the captured call", cursor};
    }
    Tegra::TuringMacroInterpreter interpreter;
    interpreter.active_dram = &ram;
    result = interpreter.Execute(
        code, static_cast<u32>(number("start")), parameters,
        [&](u32 address) {
          const Event &event = next(1);
          if (event.address != address) {
            std::ostringstream text;
            text << "read expected address=" << event.address
                 << ", interpreter address=" << address;
            throw Stop{"mismatch", text.str(), cursor};
          }
          ++cursor;
          return event.value;
        },
        [&](u32 method, u32 value) {
          const Event &event = next(2);
          if (event.address != method || event.value != value) {
            std::ostringstream text;
            text << "emit expected method=" << event.address
                 << " value=" << event.value
                 << ", interpreter method=" << method << " value=" << value;
            throw Stop{"mismatch", text.str(), cursor};
          }
          const bool accepted = event.count != 0;
          ++cursor;
          if (accepted && (method == 0x157 || method == 0x158)) {
            const Event &read = next(3);
            if (read.address != method ||
                read.count != std::min(value, 0x1000U)) {
              throw Stop{"mismatch", "different DMA method/count", cursor};
            }
            if (read.word_count != read.count) {
              throw Stop{"missing_context", "actual DMA words were truncated",
                         cursor};
            }
            if (u64{read.word_offset} + read.word_count > dma.size()) {
              throw std::runtime_error("DMA payload outside captured words");
            }
            for (u32 i = 0; i < read.word_count; ++i) {
              const u32 word = dma[read.word_offset + i];
              if (method == 0x158)
                parameters.push_back(word);
              else if (const u32 index = read.value + i; index < ram.size())
                ram[index] = word;
            }
            ++cursor;
          }
          return accepted;
        });
    difference = static_cast<size_t>(
        std::mismatch(ram.begin(), ram.end(), after.begin()).first -
        ram.begin());
    if (result->error != "instruction limit exceeded" &&
        (result->error != meta.at("error") ||
         result->success != meta.at("error").empty())) {
      status = "mismatch";
      detail = "interpreter terminated differently after the matched callback "
               "prefix";
    } else if (result->error == "instruction limit exceeded" ||
               meta.at("error") == "instruction limit exceeded") {
      status = truncated ? "missing_context" : "inconclusive_budget";
      detail = "JIT dispatch and interpreter instruction budgets count "
               "different work; not a termination equivalence proof";
    } else if (truncated) {
      status = "missing_context";
      detail =
          "capture is truncated; a matching prefix is not a complete replay";
    } else if (cursor != events.size() || result->error != meta.at("error") ||
               result->success != meta.at("error").empty() ||
               result->consumed != number("consumed") ||
               parameters.size() != number("parameter_count_after") ||
               difference != ram.size()) {
      status = "mismatch";
      detail =
          "termination, remaining callbacks, FIFO or final data RAM differ";
    } else {
      status = "match";
      detail = "complete recorded callbacks, termination, FIFO and final data "
               "RAM match";
    }
  } catch (const Stop &stop) {
    status = stop.status;
    detail = stop.detail;
    cursor = stop.event;
  }
  std::cout << "{\n  \"status\":" << Json(status)
            << ",\n  \"detail\":" << Json(detail)
            << ",\n  \"matched_event_prefix\":" << cursor
            << ",\n  \"recorded_events_captured\":" << events.size()
            << ",\n  \"recorded_events_total\":" << number("events_total")
            << ",\n  \"capture_truncated\":" << (truncated ? "true" : "false")
            << ",\n  \"recorded_error\":" << Json(meta.at("error"))
            << ",\n  \"recorded_pc_valid\":"
            << (meta.contains("attempted_pc_valid") &&
                        meta.at("attempted_pc_valid") == "1"
                    ? "true"
                    : "false")
            << ",\n  \"recorded_pc\":" << number("attempted_pc");
  if (result) {
    std::cout << ",\n  \"interpreter_success\":"
              << (result->success ? "true" : "false")
              << ",\n  \"interpreter_error\":"
              << Json(std::string{result->error})
              << ",\n  \"interpreter_pc\":" << result->pc
              << ",\n  \"interpreter_consumed\":" << result->consumed
              << ",\n  \"interpreter_final_fifo_size\":" << parameters.size()
              << ",\n  \"first_data_ram_difference\":";
    if (difference == ram.size())
      std::cout << "null";
    else
      std::cout << difference;
  }
  std::cout << "\n}\n";
  return status == "match"                 ? 0
         : status == "missing_context"     ? 2
         : status == "inconclusive_budget" ? 3
                                           : 1;
}
} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: mme-failure-replay CAPTURE_PREFIX\n";
    return 4;
  }
  try {
    return Replay(argv[1]);
  } catch (const std::exception &error) {
    std::cout << "{\"status\":\"input_error\",\"detail\":" << Json(error.what())
              << "}\n";
    return 4;
  }
}
