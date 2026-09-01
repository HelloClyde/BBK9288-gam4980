#!/usr/bin/env python3
"""Source-level coverage checks for the resident S1C33 opcode table."""

from __future__ import annotations

from pathlib import Path
import re
import unittest


PROJECT_ROOT = Path(__file__).resolve().parents[1]
ASSEMBLY = PROJECT_ROOT / "src" / "s6502_iram_asm.S"

ADDED_HANDLERS = {
    0x05: ".Lop05",
    0x06: ".Lop06",
    0x0A: ".Lop0A",
    0x0D: ".Lop0D",
    0x2A: ".Lop2A",
    0x49: ".Lop49",
    0x4E: ".Lop4E",
    0x6E: ".Lop6E",
    0x84: ".Lop84",
    0xA4: ".LopA4",
    0xA6: ".LopA6",
    0xAC: ".LopAC",
    0xC0: ".LopC0",
    0xC5: ".LopC5",
    0xE6: ".LopE6",
    0xE8: ".LopE8",
    0xEE: ".LopEE",
}


def dispatch_entries(source: str) -> list[str]:
    table = source.split("s6502_iram_dispatch_table:", 1)[1]
    table = table.split("s6502_iram_dispatch_table_end:", 1)[0]
    entries: list[str] = []
    for values in re.findall(r"(?:DISPATCH16|\.long)\s+([^\n]+)", table):
        entries.extend(value.strip() for value in values.split(","))
    return entries


class IramAsmHotOpcodeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = ASSEMBLY.read_text(encoding="utf-8")
        cls.entries = dispatch_entries(cls.source)

    def test_dispatch_table_remains_complete(self) -> None:
        self.assertEqual(len(self.entries), 256)

    def test_new_hot_opcodes_use_their_resident_handlers(self) -> None:
        for opcode, handler in ADDED_HANDLERS.items():
            with self.subTest(opcode=f"{opcode:02x}"):
                self.assertEqual(self.entries[opcode], handler)
                self.assertIn(f"{handler}:\n", self.source)

    def test_supported_coverage_grows_from_64_to_81(self) -> None:
        supported = sum(
            entry != ".Lslow_opcode" and entry != ".Lsuper"
            for entry in self.entries
        )
        self.assertEqual(supported, 64 + len(ADDED_HANDLERS))

    def test_shadow_opcode_is_not_counted_as_a_guest_handler(self) -> None:
        self.assertEqual(self.entries[0x02], ".Lsuper")
        self.assertIn(".Lsuper:\n", self.source)

    def test_dispatch_table_uses_resolved_16_bit_iram_addresses(self) -> None:
        table = self.source.split("s6502_iram_dispatch_table:", 1)[1]
        table = table.split("s6502_iram_dispatch_table_end:", 1)[0]
        macro = self.source.split(".macro DISPATCH16", 1)[1].split(
            ".endm", 1
        )[0]
        self.assertNotIn(".long", table)
        self.assertEqual(table.count("DISPATCH16 "), 64)
        self.assertNotIn(".short", macro)
        self.assertEqual(macro.count(".long"), 2)
        self.assertIn("<< 16", macro)
        self.assertIn("- s6502_iram_exec_burst_asm + 0x800", macro)


if __name__ == "__main__":
    unittest.main()
