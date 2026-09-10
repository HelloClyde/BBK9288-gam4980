#!/usr/bin/env python3
"""Compile every recovered GAM block to S1C33 with no interpreter fallback.

This is a size/coverage prototype for the real GAM recompiler.  Unlike the
pageable emulator modules, generated code has no guest-cycle deadline,
profiling, per-block epoch check, or route back to a 6502 interpreter.
Transfers outside one 16 KiB game bank return to a native bank dispatcher;
an address not present in the native registry is a hard translation trap.
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

import pack_native_module as native  # noqa: E402


GAME_BANK_SIZE = 0x4000


def _direct_zp(address: int) -> bool:
    return address < 0x100 and address not in (
        native.aotgen.PAGE0_SPECIAL_READ | native.aotgen.PAGE0_SPECIAL_WRITE
    )


def _zp_pair(low: int, high: int) -> bool:
    return _direct_zp(low) and high == ((low + 1) & 0xFF) and _direct_zp(high)


def _semantic_peephole(
    instructions: list[native.aotgen.InstructionIR], index: int,
) -> tuple[str, int] | None:
    """Recover high-level C6502 operations before native lowering."""

    remaining = instructions[index:]
    opcodes = tuple(item.data[0] for item in remaining[:10])

    # PHP/SEI are emitted by C6502 only to preserve flags while changing one
    # 16-bit software-stack/pointer value.  The native operation has no need
    # to touch the 6502 interrupt/status model at all.
    if len(remaining) >= 10 and opcodes[:10] in (
        (0x08, 0x78, 0x18, 0xA5, 0x69, 0x85, 0xA5, 0x69, 0x85, 0x28),
        (0x08, 0x78, 0x38, 0xA5, 0xE9, 0x85, 0xA5, 0xE9, 0x85, 0x28),
    ):
        src0 = remaining[3].data[1]
        src1 = remaining[6].data[1]
        dst0 = remaining[5].data[1]
        dst1 = remaining[8].data[1]
        if _zp_pair(src0, src1) and _zp_pair(dst0, dst1):
            value = remaining[4].data[1] | (remaining[7].data[1] << 8)
            name = "ADD16_PRESERVE" if remaining[2].data[0] == 0x18 else "SUB16_PRESERVE"
            return f"C6502_{name}(0x{src0:02x}u, 0x{dst0:02x}u, 0x{value:04x}u);", 10

    # General 16-bit pointer/integer add and subtract.  Keep final ADC/SBC
    # flags because a following branch may consume them.
    if len(remaining) >= 7 and opcodes[:7] in (
        (0x18, 0xA5, 0x69, 0x85, 0xA5, 0x69, 0x85),
        (0x38, 0xA5, 0xE9, 0x85, 0xA5, 0xE9, 0x85),
    ):
        src0 = remaining[1].data[1]
        src1 = remaining[4].data[1]
        dst0 = remaining[3].data[1]
        dst1 = remaining[6].data[1]
        if _zp_pair(src0, src1) and _zp_pair(dst0, dst1):
            value = remaining[2].data[1] | (remaining[5].data[1] << 8)
            name = "ADD16" if remaining[0].data[0] == 0x18 else "SUB16"
            return f"C6502_{name}(0x{src0:02x}u, 0x{dst0:02x}u, 0x{value:04x}u);", 7

    # C6502's canonical 16-bit binary expression.  Seven 6502 operations are
    # one typed IR operation; the second ADC/SBC supplies the observable
    # N/Z/C/V result exactly as an unsigned/signed 16-bit C expression does.
    if len(remaining) >= 7 and opcodes[:7] in (
        (0x18, 0xA5, 0x65, 0x85, 0xA5, 0x65, 0x85),
        (0x38, 0xA5, 0xE5, 0x85, 0xA5, 0xE5, 0x85),
    ):
        left0 = remaining[1].data[1]
        right0 = remaining[2].data[1]
        dst0 = remaining[3].data[1]
        left1 = remaining[4].data[1]
        right1 = remaining[5].data[1]
        dst1 = remaining[6].data[1]
        if (
            _zp_pair(left0, left1) and
            _zp_pair(right0, right1) and
            _zp_pair(dst0, dst1)
        ):
            name = "ADD16_REGS" if remaining[0].data[0] == 0x18 else "SUB16_REGS"
            return (
                f"C6502_{name}(0x{left0:02x}u, 0x{right0:02x}u, "
                f"0x{dst0:02x}u);",
                7,
            )

    # 16-bit immediate assignment and zero-page copy.
    if len(remaining) >= 4 and opcodes[:4] == (0xA9, 0x85, 0xA9, 0x85):
        dst0 = remaining[1].data[1]
        dst1 = remaining[3].data[1]
        if _zp_pair(dst0, dst1):
            value = remaining[0].data[1] | (remaining[2].data[1] << 8)
            return f"C6502_STORE16_IMM(0x{dst0:02x}u, 0x{value:04x}u);", 4
    if len(remaining) >= 4 and opcodes[:4] == (0xA5, 0x85, 0xA5, 0x85):
        src0 = remaining[0].data[1]
        src1 = remaining[2].data[1]
        dst0 = remaining[1].data[1]
        dst1 = remaining[3].data[1]
        if _zp_pair(src0, src1) and _zp_pair(dst0, dst1):
            return f"C6502_COPY16(0x{src0:02x}u, 0x{dst0:02x}u);", 4

    # Compiler's canonical 16-bit indirect load/store.
    if len(remaining) >= 6 and opcodes[:6] == (0xA0, 0xB1, 0x85, 0xC8, 0xB1, 0x85):
        index0 = remaining[0].data[1]
        pointer0 = remaining[1].data[1]
        pointer1 = remaining[4].data[1]
        dst0 = remaining[2].data[1]
        dst1 = remaining[5].data[1]
        if pointer0 == pointer1 and _zp_pair(dst0, dst1):
            return f"C6502_LOAD16_INDIRECT(0x{pointer0:02x}u, 0x{dst0:02x}u, 0x{index0:02x}u);", 6
    if len(remaining) >= 6 and opcodes[:6] == (0xA0, 0xA5, 0x91, 0xC8, 0xA5, 0x91):
        index0 = remaining[0].data[1]
        src0 = remaining[1].data[1]
        src1 = remaining[4].data[1]
        pointer0 = remaining[2].data[1]
        pointer1 = remaining[5].data[1]
        if pointer0 == pointer1 and _zp_pair(src0, src1):
            return f"C6502_STORE16_INDIRECT(0x{pointer0:02x}u, 0x{src0:02x}u, 0x{index0:02x}u);", 6

    # Constant-offset accesses through $28 are C locals/arguments, not
    # arbitrary 6502 indirect addressing.  Preserve Y and the final N/Z
    # producer while exposing the stack slot to the native back end.
    if len(remaining) >= 2 and opcodes[:2] == (0xA0, 0xB1):
        if remaining[1].data[1] == 0x28:
            return f"C6502_LOAD_STACK8(0x{remaining[0].data[1]:02x}u);", 2
    if len(remaining) >= 2 and opcodes[:2] == (0xA0, 0x91):
        if remaining[1].data[1] == 0x28:
            return f"C6502_STORE_STACK8(0x{remaining[0].data[1]:02x}u);", 2
    return None


def semantic_decode_record(
    signature: bytes, record: tuple[int, ...]
) -> tuple[list[str], bool]:
    _physical, virtual, offset, size, expected_count, _bank2 = record
    data = signature[offset : offset + size]
    cursor = 0
    pc = virtual
    instructions: list[native.aotgen.InstructionIR] = []
    while cursor < len(data):
        opcode = data[cursor]
        length = native.aotgen.OPCODE_LENGTHS[opcode]
        raw = data[cursor : cursor + length]
        reads, writes = native.aotgen.flag_effects(opcode)
        instructions.append(native.aotgen.InstructionIR(pc, raw, reads, writes))
        cursor += length
        pc = (pc + length) & 0xFFFF
    if len(instructions) != expected_count:
        raise RuntimeError("semantic decode instruction count mismatch")
    native.aotgen.analyze_flag_liveness(instructions)
    emitted: list[str] = []
    requires_binary = False
    index = 0
    while index < len(instructions):
        phrase = _semantic_peephole(instructions, index)
        if phrase is not None:
            line, count = phrase
            emitted.append(line)
            index += count
            continue
        lines, binary = native.aotgen.emit_instruction(instructions[index])
        emitted.extend(lines)
        requires_binary |= binary
        index += 1
    return emitted, requires_binary


SEMANTIC_NATIVE_MACROS = r"""
#define C6502_ZP16(addr) ((native_u16)(ram[(native_u8)(addr)] | ((native_u16)ram[(native_u8)((addr) + 1u)] << 8)))
#define C6502_WRITE16(addr, value) do { native_u16 c6502_v_ = (native_u16)(value); ram[(native_u8)(addr)] = (native_u8)c6502_v_; ram[(native_u8)((addr) + 1u)] = (native_u8)(c6502_v_ >> 8); ac = (native_u8)(c6502_v_ >> 8); } while (0)
#define C6502_STORE16_IMM(dst, value) do { C6502_WRITE16((dst), (value)); SET_NZ(ac); } while (0)
#define C6502_COPY16(src, dst) do { C6502_WRITE16((dst), C6502_ZP16(src)); SET_NZ(ac); } while (0)
#define C6502_ADD16_PRESERVE(src, dst, value) do { C6502_WRITE16((dst), (native_u16)(C6502_ZP16(src) + (native_u16)(value))); } while (0)
#define C6502_SUB16_PRESERVE(src, dst, value) do { C6502_WRITE16((dst), (native_u16)(C6502_ZP16(src) - (native_u16)(value))); } while (0)
#define C6502_ADD16(src, dst, value) do { native_u16 c6502_l_ = C6502_ZP16(src); native_u16 c6502_r_ = (native_u16)(value); native_u16 c6502_lo_ = (native_u16)((c6502_l_ & 0xffu) + (c6502_r_ & 0xffu)); native_u16 c6502_hi_ = (native_u16)((c6502_l_ >> 8) + (c6502_r_ >> 8) + (c6502_lo_ >> 8)); native_u8 c6502_lh_ = (native_u8)(c6502_l_ >> 8); native_u8 c6502_rh_ = (native_u8)(c6502_r_ >> 8); native_u8 c6502_ah_ = (native_u8)c6502_hi_; C6502_WRITE16((dst), (native_u16)((c6502_lo_ & 0xffu) | ((native_u16)c6502_ah_ << 8))); SET_C(c6502_hi_ > 0xffu); SET_V((~(c6502_lh_ ^ c6502_rh_) & (c6502_lh_ ^ c6502_ah_) & 0x80u) != 0u); SET_NZ(ac); } while (0)
#define C6502_SUB16(src, dst, value) do { native_u16 c6502_l_ = C6502_ZP16(src); native_u16 c6502_r_ = (native_u16)(value); native_u8 c6502_ll_ = (native_u8)c6502_l_; native_u8 c6502_rl_ = (native_u8)c6502_r_; native_u8 c6502_lh_ = (native_u8)(c6502_l_ >> 8); native_u8 c6502_rh_ = (native_u8)(c6502_r_ >> 8); native_u8 c6502_al_ = (native_u8)(c6502_ll_ - c6502_rl_); native_u16 c6502_hsub_ = (native_u16)c6502_rh_ + (c6502_ll_ < c6502_rl_); native_u8 c6502_ah_ = (native_u8)(c6502_lh_ - c6502_hsub_); C6502_WRITE16((dst), (native_u16)(c6502_al_ | ((native_u16)c6502_ah_ << 8))); SET_C((native_u16)c6502_lh_ >= c6502_hsub_); SET_V(((c6502_lh_ ^ c6502_rh_) & (c6502_lh_ ^ c6502_ah_) & 0x80u) != 0u); SET_NZ(ac); } while (0)
#define C6502_LOAD16_INDIRECT(pointer, dst, index) do { native_u16 c6502_base_ = C6502_ZP16(pointer); native_u8 c6502_i0_ = (native_u8)(index); native_u8 c6502_i1_ = (native_u8)(c6502_i0_ + 1u); native_u16 c6502_v_ = (native_u16)(READ8((native_u16)(c6502_base_ + c6502_i0_)) | ((native_u16)READ8((native_u16)(c6502_base_ + c6502_i1_)) << 8)); iy = c6502_i1_; C6502_WRITE16((dst), c6502_v_); SET_NZ(ac); } while (0)
#define C6502_STORE16_INDIRECT(pointer, src, index) do { native_u16 c6502_base_ = C6502_ZP16(pointer); native_u16 c6502_v_ = C6502_ZP16(src); native_u8 c6502_i0_ = (native_u8)(index); iy = c6502_i0_; ac = (native_u8)c6502_v_; WRITE8((native_u16)(c6502_base_ + iy), ac); iy = (native_u8)(iy + 1u); ac = (native_u8)(c6502_v_ >> 8); WRITE8((native_u16)(c6502_base_ + iy), ac); SET_NZ(ac); } while (0)
#define C6502_LOAD_STACK8(index) do { iy = (native_u8)(index); ac = READ8((native_u16)(C6502_ZP16(0x28u) + iy)); SET_NZ(ac); } while (0)
#define C6502_STORE_STACK8(index) do { iy = (native_u8)(index); SET_NZ(iy); WRITE8((native_u16)(C6502_ZP16(0x28u) + iy), ac); } while (0)
"""


def make_direct_source(source: str, module_index: int) -> str:
    replacements = {
        "#define CYCLES(value) do { executed += (native_u32)(value); } while (0)":
            "#define CYCLES(value) ((void)0)",
        "#define NATIVE_MISS() do { if (native_blocks) goto native_return; "
        "if (metrics_enabled) ++metrics[4]; return 0u; } while (0)":
            "#define NATIVE_MISS() do { native_trap = 1u; goto native_return; } while (0)",
        "#define NATIVE_BLOCK_DONE() do { ++native_blocks; "
        "if (context->cycles + executed >= context->cycle_budget || "
        "(ram[0x200u] & 0x08u) || *native_epoch != entry_epoch) "
        "goto native_return; } while (0)":
            "#define NATIVE_BLOCK_DONE() do { ++native_blocks; } while (0)",
        "#define NATIVE_DIRECT(label) do { NATIVE_BLOCK_DONE(); "
        "++native_direct_links; goto label; } while (0)":
            "#define NATIVE_DIRECT(label) do { ++native_blocks; goto label; } while (0)",
        "#define NATIVE_EXTERNAL() do { NATIVE_BLOCK_DONE(); "
        "goto native_return; } while (0)":
            "#define NATIVE_EXTERNAL() do { ++native_blocks; goto native_return; } while (0)",
    }
    for old, new in replacements.items():
        if old not in source:
            raise RuntimeError(f"native source contract changed: {old[:60]}")
        source = source.replace(old, new)

    source = source.replace(
        "    native_u32 executed = 0u;",
        "    native_u32 executed = 0u;\n    native_u32 native_trap = 0u;",
    )
    entry = (
        f'__attribute__((used, noinline, section(".text.s6502_native_module_'
        f'{module_index:02d}")))\nnative_u32 s6502_native_module_{module_index:02d}'
    )
    if entry not in source:
        raise RuntimeError("native module function marker changed")
    source = source.replace(entry, SEMANTIC_NATIVE_MACROS + "\n" + entry)
    source = source.replace(
        "    if (dispatch_bits && dispatch_bits[pc]) NATIVE_MISS();\n", ""
    )
    # C6502-generated GAM code never executes SED.  Binary arithmetic is
    # therefore a whole-program invariant, verified by the driver below.
    source = source.replace("    if (DECIMAL_p) NATIVE_MISS();\n", "")
    source = source.replace(
        "    return 1u;\n}", "    return native_trap ? 0u : 1u;\n}"
    )
    return source


def compile_one(
    clang: str, objcopy: str, output: Path, index: int,
    blocks: list[tuple[int, tuple[int, ...]]], game: bytes,
) -> dict[str, int | str]:
    source, section = native.render_module_source(
        index,
        blocks,
        game,
        skip_hle_entries=False,
        defer_dispatch_entries=False,
        module_key=index,
        decoder=semantic_decode_record,
    )
    source = make_direct_source(source, index)
    c_path = output / f"direct_bank_{index:02d}.c"
    o_path = output / f"direct_bank_{index:02d}.o"
    b_path = output / f"direct_bank_{index:02d}.bin"
    c_path.write_text(source, encoding="utf-8")
    subprocess.run(
        [
            clang,
            "--target=s1c33-none-elf",
            "-Oz",
            "-ffreestanding",
            "-fno-builtin",
            "-fno-jump-tables",
            "-fomit-frame-pointer",
            "-fno-strict-aliasing",
            "-ffunction-sections",
            "-fdata-sections",
            "-I", str(ROOT / "src"),
            "-c", str(c_path),
            "-o", str(o_path),
        ],
        check=True,
    )
    subprocess.run(
        [objcopy, "-O", "binary", f"--only-section={section}",
         str(o_path), str(b_path)],
        check=True,
    )
    return {
        "index": index,
        "blocks": len(blocks),
        "guest_bytes": sum(record[3] for _block_id, record in blocks),
        "guest_instructions": sum(record[4] for _block_id, record in blocks),
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
        default=ROOT / "build" / "9288" / "direct-native-prototype",
    )
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()

    game = args.game.read_bytes()
    records, stats = native.recover_game_blocks(game)
    # Decimal arithmetic needs a dedicated semantic operation.  Do not
    # silently generate binary arithmetic if any reachable SED exists.
    decimal_mode = False
    for record in records:
        cursor = record[2]
        end = cursor + record[3]
        while cursor < end:
            opcode = game[cursor]
            if opcode == 0xF8:
                decimal_mode = True
                break
            cursor += native.aotgen.OPCODE_LENGTHS[opcode]
        if decimal_mode:
            break
    if decimal_mode:
        raise SystemExit("reachable SED requires a native decimal IR operation")

    grouped: dict[int, list[tuple[int, tuple[int, ...]]]] = {}
    for block_id, record in enumerate(records):
        bank = (record[0] - native.GAME_PHYSICAL_BASE) // GAME_BANK_SIZE
        grouped.setdefault(bank, []).append((block_id, record))
    args.output.mkdir(parents=True, exist_ok=True)
    for pattern in ("direct_bank_*.c", "direct_bank_*.o", "direct_bank_*.bin"):
        for path in args.output.glob(pattern):
            path.unlink()

    work = sorted(grouped.items())
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        futures = [
            pool.submit(
                compile_one, args.clang, args.objcopy, args.output,
                bank, blocks, game,
            )
            for bank, blocks in work
        ]
        modules = [future.result() for future in futures]
    modules.sort(key=lambda item: int(item["index"]))
    report = {
        "format": "gam4980-direct-native-size-prototype-v1",
        "game": str(args.game.resolve()),
        "game_bytes": len(game),
        "game_code_bytes": stats["code_size"],
        "entry_pc": stats["entry_pc"],
        "recovered_blocks": len(records),
        "recovered_guest_bytes": sum(record[3] for record in records),
        "recovered_guest_instructions": sum(record[4] for record in records),
        "native_modules": len(modules),
        "native_code_bytes": sum(int(item["native_bytes"]) for item in modules),
        "interpreter_fallbacks": 0,
        "modules": modules,
    }
    report["native_bytes_per_guest_instruction_x1000"] = (
        int(report["native_code_bytes"]) * 1000
        // int(report["recovered_guest_instructions"])
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
