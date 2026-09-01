#!/usr/bin/env python3
"""Audit the linked 9288 IRAM overlay and assembly execution engine."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import re
import sys


MAP_SYMBOLS = (
    "__iram_start",
    "__iram_end",
    "__iram_exec_engine_start",
    "__iram_exec_engine_end",
)


@dataclass(frozen=True)
class AuditSummary:
    iram_size: int
    engine_size: int
    engine_instructions: int
    dispatch_entries: int


def read_map_symbols(path: Path) -> dict[str, int]:
    wanted = set(MAP_SYMBOLS)
    values: dict[str, int] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        for name in tuple(wanted):
            if not line.endswith(f" {name} = ."):
                continue
            try:
                values[name] = int(line.split()[0], 16)
            except (IndexError, ValueError) as exc:
                raise ValueError(f"invalid {name} entry in {path}") from exc
            wanted.remove(name)
    if wanted:
        raise ValueError(
            f"missing linker symbols in {path}: {', '.join(sorted(wanted))}"
        )
    return values


def parse_disassembly(
    text: str,
) -> tuple[dict[str, int], list[tuple[int, str]]]:
    labels: dict[str, int] = {}
    instructions: list[tuple[int, str]] = []
    label_pattern = re.compile(
        r"^\s*([0-9a-fA-F]+)\s+<([^>]+)>:\s*$"
    )
    instruction_pattern = re.compile(
        r"^\s*([0-9a-fA-F]+):(?:\s+[0-9a-fA-F]{2})+\s+(.+?)\s*$"
    )
    for line in text.splitlines():
        match = label_pattern.match(line)
        if match is not None:
            labels[match.group(2)] = int(match.group(1), 16)
            continue
        match = instruction_pattern.match(line)
        if match is not None:
            instruction = match.group(2).split(";", 1)[0].rstrip()
            instructions.append((int(match.group(1), 16), instruction))
    return labels, instructions


def parse_disassembly_bytes(text: str) -> dict[int, int]:
    """Recover raw section bytes from llvm-objdump's code/data rows."""
    result: dict[int, int] = {}
    pattern = re.compile(
        r"^\s*([0-9a-fA-F]+):\s+((?:[0-9a-fA-F]{2}\s+)+)"
    )
    for line in text.splitlines():
        match = pattern.match(line)
        if match is None:
            continue
        address = int(match.group(1), 16)
        for offset, value in enumerate(match.group(2).split()):
            result[address + offset] = int(value, 16)
    return result


def annotated_target(annotation: str, labels: dict[str, int]) -> int | None:
    match = re.fullmatch(r"(.+?)(?:\+0x([0-9a-fA-F]+))?", annotation)
    if match is None:
        return None
    base = labels.get(match.group(1))
    if base is None:
        return None
    return base + (int(match.group(2), 16) if match.group(2) else 0)


def audit(
    map_path: Path,
    disassembly_path: Path,
    max_size: int,
    expect_asm: bool = False,
    required_symbol: str | None = None,
    dispatch_table_symbol: str | None = None,
    dispatch_table_entries: int = 256,
    allow_indirect_call_register: str | None = None,
    expected_indirect_calls: int = 0,
) -> AuditSummary:
    symbols = read_map_symbols(map_path)
    iram_start = symbols["__iram_start"]
    iram_end = symbols["__iram_end"]
    engine_start = symbols["__iram_exec_engine_start"]
    engine_end = symbols["__iram_exec_engine_end"]
    iram_size = iram_end - iram_start
    engine_size = engine_end - engine_start
    errors: list[str] = []
    allowed_indirect_calls = 0

    if expected_indirect_calls < 0:
        errors.append("expected indirect-call count cannot be negative")
    if expected_indirect_calls and allow_indirect_call_register is None:
        errors.append(
            "expected indirect calls require an allowed call register"
        )

    if not (0 <= iram_size <= max_size):
        errors.append(
            f"IRAM size {iram_size} is outside 0..{max_size} bytes"
        )
    if not (iram_start <= engine_start <= engine_end <= iram_end):
        errors.append(
            "IRAM engine range is outside the overlay: "
            f"0x{engine_start:x}..0x{engine_end:x} vs "
            f"0x{iram_start:x}..0x{iram_end:x}"
        )
    if expect_asm and engine_size == 0:
        errors.append("assembly IRAM execution engine is empty")

    disassembly_text = disassembly_path.read_text(encoding="utf-8")
    labels, all_instructions = parse_disassembly(disassembly_text)
    engine_instructions = [
        (address, instruction)
        for address, instruction in all_instructions
        if engine_start <= address < engine_end
    ]
    if expect_asm and not engine_instructions:
        errors.append("assembly IRAM execution engine has no instructions")
    if required_symbol is not None:
        address = labels.get(required_symbol)
        if address is None:
            errors.append(f"required IRAM symbol is absent: {required_symbol}")
        elif not (engine_start <= address < engine_end):
            errors.append(
                f"required IRAM symbol {required_symbol} is outside the "
                f"engine range at 0x{address:x}"
            )
        elif (
            expect_asm
            and required_symbol == "s6502_iram_exec_burst_asm"
            and address != 0x800
        ):
            # The compact dispatch table stores absolute 16-bit addresses as
            # (handler - entry + 0x800).  Merely keeping the entry somewhere
            # inside the overlay is therefore insufficient: placing another
            # input section before it would silently bias every table target.
            errors.append(
                "required IRAM assembly entry must be exactly 0x800: "
                f"{required_symbol}=0x{address:x}"
            )

    for address, instruction in engine_instructions:
        if re.search(r"%r15\b", instruction):
            errors.append(
                f"0x{address:x}: reserved loader register R15 is referenced: "
                f"{instruction}"
            )

        mnemonic = instruction.split(None, 1)[0]
        if expect_asm and "%sp" in instruction and mnemonic not in (
            "pushn",
            "popn",
        ):
            errors.append(
                f"0x{address:x}: unexpected assembly stack access/spill: "
                f"{instruction}"
            )

        transfer = re.search(
            r"\b(call|jp(?:\.d)?|jr[a-z]*(?:\.d)?)\b\s+(.+)$",
            instruction,
        )
        if transfer is None:
            continue
        mnemonic = transfer.group(1)
        operand = transfer.group(2).strip()
        if operand.startswith("%"):
            if mnemonic == "call":
                allowed_operand = (
                    f"%{allow_indirect_call_register}"
                    if allow_indirect_call_register is not None else None
                )
                if operand == allowed_operand:
                    allowed_indirect_calls += 1
                else:
                    errors.append(
                        f"0x{address:x}: indirect call target cannot be "
                        f"proven resident or approved by the shared ABI: "
                        f"{instruction}"
                    )
            continue
        annotation_match = re.search(r"<([^>]+)>", operand)
        if annotation_match is None:
            errors.append(
                f"0x{address:x}: unresolved direct IRAM transfer: "
                f"{instruction}"
            )
            continue
        target = annotated_target(annotation_match.group(1), labels)
        if target is None or not (iram_start <= target < iram_end):
            errors.append(
                f"0x{address:x}: direct transfer leaves IRAM: {instruction}"
            )

    if allowed_indirect_calls != expected_indirect_calls:
        errors.append(
            "controlled indirect-call count mismatch: "
            f"found {allowed_indirect_calls}, expected "
            f"{expected_indirect_calls} through "
            f"%{allow_indirect_call_register or '?'}"
        )

    checked_dispatch_entries = 0
    if dispatch_table_symbol is not None:
        table_start = labels.get(dispatch_table_symbol)
        table_end = labels.get(f"{dispatch_table_symbol}_end")
        dispatch_entry_size = 4
        if table_start is not None:
            # llvm-objdump does not necessarily print a zero-sized local end
            # label at the end of a section.  The assembly table is the final
            # object in the resident engine, so engine_end is an equivalent
            # fallback bound.
            encoded_size = (table_end or engine_end) - table_start
            if encoded_size in (
                dispatch_table_entries * 2,
                dispatch_table_entries * 4,
            ):
                dispatch_entry_size = encoded_size // dispatch_table_entries
            else:
                errors.append(
                    "IRAM dispatch table has unexpected encoded size: "
                    f"{encoded_size} bytes"
                )
        table_size = dispatch_table_entries * dispatch_entry_size
        if table_start is None:
            errors.append(
                f"required IRAM dispatch table is absent: "
                f"{dispatch_table_symbol}"
            )
        elif table_start & (dispatch_entry_size - 1):
            errors.append(
                f"IRAM dispatch table is not {dispatch_entry_size * 8}-bit "
                "aligned: "
                f"0x{table_start:x}"
            )
        elif dispatch_entry_size == 2 and (
            engine_start != 0x800 or engine_end > 0x10000
        ):
            errors.append(
                "16-bit IRAM dispatch table requires engine VMA "
                "0x800..0xffff"
            )
        elif not (
            engine_start <= table_start
            and table_start + table_size <= engine_end
        ):
            errors.append(
                "IRAM dispatch table is outside the engine range: "
                f"0x{table_start:x}..0x{table_start + table_size:x}"
            )
        else:
            raw_bytes = parse_disassembly_bytes(disassembly_text)
            table_end_address = table_start + table_size
            executable_addresses = {
                address
                for address, _instruction in engine_instructions
                if not (table_start <= address < table_end_address)
            }
            for index in range(dispatch_table_entries):
                address = table_start + index * dispatch_entry_size
                try:
                    target = int.from_bytes(
                        bytes(
                            raw_bytes[address + offset]
                            for offset in range(dispatch_entry_size)
                        ),
                        "little",
                    )
                except KeyError:
                    errors.append(
                        f"dispatch table entry {index:#04x} has missing bytes"
                    )
                    break
                if not (engine_start <= target < engine_end):
                    errors.append(
                        f"dispatch table entry {index:#04x} leaves IRAM: "
                        f"0x{target:x}"
                    )
                elif target not in executable_addresses:
                    # Range checks alone accept a target in the middle of an
                    # instruction (or in the table's own data).  Objdump does
                    # not retain every local handler label, but it does give
                    # us every decoded instruction boundary; require one of
                    # those boundaries as the strictest linked-image proof
                    # available here.
                    errors.append(
                        f"dispatch table entry {index:#04x} does not target "
                        "an executable instruction boundary: "
                        f"0x{target:x}"
                    )
                checked_dispatch_entries += 1

    if errors:
        raise ValueError("IRAM audit failed:\n" + "\n".join(errors))
    return AuditSummary(
        iram_size=iram_size,
        engine_size=engine_size,
        engine_instructions=len(engine_instructions),
        dispatch_entries=checked_dispatch_entries,
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--map", type=Path, required=True)
    parser.add_argument("--disassembly", type=Path, required=True)
    parser.add_argument("--max-size", type=lambda value: int(value, 0), required=True)
    parser.add_argument("--expect-asm", action="store_true")
    parser.add_argument("--required-symbol")
    parser.add_argument("--dispatch-table-symbol")
    parser.add_argument("--dispatch-table-entries", type=int, default=256)
    parser.add_argument("--allow-indirect-call-register")
    parser.add_argument("--expected-indirect-calls", type=int, default=0)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        summary = audit(
            args.map,
            args.disassembly,
            args.max_size,
            expect_asm=args.expect_asm,
            required_symbol=args.required_symbol,
            dispatch_table_symbol=args.dispatch_table_symbol,
            dispatch_table_entries=args.dispatch_table_entries,
            allow_indirect_call_register=args.allow_indirect_call_register,
            expected_indirect_calls=args.expected_indirect_calls,
        )
    except (OSError, ValueError) as exc:
        print(exc, file=sys.stderr)
        return 1
    print(
        "IRAM audit passed: "
        f"overlay={summary.iram_size} bytes, "
        f"engine={summary.engine_size} bytes, "
        f"instructions={summary.engine_instructions}, "
        f"dispatch_entries={summary.dispatch_entries}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
