#!/usr/bin/env python3
"""Synthetic captures exercising the real production interpreter replay helper."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

REPLAY = Path(sys.argv.pop(1)).resolve()


def instruction(*, end=False, op=(0, 0), dst=(24, 24), a=(24, 24), b=(24, 24),
                imm=(0, 0), method=(0, 0), emit=(0, 0)):
    bits = int(end)
    for lane in range(2):
        for offset, value in ((10, op[lane]), (15, dst[lane]),
                              (20, a[lane]), (25, b[lane])):
            bits |= value << (offset + lane * 36)
        bits |= imm[lane] << (30 + lane * 36)
        bits |= method[lane] << (82 + lane * 7)
        bits |= emit[lane] << (85 + lane * 7)
    return struct.pack("<III", bits >> 64, (bits >> 32) & 0xffffffff, bits & 0xffffffff)


def fifo(method, end=False):
    return instruction(end=end, imm=(method, 0), method=(5, 0), emit=(3, 0))


def emitted(method, value):
    return (2, method, value, 1, 0, 0, 0)


class ReplayTests(unittest.TestCase):
    def run_capture(self, code, parameters, events, *, dma=(), after=None, consumed=None,
                    final_parameters=None, total_events=None, total_dma=None, error="", start=0):
        with tempfile.TemporaryDirectory(prefix="mme-replay-test-") as folder:
            prefix = Path(folder) / "capture"
            raw = b"".join(code)
            self.assertLessEqual(len(raw), 0x8000 * 4)
            files = {
                ".code": raw.ljust(0x8000 * 4, b"\0"),
                ".parameters": struct.pack(f"<{len(parameters)}I", *parameters),
                ".dram-before": bytes(0xc00 * 4),
                ".dram-after": struct.pack("<3072I", *(after or [0] * 0xc00)),
                ".events": b"".join(struct.pack("<6IQ", *e) for e in events),
                ".dma-words": struct.pack(f"<{len(dma)}I", *dma),
            }
            for suffix, data in files.items():
                prefix.with_name(prefix.name + suffix).write_bytes(data)
            metadata = {
                "format": "split-front-mme-failure-v1", "byte_order": "little", "start": start,
                "code_generation_before": 1, "code_generation_after": 1,
                "parameter_count_before": len(parameters),
                "parameter_count_after": len(parameters) if final_parameters is None else final_parameters,
                "consumed": len(parameters) if consumed is None else consumed, "attempted_pc": start,
                "events_captured": len(events),
                "events_total": len(events) if total_events is None else total_events,
                "dma_words_captured": len(dma),
                "dma_words_total": len(dma) if total_dma is None else total_dma, "error": error,
            }
            prefix.with_suffix(".txt").write_text("".join(f"{k}={v}\n" for k, v in metadata.items()))
            result = subprocess.run([str(REPLAY), str(prefix)], capture_output=True, text=True, timeout=10)
            return result.returncode, json.loads(result.stdout)

    def test_exact_emit(self):
        rc, report = self.run_capture([fifo(0x100, True), instruction()], [42], [emitted(0x100, 42)])
        self.assertEqual((rc, report["status"]), (0, "match"))
        self.assertIsNone(report["first_data_ram_difference"])

    def test_actual_register_callback(self):
        code = instruction(end=True, op=(22, 0), a=(25, 24), imm=(0x100, 0x200),
                           method=(6, 0), emit=(1, 0))
        rc, report = self.run_capture([code, instruction()], [],
                                      [(1, 0x100, 77, 0, 0, 0, 0), emitted(0x200, 77)])
        self.assertEqual((rc, report["matched_event_prefix"]), (0, 2))

    def test_recorded_dma_appends_fifo(self):
        code = [fifo(0x158), fifo(0x200, True), fifo(0x201)]
        events = [emitted(0x158, 2), (3, 0x158, 0, 2, 0, 2, 0x12340000),
                  emitted(0x200, 11), emitted(0x201, 12)]
        rc, report = self.run_capture(code, [2], events, dma=[11, 12], consumed=3, final_parameters=3)
        self.assertEqual((rc, report["status"]), (0, "match"))

    def ram_capture(self, incorrect=False):
        read = instruction(end=True, op=(30, 0), a=(25, 24), imm=(5, 0x200),
                           method=(6, 0), emit=(1, 0))
        after = [0] * 0xc00
        after[5:7] = [99 if incorrect else 11, 12]
        return self.run_capture([fifo(0x157), read, instruction()], [2],
            [emitted(0x157, 2), (3, 0x157, 5, 2, 0, 2, 0x12340000), emitted(0x200, 11)],
            dma=[11, 12], after=after)

    def test_recorded_dma_updates_real_interpreter_data_ram(self):
        rc, report = self.ram_capture()
        self.assertEqual((rc, report["status"]), (0, "match"))

    def test_final_ram_difference(self):
        rc, report = self.ram_capture(True)
        self.assertEqual((rc, report["first_data_ram_difference"]), (1, 5))

    def test_first_emit_divergence(self):
        rc, report = self.run_capture([fifo(0x100, True), instruction()], [42], [emitted(0x100, 43)])
        self.assertEqual((rc, report["status"], report["matched_event_prefix"]), (1, "mismatch", 0))

    def test_event_trace_exhaustion_is_missing_context(self):
        rc, report = self.run_capture([fifo(0x100, True), fifo(0x101)], [42, 43],
                                      [emitted(0x100, 42)], total_events=2)
        self.assertEqual((rc, report["status"], report["matched_event_prefix"]),
                         (2, "missing_context", 1))

    def test_early_return_with_captured_events_is_divergence_even_if_truncated(self):
        rc, report = self.run_capture([fifo(0x100, True), instruction()], [42],
            [emitted(0x100, 42), emitted(0x101, 43)], total_events=3)
        self.assertEqual((rc, report["status"], report["matched_event_prefix"]),
                         (1, "mismatch", 1))
        self.assertTrue(report["capture_truncated"])

    def test_truncated_dma_words_are_never_invented(self):
        rc, report = self.run_capture([fifo(0x158, True), instruction()], [2],
            [emitted(0x158, 2), (3, 0x158, 0, 2, 0, 1, 0x12340000)], dma=[11], total_dma=2)
        self.assertEqual((rc, report["status"]), (2, "missing_context"))

    def test_step_limits_do_not_prove_equivalent_budgets(self):
        rc, report = self.run_capture([instruction(), instruction(), instruction(op=(24, 0))], [], [],
                                      error="instruction limit exceeded", start=2)
        self.assertEqual((rc, report["status"]), (3, "inconclusive_budget"))
        self.assertEqual(report["interpreter_pc"], 2)


if __name__ == "__main__":
    unittest.main()
