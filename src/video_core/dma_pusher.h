// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <optional>
#include <span>
#include <vector>
#include <boost/container/small_vector.hpp>
#include <deque>
#include <queue>

#include "common/bit_field.h"
#include "common/common_types.h"
#include "common/scratch_buffer.h"
#include "video_core/engines/engine_interface.h"
#include "video_core/engines/puller.h"

namespace Core {
class System;
}

namespace Tegra {

namespace Control {
struct ChannelState;
}

class GPU;
class MemoryManager;

enum class SubmissionMode : u32 {
    IncreasingOld = 0,
    Increasing = 1,
    NonIncreasingOld = 2,
    NonIncreasing = 3,
    Inline = 4,
    IncreaseOnce = 5
};

// Note that, traditionally, methods are treated as 4-byte addressable locations, and hence
// their numbers are written down multiplied by 4 in Docs. Here we are not multiply by 4.
// So the values you see in docs might be multiplied by 4.
// Register documentation:
// https://github.com/NVIDIA/open-gpu-doc/blob/ab27fc22db5de0d02a4cabe08e555663b62db4d4/classes/host/cla26f.h
//
// Register Description (approx):
// https://github.com/NVIDIA/open-gpu-doc/blob/ab27fc22db5de0d02a4cabe08e555663b62db4d4/manuals/volta/gv100/dev_pbdma.ref.txt
enum class BufferMethods : u32 {
    BindObject = 0x0,
    Illegal = 0x1,
    Nop = 0x2,
    SemaphoreAddressHigh = 0x4,
    SemaphoreAddressLow = 0x5,
    SemaphoreSequencePayload = 0x6,
    SemaphoreOperation = 0x7,
    NonStallInterrupt = 0x8,
    WrcacheFlush = 0x9,
    MemOpA = 0xA,
    MemOpB = 0xB,
    MemOpC = 0xC,
    MemOpD = 0xD,
    RefCnt = 0x14,
    SemaphoreAcquire = 0x1A,
    SemaphoreRelease = 0x1B,
    SyncpointPayload = 0x1C,
    SyncpointOperation = 0x1D,
    WaitForIdle = 0x1E,
    CRCCheck = 0x1F,
    Yield = 0x20,
    NonPullerMethods = 0x40,
};

struct CommandListHeader {
    union {
        u64 raw;
        BitField<0, 40, GPUVAddr> addr;
        BitField<40, 1, u64> allow_flush;
        BitField<41, 1, u64> is_push_buffer;
        BitField<42, 21, u64> size;
        BitField<63, 1, u64> sync;
    };
};
static_assert(sizeof(CommandListHeader) == sizeof(u64), "CommandListHeader is incorrect size");

union CommandHeader {
    u32 argument;
    BitField<0, 13, u32> method;
    BitField<0, 24, u32> method_count_;
    BitField<13, 3, u32> subchannel;
    BitField<16, 13, u32> arg_count;
    BitField<16, 13, u32> method_count;
    BitField<29, 3, SubmissionMode> mode;
};
static_assert(std::is_standard_layout_v<CommandHeader>, "CommandHeader is not standard layout");
static_assert(sizeof(CommandHeader) == sizeof(u32), "CommandHeader has incorrect size!");

// Diagnostic context: spans borrow the already-read entry and its current mutable snapshot.
struct DmaMethodHeaderOrigin {
    bool known{};
    bool address_known{};
    GPUVAddr address{};
    u32 word{};
    size_t word_offset{};
    u64 parse_id{};
    const char* read_path{"unknown"};
    u64 entry_raw{};
    GPUVAddr entry_address{};
    size_t entry_index{};
    size_t entry_words{};
};

struct DmaReadOriginRange {
    size_t begin{};
    size_t end{};
    const char* path{"unknown"};
};

struct DmaParsedOrigin {
    bool entry_known{};
    u64 entry_raw{};
    GPUVAddr entry_address{};
    size_t entry_words{};
    size_t entry_index{};
    u32 channel{};
    const char* read_path{"unknown"};
    bool gpu_written{};
    size_t resume_offset{};
    u64 snapshot_suspensions{};
    u64 current_suspensions{};
    bool snapshot_deferred{};
    u64 parse_id{};
    std::span<const CommandHeader> words;
    std::span<const CommandHeader> snapshot;
    std::span<const DmaReadOriginRange> read_ranges;
};

struct DmaCommandOrigin {
    static constexpr size_t MaxWords = 64;
    static constexpr size_t NeighborWords = 8;
    DmaParsedOrigin entry;
    DmaMethodHeaderOrigin header;
    size_t word_offset{};
    u32 method{};
    u32 subchannel{};
    u32 methods_remaining{};
    size_t segment_words{};
    bool inline_argument{};
    bool source_address_known{};
    GPUVAddr source_address{};
    size_t copied_words{};
    std::array<u32, MaxWords> raw_words{};
    std::array<u32, MaxWords> snapshot_words{};
    std::array<bool, MaxWords> raw_known{};
    std::array<bool, MaxWords> snapshot_known{};
    std::array<const char*, MaxWords> read_paths{};
    size_t neighbor_begin{};
    size_t neighbor_count{};
    std::array<u32, NeighborWords> neighbors{};
    size_t header_neighbor_begin{};
    size_t header_neighbor_count{};
    std::array<u32, NeighborWords> header_neighbors{};
};

inline DmaCommandOrigin BuildDmaCommandOrigin(const DmaParsedOrigin& entry,
                                            const DmaMethodHeaderOrigin& header,
                                            size_t word_offset, size_t count, u32 method,
                                            u32 subchannel, u32 remaining, bool inline_argument) {
    DmaCommandOrigin result;
    result.entry = entry;
    result.header = header;
    result.word_offset = word_offset;
    result.method = method;
    result.subchannel = subchannel;
    result.methods_remaining = remaining;
    result.segment_words = count;
    result.inline_argument = inline_argument;
    result.source_address_known = entry.entry_known && word_offset < entry.words.size();
    if (result.source_address_known) result.source_address = entry.entry_address + word_offset * 4;
    result.copied_words = (std::min)(count, DmaCommandOrigin::MaxWords);
    for (size_t i = 0; i < result.copied_words; ++i) {
        const size_t offset = word_offset + (inline_argument ? 0 : i);
        result.read_paths[i] = entry.read_path;
        if (!entry.read_ranges.empty()) {
            result.read_paths[i] = "unknown-read-range-cap";
            for (const auto& range : entry.read_ranges)
                if (offset >= range.begin && offset < range.end) {
                    result.read_paths[i] = range.path;
                    break;
                }
        }
        if (offset < entry.words.size()) {
            result.raw_words[i] = entry.words[offset].argument;
            result.raw_known[i] = true;
        }
        if (offset < entry.snapshot.size()) {
            result.snapshot_words[i] = entry.snapshot[offset].argument;
            result.snapshot_known[i] = true;
        }
    }
    const auto neighbors = [&](size_t at, size_t& begin, size_t& size, auto& words) {
        if (at >= entry.words.size()) return;
        begin = at > 3 ? at - 3 : 0;
        size = (std::min)(DmaCommandOrigin::NeighborWords, entry.words.size() - begin);
        for (size_t i = 0; i < size; ++i) words[i] = entry.words[begin + i].argument;
    };
    neighbors(word_offset, result.neighbor_begin, result.neighbor_count, result.neighbors);
    // A header retained across an entry/resume has no borrowed old span: its raw word remains
    // known, but never label a later ring reuse as the window that originally supplied it.
    if (header.known && header.parse_id == entry.parse_id) {
        neighbors(header.word_offset, result.header_neighbor_begin,
                  result.header_neighbor_count, result.header_neighbors);
    }
    result.entry.words = {};
    result.entry.snapshot = {};
    result.entry.read_ranges = {};
    return result;
}

inline CommandHeader BuildCommandHeader(BufferMethods method, u32 arg_count, SubmissionMode mode) {
    CommandHeader result{};
    result.method.Assign(static_cast<u32>(method));
    result.arg_count.Assign(arg_count);
    result.mode.Assign(mode);
    return result;
}

struct CommandList final {
    CommandList() = default;
    explicit CommandList(std::size_t size) : command_lists(size) {}
    explicit CommandList(boost::container::small_vector<CommandHeader, 512>&& prefetch_command_list_)
        : prefetch_command_list{std::move(prefetch_command_list_)} {}

    boost::container::small_vector<CommandListHeader, 512> command_lists;
    boost::container::small_vector<CommandHeader, 512> prefetch_command_list;
    /// EDEN_NX2_PB_CHECK: hash of each entry's words when it was submitted.
    std::vector<u64> submit_hashes;
    /// The entries' words when they were submitted (NX2), each entry's at submitted_offsets[i]:
    /// executed instead of the memory, which the game may already be refilling for a later frame.
    std::vector<CommandHeader> submitted_words;
    std::vector<u32> submitted_offsets;
    /// Entries (ascending) not copied when submitted, because a GPU map still queued covers them:
    /// DmaPusher::Push copies them after the map, and until then they read memory.
    std::vector<u32> deferred_entries;
    /// DmaPusher::suspensions when the words were copied. A list copied during a suspension, or
    /// that waited behind one, reads memory instead: games write the commands it runs before they
    /// release the acquire.
    u64 snapshot_suspensions = 0;
};

/// FNV-1a over pushbuffer words (EDEN_NX2_PB_CHECK).
inline u64 HashPushbufferWords(std::span<const u32> words) {
    u64 hash = 0xcbf29ce484222325ULL;
    for (const u32 word : words) {
        hash = (hash ^ word) * 0x100000001b3ULL;
    }
    return hash;
}

/// @brief The DmaPusher class implements DMA submission to FIFOs, providing an area of memory that the
/// emulated app fills with commands and tells PFIFO to process. The pushbuffers are then assembled
/// into a "command stream" consisting of 32-bit words that make up "commands".
/// See https://envytools.readthedocs.io/en/latest/hw/fifo/dma-pusher.html#fifo-dma-pusher for
/// details on this implementation.
class DmaPusher final {
public:
    /// Counts suspensions on a semaphore acquire and the resumes after them, so it is odd while
    /// the channel is suspended (see CommandList::snapshot_suspensions).
    std::atomic<u64> suspensions{0};

    explicit DmaPusher(Core::System& system_, MemoryManager& memory_manager_, Control::ChannelState& channel_state_);
    ~DmaPusher();

    void Push(CommandList&& entries) {
        // Copied during a suspension, or behind one: copy again now, unless still suspended.
        const u64 now = suspensions.load(std::memory_order_acquire);
        if (!entries.submitted_offsets.empty() && (now & 1) == 0) {
            if (ResnapshotEnabled() && entries.snapshot_suspensions != now) {
                RefreshSnapshot(entries, now);
            } else if (!entries.deferred_entries.empty()) {
                // Behind a GPU map when submitted: the map has run by now (GPU::MapInOrder).
                RefreshDeferredEntries(entries);
            }
        }
        dma_pushbuffer.push_back(std::move(entries));
    }

    void DispatchCalls();

    /// Switch 2: true while the channel is suspended, on a semaphore acquire or on the GPU's copy
    /// of a command list it reads next.
    [[nodiscard]] bool HasPendingAcquire() const {
        return puller.HasPendingAcquire() || pending_list_tick.has_value();
    }
    [[nodiscard]] bool CanResumeAcquire();

    void BindSubchannel(Engines::EngineInterface* engine, u32 subchannel_id, Engines::EngineTypes engine_type) {
        subchannels[subchannel_id] = engine;
        subchannel_type[subchannel_id] = engine_type;
    }

    void BindRasterizer(VideoCore::RasterizerInterface* rasterizer);

private:
    friend struct DmaPusherTestAccess;

    static bool IsMemoryWriter(const Engines::EngineInterface& engine, Engines::EngineTypes type,
                               u32 method);

    static constexpr u32 non_puller_methods = 0x40;
    static constexpr u32 max_subchannels = 8;
    bool Step();
    /// Returns how many words were processed: fewer than given when a semaphore acquire
    /// suspended the channel.
    size_t ProcessCommands(std::span<const CommandHeader> commands, size_t word_offset = 0);
    size_t command_resume_offset{}; ///< Words of the current entry done before a suspension

    void SetState(const CommandHeader& command_header, size_t word_offset = 0);
    void NoteMethodHeader(const CommandHeader& command_header, size_t word_offset);
    void BeginOriginParse(const CommandListHeader* header, std::span<const CommandHeader> words,
                          const CommandList& list, const char* path, bool gpu_written = false);

    void CallMethod(u32 argument);
    void CallMultiMethod(const u32* base_start, u32 num_methods);
    [[nodiscard]] bool WantsSegment() const;
    void ReportUnboundSubchannel();
    void WatchArgument(u32 method, u32 argument) const;

public:
    /// Logs the GPFIFO entry being processed (and the one before it), read through the GPU MMU
    /// and straight from guest memory, to find pushbuffer desyncs. Debug aid for Switch 2.
    void DumpCurrentEntry(const char* reason);
    void CheckSubmitHash(const CommandListHeader& header, u64 submit_hash);
    void RecordParsedEntry(const CommandListHeader& header, std::span<const CommandHeader> commands);
    /// Whether the entry lies on device pages the GPU has written (NX2).
    bool IsGpuWrittenCommands(GPUVAddr address, size_t size);
    /// A copy engine method sent to the split's back: a launch that releases its semaphore tells
    /// the front what it writes (GpuSplit::NoteSemaphoreWrite).
    void NoteDmaSemaphore(u32 method, u32 argument);
    /// Copies the list's words from memory again, as of suspension count `now`.
    void RefreshSnapshot(CommandList& list, u64 now);
    /// Copies the list's deferred entries (CommandList::deferred_entries) from memory.
    void RefreshDeferredEntries(CommandList& list);

    /// EDEN_NX2_RESNAPSHOT=0 turns snapshot refreshes off.
    static bool ResnapshotEnabled() {
        static const bool enabled = [] {
            const char* value = std::getenv("EDEN_NX2_RESNAPSHOT");
            return value == nullptr || value[0] != '0';
        }();
        return enabled;
    }
    /// Reads a command list on pages the GPU wrote into command_headers, from the copies the
    /// buffer cache made after the work that wrote them or from guest memory for pages flushed
    /// since. False when not all are current (or the list isn't GPU-written): the caller drains,
    /// flushes and reads guest memory. With `pending`, a copy the GPU has not finished sets
    /// `*pending` and pending_list_tick instead of waiting (the result is false then).
    bool ReadGpuWrittenCommands(GPUVAddr address, u32 count, bool* pending = nullptr);
    /// Brings command list words the GPU wrote (still only in host buffers) to guest memory.
    bool FlushGpuWrittenCommands(GPUVAddr address, size_t size);

private:

public:
    Common::ScratchBuffer<CommandHeader> command_headers; ///< Buffer for list of commands fetched at once

    std::deque<CommandList> dma_pushbuffer; ///< Queue of command lists to be processed
    std::size_t dma_pushbuffer_subindex{};  ///< Index within a command list within the pushbuffer

    struct DmaState {
        u32 method;            ///< Current method
        u32 subchannel;        ///< Current subchannel
        u32 method_count;      ///< Current method count
        u32 length_pending;    ///< Large NI command length pending
        GPUVAddr dma_get;      ///< Currently read segment
        u64 dma_word_offset;   ///< Current word offset from address
        bool non_incrementing; ///< Current command's NI flag
        bool is_last_call;
    };

    Core::System& system;
    MemoryManager& memory_manager;
    Control::ChannelState& channel_state;

    DmaState dma_state{};
    DmaParsedOrigin origin_parse;
    DmaMethodHeaderOrigin origin_header;
    u64 origin_parse_id{};
    bool origin_inline_argument{};
    std::array<DmaReadOriginRange, 64> origin_read_ranges{};
    size_t origin_read_range_count{};

    // Switch 2: methods whose data continues in a later GPFIFO entry, set aside while other
    // command buffers run (NVN interleaves several, so more than one can be pending).
    struct ParkedMethod {
        u32 method;
        u32 subchannel;
        u32 method_count;
        bool non_incrementing;
        bool increment_once;
        GPUVAddr resume_address; ///< Where the entry that continues the method starts
        DmaMethodHeaderOrigin header_origin;
    };
    void ResolveSplitMethod(GPUVAddr entry_address);
    static constexpr size_t MaxParkedMethods = 16;

    // Read per channel, not at static initialization, which runs before the frontend loads
    // nx2_switches.txt.
    // EDEN_NX2_SEM_WATCH=<gpu address>: log every method whose argument is the low word of an
    // address in the 16 bytes there (finds the engine and method that release a semaphore).
    const u32 semaphore_watch = [] {
        const char* value = std::getenv("EDEN_NX2_SEM_WATCH");
        return value ? static_cast<u32>(std::strtoull(value, nullptr, 16)) : 0U;
    }();
    // EDEN_NX2_PB_CHECK also keeps the last parsed entries, for DumpCurrentEntry.
    const bool record_parsed_entries = std::getenv("EDEN_NX2_PB_CHECK") != nullptr;

    // Debug aid: hashes of the words of the last entries as they were parsed.
    struct ParsedEntry {
        GPUVAddr address;
        u32 size;
        u64 hash;
    };
    std::array<ParsedEntry, 64> parsed_entries{};
    size_t parsed_entry_count{};
    std::vector<ParkedMethod> parked_methods;
    GPUVAddr split_method_end{}; ///< End of the last entry that ended inside a method

    std::array<Engines::EngineInterface*, max_subchannels> subchannels{};
    std::array<Engines::EngineTypes, max_subchannels> subchannel_type;
    /// The copy engine's semaphore address (high, low) and payload as sent (NoteDmaSemaphore).
    std::array<u32, 3> dma_semaphore{};

    Engines::Puller puller;
    std::mutex sync_mutex;
    std::condition_variable sync_cv;

    VideoCore::RasterizerInterface* rasterizer = nullptr;
    /// Host GPU tick of the command list copy this channel waits for (ReadGpuWrittenCommands).
    std::optional<u64> pending_list_tick;
    /// The entry parsed next was suspended on pending_list_tick, which is done now.
    bool resumed_list_read{};

    // Not bitfields: the fence thread sets synced, and a bitfield write would rewrite the byte it
    // shares with dma_increment_once and signal_sync (#479).
    const bool ib_enable = true; ///< IB mode enabled
    bool dma_increment_once = false;
    bool signal_sync = false;
    std::atomic<bool> synced{false};
};

} // namespace Tegra
