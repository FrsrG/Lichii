# Yoshi and the Mysterious Book: Build 150 investigation

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

The current log identifies `nx2-build-150` and boots the full game's program ID.
The older rotated log identifies Build 138; keep these sessions distinct.

| Build 150 elapsed time | Observed rejection | Interpretation |
| --- | --- | --- |
| 44.907 s | graphics op `321`, `ff087321 00007007 00000c00 000e2800`: instruction before exit setup state is dead | Geometry exit liveness does not model an attribute load. |
| 71.978 s | graphics op `f63`, `00247f63 3000040e 0c1e1f0e 000f4200`: native texture gather mode | Uniform-handle bindless gather with offsets is rejected. |
| 72.504 s | compute op `364`, `02101364 38000010 001e0f12 00014200`: unsupported native opcode | Register-handle bindless gather is missing. |
| 72.562 s | compute op `59f`, `0800759f a0c00201 0310e000 000fe200`: native surface reduction mode | A 3D surface reduction is rejected. |

These are direct local observations. Their contribution to the final black
image remains a hypothesis until a before/after frame comparison. The log
continues presenting frames, consistent with the user's live dialogue and FPS.
Missing avatar and optional Streamline low-latency DLL warnings are not evidence
that they cause the missing 3D world.

The current process has no listening debug server. It was left running.

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
   clang-cl/Qt toolchain as upstream. This PC has no C++ toolchain installed;
   a fork-only GitHub Actions workflow produces the candidate package.
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
   sustained run. Check another game sharing changed instruction forms before
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
  Compilation/test result and gameplay result are pending. Later test and
  buffer-boundary commits do not change the captured 3D/R8 instruction.
- Test profile: copied locally into the candidate's portable `user` directory;
  configuration NAND, SDMC, load, and dump paths were rewritten to that directory.
  Shader caches were not copied. The original application was left running;
  its live save snapshot is not an atomic shutdown snapshot.

The independent reviewer audits progress every 15 minutes and checks that work
continues toward an observed gameplay result. Its first audit prioritized the
Windows package, exact captured-instruction coverage, and avoiding unrelated
tooling work. It identified the buffer-coordinate boundary regression above.
