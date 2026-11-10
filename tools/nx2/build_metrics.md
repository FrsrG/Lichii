# Build measurements for the Yoshi investigation

`build_metrics.py` reads recorded GitHub REST job/run JSON and decoded job logs.
It performs no network requests and changes no caches or build outputs.

```powershell
python tools/nx2/build_metrics.py --job job.json --run run.json --log job.log `
  --cache-state caches.json --case cold --out report.json
python tools/nx2/test_build_metrics.py
```

`--job` takes one job response, not a workflow's job list. `--cache-state` is
optional and takes the cache-list response, optionally augmented with an
`observed_at` UTC timestamp. Record `key`, `ref`, version and size together:
restore-key prefix matching does not bypass GitHub's branch scopes.

Missing measurements remain `null` or absent. Skipped tests are not a measured
zero-second test pass. Parallel Ninja status lines count observed compile/link
commands; they cannot separate compile and link wall time. The existing workflow
did not capture peak memory. A Ninja no-op has zero compiler calls and is not a
ccache hit-rate experiment.

## First CI run, 2026-11-10

Source `db65715f3401ba2cd0ecda91f677e06ff6e20e85`,
[run 42910339018](https://github.com/FrsrG/Lichii/actions/runs/42910339018),
Windows clang-cl 22.1.8. This was the first fork compiler/dependency-cache run;
the log explicitly reports no matching cache at restore.

| Measurement | Result |
| --- | ---: |
| Setup step time (including checkout and installers) | 211 s |
| Combined dependency/object restore | 0 s, miss |
| Configure | 78 s |
| Combined compile/link | 3,232 s (53 min 52 s), failed |
| Observed Ninja compile commands | 2,038 |
| Observed Ninja link commands | 54 |
| Cache save | 11 s |
| Saved combined cache archive | 285,399,123 bytes |
| Tests, package, artifact upload | Skipped |

The last failure was `shader-sweep.exe` linking IP Helper functions referenced
by `core.lib(network_interface.cpp.obj)`. The log shows `lichii.exe` linked at
`[2349/2353]`; this is not proof of gameplay. The saved cache belongs to
`refs/heads/compat/yoshi-build150`. The shell exited before `ccache --show-stats`,
so its hit/miss/cleanup/eviction counters are unknown. Peak memory and the
compile/link time split are also unknown.

A separate upstream reference job had 1,556 direct hits and 434 misses (78.19%
hit rate), with reported cache size 0.7 GB of 1 GB. That different source/build
is context, not a controlled before/after comparison.

## Local standalone cache experiment

Dedicated worktree: `tooling/build-measurements` at
`34900146236dc29d1931d913e93f5eaa9662aa4f`. Its production headers match the
reviewed Yoshi source. The test target includes the existing Yoshi compute,
geometry-exit and compute-lowering test files and the repository-pinned Catch2.

Toolchain: MSVC 19.44.35229, Windows SDK 10.0.26100.0, CMake 3.31.6-msvc6,
Ninja 1.12.1, ccache 4.14.2, four jobs, Release, LTO disabled, compiler content
checking, fresh task-owned build/cache directories. The ccache archive SHA-256
matches the existing CI pin. This compiler is different from CI's clang-cl.

| Case | Wall time | Actual compiler-cache result |
| --- | ---: | --- |
| Fresh configure | 3.631 s | Pinned Catch2 fetched |
| Cold selected-target build | 44.181 s | 110 misses, zero hits |
| Unchanged Ninja rebuild | 0.106 s | Zero compiler calls |
| Four test objects removed, source unchanged | 0.344 s | 4/4 direct hits |
| Representative header edit (`216` to `216U`) | 14.191 s | Three misses |
| Original header restored byte for byte | 0.386 s | 3/3 direct hits |

CTest passed both entries after cold, edited and restored builds: the lowering
tests and the empty-selection failure check. Test command wall times were
0.176 s, 0.174 s and 0.168 s respectively. Every cache case reported zero cleanup
passes; ccache does not supply a separate eviction count in these captured
counters. The maximum cache size observed was 5,152 KiB. Peak process memory was
not sampled by this bounded experiment.

The cold case compiled Catch2 plus four test objects; the forced unchanged case
compiled four test objects. These are not equal workloads, so do not derive a
speedup ratio by dividing those wall times. Raw command arguments, logs,
machine-readable counters and provenance are kept locally under
`lichii-investigation/benchmarks/ampere-ccache-msvc-20261110`.

## Commands for the measured local stage

Use a Visual Studio developer environment and a task-owned compiler cache.
The paths below are examples; retain one consistent source/build/cache path.

```powershell
. tools/windows/load-msvc-env.ps1
$cacheExe = 'C:/path/to/verified/ccache.exe'
$env:CCACHE_DIR = 'C:/path/to/ampere-cache'
$env:CCACHE_MAXSIZE = '1G'
$env:CCACHE_COMPILERCHECK = 'content'
cmake -S tools/ampere-tests -B build/ampere-ninja -G Ninja `
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=cl.exe `
  "-DCMAKE_CXX_COMPILER_LAUNCHER=$cacheExe" -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF
& $cacheExe --zero-stats
cmake --build build/ampere-ninja --target ampere-quick-tests --parallel 4
& $cacheExe --print-stats --format=json
ctest --test-dir build/ampere-ninja --output-on-failure --no-tests=error
```

For the matching CI compiler, substitute a verified clang-cl 22.1.8 path and
measure it separately. Do not claim the MSVC measurements are clang-cl timings.
Store statistics in an always-run step or `finally` block so failed builds still
leave evidence. Ccache 4.14.2 supports JSON through `--print-stats --format=json`.

## Changes justified for future workflows

1. Build selected emulator/replay targets and upload a clearly provisional
   package first, then retain the complete build and CTest checks in the same
   source/configuration/build directory. GitHub artifacts are downloadable after
   their upload step. Use distinct immutable provisional/validated artifact names
   and record the source and executable hashes at each upload.
2. Keep the immediate retry on `compat/yoshi-build150` with the existing combined
   restore paths/prefix. Sibling branches cannot restore its cache, and changing
   cached paths changes cache-version compatibility. A split dependency/object
   layout should be seeded from a compatible restored archive, then benchmarked.
3. Keep the 1 GB compiler-cache cap until full-run counters show eviction or
   useful retained objects exceed it. The first archive transfer took only 11 s;
   no measured bottleneck currently justifies a cache-size increase.
4. Preserve separate timing and always-run cache statistics for each stage.
   Measure compiler/SDK identity after any restored installation. Peak-memory
   instrumentation and parallel-job comparisons remain future measurements,
   not results from this run.

Sources: [GitHub artifact availability](https://github.blog/changelog/2024-02-12-github-actions-artifacts-v4-is-now-generally-available/),
[cache scopes and versions](https://docs.github.com/en/actions/reference/workflows-and-actions/dependency-caching),
and [ccache 4.14.2 options](https://ccache.dev/manual/4.14.2.html).
