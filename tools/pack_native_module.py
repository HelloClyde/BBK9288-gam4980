#!/usr/bin/env python3
"""Compile every guarded E.BIN AOT block into pageable S1C33 modules."""

from __future__ import annotations

import argparse
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile

import generate_aot_ebin as aotgen


MAGIC = 0x54414E47  # GNAT
FORMAT_VERSION = 1
GAME_FORMAT_VERSION = 3
ABI_VERSION = 4
HEADER_SIZE = 64
GAME_HEADER_SIZE = 96
MODULE_SIZE = 56
MATCH_SIZE = 16
LINK_SIZE = 16
MODULE_PIC = 0x1
MODULE_PRELOAD = 0x2
MODULE_GAME = 0x8
PACKAGE_GAME = 0x1
RELOC_GAME_CODE_SPAN = 2
RELOC_GAME_OWNER = 0xFFFFFFFF
PRELOAD_MAPPING = (7, 0x0EB6)
RUNTIME_NATIVE_ARENA_SIZE = 0x40000
RUNTIME_NATIVE_CODE_SLOTS = 4
GAME_NATIVE_MODULE_TARGET_SIZE = 0xD000
GAME_PHYSICAL_BASE = 0x20D000
GAME_BANK_SIZE = 0x4000
GAME_VIRTUAL_BASE = 0x5000
GAME_VIRTUAL_END = 0x9000
FAR_CALL_BYTES = bytes.fromhex("a2 00 86 26 a2 00 86 27 20 f6 d2")
FAR_CALL_MASK = bytes.fromhex("ff 00 ff ff ff 00 ff ff ff ff ff")


def game_far_table_offset(
    game_size: int, code_size: int, table_address: int,
) -> int | None:
    """Map a C6502 `.bf_call` descriptor to its GAM file offset.

    Larger games may place the three-byte descriptor in the tail of bank E0
    ($5000-$8fff), while compact games place it in the appended constant-data
    image ($9000-$cfff).  Treating every table address as `$5000`-relative
    accidentally decoded another code bank for the latter layout.
    """

    if GAME_VIRTUAL_BASE <= table_address < GAME_VIRTUAL_END:
        offset = table_address - GAME_VIRTUAL_BASE
    elif 0x9000 <= table_address < 0xD000:
        offset = code_size + table_address - 0x9000
    else:
        return None
    return offset if 0 <= offset <= game_size - 3 else None


@dataclass
class NativeModule:
    mapping_slot: int
    physical_bank: int
    code: bytes
    blocks: list[tuple[int, tuple[int, ...]]]
    entry_offset: int = 0
    flags: int = 0
    module_key: int = 0


@dataclass(frozen=True)
class GameMetadata:
    file_size: int
    code_size: int
    file_hash: int
    entry_pc: int
    block_count: int
    native_bytes: int
    code_spans: tuple[tuple[int, int], ...]


def game_far_call_at(data: bytes, offset: int) -> bool:
    if offset < 0 or offset + len(FAR_CALL_BYTES) > len(data):
        return False
    return all(
        not mask or (data[offset + index] & mask) == (expected & mask)
        for index, (expected, mask) in enumerate(
            zip(FAR_CALL_BYTES, FAR_CALL_MASK)
        )
    )


def game_switch_targets(game: bytes, jump_offset: int) -> tuple[int, ...] | None:
    """Decode C6502's statically linked __switch_comparison descriptor.

    Match instructions, not arbitrary pointer-looking words in game data.
    Cases/default are intra-function CFG successors of JMP $DB5C.
    """
    if jump_offset < 20 or game[jump_offset:jump_offset+3] != b"\x4c\x5c\xdb":
        return None
    setup = game[jump_offset-20:jump_offset]
    for index, value in ((0,0xa9),(2,0x85),(3,0x26),(4,0xa9),(6,0x85),
                         (7,0x27),(8,0xa2),(10,0xa0),(12,0xa9),(14,0x85),
                         (15,0x20),(16,0xa9),(18,0x85),(19,0x21)):
        if setup[index] != value:
            return None
    table = setup[1] | setup[5] << 8
    count = setup[9] | setup[11] << 8
    default = setup[13] | setup[17] << 8
    base = jump_offset & ~(GAME_BANK_SIZE-1)
    data = base + table - GAME_VIRTUAL_BASE
    if not count or data < base or data+4*count > min(base+GAME_BANK_SIZE,len(game)):
        raise ValueError(f"invalid C6502 switch table at GAM+0x{jump_offset:x}")
    targets = [default] + [int.from_bytes(game[data+count*2+i*2:data+count*2+i*2+2],"little")
                           for i in range(count)]
    if any(not GAME_VIRTUAL_BASE <= pc < GAME_VIRTUAL_END for pc in targets):
        raise ValueError(f"out-of-bank C6502 switch target at GAM+0x{jump_offset:x}")
    return tuple(dict.fromkeys(base+pc-GAME_VIRTUAL_BASE for pc in targets))


def recover_game_blocks(game: bytes) -> tuple[list[tuple[int, ...]], dict[str, int]]:
    """Recover every statically reachable C6502 basic block in a GAM.

    C6502 maps each 16 KiB code bank into guest $5000-$8fff.  Direct calls
    stay in the current physical bank, while the compiler's .bf_call template
    names a three-byte target/bank record in bank E0.  Unknown indirect
    targets deliberately remain runtime fallbacks; no guessed data bytes are
    published as executable entry points.
    """

    if len(game) < 0x46:
        raise SystemExit("GAM is shorter than its 0x46-byte header")
    entry_pc = game[0x40] | game[0x41] << 8
    code_size = int.from_bytes(game[0x42:0x46], "little")
    if code_size < 0x46 or code_size > len(game):
        code_size = len(game)
    code = game[:code_size]
    if not GAME_VIRTUAL_BASE <= entry_pc < GAME_VIRTUAL_END:
        raise SystemExit(f"GAM entry is outside $5000-$8fff: 0x{entry_pc:04x}")

    queued: set[int] = set()
    queue: list[tuple[int, int]] = []
    blocks: list[tuple[int, ...]] = []
    unsupported: dict[int, int] = {}
    far_calls = 0
    far_targets = 0
    direct_targets = 0

    def enqueue(offset: int, virtual_pc: int) -> None:
        if (
            offset < 0 or offset >= code_size or offset in queued or
            not GAME_VIRTUAL_BASE <= virtual_pc < GAME_VIRTUAL_END or
            (offset & (GAME_BANK_SIZE - 1)) != virtual_pc - GAME_VIRTUAL_BASE
        ):
            return
        queued.add(offset)
        queue.append((offset, virtual_pc))

    enqueue(entry_pc - GAME_VIRTUAL_BASE, entry_pc)
    # Every exact .bf_call template is itself an instruction-aligned compiler
    # landmark.  Seeding all of them discovers functions selected through
    # data-driven menus and callback tables that are not reachable from the
    # initial entry by ordinary direct edges.  The eleven fixed opcode bytes
    # make accidental matches in picture/data payloads vanishingly unlikely;
    # target records still receive strict virtual/bank/range validation.
    for offset in range(0, code_size - len(FAR_CALL_BYTES) + 1):
        if not game_far_call_at(code, offset):
            continue
        enqueue(
            offset,
            GAME_VIRTUAL_BASE + (offset & (GAME_BANK_SIZE - 1)),
        )
        table_address = code[offset + 1] | code[offset + 5] << 8
        table_offset = game_far_table_offset(
            len(game), code_size, table_address,
        )
        if table_offset is None:
            continue
        target = game[table_offset] | game[table_offset + 1] << 8
        bank = game[table_offset + 2]
        if not (GAME_VIRTUAL_BASE <= target < GAME_VIRTUAL_END and bank >= 0xE0):
            continue
        target_offset = (
            (bank - 0xE0) * GAME_BANK_SIZE + target - GAME_VIRTUAL_BASE
        )
        if target_offset < code_size:
            enqueue(target_offset, target)

    head = 0
    while head < len(queue):
        block_offset, block_pc = queue[head]
        head += 1
        offset = block_offset
        pc = block_pc
        instruction_count = 0
        while offset < code_size and GAME_VIRTUAL_BASE <= pc < GAME_VIRTUAL_END:
            opcode = code[offset]
            length = aotgen.OPCODE_LENGTHS[opcode]
            if not length or offset + length > code_size:
                break
            instruction = code[offset:offset + length]
            reads, writes = aotgen.flag_effects(opcode)
            try:
                aotgen.emit_instruction(
                    aotgen.InstructionIR(pc, instruction, reads, writes)
                )
            except ValueError:
                unsupported[opcode] = unsupported.get(opcode, 0) + 1
                break
            instruction_count += 1
            next_offset = offset + length
            next_pc = (pc + length) & 0xFFFF

            if game_far_call_at(code, offset):
                far_calls += 1
                table_address = code[offset + 1] | code[offset + 5] << 8
                table_offset = game_far_table_offset(
                    len(game), code_size, table_address,
                )
                if table_offset is not None:
                    target = game[table_offset] | game[table_offset + 1] << 8
                    bank = game[table_offset + 2]
                    if (
                        GAME_VIRTUAL_BASE <= target < GAME_VIRTUAL_END and
                        bank >= 0xE0
                    ):
                        target_offset = (
                            (bank - 0xE0) * GAME_BANK_SIZE +
                            target - GAME_VIRTUAL_BASE
                        )
                        if target_offset < code_size:
                            enqueue(target_offset, target)
                            far_targets += 1

            terminal = opcode in aotgen.TERMINATORS
            if opcode == 0x20:
                target = instruction[1] | instruction[2] << 8
                if GAME_VIRTUAL_BASE <= target < GAME_VIRTUAL_END:
                    enqueue(
                        (offset & ~(GAME_BANK_SIZE - 1)) +
                        target - GAME_VIRTUAL_BASE,
                        target,
                    )
                    direct_targets += 1
                enqueue(next_offset, next_pc)
            elif opcode in aotgen.BRANCH_FLAG_READS:
                target = aotgen.branch_target(pc, instruction[1])
                signed = instruction[1] if instruction[1] < 0x80 else instruction[1] - 0x100
                enqueue(next_offset + signed, target)
                enqueue(next_offset, next_pc)
            elif opcode == 0x4C:
                target = instruction[1] | instruction[2] << 8
                if target == 0xdb5c:
                    for case_offset in game_switch_targets(game, offset) or ():
                        enqueue(case_offset, GAME_VIRTUAL_BASE + (case_offset & (GAME_BANK_SIZE-1)))
                if GAME_VIRTUAL_BASE <= target < GAME_VIRTUAL_END:
                    enqueue(
                        (offset & ~(GAME_BANK_SIZE - 1)) +
                        target - GAME_VIRTUAL_BASE,
                        target,
                    )
            elif opcode == 0x80:
                target = aotgen.branch_target(pc, instruction[1])
                signed = instruction[1] if instruction[1] < 0x80 else instruction[1] - 0x100
                enqueue(next_offset + signed, target)

            offset = next_offset
            pc = next_pc
            if terminal:
                break

        if instruction_count and offset > block_offset:
            blocks.append(
                (
                    GAME_PHYSICAL_BASE + block_offset,
                    block_pc,
                    block_offset,
                    offset - block_offset,
                    instruction_count,
                    0,
                )
            )

    blocks.sort(key=lambda record: (record[0], record[1]))
    if unsupported:
        detail = ", ".join(
            f"0x{opcode:02x}x{count}" for opcode, count in sorted(unsupported.items())
        )
        raise SystemExit(f"reachable GAM code uses unsupported opcodes: {detail}")
    return blocks, {
        "entry_pc": entry_pc,
        "code_size": code_size,
        "far_calls": far_calls,
        "far_targets": far_targets,
        "direct_targets": direct_targets,
        "queued_entries": len(queued),
    }


def align(value: int, alignment: int) -> int:
    return (value + alignment - 1) & -alignment


def merge_game_code_spans(
    records: list[tuple[int, ...]],
) -> tuple[tuple[int, int], ...]:
    spans: list[tuple[int, int]] = []
    for record in sorted(records, key=lambda item: item[2]):
        start = record[2]
        end = start + record[3]
        if spans and start <= spans[-1][1]:
            spans[-1] = (spans[-1][0], max(spans[-1][1], end))
        else:
            spans.append((start, end))
    return tuple((start, end - start) for start, end in spans)


def fnv1a(data: bytes) -> int:
    value = 2166136261
    for byte in data:
        value ^= byte
        value = (value * 16777619) & 0xFFFFFFFF
    return value


def parse_aot_header(path: Path) -> tuple[bytes, list[tuple[int, ...]]]:
    text = path.read_text(encoding="utf-8")
    signature_match = re.search(
        r"static const uint8_t s6502_aot_signature\[\]\s*=\s*\{(.*?)\};",
        text,
        re.S,
    )
    block_match = re.search(
        r"static const s6502_aot_block_t s6502_aot_blocks\[\]\s*=\s*\{"
        r"(.*?)\};",
        text,
        re.S,
    )
    if signature_match is None or block_match is None:
        raise SystemExit(f"cannot parse generated AOT data: {path}")
    signature = bytes(
        int(token, 16)
        for token in re.findall(r"0x([0-9a-fA-F]{1,2})", signature_match.group(1))
    )
    records: list[tuple[int, ...]] = []
    record_pattern = re.compile(
        r"\{\s*(0x[0-9a-fA-F]+|\d+)u\s*,\s*"
        r"(0x[0-9a-fA-F]+|\d+)u\s*,\s*"
        r"(0x[0-9a-fA-F]+|\d+)u\s*,\s*"
        r"(0x[0-9a-fA-F]+|\d+)u\s*,\s*"
        r"(0x[0-9a-fA-F]+|\d+)u\s*,\s*"
        r"(0x[0-9a-fA-F]+|\d+)u\s*\}"
    )
    for match in record_pattern.finditer(block_match.group(1)):
        records.append(tuple(int(value, 0) for value in match.groups()))
    if not records:
        raise SystemExit(f"AOT block table is empty: {path}")
    return signature, records


def decode_record(
    signature: bytes, record: tuple[int, ...]
) -> tuple[list[str], bool]:
    _physical, virtual, offset, size, expected_count, _bank2 = record
    data = signature[offset : offset + size]
    cursor = 0
    pc = virtual
    ir: list[aotgen.InstructionIR] = []
    while cursor < len(data):
        opcode = data[cursor]
        length = aotgen.OPCODE_LENGTHS[opcode]
        instruction = data[cursor : cursor + length]
        if len(instruction) != length:
            raise SystemExit(
                f"truncated AOT signature at virtual 0x{pc:04x}"
            )
        reads, writes = aotgen.flag_effects(opcode)
        ir.append(aotgen.InstructionIR(pc, instruction, reads, writes))
        cursor += length
        pc = (pc + length) & 0xFFFF
    if len(ir) != expected_count or not ir or ir[-1].data[0] not in aotgen.TERMINATORS:
        raise SystemExit(
            f"invalid AOT block at virtual 0x{virtual:04x}: "
            f"decoded={len(ir)} expected={expected_count}"
        )
    aotgen.analyze_flag_liveness(ir)
    emitted, requires_binary, _fusion_count = aotgen.emit_block_ir(ir)
    return emitted, requires_binary


def _semantic_direct_zp(address: int) -> bool:
    return address < 0x100 and address not in (
        aotgen.PAGE0_SPECIAL_READ | aotgen.PAGE0_SPECIAL_WRITE
    )


def _semantic_zp_pair(low: int, high: int) -> bool:
    return (
        _semantic_direct_zp(low) and
        high == ((low + 1) & 0xFF) and
        _semantic_direct_zp(high)
    )


def _semantic_pairs_safe(left: int, right: int) -> bool:
    """Return whether snapshotting two compiler words preserves aliasing.

    Equal pairs are safe and disjoint pairs are safe.  A one-byte overlap is
    not: C6502 reloads the high byte after storing the low byte, while a
    semantic word operation snapshots both bytes before either store.
    """

    left_bytes = {left & 0xFF, (left + 1) & 0xFF}
    right_bytes = {right & 0xFF, (right + 1) & 0xFF}
    return left == right or left_bytes.isdisjoint(right_bytes)


def semantic_peephole(
    instructions: list[aotgen.InstructionIR], index: int,
) -> tuple[str, int, bool] | None:
    """Recover exact C6502 integer/pointer phrases for emulator modules.

    The standalone translator recognizes the same source-level idioms.  This
    variant deliberately retains guest cycles, flags, registers and memory
    side effects so it is safe inside the emulator's local-fallback model.
    """

    remaining = instructions[index:]
    opcodes = tuple(item.data[0] for item in remaining[:10])
    if len(remaining) >= 10 and opcodes[:10] in (
        (0x08, 0x78, 0x18, 0xA5, 0x69, 0x85, 0xA5, 0x69, 0x85, 0x28),
        (0x08, 0x78, 0x38, 0xA5, 0xE9, 0x85, 0xA5, 0xE9, 0x85, 0x28),
    ):
        src0, src1 = remaining[3].data[1], remaining[6].data[1]
        dst0, dst1 = remaining[5].data[1], remaining[8].data[1]
        if (_semantic_zp_pair(src0, src1) and
                _semantic_zp_pair(dst0, dst1) and
                _semantic_pairs_safe(src0, dst0)):
            value = remaining[4].data[1] | remaining[7].data[1] << 8
            name = "ADD16_PRESERVE" if remaining[2].data[0] == 0x18 else \
                "SUB16_PRESERVE"
            return (
                f"S6502_SEM_{name}(0x{src0:02x}u, 0x{dst0:02x}u, "
                f"0x{value:04x}u);",
                10,
                True,
            )
    if len(remaining) >= 7 and opcodes[:7] in (
        (0x18, 0xA5, 0x69, 0x85, 0xA5, 0x69, 0x85),
        (0x38, 0xA5, 0xE9, 0x85, 0xA5, 0xE9, 0x85),
    ):
        src0, src1 = remaining[1].data[1], remaining[4].data[1]
        dst0, dst1 = remaining[3].data[1], remaining[6].data[1]
        if (_semantic_zp_pair(src0, src1) and
                _semantic_zp_pair(dst0, dst1) and
                _semantic_pairs_safe(src0, dst0)):
            value = remaining[2].data[1] | remaining[5].data[1] << 8
            name = "ADD16_IMM" if remaining[0].data[0] == 0x18 else \
                "SUB16_IMM"
            return (
                f"S6502_SEM_{name}(0x{src0:02x}u, 0x{dst0:02x}u, "
                f"0x{value:04x}u);",
                7,
                True,
            )
    if len(remaining) >= 7 and opcodes[:7] in (
        (0x18, 0xA5, 0x65, 0x85, 0xA5, 0x65, 0x85),
        (0x38, 0xA5, 0xE5, 0x85, 0xA5, 0xE5, 0x85),
    ):
        left0, left1 = remaining[1].data[1], remaining[4].data[1]
        right0, right1 = remaining[2].data[1], remaining[5].data[1]
        dst0, dst1 = remaining[3].data[1], remaining[6].data[1]
        if (_semantic_zp_pair(left0, left1) and
                _semantic_zp_pair(right0, right1) and
                _semantic_zp_pair(dst0, dst1) and
                _semantic_pairs_safe(left0, right0) and
                _semantic_pairs_safe(left0, dst0) and
                _semantic_pairs_safe(right0, dst0)):
            name = "ADD16_REGS" if remaining[0].data[0] == 0x18 else \
                "SUB16_REGS"
            return (
                f"S6502_SEM_{name}(0x{left0:02x}u, 0x{right0:02x}u, "
                f"0x{dst0:02x}u);",
                7,
                True,
            )
    if len(remaining) >= 4 and opcodes[:4] == (0xA9, 0x85, 0xA9, 0x85):
        dst0, dst1 = remaining[1].data[1], remaining[3].data[1]
        if _semantic_zp_pair(dst0, dst1):
            value = remaining[0].data[1] | remaining[2].data[1] << 8
            return (
                f"S6502_SEM_STORE16_IMM(0x{dst0:02x}u, 0x{value:04x}u);",
                4,
                False,
            )
    if len(remaining) >= 4 and opcodes[:4] == (0xA5, 0x85, 0xA5, 0x85):
        src0, src1 = remaining[0].data[1], remaining[2].data[1]
        dst0, dst1 = remaining[1].data[1], remaining[3].data[1]
        if _semantic_zp_pair(src0, src1) and _semantic_zp_pair(dst0, dst1):
            return (
                f"S6502_SEM_COPY16(0x{src0:02x}u, 0x{dst0:02x}u);",
                4,
                False,
            )
    if len(remaining) >= 6 and opcodes[:6] == (
        0xA0, 0xB1, 0x85, 0xC8, 0xB1, 0x85,
    ):
        pointer0, pointer1 = remaining[1].data[1], remaining[4].data[1]
        dst0, dst1 = remaining[2].data[1], remaining[5].data[1]
        if (pointer0 == pointer1 and _semantic_direct_zp(pointer0) and
                _semantic_direct_zp((pointer0 + 1) & 0xFF) and
                _semantic_zp_pair(dst0, dst1) and
                {pointer0, (pointer0 + 1) & 0xFF}.isdisjoint({dst0, dst1})):
            return (
                f"S6502_SEM_LOAD16_INDIRECT(0x{pointer0:02x}u, "
                f"0x{dst0:02x}u, 0x{remaining[0].data[1]:02x}u);",
                6,
                False,
            )
    if len(remaining) >= 6 and opcodes[:6] == (
        0xA0, 0xA5, 0x91, 0xC8, 0xA5, 0x91,
    ):
        src0, src1 = remaining[1].data[1], remaining[4].data[1]
        pointer0, pointer1 = remaining[2].data[1], remaining[5].data[1]
        if (pointer0 == pointer1 and _semantic_zp_pair(src0, src1) and
                _semantic_direct_zp(pointer0) and
                _semantic_direct_zp((pointer0 + 1) & 0xFF)):
            return (
                f"S6502_SEM_STORE16_INDIRECT(0x{pointer0:02x}u, "
                f"0x{src0:02x}u, 0x{remaining[0].data[1]:02x}u);",
                6,
                False,
            )
    if len(remaining) >= 2 and opcodes[:2] == (0xA0, 0xB1):
        if remaining[1].data[1] == 0x28:
            return (
                f"S6502_SEM_LOAD_STACK8(0x{remaining[0].data[1]:02x}u);",
                2,
                False,
            )
    if len(remaining) >= 2 and opcodes[:2] == (0xA0, 0x91):
        if remaining[1].data[1] == 0x28:
            return (
                f"S6502_SEM_STORE_STACK8(0x{remaining[0].data[1]:02x}u);",
                2,
                False,
            )
    return None


def semantic_decode_record(
    signature: bytes, record: tuple[int, ...]
) -> tuple[list[str], bool]:
    _physical, virtual, offset, size, expected_count, _bank2 = record
    data = signature[offset : offset + size]
    cursor = 0
    pc = virtual
    instructions: list[aotgen.InstructionIR] = []
    while cursor < len(data):
        opcode = data[cursor]
        length = aotgen.OPCODE_LENGTHS[opcode]
        raw = data[cursor : cursor + length]
        if not length or len(raw) != length:
            raise SystemExit(f"truncated semantic block at 0x{pc:04x}")
        reads, writes = aotgen.flag_effects(opcode)
        instructions.append(aotgen.InstructionIR(pc, raw, reads, writes))
        cursor += length
        pc = (pc + length) & 0xFFFF
    if len(instructions) != expected_count:
        raise SystemExit("semantic decode instruction count mismatch")
    aotgen.analyze_flag_liveness(instructions)
    emitted: list[str] = []
    requires_binary = False
    index = 0
    while index < len(instructions):
        phrase = semantic_peephole(instructions, index)
        if phrase is not None:
            line, count, binary = phrase
            emitted.append(line)
            requires_binary |= binary
            index += count
            continue
        lines, binary = aotgen.emit_instruction(instructions[index])
        emitted.extend(lines)
        requires_binary |= binary
        index += 1
    return emitted, requires_binary


SEMANTIC_EMULATOR_MACROS = r"""
#define S6502_SEM_ZP16(addr) ((native_u16)(ram[(native_u8)(addr)] | ((native_u16)ram[(native_u8)((addr) + 1u)] << 8)))
#define S6502_SEM_SET16(dst, value) do { native_u16 sem_v_ = (native_u16)(value); ram[(native_u8)(dst)] = (native_u8)sem_v_; ram[(native_u8)((dst) + 1u)] = (native_u8)(sem_v_ >> 8); ac = (native_u8)(sem_v_ >> 8); } while (0)
#define S6502_SEM_STORE16_IMM(dst, value) do { S6502_SEM_SET16((dst), (value)); SET_NZ(ac); CYCLES(10u); } while (0)
#define S6502_SEM_COPY16(src, dst) do { ac = ram[(native_u8)(src)]; ram[(native_u8)(dst)] = ac; ac = ram[(native_u8)((src) + 1u)]; ram[(native_u8)((dst) + 1u)] = ac; SET_NZ(ac); CYCLES(12u); } while (0)
#define S6502_SEM_ADD16_PRESERVE(src, dst, value) do { ram[0x100u | sp] = (native_u8)(status | FLAG_B | FLAG_U); S6502_SEM_SET16((dst), (native_u16)(S6502_SEM_ZP16(src) + (native_u16)(value))); status = (native_u8)(status | FLAG_B | FLAG_U); CYCLES(27u); } while (0)
#define S6502_SEM_SUB16_PRESERVE(src, dst, value) do { ram[0x100u | sp] = (native_u8)(status | FLAG_B | FLAG_U); S6502_SEM_SET16((dst), (native_u16)(S6502_SEM_ZP16(src) - (native_u16)(value))); status = (native_u8)(status | FLAG_B | FLAG_U); CYCLES(27u); } while (0)
#define S6502_SEM_ADD_BODY(left, right, dst, cost) do { native_u16 sem_l_ = (native_u16)(left); native_u16 sem_r_ = (native_u16)(right); native_u16 sem_lo_ = (native_u16)((sem_l_ & 0xffu) + (sem_r_ & 0xffu)); native_u16 sem_hi_ = (native_u16)((sem_l_ >> 8) + (sem_r_ >> 8) + (sem_lo_ >> 8)); native_u8 sem_lh_ = (native_u8)(sem_l_ >> 8); native_u8 sem_rh_ = (native_u8)(sem_r_ >> 8); native_u8 sem_ah_ = (native_u8)sem_hi_; S6502_SEM_SET16((dst), (native_u16)((sem_lo_ & 0xffu) | ((native_u16)sem_ah_ << 8))); SET_C(sem_hi_ > 0xffu); SET_V((~(sem_lh_ ^ sem_rh_) & (sem_lh_ ^ sem_ah_) & 0x80u) != 0u); SET_NZ(ac); CYCLES(cost); } while (0)
#define S6502_SEM_SUB_BODY(left, right, dst, cost) do { native_u16 sem_l_ = (native_u16)(left); native_u16 sem_r_ = (native_u16)(right); native_u8 sem_ll_ = (native_u8)sem_l_; native_u8 sem_rl_ = (native_u8)sem_r_; native_u8 sem_lh_ = (native_u8)(sem_l_ >> 8); native_u8 sem_rh_ = (native_u8)(sem_r_ >> 8); native_u8 sem_al_ = (native_u8)(sem_ll_ - sem_rl_); native_u16 sem_hsub_ = (native_u16)sem_rh_ + (sem_ll_ < sem_rl_); native_u8 sem_ah_ = (native_u8)(sem_lh_ - sem_hsub_); S6502_SEM_SET16((dst), (native_u16)(sem_al_ | ((native_u16)sem_ah_ << 8))); SET_C((native_u16)sem_lh_ >= sem_hsub_); SET_V(((sem_lh_ ^ sem_rh_) & (sem_lh_ ^ sem_ah_) & 0x80u) != 0u); SET_NZ(ac); CYCLES(cost); } while (0)
#define S6502_SEM_ADD16_IMM(src, dst, value) S6502_SEM_ADD_BODY(S6502_SEM_ZP16(src), (value), (dst), 18u)
#define S6502_SEM_SUB16_IMM(src, dst, value) S6502_SEM_SUB_BODY(S6502_SEM_ZP16(src), (value), (dst), 18u)
#define S6502_SEM_ADD16_REGS(left, right, dst) S6502_SEM_ADD_BODY(S6502_SEM_ZP16(left), S6502_SEM_ZP16(right), (dst), 20u)
#define S6502_SEM_SUB16_REGS(left, right, dst) S6502_SEM_SUB_BODY(S6502_SEM_ZP16(left), S6502_SEM_ZP16(right), (dst), 20u)
#define S6502_SEM_LOAD16_INDIRECT(pointer, dst, index) do { native_u16 sem_base_; native_u16 sem_ea_; native_u32 sem_cost_ = 20u; iy = (native_u8)(index); SET_NZ(iy); sem_base_ = S6502_SEM_ZP16(pointer); sem_ea_ = (native_u16)(sem_base_ + iy); sem_cost_ += !!(0xff00u & (sem_base_ ^ sem_ea_)); ac = READ8(sem_ea_); SET_NZ(ac); ram[(native_u8)(dst)] = ac; iy = (native_u8)(iy + 1u); SET_NZ(iy); sem_base_ = S6502_SEM_ZP16(pointer); sem_ea_ = (native_u16)(sem_base_ + iy); sem_cost_ += !!(0xff00u & (sem_base_ ^ sem_ea_)); ac = READ8(sem_ea_); SET_NZ(ac); ram[(native_u8)((dst) + 1u)] = ac; CYCLES(sem_cost_); } while (0)
#define S6502_SEM_STORE16_INDIRECT(pointer, src, index) do { native_u16 sem_base_; iy = (native_u8)(index); SET_NZ(iy); ac = ram[(native_u8)(src)]; SET_NZ(ac); sem_base_ = S6502_SEM_ZP16(pointer); WRITE8((native_u16)(sem_base_ + iy), ac); iy = (native_u8)(iy + 1u); SET_NZ(iy); ac = ram[(native_u8)((src) + 1u)]; SET_NZ(ac); sem_base_ = S6502_SEM_ZP16(pointer); WRITE8((native_u16)(sem_base_ + iy), ac); CYCLES(22u); } while (0)
#define S6502_SEM_LOAD_STACK8(index) do { native_u16 sem_base_ = S6502_SEM_ZP16(0x28u); iy = (native_u8)(index); ea = (native_u16)(sem_base_ + iy); ac = READ8(ea); SET_NZ(ac); CYCLES((native_u32)(7u + (!!(0xff00u & (sem_base_ ^ ea))))); } while (0)
#define S6502_SEM_STORE_STACK8(index) do { iy = (native_u8)(index); SET_NZ(iy); WRITE8((native_u16)(S6502_SEM_ZP16(0x28u) + iy), ac); CYCLES(8u); } while (0)
"""


def native_macro_source() -> str:
    return aotgen.MACROS.replace(
        "s6502_page3[(uint8_t)(addr)]", "READ8((uint16_t)(addr))"
    )


def parse_macro_call(line: str) -> tuple[str, list[str]] | None:
    """Parse the single terminal macro emitted for an AOT basic block."""

    match = re.fullmatch(r"([A-Z0-9_]+)\((.*)\);", line.strip())
    if match is None:
        return None
    arguments: list[str] = []
    start = 0
    depth = 0
    source = match.group(2)
    for index, character in enumerate(source):
        if character == "(":
            depth += 1
        elif character == ")":
            depth -= 1
        elif character == "," and depth == 0:
            arguments.append(source[start:index].strip())
            start = index + 1
    arguments.append(source[start:].strip())
    return match.group(1), arguments


def parse_c_integer(value: str) -> int:
    return int(value.rstrip("uUlL"), 0) & 0xFFFF


def native_direct_transfer(target: str, direct_targets: set[int]) -> str:
    address = parse_c_integer(target)
    if address in direct_targets:
        return f"NATIVE_DIRECT(native_block_{address:04x});"
    return "NATIVE_EXTERNAL();"


def emit_native_control_tail(
    line: str, direct_targets: set[int]
) -> list[str] | None:
    """Lower fixed control targets to labels in the current native module.

    Dynamic RTS/RTI/indirect targets retain the central switch.  Known branch,
    JSR and JMP targets never pay that lookup when the destination belongs to
    the same verified virtual-window/physical-bank module.
    """

    parsed = parse_macro_call(line)
    if parsed is None:
        return None
    name, args = parsed
    if name == "S6502_AOT_BRANCH" and len(args) == 3:
        condition, fallthrough, target = args
        return [
            f"pc = (native_u16)({fallthrough});",
            f"if ({condition}) {{",
            "    CYCLES(1);",
            "    CYCLES((!!(0xff00u & (pc ^ (native_u16)("
            f"        {target})))));",
            f"    pc = (native_u16)({target});",
            "    CYCLES(2);",
            f"    {native_direct_transfer(target, direct_targets)}",
            "}",
            "CYCLES(2);",
            native_direct_transfer(fallthrough, direct_targets),
        ]
    if name == "S6502_AOT_JSR" and len(args) == 2:
        return_pc, target = args
        return [
            f"PUSH((native_u16)({return_pc}) >> 8);",
            f"PUSH((native_u16)({return_pc}) & 0xffu);",
            f"pc = (native_u16)({target});",
            "CYCLES(6);",
            native_direct_transfer(target, direct_targets),
        ]
    if name == "S6502_AOT_JMP" and len(args) == 1:
        target = args[0]
        return [
            f"pc = (native_u16)({target});",
            "CYCLES(3);",
            native_direct_transfer(target, direct_targets),
        ]
    return None


def emit_compiler_runtime_lift_tail(
    line: str, direct_targets: set[int]
) -> list[str] | None:
    """Inline C6502's two ubiquitous argument-transport leaf calls.

    These routines are compiler ABI mechanics rather than guest application
    logic.  Keeping them as separate native modules still pays a native/IRAM
    boundary, another page lookup and a second module activation.  Recreate
    their exact 6502-visible result at the caller, including cycle count and
    stale hardware-stack bytes.  If the current scheduling slice ends at the
    original JSR boundary (or decimal mode is unexpectedly active), execute
    only that JSR and leave the original runtime path as the local fallback.
    """

    parsed = parse_macro_call(line)
    if parsed is None:
        return None
    name, args = parsed
    if name != "S6502_AOT_JSR" or len(args) != 2:
        return None
    return_pc = parse_c_integer(args[0])
    target = parse_c_integer(args[1])
    if target not in (0xDAAA, 0xDACA):
        return None
    next_pc = (return_pc + 1) & 0xFFFF
    call_cycles = 51 if target == 0xDAAA else 61
    runtime_instructions = 15 if target == 0xDAAA else 17
    result = [
        "/* compiler runtime lift: preserve the original JSR boundary */",
        "if (DECIMAL_p || context->cycles + executed + "
        f"{call_cycles}u > context->cycle_budget ||",
        "    (ram[0x200u] & 0x08u) || *native_epoch != entry_epoch) {",
        f"    PUSH(0x{return_pc >> 8:02x}u);",
        f"    PUSH(0x{return_pc & 0xff:02x}u);",
        f"    pc = 0x{target:04x}u;",
        "    CYCLES(6u);",
        "    NATIVE_EXTERNAL();",
        "}",
        f"ram[0x100u | sp] = 0x{return_pc >> 8:02x}u;",
        f"ram[0x100u | (native_u8)(sp - 1u)] = 0x{return_pc & 0xff:02x}u;",
        "ram[0x100u | (native_u8)(sp - 2u)] = "
        "    (native_u8)(status | FLAG_B | FLAG_U);",
        "ea = (native_u16)(ram[0x28u] | "
        "    ((native_u16)ram[0x29u] << 8));",
    ]
    if target == 0xDAAA:
        result.extend([
            "ea = (native_u16)(ea - 1u);",
            "ram[0x28u] = (native_u8)ea;",
            "ram[0x29u] = (native_u8)(ea >> 8);",
            "WRITE8(ea, ac);",
            "ix = ac;",
            "iy = 0u;",
        ])
    else:
        result.extend([
            "ea = (native_u16)(ea - 2u);",
            "ram[0x28u] = (native_u8)ea;",
            "ram[0x29u] = (native_u8)(ea >> 8);",
            "iy = 0u;",
            "ac = ram[0x20u];",
            "WRITE8(ea, ac);",
            "iy = 1u;",
            "ac = ram[0x21u];",
            "WRITE8((native_u16)(ea + 1u), ac);",
        ])
    result.extend([
        "status = (native_u8)(status | FLAG_B | FLAG_U);",
        f"CYCLES({call_cycles}u);",
        f"native_instruction_count += {runtime_instructions}u;",
        f"pc = 0x{next_pc:04x}u;",
        native_direct_transfer(f"0x{next_pc:04x}u", direct_targets),
    ])
    return result


def render_module_source(
    module_index: int,
    blocks: list[tuple[int, tuple[int, ...]]],
    signature: bytes,
    skip_hle_entries: bool = True,
    defer_dispatch_entries: bool = False,
    module_key: int = 0,
    decoder=None,
) -> tuple[str, str]:
    symbol = f"s6502_native_module_{module_index:02d}"
    section = f".text.{symbol}"
    out = [
        "/* Generated by tools/pack_native_module.py; do not edit. */",
        '#include "s6502_iram_exec_abi.h"',
        "typedef unsigned char native_u8;",
        "typedef unsigned short native_u16;",
        "typedef unsigned long native_u32;",
        "typedef native_u8 (*native_read8_fn)(native_u16);",
        "typedef void (*native_write8_fn)(native_u16, native_u8);",
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
        "#define SET_BIT(flag, value) do { status = (native_u8)((status & "
        "    (native_u8)~(flag)) | ((!!(value)) * (flag))); } while (0)",
        "#define SET_N(value) SET_BIT(FLAG_N, value)",
        "#define SET_V(value) SET_BIT(FLAG_V, value)",
        "#define SET_I(value) SET_BIT(FLAG_I, value)",
        "#define SET_Z(value) SET_BIT(FLAG_Z, value)",
        "#define SET_C(value) SET_BIT(FLAG_C, value)",
        "#define SET_NZ(value) do { native_u8 native_v = (native_u8)(value); "
        "status = (native_u8)((status & (native_u8)~(FLAG_N | FLAG_Z)) | "
        "(native_v & FLAG_N) | (!native_v * FLAG_Z)); } while (0)",
        "#define CYCLES(value) do { executed += (native_u32)(value); } while (0)",
        "#define S6502_FAST_STACK_RAM ram",
        "#define READ8(address) native_read8(context, pages, page_kind, "
        "    (native_u16)(address))",
        "#define WRITE8(address, value) native_write8(context, "
        "    (native_u16)(address), (native_u8)(value))",
        "#define READ16(address) native_read16(context, pages, page_kind, "
        "    (native_u16)(address))",
        "#define PUSH(value) do { ram[0x100u | sp] = (native_u8)(value); "
        "sp = (native_u8)(sp - 1u); } while (0)",
        "#define POP(value) ram[0x100u | ++sp]",
        "#define NATIVE_MISS() do { if (native_blocks) goto native_return; "
        "if (metrics_enabled) ++metrics[4]; return 0u; } while (0)",
        "#define NATIVE_BLOCK_DONE() do { ++native_blocks; "
        "if (context->cycles + executed >= context->cycle_budget || "
        "(ram[0x200u] & 0x08u) || *native_epoch != entry_epoch) "
        "goto native_return; } while (0)",
        "#define NATIVE_DIRECT(label) do { NATIVE_BLOCK_DONE(); "
        "++native_direct_links; goto label; } while (0)",
        "#define NATIVE_EXTERNAL() do { NATIVE_BLOCK_DONE(); "
        "goto native_return; } while (0)",
        "static __attribute__((always_inline)) inline native_u8 native_read8(",
        "    s6502_iram_asm_context_t *context, native_u8 **pages,",
        "    native_u8 *page_kind, native_u16 address) {",
        "    native_u32 page = (native_u32)(address >> 8);",
        "    native_u8 *base = pages[page];",
        "    if ((page_kind[page] & S6502_IRAM_PAGE_READ_DIRECT) && base)",
        "        return base[(native_u8)address];",
        "    native_read8_fn function = (native_read8_fn)(unsigned long)context->read8;",
        "    return function(address);",
        "}",
        f'static __attribute__((used, noinline, section("{section}")))',
        "void native_write8(",
        "    s6502_iram_asm_context_t *context, native_u16 address,",
        "    native_u8 value) {",
        "    native_u8 **pages = (native_u8 **)(unsigned long)context->pages;",
        "    native_u8 *page_kind = (native_u8 *)(unsigned long)"
        "context->page_kind;",
        "    native_u8 *ram = (native_u8 *)(unsigned long)context->ram;",
        "    native_u32 *dirty = (native_u32 *)(unsigned long)context->dirty;",
        "    native_u32 page = (native_u32)(address >> 8);",
        "    native_u8 kind = page_kind[page];",
        "    native_u8 *base = pages[page];",
        "    if ((kind & S6502_IRAM_PAGE_WRITE_DIRECT) && base) {",
        "        native_u8 *pointer = base + (native_u8)address;",
        "        native_u8 old = *pointer;",
        "        native_u8 *lcd_first = ram + 0x0400u;",
        "        native_u8 *lcd_last = ram + 0x1000u;",
        "        int lcd = pointer >= lcd_first && pointer <= lcd_last;",
        "        native_u32 *write_calls = (native_u32 *)(unsigned long)"
        "context->lcd_write_calls;",
        "        native_u32 *changed_writes = (native_u32 *)(unsigned long)"
        "context->lcd_changed_writes;",
        "        if (lcd && write_calls) ++*write_calls;",
        "        *pointer = value;",
        "        if (lcd && old != value) {",
        "            if (dirty) *dirty = 1u;",
        "            if (changed_writes) ++*changed_writes;",
        "        }",
        "        if ((kind & S6502_IRAM_PAGE_FORCE_PB_ZERO) &&",
        "            pointer == ram + 0x021bu) *pointer = 0u;",
        "        if ((kind & S6502_IRAM_PAGE_FORCE_APO_FF) &&",
        "            pointer == ram + 0x2028u) *pointer = 0xffu;",
        "        return;",
        "    }",
        "    native_write8_fn function = (native_write8_fn)(unsigned long)context->write8;",
        "    function(address, value);",
        "}",
        "static __attribute__((always_inline)) inline native_u16 native_read16(",
        "    s6502_iram_asm_context_t *context, native_u8 **pages,",
        "    native_u8 *page_kind, native_u16 address) {",
        "    native_u16 low = native_read8(context, pages, page_kind, address);",
        "    return (native_u16)(low | ((native_u16)native_read8(context,"
        "        pages, page_kind, (native_u16)(address + 1u)) << 8));",
        "}",
        native_macro_source(),
        SEMANTIC_EMULATOR_MACROS,
        f'__attribute__((used, noinline, section("{section}")))',
        f"native_u32 {symbol}(s6502_iram_asm_context_t *context) {{",
        "    native_u32 executed = 0u;",
        "    native_u32 native_instruction_count = 0u;",
        "    native_u32 native_blocks = 0u;",
        "    native_u32 native_7c30_entries = 0u;",
        "    native_u32 native_direct_links = 0u;",
        "    native_u16 pc = (native_u16)context->pc;",
        "    native_u16 ea = 0u;",
        "    native_u16 et = 0u;",
        "    native_u8 ac = (native_u8)context->ac;",
        "    native_u8 ix = (native_u8)context->ix;",
        "    native_u8 iy = (native_u8)context->iy;",
        "    native_u8 sp = (native_u8)context->sp;",
        "    native_u8 status = (native_u8)context->status;",
        "    native_u8 dt = 0u;",
        "    native_u8 *ram = (native_u8 *)(unsigned long)context->ram;",
        "    native_u8 **pages = (native_u8 **)(unsigned long)context->pages;",
        "    native_u8 *page_kind = (native_u8 *)(unsigned long)"
        "context->page_kind;",
        "    native_u8 *dispatch_bits = (native_u8 *)(unsigned long)"
        "context->dispatch_bits;",
        "    native_u32 *metrics = (native_u32 *)(unsigned long)"
        "context->native_shared_metrics;",
        "    native_u32 metrics_enabled = metrics ? metrics[9] : 0u;",
        "    native_u32 *native_epoch = (native_u32 *)(unsigned long)"
        "context->native_epoch;",
        "    native_u32 entry_epoch;",
        f"    if (metrics) metrics[8] = 0x{module_key:08x}u;",
        "    if (metrics_enabled) ++metrics[0];",
        "    if (!ram || !pages || !page_kind || !context->read8 ||",
        "        !context->write8 || !native_epoch)",
        "        NATIVE_MISS();",
        "    entry_epoch = *native_epoch;",
        "native_dispatch:",
        "    switch (pc) {",
    ]
    seen: set[int] = set()
    decoded_blocks: list[tuple[int, tuple[int, ...], list[str], bool]] = []
    for _block_id, record in blocks:
        virtual = record[1]
        # These entry PCs already have verified high-level S1C33 handlers in
        # the main executable.  Keep the page module available for its other
        # blocks, but deliberately miss at the HLE boundary so the faster
        # whole-operation replacement retains priority.
        if skip_hle_entries and virtual in aotgen.HLE_ENTRY_PCS:
            continue
        if virtual in seen:
            raise SystemExit(
                f"duplicate virtual PC 0x{virtual:04x} in native module"
            )
        seen.add(virtual)
        emitted, requires_binary = (
            decoder(signature, record)
            if decoder is not None else decode_record(signature, record)
        )
        decoded_blocks.append((virtual, record, emitted, requires_binary))

    for virtual, _record, _emitted, _requires_binary in decoded_blocks:
        out.append(
            f"    case 0x{virtual:04x}u: goto native_block_{virtual:04x};"
        )
    out.extend(
        [
            "    default:",
            "        NATIVE_MISS();",
            "    }",
        ]
    )
    for virtual, record, emitted, requires_binary in decoded_blocks:
        out.append(f"native_block_{virtual:04x}:")
        if defer_dispatch_entries:
            out.append("    if (dispatch_bits && dispatch_bits[pc]) NATIVE_MISS();")
        if requires_binary:
            out.append("    if (DECIMAL_p) NATIVE_MISS();")
        out.append(f"    native_instruction_count += {record[4]}u;")
        if virtual == 0x7C30:
            out.append("    ++native_7c30_entries;")
        runtime_lift_tail = emit_compiler_runtime_lift_tail(
            emitted[-1], seen
        )
        direct_tail = None if runtime_lift_tail is not None else \
            emit_native_control_tail(emitted[-1], seen)
        body = emitted[:-1] if (
            runtime_lift_tail is not None or direct_tail is not None
        ) else emitted
        for line in body:
            out.append(f"    {line}")
        if runtime_lift_tail is not None:
            for line in runtime_lift_tail:
                out.append(f"    {line}")
        elif direct_tail is not None:
            for line in direct_tail:
                out.append(f"    {line}")
        out.append("    NATIVE_MISS();")
    out.extend(
        [
            "native_chain:",
            "    NATIVE_BLOCK_DONE();",
            "    goto native_dispatch;",
            "native_return:",
            "    context->pc = pc;",
            "    context->ac = ac;",
            "    context->ix = ix;",
            "    context->iy = iy;",
            "    context->sp = sp;",
            "    context->status = status;",
            "    context->cycles += executed;",
            "    if (native_instruction_count)",
            "        context->instructions += native_instruction_count - 1u;",
            "    if (native_blocks)",
            "        context->control_transitions += native_blocks - 1u;",
            "    if (metrics_enabled) {",
            "        metrics[1] += native_blocks;",
            "        metrics[2] += executed;",
            "        metrics[3] += native_7c30_entries;",
            "        if (native_blocks > 1u) metrics[5] += native_blocks - 1u;",
            "        if (native_blocks > metrics[6]) metrics[6] = native_blocks;",
            "        metrics[7] += native_direct_links;",
            "    }",
            "    return 1u;",
            "}",
        ]
    )
    source = "\n".join(out).replace("goto _exit", "goto native_chain")
    return source, section


def extract_section(objcopy: str, object_path: Path, section: str, raw: Path) -> bytes:
    subprocess.run(
        [objcopy, "-O", "binary", f"--only-section={section}", str(object_path), str(raw)],
        check=True,
    )
    code = raw.read_bytes()
    if not code:
        raise SystemExit(f"native module section is empty: {section}")
    return code


def compile_modules(
    clang: str,
    objcopy: str,
    readelf: str,
    include_dir: Path,
    signature: bytes,
    records: list[tuple[int, ...]],
    optimization: str,
    module_index_base: int = 0,
    module_flags: int = 0,
    skip_hle_entries: bool = True,
    defer_dispatch_entries: bool = False,
    group_shift: int = 12,
) -> list[NativeModule]:
    groups: dict[tuple[int, int], list[tuple[int, tuple[int, ...]]]] = defaultdict(list)
    for block_id, record in enumerate(records):
        physical, virtual = record[:2]
        groups[(virtual >> group_shift, physical >> group_shift)].append(
            (block_id, record)
        )
    modules: list[NativeModule] = []
    with tempfile.TemporaryDirectory(prefix="gam4980-native-all-") as directory:
        temporary = Path(directory)

        def compile_group(
            item: tuple[int, tuple[tuple[int, int], list[tuple[int, tuple[int, ...]]]]]
        ) -> tuple[int, NativeModule]:
            local_index, ((virtual_group, physical_group), blocks) = item
            slot = blocks[0][1][1] >> 12
            bank = blocks[0][1][0] >> 12
            first_page = min(record[1] for _block_id, record in blocks) >> 8
            module_key = (
                ((slot & 0x0F) << 28) |
                ((bank & 0xFFFF) << 12) |
                ((first_page & 0xFF) << 4) |
                (group_shift & 0x0F)
            )
            module_index = module_index_base + local_index
            symbol = f"s6502_native_module_{module_index:02d}"
            source_text, section = render_module_source(
                module_index, blocks, signature, skip_hle_entries,
                defer_dispatch_entries, module_key,
                semantic_decode_record if module_flags & MODULE_GAME else None,
            )
            source = temporary / f"native_{module_index:02d}.c"
            object_path = temporary / f"native_{module_index:02d}.o"
            raw = temporary / f"native_{module_index:02d}.bin"
            source.write_text(source_text, encoding="utf-8")
            subprocess.run(
                [
                    clang,
                    "--target=s1c33-none-elf",
                    f"-O{optimization}",
                    "-ffreestanding",
                    "-fno-builtin",
                    "-fno-jump-tables",
                    "-fomit-frame-pointer",
                    "-fno-strict-aliasing",
                    "-I",
                    str(include_dir),
                    "-c",
                    str(source),
                    "-o",
                    str(object_path),
                ],
                check=True,
            )
            relocations = subprocess.check_output(
                [readelf, "-r", str(object_path)], text=True
            )
            if "There are no relocations" not in relocations:
                raise SystemExit(
                    f"native module {module_index} is not position independent:\n"
                    f"{relocations}"
                )
            symbols = subprocess.check_output(
                [readelf, "-sW", str(object_path)], text=True
            )
            entry_match = re.search(
                rf"^\s*\d+:\s+([0-9a-fA-F]+)\s+\d+\s+FUNC\s+"
                rf"\S+\s+\S+\s+\S+\s+{re.escape(symbol)}\s*$",
                symbols,
                re.M,
            )
            if entry_match is None:
                raise SystemExit(
                    f"native module entry symbol is missing: {symbol}"
                )
            entry_offset = int(entry_match.group(1), 16)
            code = extract_section(objcopy, object_path, section, raw)
            if entry_offset >= len(code):
                raise SystemExit(
                    f"native module entry is outside code: {symbol}"
                )
            return (
                local_index,
                NativeModule(
                    slot, bank, code, blocks, entry_offset, module_flags,
                    module_key,
                ),
            )
        group_items = list(enumerate(sorted(groups.items())))
        worker_count = min(8, len(group_items), os.cpu_count() or 1)
        with ThreadPoolExecutor(max_workers=worker_count) as executor:
            compiled = list(executor.map(compile_group, group_items))
        modules.extend(
            module for _index, module in sorted(compiled)
        )
    return modules


def split_oversized_game_modules(
    modules: list[NativeModule],
    clang: str,
    objcopy: str,
    readelf: str,
    include_dir: Path,
    game: bytes,
    optimization: str,
    module_index_base: int,
) -> list[NativeModule]:
    """Split only dense 2 KiB game groups into 1 KiB modules.

    Four resident slots remove most two-slot ping-pong, but a few dense C6502
    pages compile above the resulting per-slot limit.  Recompiling every game
    page at 1 KiB granularity would exceed the bounded manifest.  Split only
    measured oversized modules and keep all other direct links intact.
    """

    retained: list[NativeModule] = []
    split_records: list[tuple[int, ...]] = []
    for module in modules:
        if len(module.code) <= GAME_NATIVE_MODULE_TARGET_SIZE:
            retained.append(module)
        else:
            split_records.extend(record for _block_id, record in module.blocks)
    if not split_records:
        return modules
    replacements = compile_modules(
        clang,
        objcopy,
        readelf,
        include_dir,
        game,
        split_records,
        optimization,
        module_index_base=module_index_base + len(retained),
        module_flags=MODULE_GAME,
        skip_hle_entries=False,
        defer_dispatch_entries=True,
        group_shift=10,
    )
    oversized = [
        len(module.code) for module in replacements
        if len(module.code) > GAME_NATIVE_MODULE_TARGET_SIZE
    ]
    if oversized:
        raise SystemExit(
            "1 KiB GAM native module still exceeds target: "
            f"{max(oversized)} > {GAME_NATIVE_MODULE_TARGET_SIZE}"
        )
    return retained + replacements


def build_package(
    modules: list[NativeModule], signature: bytes,
    game_metadata: GameMetadata | None = None,
) -> bytes:
    module_count = len(modules)
    header_size = GAME_HEADER_SIZE if game_metadata is not None else HEADER_SIZE
    format_version = (
        GAME_FORMAT_VERSION if game_metadata is not None else FORMAT_VERSION
    )
    match_count = sum(
        len(module.blocks)
        for module in modules
        if not (module.flags & MODULE_GAME)
    )
    module_links: list[list[int]] = []
    for module in modules:
        module_links.append(
            sorted({record[1] >> 8 for _id, record in module.blocks})
        )
    link_count = sum(len(links) for links in module_links)
    code_spans = game_metadata.code_spans if game_metadata is not None else ()
    reloc_count = len(code_spans)
    module_offset = header_size
    match_offset = module_offset + module_count * MODULE_SIZE
    reloc_offset = match_offset + match_count * MATCH_SIZE
    link_offset = reloc_offset + reloc_count * LINK_SIZE
    payload_offset = align(link_offset + link_count * LINK_SIZE, 16)
    runtime_slot_size = (
        (RUNTIME_NATIVE_ARENA_SIZE - payload_offset) //
        RUNTIME_NATIVE_CODE_SLOTS
    ) & ~15
    for module_index, module in enumerate(modules):
        if not module.code or module.entry_offset >= len(module.code):
            raise SystemExit(
                f"native module {module_index} has an invalid entry"
            )
        if len(module.code) > runtime_slot_size:
            raise SystemExit(
                f"native module {module_index} is {len(module.code)} bytes, "
                f"larger than runtime slot {runtime_slot_size}"
            )
    code_cursor = payload_offset
    code_offsets: list[int] = []
    for module in modules:
        code_cursor = align(code_cursor, 16)
        code_offsets.append(code_cursor)
        code_cursor += len(module.code)
    file_size = code_cursor
    manifest = bytearray(payload_offset)
    match_cursor = 0
    link_cursor = 0
    for module_index, module in enumerate(modules):
        flags = MODULE_PIC | module.flags
        if (module.mapping_slot, module.physical_bank) == PRELOAD_MAPPING:
            flags |= MODULE_PRELOAD
        mapping = (module.mapping_slot << 16) | module.physical_bank
        record = struct.pack(
            "<14I",
            mapping,
            flags,
            code_offsets[module_index],
            len(module.code),
            fnv1a(module.code),
            module.entry_offset,
            match_cursor,
            0 if module.flags & MODULE_GAME else len(module.blocks),
            0,
            0,
            link_cursor,
            len(module_links[module_index]),
            mapping,
            module.module_key,
        )
        start = module_offset + module_index * MODULE_SIZE
        manifest[start : start + MODULE_SIZE] = record
        for block_id, block in (
            () if module.flags & MODULE_GAME else module.blocks
        ):
            physical, _virtual, sig_offset, sig_size, _insns, _bank2 = block
            block_signature = signature[sig_offset : sig_offset + sig_size]
            match = struct.pack(
                "<4I", module_index, block_id, physical, fnv1a(block_signature)
            )
            start = match_offset + match_cursor * MATCH_SIZE
            manifest[start : start + MATCH_SIZE] = match
            match_cursor += 1
        for page in module_links[module_index]:
            link = struct.pack(
                "<4I", module_index, page << 8, mapping,
                module.entry_offset,
            )
            start = link_offset + link_cursor * LINK_SIZE
            manifest[start : start + LINK_SIZE] = link
            link_cursor += 1
    for span_index, (span_offset, span_size) in enumerate(code_spans):
        reloc = struct.pack(
            "<4I", RELOC_GAME_OWNER, span_offset,
            RELOC_GAME_CODE_SPAN, span_size,
        )
        start = reloc_offset + span_index * LINK_SIZE
        manifest[start : start + LINK_SIZE] = reloc
    if game_metadata is not None:
        manifest[HEADER_SIZE:GAME_HEADER_SIZE] = struct.pack(
            "<8I",
            PACKAGE_GAME,
            game_metadata.file_size,
            game_metadata.code_size,
            game_metadata.file_hash,
            game_metadata.entry_pc,
            game_metadata.block_count,
            game_metadata.native_bytes,
            len(code_spans),
        )
    manifest_hash = fnv1a(bytes(manifest[HEADER_SIZE:payload_offset]))
    header = struct.pack(
        "<16I",
        MAGIC,
        format_version,
        ABI_VERSION,
        header_size,
        file_size,
        module_count,
        module_offset,
        match_count,
        match_offset,
        reloc_count,
        reloc_offset,
        link_count,
        link_offset,
        payload_offset,
        file_size - payload_offset,
        manifest_hash,
    )
    manifest[:HEADER_SIZE] = header
    package = bytearray(file_size)
    package[:payload_offset] = manifest
    for offset, module in zip(code_offsets, modules):
        package[offset : offset + len(module.code)] = module.code
    return bytes(package)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Compile and pack pageable S1C33 code for GAM4980"
    )
    parser.add_argument("--clang", required=True)
    parser.add_argument("--objcopy", required=True)
    parser.add_argument("--readelf", required=True)
    parser.add_argument("--include-dir", type=Path, required=True)
    parser.add_argument("--aot-header", type=Path, required=True)
    parser.add_argument(
        "--game",
        type=Path,
        help=(
            "build a game-bound combined package containing all firmware "
            "modules plus every statically recoverable GAM block"
        ),
    )
    parser.add_argument("--optimization", choices=("2", "3", "s", "z"), default="2")
    parser.add_argument(
        "--game-optimization",
        choices=("2", "3", "s", "z"),
        default="z",
        help="S1C33 optimization used for game modules (default: z)",
    )
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    signature, records = parse_aot_header(args.aot_header)
    modules = compile_modules(
        args.clang,
        args.objcopy,
        args.readelf,
        args.include_dir,
        signature,
        records,
        args.optimization,
    )
    game_metadata = None
    if args.game is not None:
        game = args.game.read_bytes()
        game_records, game_stats = recover_game_blocks(game)
        game_modules = compile_modules(
            args.clang,
            args.objcopy,
            args.readelf,
            args.include_dir,
            game,
            game_records,
            args.game_optimization,
            module_index_base=len(modules),
            module_flags=MODULE_GAME,
            skip_hle_entries=False,
            defer_dispatch_entries=True,
            group_shift=11,
        )
        game_modules = split_oversized_game_modules(
            game_modules,
            args.clang,
            args.objcopy,
            args.readelf,
            args.include_dir,
            game,
            args.game_optimization,
            len(modules),
        )
        modules.extend(game_modules)
        game_metadata = GameMetadata(
            len(game),
            game_stats["code_size"],
            fnv1a(game),
            game_stats["entry_pc"],
            len(game_records),
            sum(len(module.code) for module in game_modules),
            merge_game_code_spans(game_records),
        )
        print(
            "recovered GAM: "
            f"blocks={len(game_records)}, code_bytes="
            f"{sum(record[3] for record in game_records)}, "
            f"modules={len(game_modules)}, far_calls={game_stats['far_calls']}"
        )
    package = build_package(modules, signature, game_metadata)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(package)
    print(
        f"built native package: {args.output} "
        f"({len(package)} bytes, modules={len(modules)}, "
        f"blocks={sum(len(module.blocks) for module in modules)}, "
        f"code={sum(len(module.code) for module in modules)} bytes)"
    )


if __name__ == "__main__":
    main()
