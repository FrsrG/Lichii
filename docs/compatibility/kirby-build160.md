# Kirby Air Riders 1.3.3 on build 160

This branch starts at Lichii `build-160`, commit
`2eb5f081c8734affd272466f89133aa4e3e56e18`.

The requested gameplay scope is Air Ride courses only, in menu order.

## Active HUD and post-race screen investigation

The subsequent user goal requires fixing the HUD disappearing during Floria
Fields and the black screen after its finish while audio continues. These
are now required defects; the earlier menu-restart workaround does not satisfy
this goal. The preceding eight-course crash/performance results remain valid
only within their stated scope.

Run 44 reproduced both failures on the delivered executable and settings:
the HUD was visible at race time 0:03.15, later disappeared while the 3D race
continued, and advancing past the finish celebration produced a black screen.
The process kept presenting rather than hanging, and closed normally. Local
RenderDoc captures cover an affected racing frame and the black post-race
frame. No raw captures or game-derived resources are published.

Source inspection identifies two discriminators, not a confirmed cause:
native draw continuations carry vertex/index counts and instance counts,
and zero-instance draws are discarded before Vulkan recording. The current
snapshot workaround can substitute pre-producer continuation values. A
supported back-thread macro mode retains the native JIT while changing the
front/back command path, so it can test this hypothesis. Upstream alias and
feedback fixes are also being compared against the actual captured output.
No rendering fix is claimed at this stage.

The follow-up controls rejected back-thread-only macros and single-thread GPU
execution as HUD fixes: both still lost the HUD and restored malformed live
macro failures. RenderDoc replay of the affected frames failed during Vulkan
opaque-address allocation, so replay yielded no pixel/draw result. CPU-only
structured capture inspection nevertheless recovered valid nonzero UI draws,
matching target/sampled image identity, enabled RGBA writes, and disabled
depth/stencil tests. Extracted compositor SPIR-V confirms intentional inverse
alpha composition (`sceneRGB * UIalpha + UIRGB`); changing the alpha blend
would not be a justified fix.

Run 48 supplies a visually verified visible/absent HUD pair with complete,
untruncated native output captures. The main UI target is cleared identically
to `(0,0,0,1)` in both. Its good frame executes 39 UI draws and produces visible
RGB/coverage, while the bad frame executes 38 draws with the same shader set
and leaves every pixel at the transparent clear value. Smaller UI targets
already lose RGB earlier in the bad frame. The first demonstrated failure is
therefore UI rendering before final composition, not missing presentation.
Initial phase misclassifications in run 47 and a help-popup false alarm in run
49 are explicitly rejected as bad-HUD evidence. Buffer readbacks can add
barriers/render-pass breaks/waits and may change the failure; they are not a fix.

A default-off shader filter now limits diagnostic frame-dump buffer readbacks
to selected pipelines, preserving the shared capture budget. This is diagnostic
tooling only. The subsequent full-draw-resolution control still loses the HUD
with no reported pipeline/view/texture-buffer reuse mismatches. Texture memo
resolution and GPU visibility remain separate required discriminators.

Further same-executable controls reject forced index rebinding (run 52) and
ending the render pass before the selected UI vertex shader (run 53): both
reach their diagnostic hooks and still show missing racing HUD. Run 54 adds
a global memory barrier and retains the HUD in early screenshots, but later
shows severely corrupted UI geometry. Another game process started during
that run, so its subsequent slowdown is not an isolated measurement of the
barrier's cost. The following finish/wait control (run 55) is inconclusive:
boot/navigation was affected by concurrent load and no race was attempted.
These diagnostics are default-off; none is a confirmed rendering fix.

CPU-only capture inspection also recovers the actual bound uniform bytes for
an affected UI draw, including its geometry/material pointers and instance
index. This narrows the next inspection to the exact consumed records rather
than assuming that an entire storage binding is bad. The actual material
contents and GPU-produced vertex positions remain separate unknowns.

The selected unit-quad region has now been identified in both visible and
affected evidence. Run 49's good GPU buffer copy contains four unit-quad
vertices and six valid indices. The initial GPU-memory snapshots in both
affected run 44 captures instead contain malformed vertices and six zero
indices at that same physical region. No recorded copy or fill repairs the
60 bytes in those frames. Earlier compute/graphics writes are not completely
excluded, so initial snapshots must not be presented as final consumed bytes.
A bounded, default-off CPU-side region trace records guest bytes, normal
upload decisions, ownership marks, buffer copies and download landings without
adding GPU readbacks or synchronization. It is intended to distinguish bad
guest data from stale GPU contents on the next controlled reproduction.

A focused tool review found a matching NVIDIA opaque-address replay failure
in [RenderDoc issue 3880](https://github.com/baldurk/renderdoc/issues/3880), but
no general documented repair for these existing captures. Repeating the
already-failed replay options adds no evidence. CPU structured capture reads
remain usable. If recorded buffer bytes do not explain the failure, selected
shader [Debug Printf](https://github.com/KhronosGroup/Vulkan-ValidationLayers/blob/main/docs/debug_printf.md)
can provide GPU values without replay, with timing effects explicitly tracked.
REA and its reconstruction companion do not add a GPU debugger to this
source-available investigation; the installed Windows session's supported
tools and missing providers were checked without changing configuration.

## Current delivered result

All eight courses completed without a crash at the current sustained 30 FPS
gate, in menu order. These are **conditional passes**: the measured continuous
ten-second intervals met 30 FPS, but individual hitches reached 2.54 seconds.
This does not establish a literal every-frame minimum, smoothness, cold-cache
performance, or every random roster/effect. Default laps and 100% game speed
were retained at full resolution. The accepted executable is compiled commit
`b2909540f832fd7676c9c433787df4b6d5d18edd`, SHA-256
`0a11f796aa9ac643f9f2d5bafa293074af00313322966f4c961e78237859aaf2`.

| Course | Laps | Accepted run | Minimum continuous interval FPS |
| --- | --- | --- | --- |
| Floria Fields | 3 | 26 | 53.87 |
| Waveflow Waters | 2 | 28 | 34.42 |
| Airtopia Ruins | 2 | 33 | 37.87 |
| Crystalline Fissure | 2 | 36 | 41.19 |
| Steamgust Forge | 2 | 37 | 40.18 |
| Cavernous Corners | 2 | 39 | 52.86 |
| Cyberion Highway | 2 | 40 | 38.29 |
| Mount Amberfalls | 1 | 41 | 34.03 |

The delivered launcher enables the snapshot, invalid-depth and list-flush
workarounds; the local accumulated shader cache is required for comparable
performance. Runs 33 onward use the fixed five-CPU roster documented below.
HUD loss, dark/incorrect shading and black post-race menus remain deferred.
Restarting the game restores readable menus. Use the validated launcher rather
than starting the executable directly; CPU Select settings must be reapplied
after restart for the tested roster. Earlier failed attempts remain failures.

Delivery updated the original build-160 installation with exact executable,
PDB and DXC dependencies, the title cache and three title-only draw-wait keys.
Original files were backed up with hashes and a rollback manifest. No test
input profile, keys or saves were copied. Both original global and title
Controls sections remained identical after installed-profile validation.
Run 43 verified readable main/Air Ride menus using the original Roaming
profile, followed by normal frontend cleanup with zero device losses/MME
limits. Run 42 was a smoke-harness error: an empty shader-dump environment
entry caused a filesystem abort. The real CMD launcher correctly removes
that variable; correcting the harness required no emulator source change.

Source fixes and diagnostics are distinguished from these workarounds in the
chronological evidence below. Game-derived caches/captures and backup/profile
data remain local, outside this repository.

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

The observed fatal timeline-semaphore wait catch now invokes the existing
feature-guarded Vulkan device-fault reporter on `VK_ERROR_DEVICE_LOST` before
rethrowing the original exception. Reporting exceptions cannot mask that
original failure, and no normal-execution behavior or recovery is added.
This covers the observed wait exception, not failures from the separate counter
refresh calls outside that catch. The affected translation unit compiles;
that check alone did not establish live fault reporting or gameplay benefit.

Run 21 verified that reporting path: a normal Floria attempt on `7da1cea`
lost the device at uptime 188.949 seconds (tick 76606) and exited unexpectedly.
Five completed ten-second windows ranged from 34.78 to 43.38 FPS, averaging
38.71 over the observed period; the subsequent 14 seconds before connection
reset were unmeasured. There was no finish or course pass.
The driver returned 165 unique instruction-pointer-unknown records, each with
16-byte precision, no vendor records and an empty description. None was
classified as invalid memory access or a fault-associated instruction pointer.
[Khronos defines these unknown pointers](https://docs.vulkan.org/refpages/latest/refpages/source/VkDeviceFaultAddressTypeKHR.html)
as potentially unrelated to the fault; these records do not identify a shader.

The next diagnostic enables the existing optional Aftermath integration with
the official NVIDIA 2026.3 SDK. The SDK archive is kept local, has SHA-256
`e38136a60110199559b7365d3ea4ec0cb5588dc2b0f593877d864e0299659a3f`, and
its DLL has a valid NVIDIA signature. The tracker registers before logical
device creation and existing module paths retain SPIR-V. To make its existing
automatic-checkpoint flag effective, the diagnostics-only device path now
enables `VK_NV_device_diagnostic_checkpoints` when supported, omits the flag
otherwise, and logs availability. The caught device-loss path uses the existing
15-second `ReportLoss` wait only when Aftermath is enabled, allowing time for
the asynchronous dump callback before rethrow. No shader-error-reporting mode
or normal gameplay behavior is added.

The changed device, tracker and semaphore objects compile against SDK 2026.3.
The full SDK-enabled GUI build on `515614e` also passed, including matching
EXE/PDB, complete dependencies and the normal 1 MiB stack.
[NVIDIA documents the collection requirements](https://docs.nvidia.com/nsight-aftermath/SDK/index.html).

Run 22 verified successful tracker initialization and checkpoint availability.
It retained 4,973 SPIR-V files but produced no `.nvdbg` sidecars or GPU crash
dump. Driver 621.14 supports the documented R615+ embedded-debug mechanism;
missing sidecars alone therefore do not disprove eventual dump correlation.
That correlation has not been tested.

The startup helper timed out at 120 seconds without sending input. A later
status at uptime 210.17 seconds still reported zero presents while the process
was consuming CPU during preload. The attempt was bounded at 240 seconds;
an owned thread snapshot was preserved, then closing was requested. The
owned process did not exit and was force-stopped. This is incomplete diagnostic
setup, not a gameplay crash or qualification. The portable global profile was
restored byte-for-byte (SHA-256 beginning `f3084cc6`); the title profile remained
unchanged. Startup stacks must justify any correction before another attempt;
no cache sweep or arbitrary longer wait is justified by missing sidecars.

The user requested an unchanged retry after reporting that another game had
been open. Run 23 reached a verified Air Ride menu at uptime 202.66 seconds,
within the same startup bound, with identical executable/settings. This does
not establish contention as the reason for the earlier delay. No cache or
shader-debug flag change was needed. The retained startup snapshot from run 22
also places busy workers in NVIDIA compute-pipeline creation from cached
preloading, rather than an observed Aftermath callback deadlock.

Run 23 selected Floria and reached the Take Off roster. The race-start input
sequence then lost its connection before completing and before a race HUD
could be verified. Device loss at uptime 279.13 seconds produced a 267,544-byte
Aftermath GPU dump, a 109,672-byte shader-debug file, retained SPIR-V and JSON.
The device state is `Hung`, with an engine reset and no adapter reset.
Automatic checkpoint stacks identify direct indexed Vulkan draws; the dump
has 18 active warps from one Geometry shader, SDK hash `06a4396286b6d8f8`.
Offline SDK decoding resolves all 18 PCs using the exact retained 224,688-byte
SPIR-V. That module consumes points and emits a triangle strip, with at most
eight output vertices. The SDK's intermediate filename/line numbers are not
guest source and must not be matched blindly to another disassembly's lines.

The active shader is a correlation at reset, not proof of faulty translation.
The SDK page-fault query returns `NotAvailable`, which does not prove absence
of a memory fault. This run logs zero native MME instruction-limit failures,
so such a failure is not a necessary logged precursor to device loss. The
critical next step is exact shader/pipeline and workload analysis offline;
another broad macro or settings experiment is not justified by this capture.
The owned process exited unexpectedly, and the portable global profile was
restored exactly. This is a verified crash-capture milestone, not a course pass.

Geometry-only replay of 14 distinct cached guest programs finds one strong
structural match, guest hash `4b8d64d0576e9e5d`: the complete 561-label,
two-loop control-flow topology matches, as do 5,308 ordered core opcodes after
explicitly lossy removal of profile-sensitive operations. Other candidates
differ. Runtime profile/linkage differences prevent an exact binary match;
the association is strong but remains structural. The native bytes also match
a preserved local shader capture. Points expanded to at most eight strip
vertices, no geometry-stage texture operations and storage-buffer inputs are
consistent with particles/billboards/ribbons; visual role remains an inference.

The exact captured SPIR-V validates with its actual version/features. Both
loops have safety counters, and ordinary nonnegative input counts bound the
two loops to four/eight iterations. A negative storage-derived count could
exercise the longer safety-bound path, but that input is not established in
the captured draw. No translation correction is justified by these facts alone.

`EDEN_NX2_SKIP_GS_HASH=<hex hash>` is a default-off dependency diagnostic that
omits only draws using the selected guest geometry hash. It logs its first
four matches and follows the existing draw-failure early exit before pipeline
configuration. This is neither an instruction rewrite nor a confirmed fix.
The changed Vulkan object compiles. The proposed single normal-speed Floria
test disables Aftermath and requires observed selector hits, full finish and
course navigation, at least 30 FPS, and usable track/rider/obstacle visibility.
Essential missing geometry invalidates the workaround; minor omitted effects
must be named. A non-matching selector or another crash cannot qualify it.

Run 24 rejects that exclusion as sufficient recovery. The guard logged matches
for the intended linked pipeline (first four indexed counts 7, 9, 11 and 13),
yet the normal, Aftermath-disabled attempt lost the device at uptime 129.865
seconds, tick 38675, and exited unexpectedly. The race-entry input completed
and a course/rider image was captured, with missing HUD and black patches;
there was no finish and no completed ten-second FPS window. The log has 51
MME-limit failures. No specific visual omission was established, and the
candidate exclusion is retired rather than extended to other shaders.

The next diagnostic revisits GPU-assisted validation with the invalid-depth
workaround enabled. Earlier validation preceded that workaround and first
reported the known Dref/color-view violation. The concrete question is whether
validation can now expose a new actionable resource-access error preceding
loss. It disables the geometry exclusion, macro capture and native shader dump.
Cache loading is disabled only for diagnostic startup/instrumentation coverage;
normal settings must be restored afterward. A repeated old Dref report or no
new actionable report before loss does not justify another unchanged attempt.

## Acceptance record

### First qualified course with explicit workarounds

Run 26 completed the configured three-lap Floria Fields race on normal-speed
`b290954`, using `NULL_INVALID_DEPTH=1`, `LIST_FORCE_FLUSH=1` and
`DRAW_CONTINUATION_SNAPSHOT=1`. Aftermath, validation layers, geometry exclusion,
macro capture and native shader dumping were off; normal cache loading and
Wait 0 remained enabled. The orange Kirby post-race celebration is verified.
Twelve ten-second racing/finish-transition windows measured 53.80–58.79 FPS
at the logged 100% speed setting. A 493.8 ms transient interval occurred;
these windows establish the sampled sustained target, not an absolute 30 FPS
per-frame floor. Essential track, rider and other racers remained visible.
The final log has zero MME-limit errors and zero device-loss reports, and
requested shutdown completed through frontend cleanup.

The HUD and post-race 2D menus remain defective: the latter became entirely
black, so seamless menu access is not qualified. A clean same-configuration
relaunch in run 27 restored the readable course list and selected Waveflow
Waters, the next card in menu order, with its default two laps. Floria is
therefore a **conditional course pass** with snapshot/depth/flush workarounds
and an explicit restart between races for usable menus. Snapshot semantic
correctness and HUD/menu repair remain unproven/deferred. This is the first
normal-stack course pass under the revised 30 FPS gate; the remaining seven
courses still require independent qualification.

Run 27 finished Waveflow Waters' default two laps without device loss or MME
limit errors, then closed cleanly. It does **not** pass the performance gate:
five measured ten-second windows were below 30 FPS (minimum 11.87 FPS).
Low windows overlap substantial distinct first-use pipeline preparation; later
windows reached roughly 54–59 FPS with little new pipeline work and no logged
pipeline eviction. Long GPU-fence waits also occurred, so compilation alone
is not established as the cause of every stall. One unchanged warmed repeat
is justified before further performance diagnosis; course three remains blocked
until course two satisfies the full gate.

Run 28 supplies that warmed Waveflow qualification: the same two-lap race
finished and reached the orange celebration with no MME-limit errors or device
loss, then closed cleanly. All completed measurement windows stayed above
30 FPS; racing windows ranged from 41.47 to 58.60 FPS at the normal 100%
setting. A 2.10-second transient interval still occurred, so this is a sampled
sustained target with occasional stutter, not an absolute per-frame floor.
Track/rider/other racers and course progress were observed; HUD loss and
intermittent black shading/water patches remain deferred rendering defects.
Waveflow is a conditional pass using the same snapshot/depth/flush settings,
the warmed local cache and restart recovery for readable menus. The first-use
run remains a performance failure and is not rewritten as a pass.

Run 29 completed Airtopia Ruins' default two laps and closed cleanly with zero
MME-limit errors/device losses. Its first-use performance gate failed on one
5.13 FPS window. That interval overlaps 34 newly observed compute-pipeline
preparations; subsequent windows recovered to 41–59 FPS with little/no new
pipeline work. Preparation timings do not fully explain the long frame waits.
One unchanged warmed repeat is justified; Airtopia and all later courses remain
unqualified until the full performance gate is met.

The warmed Airtopia repeat in run 30 also completed without MME-limit errors
or device loss and closed cleanly, but one window measured 24.92 FPS with a
2.58-second maximum interval. It therefore remains a performance failure under
the current gate. This is a different interval from the first-use failure;
no unchanged repeat or advancement to course four is justified. The exact
warmed interval must distinguish remaining pipeline compilation from GPU waits
or memory pressure before choosing a correction.

The failed warmed interval contained 16 compute keys not logged in the earlier
attempt. All 16 were verified live/integrity-correct in the saved cache and
covered by normal compute preloading. CPU identities/machines had also varied
under Random selection, a plausible but unproven source of new variants.
Run 31 explicitly matched run 30's five level-three opponents and machines.
Although its `frame_times` window measured 33.56 FPS, continuous present deltas
showed only 27.58 FPS across the corresponding 12.33-second interval. The
missing 2.33 seconds aligns with an isolated screenshot phase of 2.325 seconds;
adjacent captures cost about 0.15 seconds. This exposed observer overhead and
a measurement gap, not a gameplay performance fix.

The monitor now records continuous status-to-status present deltas and can
disable periodic screenshots. Retrospective continuous checks retain Floria
(minimum 53.87 FPS) and warmed Waveflow (minimum 34.42 FPS) as passes. A
same-process rematch could not be confirmed through the black post-race menus
and was aborted without measurement. A fresh restart restored visible menus;
the exact fixed roster was reapplied and verified before run 33.

Run 33 completed Airtopia's two laps and reached the verified celebration,
with zero MME-limit errors/device losses and clean shutdown. Its 240-second
measurement made no periodic GPU readbacks; start/finish images were outside
timing. All continuous intervals met 30 FPS; racing intervals ranged from
37.87 to 56.30 FPS. A 1.05-second single-frame hitch remains, so this is still
a sustained-window qualification, not an absolute FPS floor or smoothness
guarantee. Airtopia is a **conditional pass**, with warmed cache, existing
snapshot/depth/flush and restart workarounds, and the fixed roster below.
Removing observer overhead is not claimed to correct shader or snapshot
semantics. Remaining courses use the corrected measurement method.

| CPU | Rider | Machine | Level |
| --- | --- | --- | --- |
| 1 | Meta Knight | Winged Star | 3 |
| 2 | Waddle Dee | Wagon Star | 3 |
| 3 | King Dedede | Winged Star | 3 |
| 4 | Susie | Wagon Star | 3 |
| 5 | King Dedede | Winged Star | 3 |

Opponent count stays five, handicap None, player Kirby/Warp Star and default
course laps. The game resets CPU groups to Random after restart, so each
qualification reapplies/verifies this table; this is not all-roster coverage.

Crystalline Fissure's default two laps finished in runs 34 and 35, with
verified celebrations, zero MME-limit errors/device losses and clean shutdowns.
Neither qualifies. First-use run 34 had six continuous intervals below 30 FPS
(minimum 4.89); all six overlap 160 first-observed pipeline preparations.
The same fixed-roster warmed repeat, run 35, reduced this to one failed interval
at 17.34 FPS, but still fails the gate. That interval overlaps 21 compute and
three graphics preparations whose keys were absent from the preceding run's
runtime reports, including a 1.43-second graphics preparation. New pipeline
reports alone do not prove that their records were absent from the starting
cache. Low commit headroom and GPU-fence waits are additional unresolved
contributors. No periodic screenshots were taken in either measurement. The
next step distinguishes remaining first-use variants from missed cache reuse;
an unchanged third repeat was not yet justified at that point.

The subsequent bounded audit validated the exact startup cache prefix, record
checksums, runtime keys and native instruction fingerprints. All 21 compute
programs and three graphics records were absent at run 35 startup and persisted
successfully afterward. Seventeen compute programs had no prior matching
instruction sequence/length; four nearest matches differed in real operands,
not observed process-address patches. No shader-key normalization or cache
loading fix was supported. Run 36's actual startup prefix contained all 24
records: compute selected unconditionally, graphics in the latest-used boot
set, with no damaged/skipped records. This justified one known-key-covered
qualification rather than further blind warming.

Run 36 completed Crystalline Fissure and reached the verified celebration,
with zero MME-limit errors/device losses and clean shutdown. All 24 continuous
intervals exceeded 30 FPS (minimum 41.19); periodic screenshots remained off.
The existing full-resolution/100% settings and fixed five-CPU roster were
unchanged. A 1.98-second transient remains. Crystalline is a **conditional
covered-cache pass**, not a source/cache correctness fix, smoothness guarantee
or coverage of every random effect/roster. Earlier performance failures remain
failures. Course five may now proceed.

Run 37 completed Steamgust Forge's default two laps on its first measured
attempt, reaching the verified celebration with zero MME-limit errors/device
losses and clean shutdown. All 24 continuous intervals met 30 FPS (minimum
40.18), at the same full resolution, normal 100% speed and fixed five-CPU
roster. No periodic screenshots were taken. A 2.07-second transient remains;
this is a **conditional pass** under the accumulated local cache and existing
snapshot/depth/flush, rendering and menu-restart limitations. No warmed repeat
or additional source/configuration change was needed.

Run 38 completed Cavernous Corners' default two laps and reached the verified
celebration, with zero MME-limit errors/device losses and clean shutdown.
One continuous interval measured 27.42 FPS, so the first-use attempt fails
performance and course seven remains blocked. That interval overlaps 14
compute preparations totaling 579 ms and a 1.64-second frame interval;
nearby GPU-fence reporting includes a 1.63-second wait. Preparation alone
does not account for the full stall. Adjacent intervals recovered to 54–58
FPS. Exact cache/preload coverage is being checked before selecting a repeat
or correction; the existing rendering/menu limitations remain unchanged.

The bounded cache audit found all 14 compute keys and exact native instruction
fingerprints absent at run 38 startup and correctly persisted afterward. Run
39's actual startup prefix contained all 14 captured contexts, with intact
records and unconditional compute admission; logged settings matched run 38.
One covered-key comparison was therefore justified. It completed both laps and
reached the verified celebration, with zero MME-limit errors/device losses and
clean shutdown. Every continuous interval exceeded 30 FPS (minimum 52.86), with
a maximum sampled frame interval of 181 ms. Cavernous Corners is a
**conditional covered-cache pass**, using the same fixed roster and existing
snapshot/depth/flush, rendering/menu and accumulated-cache conditions. No
source/configuration change was needed; run 38 remains a performance failure.

Run 40 completed Cyberion Highway's default two laps on its first measured
attempt, reaching the verified celebration with zero MME-limit errors/device
losses and clean shutdown. Every continuous interval met 30 FPS (minimum
38.29) at unchanged full-resolution/100% settings and the fixed five-CPU roster.
A 2.54-second transient remains; the result is a **conditional pass** under
the existing cache, snapshot/depth/flush and rendering/menu limitations.
No warmed repeat or additional source/configuration change was needed.

Run 41 completed Mount Amberfalls' default single lap on its first measured
attempt, reaching the verified celebration with zero MME-limit errors/device
losses and clean shutdown. All 24 continuous intervals met 30 FPS (minimum
34.03) at unchanged full-resolution/100% settings and the fixed roster. A
2.20-second transient remains. This completes **eight conditional course
passes in menu order**, with the existing cache, snapshot/depth/flush and
rendering/menu limitations. It does not establish an every-frame FPS floor,
smoothness, cold-cache performance, or all-roster/effect coverage.

| Check | Result |
| --- | --- |
| Unmodified build-160 startup | Stack overflow reproduced |
| Source traversal regression tests | 32 tests, 199 assertions pass |
| Exact crash-pipeline differential replay | Baseline overflows; fixed passes; SPIR-V identical and valid |
| Cold-hint method boundary probe | Fixed 9/9; baseline 3/9 |
| Patched normal-stack, empty-cache startup | Exact shader compiles; main menu and control confirmed |
| Diagnostic Floria Fields race, result, menu | Completed; macro/render errors remain |
| Floria Fields at current 30 FPS gate | Conditional pass in run 26; readable next-course access restored by restart in run 27 |
| Pre-macro raw buffer comparison | Live stable-mapping CPU/selected/GPU agreement; earlier producer remains unknown |
| Waveflow Waters, two laps | Conditional warmed pass in run 28; first-use run 27 below 30 FPS |
| Airtopia Ruins, two laps | Conditional fixed-roster pass in run 33 with continuous counters/no periodic readbacks |
| Crystalline Fissure, two laps | Conditional covered-key pass in run 36; runs 34/35 remain performance failures |
| Steamgust Forge, two laps | Conditional pass on first measured attempt, run 37 |
| Cavernous Corners, two laps | Conditional covered-key pass in run 39; run 38 remains a performance failure |
| Cyberion Highway, two laps | Conditional pass on first measured attempt, run 40 |
| Mount Amberfalls, one lap | Conditional pass on first measured attempt, run 41 |
| All eight Air Ride courses in menu order | Conditional completion under documented cache/roster/workarounds; original installation delivered and startup/menus/clean close verified |

Only source changes, tests, and summarized verification belong in this
repository. Game files, keys, firmware, saves, native shader captures,
screenshots, and host dumps remain local.
