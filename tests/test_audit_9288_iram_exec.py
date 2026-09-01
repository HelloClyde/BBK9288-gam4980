#!/usr/bin/env python3
"""Self-tests for the linked IRAM execution-engine audit."""

from __future__ import annotations

from pathlib import Path
import sys
import tempfile
import unittest


sys.path.insert(0, str(Path(__file__).resolve().parent))
from audit_9288_iram_exec import audit  # noqa: E402


GOOD_MAP = """\
     800  2700100        0     1         __iram_start = .
     800  2700100        0     1         __iram_exec_engine_start = .
     808  2700108        0     1         __iram_exec_engine_end = .
     810  2700110        0     1         __iram_end = .
"""


def disassembly(instruction: str) -> str:
    return f"""\
Disassembly of section .iram:

00000800 <s6502_iram_exec_burst_asm>:
     800: 03 02         pushn %r3
     802: 00 00         {instruction}
     804: 00 00         jp %r10
     806: 03 03         popn %r3
"""


class IramAuditTest(unittest.TestCase):
    def run_audit(
        self,
        map_text: str,
        disassembly_text: str,
        **audit_options,
    ):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            map_path = root / "test.map"
            disassembly_path = root / "test.dis.txt"
            map_path.write_text(map_text, encoding="utf-8")
            disassembly_path.write_text(disassembly_text, encoding="utf-8")
            return audit(
                map_path,
                disassembly_path,
                0x16C8,
                expect_asm=True,
                required_symbol="s6502_iram_exec_burst_asm",
                **audit_options,
            )

    def test_clean_engine_passes(self) -> None:
        summary = self.run_audit(
            GOOD_MAP,
            disassembly("jp 2 <s6502_iram_exec_burst_asm+0x4>"),
        )
        self.assertEqual(summary.iram_size, 0x10)
        self.assertEqual(summary.engine_size, 8)

    def test_r15_is_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "R15"):
            self.run_audit(GOOD_MAP, disassembly("ld.w %r15, %r4"))

    def test_stack_spill_is_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "stack access/spill"):
            self.run_audit(GOOD_MAP, disassembly("ld.w [%sp+1], %r4"))

    def test_external_direct_call_is_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "leaves IRAM"):
            self.run_audit(GOOD_MAP, disassembly("call 1 <outside_helper>"))

    def test_external_relative_branch_is_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "leaves IRAM"):
            self.run_audit(GOOD_MAP, disassembly("jrne 1 <outside_branch>"))

    def test_indirect_call_is_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "shared ABI"):
            self.run_audit(GOOD_MAP, disassembly("call %r13"))

    def test_one_controlled_shared_abi_call_is_allowed(self) -> None:
        summary = self.run_audit(
            GOOD_MAP,
            disassembly("call %r13"),
            allow_indirect_call_register="r13",
            expected_indirect_calls=1,
        )
        self.assertEqual(summary.engine_instructions, 4)

    def test_controlled_call_rejects_the_wrong_register(self) -> None:
        with self.assertRaisesRegex(ValueError, "shared ABI"):
            self.run_audit(
                GOOD_MAP,
                disassembly("call %r12"),
                allow_indirect_call_register="r13",
                expected_indirect_calls=1,
            )

    def test_controlled_call_count_is_exact(self) -> None:
        with self.assertRaisesRegex(ValueError, "count mismatch"):
            self.run_audit(
                GOOD_MAP,
                disassembly("jp %r10"),
                allow_indirect_call_register="r13",
                expected_indirect_calls=1,
            )

    def test_assembly_entry_must_be_exactly_0x800(self) -> None:
        shifted = disassembly("jp %r10").replace(
            "00000800 <s6502_iram_exec_burst_asm>:",
            "00000802 <s6502_iram_exec_burst_asm>:",
        )
        with self.assertRaisesRegex(ValueError, "exactly 0x800"):
            self.run_audit(GOOD_MAP, shifted)

    def test_dispatch_table_targets_are_checked(self) -> None:
        table_map = GOOD_MAP.replace(
            "     808  2700108        0     1         "
            "__iram_exec_engine_end = .",
            "     810  2700110        0     1         "
            "__iram_exec_engine_end = .",
        )
        with_table = disassembly("jp %r10") + """\

00000808 <test_dispatch_table>:
     808: 00 08 00 00 04 08 00 00         ........
"""
        summary = self.run_audit(
            table_map,
            with_table,
            dispatch_table_symbol="test_dispatch_table",
            dispatch_table_entries=2,
        )
        self.assertEqual(summary.dispatch_entries, 2)

    def test_compact_16_bit_dispatch_table_targets_are_checked(self) -> None:
        table_map = GOOD_MAP.replace(
            "     808  2700108        0     1         "
            "__iram_exec_engine_end = .",
            "     810  2700110        0     1         "
            "__iram_exec_engine_end = .",
        )
        with_table = disassembly("jp %r10") + """\

00000808 <test_dispatch_table>:
     808: 00 08 04 08                     ....
0000080c <test_dispatch_table_end>:
"""
        summary = self.run_audit(
            table_map,
            with_table,
            dispatch_table_symbol="test_dispatch_table",
            dispatch_table_entries=2,
        )
        self.assertEqual(summary.dispatch_entries, 2)

    def test_dispatch_table_external_target_is_rejected(self) -> None:
        table_map = GOOD_MAP.replace(
            "     808  2700108        0     1         "
            "__iram_exec_engine_end = .",
            "     810  2700110        0     1         "
            "__iram_exec_engine_end = .",
        )
        with_table = disassembly("jp %r10") + """\

00000808 <test_dispatch_table>:
     808: 00 08 00 00 00 20 00 00         ..... ..
"""
        with self.assertRaisesRegex(ValueError, "entry 0x01 leaves IRAM"):
            self.run_audit(
                table_map,
                with_table,
                dispatch_table_symbol="test_dispatch_table",
                dispatch_table_entries=2,
            )

    def test_compact_dispatch_target_must_be_instruction_boundary(self) -> None:
        table_map = GOOD_MAP.replace(
            "     808  2700108        0     1         "
            "__iram_exec_engine_end = .",
            "     810  2700110        0     1         "
            "__iram_exec_engine_end = .",
        )
        with_table = disassembly("jp %r10") + """\

00000808 <test_dispatch_table>:
     808: 01 08 04 08                     ....
0000080c <test_dispatch_table_end>:
"""
        with self.assertRaisesRegex(ValueError, "instruction boundary"):
            self.run_audit(
                table_map,
                with_table,
                dispatch_table_symbol="test_dispatch_table",
                dispatch_table_entries=2,
            )

    def test_compact_dispatch_target_cannot_point_into_table_data(self) -> None:
        table_map = GOOD_MAP.replace(
            "     808  2700108        0     1         "
            "__iram_exec_engine_end = .",
            "     810  2700110        0     1         "
            "__iram_exec_engine_end = .",
        )
        with_table = disassembly("jp %r10") + """\

00000808 <test_dispatch_table>:
     808: 08 08 04 08                     ....
0000080c <test_dispatch_table_end>:
"""
        with self.assertRaisesRegex(ValueError, "instruction boundary"):
            self.run_audit(
                table_map,
                with_table,
                dispatch_table_symbol="test_dispatch_table",
                dispatch_table_entries=2,
            )

    def test_oversized_overlay_is_rejected(self) -> None:
        oversized = GOOD_MAP.replace(
            "     810  2700110        0     1         __iram_end = .",
            "    1ec9  27017c9        0     1         __iram_end = .",
        )
        with self.assertRaisesRegex(ValueError, "outside"):
            self.run_audit(oversized, disassembly("jp %r10"))


if __name__ == "__main__":
    unittest.main()
