# One split-front MME failure snapshot

The split front calls the native JIT directly, bypassing `MacroEngine::Execute` and its
`EDEN_NX2_MME_RECORD` hooks. This diagnostic observes that actual call. It does not retry it,
increase its budget, drain the GPU, or read memory a second time.

Before launching a diagnostic candidate, set a local output prefix whose parent directory
already exists:

```powershell
$env:EDEN_NX2_MME_FRONT_FAILURE = 'C:/local-investigation/mme-failure'
$env:EDEN_NX2_MME_FRONT_FAILURE_START = '0x1d2'
```

The start filter defaults to `0x1d2`; an invalid explicit filter disables capture. Only matching
calls collect PRE state and callback data. A successful call writes nothing and the next call
reuses the allocations. The first matching failure writes one timestamp-suffixed set of files
and disarms capture for the process, including if writing fails. Leave both variables unset for
normal use. Captures can contain game code and data: keep them local.

The files use the native byte order stated in `.txt`; Windows x86-64 uses little endian. There
are no binary headers or host pointers. Each suffix contains:

| Suffix | Element type | Contents |
|---|---|---|
| `.code` | u32 | All 0x8000 uploaded macro RAM words, 128 KiB, before the call |
| `.positions` | u32 | Complete method-to-entry position table before the call |
| `.parameters` | u32 | Original FIFO parameters, up to 4096; original count is in `.txt` |
| `.dram-before`, `.dram-after` | u32 | Complete 0xc00-word MME data RAM before/after execution |
| `.front-regs`, `.front-shadow` | u32 | PRE front register and shadow arrays |
| `.back-owned` | u32 | PRE ownership flags, one 0/1 word per register |
| `.back-regs`, `.back-shadow` | u64 | PRE pending back positions for each register/shadow entry |
| `.front-flags` | u32 | PRE control, draw_pending, sink_pending, split_start, skip_same, instance_loop, back_code_copied, back_escapes_seen |
| `.front-counters` | u64 | PRE calls, back_generation, back_last, back_escapes |
| `.events` | record | Ordered actual register-read, emit-attempt and DMA events, at most 4096 |
| `.dma-words` | u32 | Actual words from the normal DMA read loop, at most 65536 |
| `.jit-state` | u32 | 38 initialized post-call JIT state words; empty on an interpreter path |
| `.txt` | text | Version, start/method, generations, FIFO sizes, result, attempted PC, totals, captured/dropped counts and completeness |

An event is six u32 words followed by one u64, exactly 32 bytes:
`kind,address,value,count,word_offset,word_count,source`. Kind 1 records a register address and
the actual returned value. Kind 2 records an attempted emitted method/value; `count` is 1 if its
method was accepted and 0 if rejected. Kind 3 records a DMA: `address` is method 0x157/0x158,
`value` is the destination data-RAM index, `count` is the actual bounded word count, `source` is
the actual GPU address, and `word_offset/word_count` select its captured `.dma-words` prefix.
DMA words omitted because either cap was reached are included in the dropped count.

The 38 `.jit-state` words are registers R0-R23 followed by carry, method, increment, loop_count,
loop_start, loop_end, remaining dispatch budget, end_next, stopped, cancel_end, start, consumed,
final FIFO size, and failure PC. For a step-limit failure, the PC is the attempted next native
instruction at the dispatch that exhausted the original budget. No instruction history is added.
Other JIT errors retain the old unspecified PC; `.txt` marks that PC invalid instead of claiming
it is the attempted instruction.

PRE back-owned register copies may be stale. The ordered actual callback returns are authoritative;
capture does not synchronize them early. A truncated parameter/event/DMA trace is incomplete for
deterministic replay. A changed code generation is separately visible. Even a complete callback
trace does not establish correct GPU output or timing, and collection itself can perturb timing.

For offline diagnosis, restore the PRE code, original parameters and data RAM, supply the captured
ordered reads/DMA values, and compare JIT and interpreter termination, ordered writes and final
data RAM. Do not replay effects into the live emulator. Their dispatch/instruction budgets count
different work; a higher budget or a blind retry is not a control-flow fix.

## Offline interpreter comparison

`tools/nx2/mme_failure_replay.cpp` uses the actual production `TuringMacroInterpreter` header
and only the C++ standard library. In a Visual Studio C++ developer shell, from the repository:

```powershell
cl /nologo /std:c++20 /EHsc /O2 /W4 /WX /Isrc tools/nx2/mme_failure_replay.cpp /Fe:mme-failure-replay.exe
./mme-failure-replay.exe 'C:/local-investigation/mme-failure-TIMESTAMP'
python tools/nx2/test_mme_failure_replay.py ./mme-failure-replay.exe
```

Pass the common capture prefix, without a suffix. The helper restores original code, parameters
and data RAM; checks ordered read/emit callbacks against their recorded addresses/values;
applies only the captured actual DMA words to FIFO/data RAM; and compares completed data RAM,
FIFO and termination. It never accesses a GPU, injects effects into an emulator, or fills missing
reads with zero. Changed code generation, truncated original parameters and exhausted observations
are explicit context gaps. Final data RAM is compared only when the interpreter returns.

Stdout is JSON. Exit codes are 0 for a complete observation match, 1 for a mismatch, 2 for missing
context, 3 for an inconclusive budget comparison, and 4 for invalid input. `matched_event_prefix`
locates the first divergent or unavailable observation. An interpreter error or early termination
can expose a mismatch before a cap, but a matching truncated prefix never passes. Even matching
step-limit errors cannot prove equivalent termination because the budgets count different work.
The recorded JIT trace is the reference; this helper does not compile a substitute JIT configuration.
