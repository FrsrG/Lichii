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

Armed origins also translate each known selected argument address through the
current GMMU into `source_device_address`, with unmapped/unknown values null.
This is a DAddr device-memory offset in the same domain as BufferBase::CpuAddr
and Vulkan object names `Buffer 0x...`; it is neither a host pointer, guest CPU
virtual address, nor Vulkan buffer device address. `address_space_id` is the
existing MemoryManager identity. Translation occurs only while copying the
bounded armed origin, without guest reads; it is not a historical mapping epoch
and cannot prove the mapping remained unchanged since an earlier entry read.

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
The current acceptance gate is a complete crash-free race at at least 30 FPS
at normal game speed, followed by usable course selection, before advancing
to the next Air Ride course in menu order. This supersedes the earlier 60 FPS
target. Non-critical bugs are recorded for later work.

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

## Floria command-source control and post-race trajectory

On `2574a43`, the null-depth workaround alone still loses the GPU. Its
complete macro-origin capture identifies an accepted six-word packet:
one argument comes from a submission snapshot, and the next five are
fetched from a copied command-list page. Those five actual values are
float-like position data, unlike the current mutable snapshot. Offline
replay reproduces the input-driven macro failure; the parser delivered
the fetched words faithfully.

Adding only `EDEN_NX2_LIST_FORCE_FLUSH=1` changes those continuation reads
to `slow-guest-memory`, yet malformed parameters and macro failures remain.
A defect confined to the staging-copy path therefore cannot explain them.
The GPU-written provenance flag means a 4 KiB page was written sometime in
the session; it does not prove a current producer for these argument bytes.
The flush also gates actual GPU readback on current cache dirty ranges.

This control run reaches the orange Kirby post-race pose and subsequent
selection transitions, then closes cleanly: no device loss during gameplay
or shutdown, 649 macro-limit failures, zero native shader rejections.
The HUD and menu/result text disappear, so placement, finish time and a
usable post-race menu are not verified. Point FPS samples of 44.9 and 56.6
are observations, not a steady performance benchmark. This is a partial
progression milestone, not a playable-build or 60 FPS acceptance pass.

Both captured failures have equal zero semaphore-suspension counters,
excluding semaphore-driven snapshot refresh for these calls. An earlier
map-deferred fill remains possible. Submission does not record writer,
host-write or mapping generations, so the existing evidence cannot yet
distinguish CPU ring reuse from genuinely GPU-produced argument data.

## Diagnostic draw-continuation snapshot selection

Run 11 captured a valid six-argument `0xe50` header crossing into a five-word
GPU-written continuation. Those fetched words contained float-looking draw
counts while the current submission snapshot held small integers. Run 12's
`LIST_FORCE_FLUSH=1` control changed the exact path from copied-list memory to
slow guest memory and still captured malformed counts. It completed a post-race
selection trajectory without observed device loss, but HUD/text remained
missing and MME limits persisted. The slow path can perform no GPU readback
when current dirty bookkeeping selects no ranges; it is not unconditional
proof of fresh GPU bytes.

`EDEN_NX2_DRAW_CONTINUATION_SNAPSHOT=1` provides a narrower cause-discriminating
A/B. It applies only to Kirby's active split-front native Maxwell macros mapped
to the captured starts `0x1d2` or `0x1e7`, where a whole entry exactly supplies
the already-active packet's remaining one to eight arguments. It requires a
present, nondeferred, bounds-checked snapshot with the same even suspension
count, no entry resume, and normal DMA. Only the historical ever-GPU-written
exclusion is overridden. Every other entry keeps the existing selection rules.

This is default-off and not a proven coherency correction: snapshots can be
genuine pre-producer placeholders. A failed or missing draw under this A/B is
evidence against choosing them, not justification for fabricating counts.
Origins use `diagnostic-native-draw-current-snapshot`, and the first 16 selected
continuations log their macro, entry and address. A result restoring HUD and
draw behavior would implicate late continuation reads/ring reuse; a clean full
race and warmed repeat remain required runtime checks.

## Snapshot comparison and bounded GPU capture

The opt-in draw-continuation snapshot comparison removes the observed
macro-limit failures in its test, but the race HUD still disappears during
ordinary racing. This is not a playability fix and does not establish that
submission snapshots are final GPU argument data. The comparison stays off.

A single RenderDoc 1.47 first-HUD frame was captured and replayed offline
after the emulator exited. Mapped argument provenance identifies the exact
containing 17 MiB Vulkan buffer and 16-byte offset. Initial and final bytes
match, and all 454 distinct write-capable events in that frame leave them
unchanged. Its later failed invocation's different bytes are not observed
in the captured frame. Thus this capture cannot identify the later writer
or establish who corrupted the arguments. Resource overlap or RW binding
alone is not evidence of an actual write.

The raw capture, shader/resource data and replay observations stay local.
Only summarized evidence is published. The portable global debug setting
used for resource names was restored exactly after capture. The next normal
control disables disk-cache loading, testing persisted captured-environment
behavior without either RenderDoc or GPU-assisted validation.

## Retained list copies and delivered host-write notifications

The command-list copy cache can outlive registration of its source host buffer.
Previously, `CachedWriteMemory` and `OnCPUWrite` returned early for an
unregistered range without invalidating a retained copy. A later list read could
therefore select that older completed GPU copy over newly written guest bytes.
Both unregistered branches now notify the existing host-write epoch helper;
registered routes retain their existing single notification.

The actual BufferCache fixture reproduces both unregistered failures: an old
completed copy remains `Copied` after a host write. The correction makes that
copy `Unknown`, preserves the new guest packet, and permits a subsequent GPU
write/copy generation to publish new data. Registered and unregistered variants
also check one epoch increment, and the helper reports only the first
invalidation of a current copy generation.

`EDEN_NX2_LIST_HOST_WRITE_DIAG=1` logs the first 16 actual current-generation
copy invalidations delivered through these unregistered branches, including
API, DAddr and size. It does not read guest data, free in-flight copies, or
change historical page-bitmap policy. Coverage is limited to delivered
notifications: `CachedWriteMemory` has no current source callers, and retirement
may remove CPU tracking so that ordinary later stores produce no callback.
Runtime relevance to Kirby requires observing a relevant invalidation; the
unit regression alone establishes neither reachability nor gameplay causality.

The normal copied-path control produced another complete failed-macro origin
trace, but no unregistered current-copy invalidation diagnostic during the run.
It therefore does not establish that this notification correction reaches the
observed gameplay failure.

## One-shot pre-macro raw buffer comparison

`EDEN_NX2_PRE_MME_BUFFER_PROBE=1` additionally requires an already armed matching
failure capture. It selects only the complete known draw-macro body fingerprint,
matching owner/generation, exact 5/6-word FIFO, a diagnostically large count, and
complete contiguous body origins. The count threshold is a probe selector and
does not validate, reject or replace guest parameters.

One owned request per process freezes at most 64 bytes of selected arguments,
their GPUVA/DAddr/address-space identity, and a separate no-flush CPU sample
with mapping and host-pointer serials before/after. Before executing this macro,
the front queues the backend request with `DeferFunction` and calls `Publish`.
This conservatively marks `Writes::Anywhere` once and changes scheduling;
the original macro words, instruction budget and execution remain unchanged.
The GPU copy is ordered before this invocation's emits, after previously queued
work. Explicit mapping/sample status must be considered before comparing bytes.

The backend uses only an existing fully covering buffer and retains its small
readback until completion; an absent or partially covering buffer is reported
without creating one. Results are written alongside the capture as
`pre-mme-buffer-probe.json`. No raw macro bodies are added to source: the selector
stores opaque fingerprints. Local production-header probes exercise armed
owner/generation gates, rejected/missing/noncontiguous origins, immutable request
ownership, disabled behavior and the one-request cap for both captured entries.
This diagnostic seeks a CPU/copy versus raw-GPU discrepancy; it is not recovery.

Validation is limited to production-header freeze/gate probes for both entries,
Maxwell translation-unit syntax compilation, cross-review, and compilation of
the affected Vulkan object. No live GPU readback, runtime completion, byte
comparison or gameplay benefit has been demonstrated for this probe. Mapping
serials detect a changed mapping; they do not establish command publication or
identify the writer. The diagnostic was off during the subsequent gameplay
gate; its first live comparison is recorded below.

## Revised gameplay gate and live buffer comparison

Run 19 used compiled `e094666`, normal speed, warmed disk cache, Wait 0,
the invalid-depth workaround and forced list flush. Macro capture, native
shader dumping and Vulkan diagnostic layers were disabled. A real Floria
race reached three measured ten-second windows of 51.97, 50.55 and 38.91 FPS;
the observed period averaged 47.20 FPS. It then exited unexpectedly after
`VK_ERROR_DEVICE_LOST` at uptime 394.575 seconds. These are pre-crash samples,
not a course pass. The preserved dump contains the downstream Vulkan device
loss exception; it does not identify the faulting GPU producer.

Run 20 used the clean compiled `496a218` candidate with the one-shot probe
enabled and otherwise retained the normal configuration above, adding macro
capture but no shader dump or Vulkan layer. The existing-buffer readback
completed before the selected macro invocation's emitted work. Selected
arguments, the CPU sample before enqueue, CPU at recording/completion, and raw
GPU buffer bytes all matched. Mapping and host-pointer serials remained stable;
the probe reported `completed`, complete GPU bytes and matching mapping identity.
The CPU words did not change across the samples.

For this specific invocation, stale selected-copy data or a CPU-versus-GPU
buffer discrepancy is therefore not the cause of the bad macro arguments.
The next investigation must distinguish an earlier producer/publication problem
from command or macro ownership; this comparison identifies neither writer nor
root cause. The probe changes scheduling once and remains diagnostic, not a fix.

The run was deliberately stopped after obtaining the decisive comparison and
exited through frontend cleanup without device loss. Its four measured windows
were 16.18, 37.40, 38.36 and 36.29 FPS. It did not finish a race and is not an
acceptance pass. HUD/results defects, macro warnings without demonstrated
playability impact, and performance between 30 and 60 FPS remain deferred.
All eight courses still require qualification; Floria remains the active course.

## Acceptance record

| Check | Result |
| --- | --- |
| Unmodified build-160 startup | Stack overflow reproduced |
| Source traversal regression tests | 32 tests, 199 assertions pass |
| Exact crash-pipeline differential replay | Baseline overflows; fixed passes; SPIR-V identical and valid |
| Cold-hint method boundary probe | Fixed 9/9; baseline 3/9 |
| Patched normal-stack, empty-cache startup | Exact shader compiles; main menu and control confirmed |
| Diagnostic Floria Fields race, result, menu | Completed; macro/render errors remain |
| Patched Floria Fields at current 30 FPS gate | Run 19 device loss; run 20 diagnostic stopped; no complete pass |
| Pre-macro raw buffer comparison | Live stable-mapping CPU/selected/GPU agreement; earlier producer remains unknown |
| Floria Fields warmed repeat | Pending |
| Remaining courses in menu order | Pending |

Only source changes, tests, and summarized verification belong in this
repository. Game files, keys, firmware, saves, native shader captures,
screenshots, and host dumps remain local.
