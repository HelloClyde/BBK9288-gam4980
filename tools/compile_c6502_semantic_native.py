#!/usr/bin/env python3
"""Compile recovered C6502 functions to direct S1C33 ELF objects.

Unlike the emulator AOT path, this back end emits one native function for
each recovered C6502 function.  Game calls become real S1C33 calls, compiler
runtime calls and firmware bank-table calls become native adapter calls, and
control flow uses ordinary C labels.  There is no guest-PC dispatch loop,
cycle scheduler, or interpreter fallback in the generated objects.

The v1 objects intentionally leave the native runtime/firmware adapters as
ELF relocations.  They are a code-density and front/back-end contract build;
the standalone KF2 linker supplies those adapters in the next stage.
"""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import compile_direct_native_prototype as semantic  # noqa: E402
import pack_native_module as native  # noqa: E402
import recompile_c6502_frontend as frontend  # noqa: E402


GAME_BANK_SIZE = 0x4000
GAME_VIRTUAL_BASE = 0x5000
RUNTIME_FIRST = 0xD000
RUNTIME_LAST = 0xE534


def function_name(offset: int) -> str:
    return f"c6502_game_fn_{offset:05x}"


def far_call_table(instructions: list[native.aotgen.InstructionIR]) -> int | None:
    if len(instructions) < 5:
        return None
    window = instructions[-5:]
    if (window[0].data[0] != 0xA2 or window[2].data[0] != 0xA2 or
            window[4].data[0] != 0x20):
        return None
    if (
        window[1].data not in (bytes((0x86, 0x26)), bytes((0x8E, 0x26, 0))) or
        window[3].data not in (bytes((0x86, 0x27)), bytes((0x8E, 0x27, 0))) or
        window[4].data[1:] != bytes((0xF6, 0xD2))
    ):
        return None
    return window[0].data[1] | window[2].data[1] << 8


def far_game_target(game: bytes, code_size: int, table: int) -> int | None:
    table_offset = native.game_far_table_offset(
        len(game), code_size, table,
    )
    if table_offset is None:
        return None
    target = game[table_offset] | game[table_offset + 1] << 8
    bank = game[table_offset + 2]
    if bank < 0xE0 or not GAME_VIRTUAL_BASE <= target < 0x9000:
        return None
    offset = (bank - 0xE0) * GAME_BANK_SIZE + target - GAME_VIRTUAL_BASE
    return offset if 0 <= offset < code_size else None


def discover_functions(
    game: bytes, records: list[tuple[int, ...]], stats: dict[str, int]
) -> tuple[set[int], dict[int, list[tuple[int, ...]]]]:
    entries = {stats["entry_pc"] - GAME_VIRTUAL_BASE}
    record_by_offset = {record[2]: record for record in records}
    for record in records:
        instructions = frontend.decode_record(game, record)
        for instruction in instructions:
            if instruction.data[0] != 0x20:
                continue
            target = instruction.data[1] | instruction.data[2] << 8
            offset = frontend.physical_target(record, target)
            if offset is not None and offset in record_by_offset:
                entries.add(offset)
        table = far_call_table(instructions)
        if table is not None:
            offset = far_game_target(game, stats["code_size"], table)
            if offset is not None and offset in record_by_offset:
                entries.add(offset)

    bodies: dict[int, list[tuple[int, ...]]] = {}
    for entry in sorted(entries):
        pending = [entry]
        visited: set[int] = set()
        while pending:
            offset = pending.pop()
            if offset in visited or offset not in record_by_offset:
                continue
            if offset != entry and offset in entries:
                continue
            visited.add(offset)
            pending.extend(frontend.block_successors(game, record_by_offset[offset]))
        if visited:
            bodies[entry] = [record_by_offset[item] for item in sorted(visited)]
    return entries, bodies


BRANCH_CONDITION = {
    0x10: "!NEGATIVE_p",
    0x30: "NEGATIVE_p",
    0x50: "!OVERFLOW_p",
    0x70: "OVERFLOW_p",
    0x90: "!CARRY_p",
    0xB0: "CARRY_p",
    0xD0: "!ZERO_p",
    0xF0: "ZERO_p",
}


def transfer_lines(
    target: int | None, local_offsets: set[int], entries: set[int],
    global_abi: bool, indent: str = "    ",
) -> list[str]:
    if target is not None and target in local_offsets:
        return [f"{indent}goto block_{target:05x};"]
    if target is not None and target in entries:
        if global_abi:
            return [
                f"{indent}{function_name(target)}();",
                f"{indent}goto native_return;",
            ]
        return [
            f"{indent}C6502_SAVE_STATE();",
            f"{indent}{function_name(target)}(context);",
            f"{indent}C6502_LOAD_STATE();",
            f"{indent}goto native_return;",
        ]
    value = 0xFFFFFFFF if target is None else target
    if global_abi:
        return [
            f"{indent}c6502_native_trap(0x{value:08x}u);",
            f"{indent}goto native_return;",
        ]
    return [
        f"{indent}C6502_SAVE_STATE();",
        f"{indent}c6502_native_trap(context, 0x{value:08x}u);",
        f"{indent}C6502_LOAD_STATE();",
        f"{indent}goto native_return;",
    ]


def emit_body_operations(
    instructions: list[native.aotgen.InstructionIR], end: int,
) -> list[str]:
    result: list[str] = []
    index = 0
    while index < end:
        phrase = semantic._semantic_peephole(instructions, index)
        if phrase is not None and index + phrase[1] <= end:
            result.append(phrase[0])
            index += phrase[1]
            continue
        lines, _binary = native.aotgen.emit_instruction(instructions[index])
        if instructions[index].data[0] in native.aotgen.TERMINATORS:
            raise RuntimeError("control instruction reached semantic body emitter")
        result.extend(lines)
        index += 1
    return result


def emit_function(
    game: bytes, code_size: int, entry: int,
    records: list[tuple[int, ...]], entries: set[int], global_abi: bool,
) -> list[str]:
    local_offsets = {record[2] for record in records}
    signature = "void" if global_abi else "s6502_iram_asm_context_t *context"
    lines = [
        f'__attribute__((used, noinline, section(".text.c6502_native_{entry // GAME_BANK_SIZE:02d}")))',
        f"void {function_name(entry)}({signature}) {{",
        "    native_u16 pc = 0u, ea = 0u, et = 0u;",
        "    native_u32 executed = 0u, cycles = 0xffffffffu;",
        "    native_u8 dt = 0u;",
        f"    goto block_{entry:05x};",
    ]
    if not global_abi:
        lines[4:4] = [
            "    native_u8 ac, ix, iy, sp, status;",
            "    native_u8 *ram;",
            "    C6502_LOAD_STATE();",
        ]
    for record in records:
        instructions = frontend.decode_record(game, record)
        terminal = instructions[-1]
        opcode = terminal.data[0]
        next_offset = record[2] + record[3]
        lines.append(f"block_{record[2]:05x}:")
        lines.append(f"    pc = 0x{record[1]:04x}u;")

        table = far_call_table(instructions)
        body_end = len(instructions) - (5 if table is not None else 1)
        for body_line in emit_body_operations(instructions, body_end):
            lines.append(f"    {body_line}")

        if table is not None:
            target = far_game_target(game, code_size, table)
            if not global_abi:
                lines.append("    C6502_SAVE_STATE();")
            if target is not None and target in entries:
                suffix = "" if global_abi else "context"
                lines.append(f"    {function_name(target)}({suffix});")
            else:
                prefix = "" if global_abi else "context, "
                lines.append(f"    c6502_native_firmware_call({prefix}0x{table:04x}u);")
            if not global_abi:
                lines.append("    C6502_LOAD_STATE();")
            lines.extend(transfer_lines(next_offset, local_offsets, entries, global_abi))
            continue

        if opcode == 0x20:
            target_pc = terminal.data[1] | terminal.data[2] << 8
            target = frontend.physical_target(record, target_pc)
            if not global_abi:
                lines.append("    C6502_SAVE_STATE();")
            if target is not None and target in entries:
                suffix = "" if global_abi else "context"
                lines.append(f"    {function_name(target)}({suffix});")
            elif RUNTIME_FIRST <= target_pc <= RUNTIME_LAST:
                prefix = "" if global_abi else "context, "
                lines.append(f"    c6502_native_runtime_call({prefix}0x{target_pc:04x}u);")
            else:
                prefix = "" if global_abi else "context, "
                lines.append(f"    c6502_native_direct_call({prefix}0x{target_pc:04x}u);")
            if not global_abi:
                lines.append("    C6502_LOAD_STATE();")
            lines.extend(transfer_lines(next_offset, local_offsets, entries, global_abi))
            continue

        if opcode in BRANCH_CONDITION:
            target_pc = native.aotgen.branch_target(terminal.pc, terminal.data[1])
            target = frontend.physical_target(record, target_pc)
            lines.append(f"    if ({BRANCH_CONDITION[opcode]}) {{")
            lines.extend(transfer_lines(target, local_offsets, entries, global_abi, "        "))
            lines.append("    }")
            lines.extend(transfer_lines(next_offset, local_offsets, entries, global_abi))
            continue

        if opcode == 0x4C:
            target_pc = terminal.data[1] | terminal.data[2] << 8
            target = frontend.physical_target(record, target_pc)
            lines.extend(transfer_lines(target, local_offsets, entries, global_abi))
            continue
        if opcode == 0x80:
            target_pc = native.aotgen.branch_target(terminal.pc, terminal.data[1])
            target = frontend.physical_target(record, target_pc)
            lines.extend(transfer_lines(target, local_offsets, entries, global_abi))
            continue
        if opcode == 0x60:
            lines.append("    goto native_return;")
            continue
        if opcode == 0x40:
            if global_abi:
                lines.extend(["    c6502_native_rti();", "    goto native_return;"])
                continue
            lines.extend(
                [
                    "    C6502_SAVE_STATE();",
                    "    c6502_native_rti(context);",
                    "    C6502_LOAD_STATE();",
                    "    goto native_return;",
                ]
            )
            continue
        lines.extend(transfer_lines(None, local_offsets, entries, global_abi))

    lines.append("native_return:")
    if not global_abi:
        lines.append("    C6502_SAVE_STATE();")
    lines.extend(["}", ""])
    return lines


def source_prelude(entries: set[int], global_abi: bool) -> str:
    lines = [
        "/* Generated direct C6502 semantic translation; do not edit. */",
        '#include "s6502_iram_exec_abi.h"',
        "typedef unsigned char native_u8;",
        "typedef unsigned short native_u16;",
        "typedef unsigned long native_u32;",
        "#define FLAG_N 0x80u",
        "#define FLAG_V 0x40u",
        "#define FLAG_U 0x20u",
        "#define FLAG_B 0x10u",
        "#define FLAG_D 0x08u",
        "#define FLAG_I 0x04u",
        "#define FLAG_Z 0x02u",
        "#define FLAG_C 0x01u",
        "#define NEGATIVE_p (status & FLAG_N)",
        "#define OVERFLOW_p (status & FLAG_V)",
        "#define DECIMAL_p (status & FLAG_D)",
        "#define ZERO_p (status & FLAG_Z)",
        "#define CARRY_p (status & FLAG_C)",
        "#define CARRY (CARRY_p ? 1u : 0u)",
        "#define SET_BIT(flag, value) do { status = (native_u8)((status & (native_u8)~(flag)) | ((!!(value)) * (flag))); } while (0)",
        "#define SET_N(value) SET_BIT(FLAG_N, value)",
        "#define SET_V(value) SET_BIT(FLAG_V, value)",
        "#define SET_I(value) SET_BIT(FLAG_I, value)",
        "#define SET_Z(value) SET_BIT(FLAG_Z, value)",
        "#define SET_C(value) SET_BIT(FLAG_C, value)",
        "#define SET_NZ(value) do { native_u8 native_v = (native_u8)(value); status = (native_u8)((status & (native_u8)~(FLAG_N | FLAG_Z)) | (native_v & FLAG_N) | (!native_v * FLAG_Z)); } while (0)",
        "#define CYCLES(value) ((void)0)",
        "#define S6502_FAST_STACK_RAM ram",
        "#define READ16(address) ((native_u16)(READ8(address) | ((native_u16)READ8((native_u16)((address) + 1u)) << 8)))",
        "#define READ16W(address) READ16(address)",
        "#define PUSH(value) do { ram[0x100u | sp] = (native_u8)(value); sp = (native_u8)(sp - 1u); } while (0)",
        "#define POP(value) ram[0x100u | ++sp]",
        native.native_macro_source(),
        semantic.SEMANTIC_NATIVE_MACROS,
        "",
    ]
    if global_abi:
        lines[5:5] = [
            "extern native_u8 c6502_native_ac, c6502_native_ix, c6502_native_iy;",
            "extern native_u8 c6502_native_sp, c6502_native_status;",
            "extern native_u8 *c6502_native_ram;",
            "#define ac c6502_native_ac",
            "#define ix c6502_native_ix",
            "#define iy c6502_native_iy",
            "#define sp c6502_native_sp",
            "#define status c6502_native_status",
            "#define ram c6502_native_ram",
            "extern native_u8 c6502_native_read8(native_u16);",
            "extern void c6502_native_write8(native_u16, native_u8);",
            "extern void c6502_native_runtime_call(native_u16);",
            "extern void c6502_native_firmware_call(native_u16);",
            "extern void c6502_native_direct_call(native_u16);",
            "extern void c6502_native_rti(void);",
            "extern void c6502_native_trap(native_u32);",
            "#define READ8(address) c6502_native_read8((native_u16)(address))",
            "#define WRITE8(address, value) c6502_native_write8((native_u16)(address), (native_u8)(value))",
        ]
    else:
        lines[5:5] = [
            "extern native_u8 c6502_native_read8(s6502_iram_asm_context_t *, native_u16);",
            "extern void c6502_native_write8(s6502_iram_asm_context_t *, native_u16, native_u8);",
            "extern void c6502_native_runtime_call(s6502_iram_asm_context_t *, native_u16);",
            "extern void c6502_native_firmware_call(s6502_iram_asm_context_t *, native_u16);",
            "extern void c6502_native_direct_call(s6502_iram_asm_context_t *, native_u16);",
            "extern void c6502_native_rti(s6502_iram_asm_context_t *);",
            "extern void c6502_native_trap(s6502_iram_asm_context_t *, native_u32);",
            "#define READ8(address) c6502_native_read8(context, (native_u16)(address))",
            "#define WRITE8(address, value) c6502_native_write8(context, (native_u16)(address), (native_u8)(value))",
            "#define C6502_LOAD_STATE() do { ac = (native_u8)context->ac; ix = (native_u8)context->ix; iy = (native_u8)context->iy; sp = (native_u8)context->sp; status = (native_u8)context->status; ram = (native_u8 *)(unsigned long)context->ram; } while (0)",
            "#define C6502_SAVE_STATE() do { context->pc = pc; context->ac = ac; context->ix = ix; context->iy = iy; context->sp = sp; context->status = status; } while (0)",
        ]
    declaration = "void" if global_abi else "s6502_iram_asm_context_t *context"
    lines.extend(f"void {function_name(entry)}({declaration});" for entry in sorted(entries))
    lines.append("")
    return "\n".join(lines)


def compile_bank(
    clang: str, objcopy: str, output: Path, bank: int,
    game: bytes, code_size: int, entries: set[int],
    functions: list[tuple[int, list[tuple[int, ...]]]], global_abi: bool,
) -> dict[str, int | str]:
    source = source_prelude(entries, global_abi)
    body: list[str] = [source]
    for entry, records in functions:
        body.extend(emit_function(game, code_size, entry, records, entries, global_abi))
    c_path = output / f"c6502_native_bank_{bank:02d}.c"
    o_path = output / f"c6502_native_bank_{bank:02d}.o"
    b_path = output / f"c6502_native_bank_{bank:02d}.bin"
    c_path.write_text("\n".join(body), encoding="utf-8")
    subprocess.run(
        [
            clang, "--target=s1c33-none-elf", "-Oz", "-ffreestanding",
            "-fno-builtin", "-fno-jump-tables", "-fomit-frame-pointer",
            "-fno-strict-aliasing", "-ffunction-sections", "-fdata-sections",
            "-I", str(ROOT / "src"), "-c", str(c_path), "-o", str(o_path),
        ],
        check=True,
    )
    section = f".text.c6502_native_{bank:02d}"
    subprocess.run(
        [objcopy, "-O", "binary", f"--only-section={section}", str(o_path), str(b_path)],
        check=True,
    )
    return {
        "bank": bank,
        "functions": len(functions),
        "guest_blocks": sum(len(records) for _entry, records in functions),
        "guest_bytes": sum(record[3] for _entry, records in functions for record in records),
        "guest_instructions": sum(record[4] for _entry, records in functions for record in records),
        "native_bytes": b_path.stat().st_size,
        "object": str(o_path),
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path)
    parser.add_argument("--clang", required=True)
    parser.add_argument("--objcopy", required=True)
    parser.add_argument(
        "--output", type=Path,
        default=ROOT / "build" / "9288" / "c6502-semantic-native",
    )
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument(
        "--abi", choices=("context", "global"), default="context",
        help="context is the reproducible comparison ABI; global is a retained negative experiment",
    )
    args = parser.parse_args()

    game = args.game.read_bytes()
    records, stats = native.recover_game_blocks(game)
    records = frontend.canonicalize_records(game, records)
    entries, bodies = discover_functions(game, records, stats)
    grouped: dict[int, list[tuple[int, list[tuple[int, ...]]]]] = {}
    for entry, body in bodies.items():
        grouped.setdefault(entry // GAME_BANK_SIZE, []).append((entry, body))
    args.output.mkdir(parents=True, exist_ok=True)
    for pattern in ("c6502_native_bank_*.c", "c6502_native_bank_*.o", "c6502_native_bank_*.bin"):
        for path in args.output.glob(pattern):
            path.unlink()

    work = [(bank, sorted(functions)) for bank, functions in sorted(grouped.items())]
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        futures = [
            pool.submit(
                compile_bank, args.clang, args.objcopy, args.output, bank,
                game, stats["code_size"], entries, functions, args.abi == "global",
            )
            for bank, functions in work
        ]
        modules = [future.result() for future in futures]
    modules.sort(key=lambda item: int(item["bank"]))
    front_report = frontend.analyze(game, frontend.DEFAULT_MAP)
    report: dict[str, object] = {
        "format": "c6502-semantic-native-s1c33-v2",
        "guest_state_abi": args.abi,
        "game": str(args.game.resolve()),
        "game_bytes": len(game),
        "declared_code_bytes": stats["code_size"],
        "resource_bytes": len(game) - stats["code_size"],
        "native_functions": len(bodies),
        "native_modules": len(modules),
        "native_code_bytes": sum(int(item["native_bytes"]) for item in modules),
        "compiled_guest_blocks": sum(int(item["guest_blocks"]) for item in modules),
        "compiled_guest_bytes": sum(int(item["guest_bytes"]) for item in modules),
        "compiled_guest_instructions": sum(int(item["guest_instructions"]) for item in modules),
        "pc_dispatch_tables": 0,
        "cycle_scheduler": False,
        "interpreter_fallbacks": 0,
        "unresolved_far_call_sites": front_report["unresolved_far_call_sites"],
        "modules": modules,
    }
    instructions = int(report["compiled_guest_instructions"])
    report["native_bytes_per_guest_instruction_x1000"] = (
        int(report["native_code_bytes"]) * 1000 // instructions
        if instructions else 0
    )
    report_path = args.output / "report.json"
    report_path.write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(report, ensure_ascii=False, indent=2))
    print(f"report: {report_path}")


if __name__ == "__main__":
    main()
