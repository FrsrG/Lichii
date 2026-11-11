# Kirby Air Riders investigation

Goal: observed playable gameplay, isolated from the Yoshi contribution. Starting
runtime is Yoshi milestone `9d71cf868a14d6217442702ddb53a3f47626580d`;
its production source is unchanged by the later documentation-only milestone.
Game: full `040077201BE50000`, locally supplied v1.3.3 NUP.

The owner reports choppy intro playback despite a 60 FPS display, followed by a
crash when skipping. Preserve frames and decode timing separately: the presentation
counter does not measure whether the decoded movie advances smoothly.

Original completed log ends after decoder Stop207 / Finalize115 and the
unimplemented gsv903 report. That report is auto-stubbed and debug assertions
are disabled, so its severity does not establish the host crash cause.
The Windows minidump instead records EXCEPTION_STACK_OVERFLOW. An isolated
cold-profile reproduction without injected input also crashes early, with the
same repeated executable RVAs in its dump. Existing decoder readiness gating
from merged PR48 is already present; no duplicate port is justified.

Private logs, crash dumps, settings, save snapshots, captures and stack analysis
remain under the local `kirby-investigation` directory and do not enter Git.

## Diagnostic build milestone

The first separate branch build adds linker PDB output without changing emulator
behavior or recompiling all source with debug flags. It is a diagnostic baseline,
not a claimed fix. Preserve compiler/toolchain/cache settings and both provisional
and complete checks. A fork-internal draft PR targets the existing cached milestone
branch solely to make its base-branch cache accessible; it must not be merged into
the Yoshi branch. The creator-facing contribution will be separate after validation.
