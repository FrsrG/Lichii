# Yoshi and the Mysterious Book: Build 150 investigation

## Observed result

The post-intro black world is fixed in the tested Build 150 derivative. The first
candidate rendered the formerly black dialogue scene and supported a roughly
28-minute session of first-level exploration; the owner reported that level one
worked perfectly. The final candidate loaded the preserved 11-star save,
re-entered the habitat, rendered movement and jumping, picked up a creature with
the tail-flick input, and returned to the book through End Exploration.

The gameplay executable was built from
`9d71cf868a14d6217442702ddb53a3f47626580d`, SHA-256
`4E03A46D307C67869AC41FD590A3D653DA16CBDC625400265F1AF3AF610BAD15`.
[Its Windows milestone run](https://github.com/FrsrG/Lichii/actions/runs/42926072741)
passed all targets, the delay-import check and 10/10 CTest entries. The tested
early package and the later validated package have identical runtime hashes.
This establishes observed first-level play on the recorded NVIDIA/Windows host,
not full-game completion or compatibility on every GPU.

## Objective and baseline

Make the full game (program ID `0400245021D2C000`, version 1.0.2) render and reach
interactive gameplay on a Build 150 derivative. Do not call it playable until
the first level, movement, interactions, scene transitions, and a sustained run
have been observed. Compilation and shader validation are separate gates.

- Source baseline: `17baed716` (`build-150`, branch `nx2`).
- Installed executable SHA-256:
  `F241693F6779D3D4DF740CB0C0D6C6142F0D6BEA7921FAE5D06AA441E53B4E5E`.
- Local package: `C:\Users\frase\Downloads\Switch Emulators\build 150 lichii`.
- Host: Windows 11 25H2, Ryzen 7 9800X3D, 32 GB RAM, RTX 5070, NVIDIA 621.14.
- User observations: title and opening movie render; the following 3D scene is
  black while dialogue remains visible. Build 138 sometimes showed red lines;
  Build 150 has the same missing world, with that artifact unconfirmed.
- Original logs and configuration snapshots are preserved outside Git. Neither
  game content, keys, firmware, save data, native shader dumps, nor frame captures
  belong in the public fork.

## Evidence ledger (2026-11-10)

The baseline log identifies `nx2-build-150` and boots the full game's program ID.
The older rotated log identifies Build 138; keep these sessions distinct.

| Build 150 elapsed time | Observed rejection | Interpretation |
| --- | --- | --- |
| 44.907 s | graphics op `321`, `ff087321 00007007 00000c00 000e2800`: instruction before exit setup state is dead | Geometry exit liveness does not model an attribute load. |
| 71.978 s | graphics op `f63`, `00247f63 3000040e 0c1e1f0e 000f4200`: native texture gather mode | Uniform-handle bindless gather with offsets is rejected. |
| 72.504 s | compute op `364`, `02101364 38000010 001e0f12 00014200`: unsupported native opcode | Register-handle bindless gather is missing. |
| 72.562 s | compute op `59f`, `0800759f a0c00201 0310e000 000fe200`: native surface reduction mode | A 3D surface reduction is rejected. |

These are direct local observations. At baseline their contribution to the black
image was a hypothesis; later screenshots establish the before/after change. The log
continues presenting frames, consistent with the user's live dialogue and FPS.
Missing avatar and optional Streamline low-latency DLL warnings are not evidence
that they cause the missing 3D world.

The original process had no listening debug server. Later candidates enabled
the opt-in server in separate portable profiles.

## Existing work and attribution

- [Compatibility reports](https://lichii.zortos.me/games/yoshi-and-the-mysterious-book-5510100)
  reproduce the post-intro symptom across AMD/NVIDIA and Windows/Linux.
- [Lichii PR #1189](https://github.com/zortos293/Lichii/pull/1189), head
  `34d4acdd458f5ffc6b610e1d4e0a8306c6e9a9f0`, implements the four locally observed
  forms plus predicated LDG and immediate I2F.F16. Its author's gameplay report
  used a hybrid build; it does not establish this exact branch is playable.
- Eglantine was fetched and surveyed, including its canonical
  [community repository](https://github.com/Eglantine-Community/Eglantine).
  Its current lowering supports 3D SURED but lacks several of the other forms.
  A blanket integration would change the requested Build 150 baseline; use
  focused attributed ports for this investigation.
- Preserve original authorship with `git cherry-pick -x`. Record any changes to
  the imported implementation as separate reviewable commits.

## Experiment and validation plan

1. Preserve logs, executable identity, settings, source revision, and symptoms.
2. Port the relevant existing shader work; review full register ranges and
   predicate liveness before relying on it.
3. Compile the Windows emulator and run the project's tests using the same
   clang-cl/Qt toolchain as upstream. Initial PATH and `vswhere` checks did not
   expose a configured compiler. Later inspection found working MSVC 19.44 and
   SDK 10.0.26100.0 under Visual Studio Build Tools 2022; local quick tests now
   use those tools. CI uses clang-cl 22.1.8, which was not found locally.
4. Launch that package locally with the opt-in debug server and shader dump.
   Record the exact source SHA and preserve the original package and saves.
5. Observe the same post-intro scene. Collect a screenshot, new log, rejected
   native programs, and one affected frame's pass inputs/outputs if needed.
6. Replay captured native programs and cache environments through
   `tools/nx2/shader_sweep.py` and `spirv-val`. Keep missing context distinct from
   compiler failure. A valid SPIR-V module does not establish correct pixels.
7. If output remains wrong, use RenderDoc to locate the first incorrect pass,
   correlate native shader hashes, inspect resources/depth/blend state, and
   compare with an uncaptured run before ruling out synchronization defects.
8. Verify first-level movement, interactions, transitions, save/reload, and a
   sustained run. Where available, check another game sharing changed instruction forms before
   preparing a focused upstream PR with measured results and limitations.

## Reusable workflow notes

Use the built-in debug server before adding instrumentation: `status`,
`screenshot`, `log_tail`, `capture_frame`, and reversible `reject_shaders`.
Read `docs/nx2/debug-mcp.md`, `tools/nx2/shader-sweep.md`, and `AGENTS.md`.
Capture only one frame at a time and restore every rejected shader hash.
Keep `.rdc`, native shaders, textures, buffers, and caches local.

Useful external tools: [RenderDoc](https://github.com/baldurk/renderdoc),
[SPIRV-Tools](https://github.com/KhronosGroup/SPIRV-Tools), and
[Nsight Graphics](https://developer.nvidia.com/nsight-graphics) for supported
NVIDIA Vulkan inspection. A third-party RenderDoc MCP adapter needs a pinned,
reviewed version and a verified real replay before its output can be trusted.

Record each experiment's revision, single changed variable, scene, output paths,
result, and next question. Convert this record into a compatibility skill only
after the workflow has an observed successful gameplay result.

## Milestones

- Baseline: preserved Build 138 and Build 150 sessions; identified four concrete
  rejected shader forms; created the FrsrG fork. Gameplay is still unverified.
- Attributed ports: imported `4c5f45332`, `39ff782b8`, `6333b09c7`, and
  `34d4acdd4` from PR #1189 with original authors and source references. Removed
  the unrelated IMAD addend-negation hunk from the final implementation.
- Review fixes (`db65715f3`): protected all three 3D reduction coordinates from
  lowering scratch and register-shadow collisions; tracked reverse-encoded LDG
  load predicates in geometry exit liveness. Added range and lifetime tests.
- Captured fixtures (`29b997409`): positive lowering tests use all four exact
  instruction sequences from this PC's Build 150 log. The geometry fixture
  keeps exit state live during the captured attribute load.
- Additional review (`5d8bc9389`): the Maxwell translator's `MakeCoords` reads one
  coordinate for buffers. Matched that count in reduction range checks and
  register metadata, with a boundary regression test, to retain valid R245
  buffer-coordinate programs.
- First Windows candidate: source `db65715f3401ba2cd0ecda91f677e06ff6e20e85`,
  [build run](https://github.com/FrsrG/Lichii/actions/runs/42910339018).
  After 53 m 52 s in the combined compile/link step, the emulator linked at
  Ninja step 2349/2353, but `shader-sweep.exe` failed at step 2352 with unresolved
  `GetAdaptersAddresses` and `ConvertLengthToIpv4Mask`. Tests and packaging were
  skipped. This is a Windows diagnostic-tool link dependency, not evidence of a
  shader compile failure. No executable from this run was gameplay-tested.
- Minimal repair (`b52706bb0`): added the existing `${PLATFORM_LIBRARIES}` list
  to the in-tree shader-sweep link, matching the other executable targets.
  It includes Windows IP Helper (`iphlpapi`). Source files under `src` are
  byte-identical to `db65715f3` in the repaired first candidate.
- Repair run: source `2a4c1797be541e5d15c48b9a0dc426bf141152a0`,
  [build run](https://github.com/FrsrG/Lichii/actions/runs/42920122983). It uses the
  same branch and combined cache layout as the first run. The saved cache is
  285,399,123 bytes; first-run hit/miss/eviction statistics and peak memory were
  not printed, so they remain unknown. The repaired workflow always reports
  cache statistics and retains compiled diagnostic packages on a later check
  failure; failures still fail the job. Gameplay is pending.
- Test profile: copied locally into the candidate's portable `user` directory;
  configuration NAND, SDMC, load, and dump paths were rewritten to that directory.
  Shader caches were not copied. The user then closed the original normally;
  its log confirmed emulation cleanup. Refreshed the candidate save after that
  cleanup and verified its SHA-256 against the original. Preserved the complete
  stock Build 150 session log locally.
- Diagnostic profile: disabled update checks to keep the candidate revision
  fixed. Rendering settings remain those of the baseline. Existing local
  Streamline, FidelityFX, and Lossless support files were copied into the profile.
- Control bridge: all 16 existing `tools/nx2/test_debug_mcp.py` tests passed on
  local Python 3.13.0. These use a fake server and establish adapter behavior,
  not actual game rendering or input delivery. The real candidate must still
  exercise screenshots and `input`/`input_sequence` through its debug server.

The independent reviewer audits progress every 15 minutes and checks that work
continues toward an observed gameplay result. Its first audit prioritized the
Windows package, exact captured-instruction coverage, and avoiding unrelated
tooling work. It identified the buffer-coordinate boundary regression above.

## Bounded iteration-loop measurements

Supporting work is isolated from the first candidate. The standalone Ampere
target uses production headers and the repository's pinned Catch2, without Qt,
audio, networking libraries, Vulkan, or the emulator core. Local MSVC checks
passed 29 cases / 2,019 assertions, including the four logged forms. An empty
selection returns an error, even with Catch2's allow-empty flag. Full production,
SPIR-V, and gameplay validation remain separate gates.

Measured on MSVC 19.44.35229, SDK 10.0.26100.0, Ninja 1.12.1, ccache 4.14.2,
four jobs, Release, LTO off; these are not CI clang-cl timings:

| Run | Wall time | Cache evidence |
| --- | ---: | --- |
| Fresh configure | 3.631 s | Pinned Catch2 source available locally |
| Cold selected-target build | 44.181 s | 110 misses, 0 hits |
| Unchanged Ninja no-op | 0.106 s | 0 compiler calls |
| Forced unchanged compilation | 0.344 s | 4/4 direct hits |
| Equivalent header-token edit | 14.191 s | 3 misses |
| Restored original header | 0.386 s | 3/3 direct hits |

All relevant CTest runs passed 2/2. The temporary header edit changed preprocessing
tokens (`216` to `216U`), preserved semantics, and was restored byte-for-byte;
no production source change was committed for benchmarking.

A bounded job-count comparison forced three compiler calls in each case:

| Jobs | Header rebuild | Sampled peak aggregate private bytes |
| ---: | ---: | ---: |
| 2 | 26.216 s | 709,476,352 (~677 MiB) |
| 4 | 14.728 s | 1,087,717,376 (~1,037 MiB) |

Sampling targeted the owned CMake process tree every 200 ms; maximum observed
gaps were 372 ms and 451 ms. Short spikes may be missed, and private allocation
is not physical RAM usage. Four jobs is justified for this small target; no
full-emulator parallelism optimum is established. Raw logs and provenance stay
under the local investigation's `benchmarks/ampere-ccache-msvc-20261110` folder.

## Gameplay and final validation

The repaired first candidate (`2a4c1797b`) reached the original black dialogue
scene with the book, Yoshis and surrounding world visible. Native debug-server
screenshots also show first-page entry, a jump and the creature knowledge page.
The owner then took control and played; subsequent input was observational only.
That approximately 28-minute session is mixed/manual evidence, not a deterministic
agent-only playthrough. The final pause screen showed 11 stars and 91 tokens;
the subsequently loaded save showed 11 stars and 38 tokens, so do not claim that
all transient token counts persisted. Save bytes were copied after normal exit.

At 561.675 seconds that first candidate encountered three previously hidden
IMAD `624` instructions. No corresponding new visible defect was identified:

```
ff117624 81000400 078e02ff 000fe200
ff0cb624 81000400 078e02ff 000fe400
ff04c624 81000400 078e02ff 000fe200
```

These justified restoring the previously deferred IMAD addend-negation hunk from
`4c5f45332`, then porting `53268eca4` (zero-product semantics) and `a1816fd27`
(trap-guard evaluator), preserving cerroverb's authorship. Five independent
semantic tests cover the exact words, predicates, wraparound arithmetic, register
aliasing, unsupported ABS and the real evaluator. Integrated standalone checks
pass 34 cases / 4,381 assertions and 2/2 CTest entries. The final persistent local
Ninja run measured 0.094 s with no compiler calls for a no-op, and 0.332 s with
5/5 direct cache hits for forced unchanged compilation.

The final executable started from an empty shader cache in its own portable
profile, with the completed first session's save verified by SHA-256. The agent
controlled this final check through frame-timed debug inputs. It observed title
and user selection, loaded the 11-star progress, entered the habitat, moved right,
jumped, picked up the creature with X, selected End Exploration and returned to
the rendered book, then entered the habitat again. Screenshots 002 through 014
and the exact debug-call records remain local. The three later IMAD native hashes
were captured in this session, establishing that those programs were encountered.

A ten-second gameplay sample recorded 588 frames, 58.82 FPS, mean 17.00 ms,
p99 20.88 ms, maximum 27.63 ms and zero reported stutters. The completed final
log contains no native graphics/compute shader rejections, PANIC or device-loss
lines. It records requested shutdown at 457.569 s and completed cleanup at
464.079 s. Its SHA-256 is
`0831375176918835A3FC5E5E27214CD87D40BF8F3474E5608EE4FA0380BC3840`.
The completed save is preserved separately. The earlier candidate's guest PANIC
occurred after requested shutdown and forced emulation-thread stop; it is a
shutdown-time observation, not evidence of a normal-gameplay crash.

Do not use `status.rejected: []` to assert compiler success: that field describes
hashes deliberately disabled through debug controls. The rejection result above
comes from searching the complete fresh log for actual native failure messages.

Offline final-source replay used five captured native programs with their actual
stage/header/resource context plus five whole production-v2 pipeline records.
All 10 jobs compiled and all 13 emitted modules passed
`spirv-val --target-env vulkan1.2`: compute 6, fragment 3, geometry 2, vertex 2.
The validator was SPIRV-Tools v2026.1-0-gfbe4f3ad, SHA-256
`14b9820bcdcfea88b0e0db5688c2fd5fe87b957497e689f3bbf535bbd0400979`;
the replay compiler SHA-256 was
`fea8a51fd18baab9bb6a712e0d8b01b0060065164855cfab83ed4fcbc7dd65be`.
Commands, module hashes and resource context remain local. The earlier cache
lacked environments for the three rejected IMAD programs; that was recorded as
missing context rather than fabricating bindings or claiming full-program replay.

After clean final shutdown, the three IMAD native hashes matched exact Program
records and real compute environments in the final cache. Their three native
and three production-v2 pipeline jobs also compiled and all six emitted compute
modules passed the same validator. Combined results are therefore 16/16 replay
jobs and 19/19 validated modules: compute 12, fragment 3, geometry 2, vertex 2.
This closes the earlier context gap without invented bindings. Cache snapshots
had two identical reads, stable metadata and valid record-header CRCs; outer
XXH3 payload hashes were not independently verified. Production-v2 payload
structure checks passed. Exact commands and context remain local in the final
session's `offline-imad-replay/validation-results.json`.

## Exercised two-stage build workflow and reusable skill

[Run 42926072741](https://github.com/FrsrG/Lichii/actions/runs/42926072741)
tested the new loop, retaining complete validation after the early package:

| Phase | Time |
| --- | ---: |
| Setup, including separately unmeasured internal tool-installer transfers | 183 s |
| Object/dependency cache restore | 11 s |
| Configure | 49 s |
| Selected runtime and replay-tool compile/link | 100 s |
| Provisional package / upload | 1 s / 9 s |
| Remaining complete milestone compile/link | 87 s |
| Complete CTest | 51 s, 10/10 passed |
| Validated package upload | 11 s |
| Object/dependency cache save | 9 s |

The early upload finished at 23:52:15 UTC, approximately 150 seconds before the
validated upload. This reduces time to begin gameplay checks; it does not claim
the total build became faster. The new source and tests differ from the earlier
83-second repaired build. Compile and link were interleaved, so separate wall
times and full-emulator peak memory remain unmeasured. Provisional cache calls
had 1,634 direct hits / 1,652 cacheable calls (98.91%), 18 misses and 37
uncacheable calls; the remaining build had 310/348 direct hits and 38 misses.
Final object-cache use was 14.80% of 1 GB. There is no measured justification
for splitting or enlarging the working cache during this investigation.

The focused contribution branch retains byte-identical `src`, replay link and
quick-test implementation from the validated source, with explanatory docs added.
Fork-specific CI and larger benchmark tooling stay on the milestone branch.
PR #1189 remained open/unmerged when rechecked; the contribution explicitly
credits its inherited implementation and identifies the extra safety fixes,
regressions and observed gameplay evidence.

The installed `lichii-compatibility` skill records diagnosis, context-preserving
replay, the measured fast loop, actual gameplay gates, attribution and provenance.
Its independent read-only forward-test caught the distinction between deliberate
hash rejection and compiler failures, and between compile-only replay and SPIR-V
validation. The resulting refinement also correlates PANIC messages with shutdown
timestamps. Skill validation passed after these changes. Public source changes
exclude game files, private keys, firmware, saves, logs, caches and screenshots.

Submitted [upstream PR #1284](https://github.com/zortos293/Lichii/pull/1284) from
`contribution/yoshi-build150`. GitHub reports the open contribution mergeable;
maintainer review/merge remains upstream's decision. Source/test/link identity
was independently reviewed against the validated gameplay revision.
