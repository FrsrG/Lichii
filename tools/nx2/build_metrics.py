#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Summarize recorded GitHub job timestamps and build logs without contacting GitHub.

  build_metrics.py --job job.json --run run.json --log job.log --case cold --out report.json

Job and run JSON are unmodified GitHub REST responses. Missing measurements remain null;
interleaved Ninja completion lines cannot separate compile and link wall time or peak memory.
"""

import argparse
import datetime as dt
import hashlib
import json
import re
from pathlib import Path

ANSI = re.compile(r"\x1b\[[0-9;]*m")
STAMP = re.compile(r"^\d{4}-\d\d-\d\dT\S+\s+")
NINJA = re.compile(r"\[(\d+)/(\d+)\]\s+(.*)")
RATIO = re.compile(r"^\s*(Cacheable calls|Uncacheable calls|Hits|Misses|Direct|Preprocessed):\s+(\d+)\s*/\s*(\d+)")


def timestamp(value):
    return dt.datetime.fromisoformat(value.replace("Z", "+00:00")) if value else None


def phase(name):
    lowered = name.lower()
    if lowered.startswith("post "):
        return "cleanup"
    if lowered.startswith("restore"):
        return "cache_restore"
    if lowered.startswith("save") and any(word in lowered for word in ("cache", "dependenc", "object")):
        return "cache_save"
    if lowered.startswith("configure"):
        return "configure"
    if lowered.startswith("build"):
        return "compile_and_link"
    if lowered.startswith("package"):
        return "package"
    if lowered.startswith("upload"):
        return "artifact_upload"
    if lowered.startswith("run tests"):
        return "tests"
    if lowered.startswith("check ") and not lowered.startswith("check out "):
        return "verification"
    return "setup"


def parse_log(text):
    stats, blocks, ninja = None, [], []
    cache_events = []
    for raw in text.splitlines():
        line = STAMP.sub("", ANSI.sub("", raw))
        match = NINJA.search(line)
        if match:
            ninja.append({"completed": int(match[1]), "total": int(match[2]), "action": match[3]})
        if "Cache restored from key:" in line or "Cache saved with key:" in line or "Cache not found for input keys:" in line:
            cache_events.append(line.strip())
        if line.startswith("Cacheable calls:"):
            stats = {}
            blocks.append(stats)
        if stats is None:
            continue
        match = RATIO.match(line)
        if match:
            key = match[1].lower().replace(" ", "_")
            # The verbose local-storage section repeats hits/misses; these values are identical.
            stats.setdefault(key, int(match[2]))
            if key == "cacheable_calls":
                stats["compiler_calls"] = int(match[3])
        match = re.match(r"\s*Cache size \(GB\):\s*([\d.]+)\s*/\s*([\d.]+)", line)
        if match:
            stats["size_gb_reported"] = float(match[1])
            stats["limit_gb_reported"] = float(match[2])
        match = re.match(r"\s*(Cleanups|Cleanups performed|Evictions):\s*(\d+)", line)
        if match:
            stats[match[1].lower().replace(" ", "_")] = int(match[2])
    return {
        "ccache_reports": blocks,
        "cache_events": cache_events,
        "ninja": {
            "observed_compile_commands": sum(bool(re.match(r"Building (?:CXX|C|ASM|RC) object\b", x["action"])) for x in ninja),
            "observed_link_commands": sum(x["action"].startswith("Linking ") for x in ninja),
            "last_progress": {k: ninja[-1][k] for k in ("completed", "total")} if ninja else None,
        },
    }


def report(job, log="", run=None, case="unknown", as_of=None):
    as_of = as_of or dt.datetime.now(dt.timezone.utc)
    steps, totals = [], {}
    for step in job.get("steps", []):
        start, end = timestamp(step.get("started_at")), timestamp(step.get("completed_at"))
        skipped = step.get("conclusion") == "skipped"
        seconds = (end - start).total_seconds() if start and end and not skipped else None
        group = phase(step["name"])
        if seconds is not None:
            totals[group] = totals.get(group, 0) + seconds
        steps.append({
            "name": step["name"], "phase": group, "status": step.get("status"),
            "conclusion": step.get("conclusion"), "seconds": seconds,
            "elapsed_as_of_report_s": (as_of - start).total_seconds()
            if start and not end and step.get("status") == "in_progress" else None,
        })
    result = {
        "schema_version": 1, "case": case, "recorded_at_utc": as_of.isoformat(),
        "source_commit": (run or {}).get("head_sha"), "run_id": (run or {}).get("id"),
        "source_branch": (run or {}).get("head_branch"), "run_event": (run or {}).get("event"),
        "job_id": job.get("id"), "job_status": job.get("status"),
        "job_conclusion": job.get("conclusion"), "steps": steps,
        "completed_phase_seconds": totals, "compile_seconds": None, "link_seconds": None,
        "peak_memory_bytes": None,
        "limitations": [
            "Case classification is supplied by the caller, not inferred from a fast restore.",
            "The original build step combines compile and link; parallel Ninja completion times do not separate their wall time.",
            "Peak memory was not recorded by this workflow. Missing cache counters remain unmeasured, not zero.",
            "Setup can include a tool installer's internal cache transfers; only separately named restore/save steps are timed as cache transfers.",
            "A Ninja no-op makes zero compiler calls and cannot establish a ccache hit rate.",
        ],
    }
    result.update(parse_log(log))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--job", required=True, type=Path)
    parser.add_argument("--run", type=Path)
    parser.add_argument("--log", type=Path)
    parser.add_argument("--cache-state", type=Path, help="recorded cache inventory, including actual branch refs")
    parser.add_argument("--case", choices=("cold", "unchanged", "shader-edit", "unknown"), default="unknown")
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    job = json.loads(args.job.read_text(encoding="utf-8-sig"))
    if "steps" not in job:
        parser.error("--job must contain one GitHub job response with steps")
    run = json.loads(args.run.read_text(encoding="utf-8-sig")) if args.run else None
    log = args.log.read_text(encoding="utf-8-sig", errors="replace") if args.log else ""
    result = report(job, log, run, args.case)
    if args.cache_state:
        cache_state = json.loads(args.cache_state.read_text(encoding="utf-8-sig"))
        result["cache_inventory_observed_at"] = cache_state.get("observed_at")
        result["observed_cache_entries"] = [
            {key: entry.get(key) for key in ("key", "ref", "version", "size_in_bytes", "created_at", "last_accessed_at")}
            for entry in cache_state.get("actions_caches", [])]
    result["input_sha256"] = {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                              for path in (args.job, args.run, args.log, args.cache_state) if path}
    output = json.dumps(result, indent=2) + "\n"
    if args.out:
        args.out.write_text(output, encoding="utf-8")
    else:
        print(output, end="")


if __name__ == "__main__":
    main()
