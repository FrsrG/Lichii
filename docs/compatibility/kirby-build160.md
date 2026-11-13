# Kirby Air Riders 1.3.3 on build 160

This branch starts at Lichii `build-160`, commit
`2eb5f081c8734affd272466f89133aa4e3e56e18`.

The requested gameplay scope is Air Ride courses only, in menu order.

## Baseline reproduced

The unmodified Windows executable (SHA-256
`7ecc9e7dddafe30d4a3068fa94631aade875ede73f99de4d6337b5072d422a98`)
crashes before reaching the main menu with Windows exception `0xc00000fd`.
The test used the full title `040077201BE50000`, version 1.3.3, a copied
portable profile, and an initially empty shader cache. Physical controller
bindings were disabled in the test profile to isolate injected game input.

The exception-thread unwind contains 1,615 recursive return frames at
`lichii.exe+0x1ac16be`, with a 0x260-byte stride. The direct self-call belongs
to `CollectIteratorWords` in
`src/shader_recompiler/ir_opt/global_memory_to_storage_buffer_pass.cpp`.
That traversal alone consumes approximately 960 KiB of the executable's
1 MiB default stack reserve. The native dump and disassembly are retained
locally; they are not repository artifacts.

## Diagnostic experiment

A separate executable with only its PE stack reserve increased to 64 MiB
reaches the main menu and starts Floria Fields. This is a diagnostic
experiment, not the proposed compatibility fix or a validated deliverable.
Reversible menu inputs confirm debug-controller control of Player 1.
The first race completed all three laps: Player 1 finished second in
2:53.00. The post-race placement screen, unlock screens, and return to the
Floria Fields course-selection menu were observed. No steering input was
injected during racing. This is a diagnostic completion only; a normal-stack
patched executable and warmed repeat remain unverified.

Later logs show invalid placeholder device addresses beginning at
`0xffffff8120def000` and native MME split-front instruction-limit errors
at macro start `0x1d2`. These are separate unresolved observations; the
stack-reserve experiment does not establish full compatibility.

## Shader traversal fix

`CollectIteratorWords` now uses an explicit DFS frame vector instead of
recursive Tarjan visits. It preserves argument order, child-return lowlinks,
back edges, component collection, and the excluded SelectU32 predicate.
Identity accessors also follow chains iteratively; that change is preventive,
while the captured active crash is specifically the Tarjan traversal.

The focused `shader_stack_tests` target includes deep 32,768-node cyclic
and acyclic graphs, a 65,536-node identity chain, and the existing storage
pointer/control-flow/shared-memory semantic cases. All 32 tests and 199
assertions pass. Restoring only the original graph traversal reproduces
`0xc00000fd` in both the deep-graph test and the exact captured compute
pipeline `630120f92d0a96a9` (64 x 1 x 1 workgroup, 512 bytes shared memory).

The patched compiler replays that pipeline on the normal stack. Its emitted
SPIR-V is byte-identical to the original compiler run with a diagnostic
64 MiB stack reserve (SHA-256
`32456f2c69b2aa903f17845bd25b63975a0e567b363076ef2cbcc1b009bae876`).
The output passes `spirv-val --target-env vulkan1.2`, using official
SPIRV-Tools tag `vulkan-sdk-1.4.341.0`. The replay used the captured v54
pipeline payload through the standalone translator; full emulator startup
and gameplay remain separate pending checks.

## Cold-hint address bounds

Ported cerroverb's focused [PR 1568](https://github.com/zortos293/Lichii/pull/1568)
from commits `05beffd91e1a33677b24bf827a3220254d65536c` and
`d6cbce6a2ec6ede7c52e7e925a2532f3905f2806`. The runtime logged 513
out-of-range page accesses beginning at the same placeholder address cited
by that investigation. `HintCold` now rejects addresses outside its device
address space and clamps ranges before computing their final page.

A local differential probe compiled the actual method body with an
instrumented page-table substitute: the fixed method passes nine boundary
cases; the original passes three and fails six. This verifies page lookup
bounds, not host page-discard behavior. Full build and gameplay verification
of this port are pending.

## Acceptance record

| Check | Result |
| --- | --- |
| Unmodified build-160 startup | Stack overflow reproduced |
| Source traversal regression tests | 32 tests, 199 assertions pass |
| Exact crash-pipeline differential replay | Baseline overflows; fixed passes; SPIR-V identical and valid |
| Cold-hint method boundary probe | Fixed 9/9; baseline 3/9 |
| Patched normal-stack startup | Pending |
| Diagnostic Floria Fields race, result, menu | Completed; macro/render errors remain |
| Patched Floria Fields race, result, menu | Pending |
| Floria Fields warmed repeat | Pending |
| Remaining courses in menu order | Pending |

Only source changes, tests, and summarized verification belong in this
repository. Game files, keys, firmware, saves, native shader captures,
screenshots, and host dumps remain local.
