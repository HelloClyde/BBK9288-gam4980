#!/usr/bin/env python3
"""Keep C-side IRAM slow diagnostics synchronized with the ASM table."""

from __future__ import annotations

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]


def asm_supported_opcodes(text: str) -> set[int]:
    table = text.split("s6502_iram_dispatch_table:", 1)[1].split(
        "s6502_iram_dispatch_table_end:", 1
    )[0]
    supported: set[int] = set()
    covered: set[int] = set()
    row_pattern = re.compile(
        r"/\*\s*([0-9a-fA-F]{2})\s*\*/\s*"
        r"(?:\.long|DISPATCH16)\s*([^\r\n]+)"
    )
    for match in row_pattern.finditer(table):
        base = int(match.group(1), 16)
        entries = [entry.strip() for entry in match.group(2).split(",")]
        if len(entries) != 4:
            raise AssertionError(f"ASM dispatch row {base:02X} has {len(entries)} entries")
        for offset, entry in enumerate(entries):
            opcode = base + offset
            covered.add(opcode)
            if entry != ".Lslow_opcode":
                if opcode == 0x02 and entry == ".Lsuper":
                    # $02 is never advertised as a guest opcode.  It exists
                    # only in the private shadow decode pages; original game
                    # bytes and the C slow-opcode diagnostics still see the
                    # real first opcode of each matched template.
                    continue
                if not re.fullmatch(r"\.Lop[0-9A-Fa-f]{2}", entry):
                    raise AssertionError(
                        f"unexpected ASM dispatch target for {opcode:02X}: {entry}"
                    )
                supported.add(opcode)
    if covered != set(range(256)):
        missing = sorted(set(range(256)) - covered)
        raise AssertionError(f"ASM dispatch table does not cover 256 opcodes: {missing}")
    return supported


def c_supported_opcodes(text: str) -> set[int]:
    body = text.split("static int s6502_iram_opcode_supported", 1)[1].split(
        "static int s6502_iram_opcode_has_word_operand", 1
    )[0]
    return {int(value, 16) for value in re.findall(r"case\s+0x([0-9a-fA-F]{2})", body)}


def main() -> None:
    asm = asm_supported_opcodes(
        (ROOT / "src" / "s6502_iram_asm.S").read_text(encoding="utf-8")
    )
    c = c_supported_opcodes(
        (ROOT / "src" / "gam4980_core.c").read_text(encoding="utf-8")
    )
    if asm != c:
        only_asm = " ".join(f"{opcode:02X}" for opcode in sorted(asm - c))
        only_c = " ".join(f"{opcode:02X}" for opcode in sorted(c - asm))
        raise AssertionError(
            "IRAM diagnostic opcode set differs from ASM dispatch table; "
            f"only ASM=[{only_asm}], only C=[{only_c}]"
        )
    print(f"IRAM diagnostic opcode set synchronized: {len(c)} handlers")


if __name__ == "__main__":
    main()
