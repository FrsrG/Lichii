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

## First source-fixed runtime

The Windows GUI built successfully from clean commit
`8f739adeebbaedad4c26600edf988c018a23b558`; executable SHA-256
`0c912c9da9b42fe9fc17cb591c65be8424c2ab2ef9b431e53ed77a8a743ac1f6`.
Its stack reserve remains 1 MiB. Stack and shader-translation CTest targets
pass, and package import checks report no missing DLLs. A broader local
comparison of 21 captured pipelines also produces identical output; all
62 emitted SPIR-V modules validate for Vulkan 1.2.

Starting with an empty shader cache, this executable compiles the exact
formerly crashing pipeline and reaches the main menu. Down/Up control was
visually confirmed. Floria Fields course selection, rider selection and
the player/CPU roster load, but rider and roster UI is missing in this
first cold-cache run. Back navigation subsequently reaches a persistent
black screen. No race was started or counted as completed on this binary.
The next controlled test uses blocking native pipeline waits; the cause
of the missing UI is not established.

## TEX register shadows

A separate startup-attract run exposed four native compute shader rejections
at TEX instructions using high registers. Register-pressure handling omitted
TEX operand spans, including the secondary destination and a high Rb source.
The fix derives spans from the existing TEX packing rules and preserves
coordinate inputs, split/half outputs and uniform mirrors. Four captured raw
programs now lower completely instead of rejecting.

The focused suite passes 36 tests and 265 assertions, including register-bank
state checks and packing/boundary cases. Replaying those four full programs
still needs uncaptured storage-pointer context; no actual-capture SPIR-V
validation is claimed for them. The native cache lowering stamp automatically
changes from `a50b9d25fc7212aa` to `0764a4eac4ed9238`, invalidating stale
lowered records. This fixes observed attract-mode shader rejections; it has
not been established as the cause of the Air Ride macro/device-loss failure.

## Current race investigation

The first controlled source-fixed Floria Fields race reached lap 1 with a
visible timer, then lost the GPU. The dump shows a downstream Vulkan
device-loss exception while a CPU memory read waits for GPU downloads, not
the original compiler stack overflow. Native macro instruction-limit errors
precede the loss; the dump alone does not establish causality.

A bounded opt-in diagnostic capture reproduced the macro's behavior exactly
offline. Both JIT and interpreter receive float-looking values as original
draw parameters, including a multi-billion instance count. Increasing the
execution limit or interpreter fallback does not correct those inputs.
Immediate inline invalidation did not resolve this symptom. A separate
draw-written-page drain test also retained the errors, but its diagnostics
showed only compute/copy-written list pages. The remaining work is to trace
the parameter bytes to their command entry and correct the failing path.

## Bounded command-origin diagnostic

`EDEN_NX2_MME_FAILURE_DUMP=<local-directory>` enables a bounded diagnostic,
not a recovery path. A first observed failure arms a following matching macro
invocation. Successful captures release their temporary state and keep waiting;
a subsequent actual failure can retarget the armed engine/start/code generation.
Only one capped code/FIFO/RAM/read/emit/DMA trace is retained at a time, and the
first captured failure saves once and permanently disarms. Captured JIT state
includes the actual failure PC.
Trace completeness flags remain explicit when output exceeds the caps.

The armed capture also records each argument's command-entry and accepted
header context, GPU address when known, already-read word/neighbors and read
path. Continued/parked methods retain header scalar context without retaining
borrowed spans. Mutable submit snapshots are labeled current, not original.
Unknown or capped provenance is not inferred. Synthetic loop/DMA capture
checks and 19 bounded origin checks pass; a real capture reproduced the JIT
state and input-driven loop offline. The new real command-origin fields still
need gameplay capture validation.

The separate diagnostic switch `EDEN_NX2_LIST_FORCE_FLUSH=1` bypasses staged
GPU-written-list reads and uses the existing drain/flush fallback. It changes
timing and is reserved for an evidence-directed comparison. Both diagnostics
are off by default; no execution budget, interpreter semantics or gameplay
parameters are changed.

## Batched CPU-write protection

Ported AmateurWizrd's original commit
[`555077432d4552e931b0b3825e851409bbd214f6`](https://github.com/atmanli/Lichii/commit/555077432d4552e931b0b3825e851409bbd214f6),
subsequently merged upstream through PR 1460. The batch invalidation path
claims all CPU-written ranges before a download for one range can overwrite
another CPU write on the same page.

The actual BufferCache regression reproduces the old caller ordering's
second-write corruption (`18 == 187`); the fixed ordering passes all 14
assertions. This verifies the cache fixture, not an end-to-end Rasterizer
or Kirby result. The next controlled race tests whether this confirmed
data-integrity fix resolves or contributes to the progression blocker.

The next candidate (run 7, source `a9df95e`) still failed in Floria Fields:
MME start `0x1d2` at 170.125 seconds, then repeated `0x1e7` failures from
173.883 seconds, followed by device loss around 192.44 seconds. No origin
capture was saved: the old diagnostic stopped after eight successful `0x1d2`
invocations at 170.145 seconds. That cutoff and its inability to retarget
could miss intermittent failures. The diagnostic now keeps waiting with bounded
live memory and allows the later actual failure to retarget it. Successful
matching calls continue to incur opt-in snapshot cost until capture; no trace
history or output files accumulate for them.

The standalone `tools/nx2/mme_capture_gate_probe.cpp` regression exercises 32
successful captures without output, target/engine/generation retargeting,
one failed save, and terminal disarming. It uses synthetic terminal results
with the actual capture implementation; it does not execute GPU commands.
Actual JIT backward-loop, FIFO/DMA replay, trace-cap, and default-off probes
also pass. The next run must still establish real origin completeness.

## Earlier GPU loss without logged macro limits

A controlled Floria Fields run on `ea02ae0` reached the visible race HUD,
then lost the GPU at 110.714668 seconds, before any logged native macro
instruction-limit failure. There were no native shader rejections and no
armed macro capture. This establishes that logged macro-limit failures are
not necessary for the observed GPU loss; it does not validate every draw
argument or establish separate root causes.

The matching host dump again observes `VK_ERROR_DEVICE_LOST` during a CPU
memory read waiting for buffer downloads. The device reports 47 unknown
instruction pointers, no explicit invalid-access address and no vendor
record. Three earlier unmapped buffer-cache reads remain unproven as a
cause. Repeating the macro-only capture unchanged would not address this
earlier failure. The next diagnostic uses process-local Khronos GPU-assisted
validation to seek a concrete shader/resource/access error. No source-fixed
full race is counted as passed.

## Instrumented texture-comparison report

A process-local Khronos GPU-assisted validation run, with disk shader cache
loading disabled to limit instrumentation work, reports
`VUID-vkCmdDrawIndexed-None-06479`: fragment sampler binding 42, array index
0, samples an `A8B8G8R8_UNORM_PACK32` color view using
`OpImageSampleDrefImplicitLod`. The format lacks sampled depth-comparison
support. The shader variable `fs_tex5_00x1024` identifies constant-buffer 5,
byte offset 0, array count 1024; the suffix is not a byte offset.

The instrumented process later terminates with an access violation inside
the validation layer on its Vulkan submission worker, after the layer
reports a failed fence wait. No application device-loss or macro-limit
message was logged. This termination is distinct from the ordinary run's
application-side device-loss exception. The report is a concrete binding
lead, not yet a confirmed explanation or fix for that loss. The exact
fragment hash and bound handle/view context remain to be captured.

Native fragment captures explicitly request comparison sampling; the TEX
shadow fix does not modify comparison flags or these captured comparison
instructions' low-register operands. RTX 5070 advertises null descriptors,
and the device feature path preserves them, so a nonnull color view is
unlikely to be the fallback constructed for devices without null-descriptor
support. No blanket texture substitution is justified by this evidence.

The portable title configuration has been restored byte-for-byte after
the diagnostic. Original installation/profile files remain untouched.
Each course must first finish without a crash, then any performance below
60 FPS is investigated before advancing to the next course.

## Opt-in invalid depth-sample workaround

GPU-assisted validation in run 10 reported
`VUID-vkCmdDrawIndexed-None-06479`: fragment shader descriptor
`fs_tex5_00x1024`, set 0/binding 42/array element 0, performed
`OpImageSampleDrefImplicitLod` on a nonnull A8B8G8R8_UNORM color view.
The name means constant buffer 5, byte offset 0, array count 1024. The
validation layer later failed waiting for a fence and crashed; this does not
establish that the descriptor mismatch caused the original device loss.

`EDEN_NX2_NULL_INVALID_DEPTH=1` is a default-off diagnostic workaround. On a
device supporting null descriptors, the descriptor upload substitutes a null
image handle only for a nonnull, non-depth image view referenced by a depth
comparison descriptor. It preserves the sampler, descriptor layout, valid
depth/depth-stencil samples, ordinary color samples, and unsupported devices.
The first 16 substitutions log image/view IDs, guest GPU address, format,
constant-buffer index/offset and array index, without additional guest reads.

This deliberately removes invalid samples and may omit intended visual content;
it is not a proven guest-semantic correction. It does not allocate a fallback
depth texture, implement software comparison, change Vulkan features, or raise
the MME execution budget. Both affected production translation units pass
syntax checks. No existing descriptor-queue fixture covers this path; the
targeted full-race runtime comparison is the required behavioral validation.

## Acceptance record

| Check | Result |
| --- | --- |
| Unmodified build-160 startup | Stack overflow reproduced |
| Source traversal regression tests | 32 tests, 199 assertions pass |
| Exact crash-pipeline differential replay | Baseline overflows; fixed passes; SPIR-V identical and valid |
| Cold-hint method boundary probe | Fixed 9/9; baseline 3/9 |
| Patched normal-stack, empty-cache startup | Exact shader compiles; main menu and control confirmed |
| Diagnostic Floria Fields race, result, menu | Completed; macro/render errors remain |
| Patched Floria Fields race, result, menu | Pending |
| Floria Fields warmed repeat | Pending |
| Remaining courses in menu order | Pending |

Only source changes, tests, and summarized verification belong in this
repository. Game files, keys, firmware, saves, native shader captures,
screenshots, and host dumps remain local.
