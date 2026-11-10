#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise package-state gates with temporary data; no executable is launched."""

import hashlib
import json
import os
import shutil
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

SCRIPT = Path(__file__).with_name("write-compatibility-provenance.ps1")
SHELL = shutil.which("pwsh") or shutil.which("powershell")
FILES = ("lichii.exe", "eden-cli.exe", "shader-sweep.exe", "spirv-val.exe", "nxformat.dll")


@unittest.skipUnless(SHELL, "PowerShell is required")
class ProvenanceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for name in FILES:
            (self.root / name).write_bytes(f"non-executable fixture: {name}".encode())
        self.toolchain = self.root / "toolchain.json"
        self.toolchain.write_text('{"clang":"fixture; no compiler is executed"}')
        self.report = self.root / "tests.xml"

    def invoke(self, stage, sha=None):
        environment = os.environ.copy()
        environment.pop("GITHUB_SHA", None)
        if sha:
            environment["GITHUB_SHA"] = sha
        return subprocess.run(
            [SHELL, "-NoProfile", "-File", str(SCRIPT), "-Stage", stage,
             "-BinDirectory", str(self.root), "-ToolchainFile", str(self.toolchain),
             "-TestReport", str(self.report)], capture_output=True, text=True, env=environment)

    def manifest(self):
        return json.loads((self.root / "COMPATIBILITY_PROVENANCE.json").read_text(encoding="utf-8-sig"))

    def junit(self, count=2, failures=0, skipped=0):
        self.report.write_text(f'<testsuite tests="{count}" failures="{failures}" errors="0" skipped="{skipped}"/>')
        os.utime(self.report, (time.time() + 2, time.time() + 2))

    def provisional(self):
        result = self.invoke("provisional")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_provisional_is_explicit_and_hashed(self):
        self.provisional()
        manifest = self.manifest()
        self.assertEqual(manifest["validation_state"], "provisional")
        self.assertEqual(manifest["gameplay_state"], "unverified")
        self.assertIsNone(manifest["ctest"])
        self.assertEqual(manifest["runtime_sha256"]["lichii.exe"], hashlib.sha256((self.root / "lichii.exe").read_bytes()).hexdigest())

    def test_validated_records_final_bytes_and_prior_bytes(self):
        self.provisional()
        original = self.manifest()["runtime_sha256"]["lichii.exe"]
        (self.root / "lichii.exe").write_bytes(b"different non-executable fixture")
        self.junit()
        result = self.invoke("validated")
        self.assertEqual(result.returncode, 0, result.stderr)
        manifest = self.manifest()
        self.assertEqual(manifest["validation_state"], "validated")
        self.assertEqual(manifest["ctest"]["total"], 2)
        self.assertEqual(manifest["provisional_runtime_sha256"]["lichii.exe"], original)
        self.assertTrue(manifest["changed_since_provisional"]["lichii.exe"])

    def test_zero_all_skipped_or_failed_tests_cannot_validate(self):
        self.provisional()
        for count, failures, skipped in ((0, 0, 0), (2, 0, 2), (2, 1, 0)):
            with self.subTest(count=count, failures=failures, skipped=skipped):
                self.junit(count, failures, skipped)
                self.assertNotEqual(self.invoke("validated").returncode, 0)
                self.assertEqual(self.manifest()["validation_state"], "provisional")

    def test_old_report_cannot_validate_new_package(self):
        self.provisional()
        self.junit()
        os.utime(self.report, (1, 1))
        self.assertNotEqual(self.invoke("validated").returncode, 0)

    def test_workflow_source_mismatch_is_an_error(self):
        self.assertNotEqual(self.invoke("provisional", "0" * 40).returncode, 0)
        self.assertFalse((self.root / "COMPATIBILITY_PROVENANCE.json").exists())


if __name__ == "__main__":
    unittest.main()
