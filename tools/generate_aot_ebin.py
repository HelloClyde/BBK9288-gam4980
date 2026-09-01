#!/usr/bin/env python3
"""Generate guarded C superblocks for the fixed GAM4980 E.BIN ROM."""

from __future__ import annotations

import argparse
import csv
from dataclasses import dataclass
import hashlib
from enum import IntFlag
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_ROM = ROOT / "应用" / "数据" / "游戏" / "gam4980" / "E.BIN"
DEFAULT_OUTPUT = ROOT / "src" / "s6502_aot_ebin_generated.h"
DEFAULT_PROFILE = ROOT / "tools" / "ebin_aot_profile.csv"
DEFAULT_PROFILE_LIMIT = 926

# Ranked by the complete 8000-frame opening-story trace documented in
# docs/aot-profiling.md.
# Each tuple is (physical PC, virtual PC).
HOTSPOTS = (
    (0xEB5A75, 0x6A75),
    (0xEB5AE0, 0x6AE0),
    (0xEB8CB3, 0x5CB3),
    (0xEB8CE5, 0x5CE5),
    (0xEB5678, 0x6678),
    (0xEAA55B, 0xF55B),
    (0xEB508A, 0x608A),
    (0xEBE937, 0x7937),
    (0xEB5111, 0x6111),
    (0xEB5AA7, 0x6AA7),
    (0xEB5AAE, 0x6AAE),
    (0xEA8352, 0xD352),
    (0xEB5655, 0x6655),
    (0xEB550F, 0x650F),
    (0xEA8349, 0xD349),
    (0xEB6233, 0x7233),
    (0xEA8340, 0xD340),
    (0xEB782A, 0x882A),
    (0xEB5646, 0x6646),
    (0xEBE933, 0x7933),
    (0xEB55A0, 0x65A0),
    (0xEA835D, 0xD35D),
    (0xEB6BD4, 0x7BD4),
    (0xEB6C30, 0x7C30),
    (0xEAA4CA, 0xF4CA),
    (0xEB5B1A, 0x6B1A),
    (0xEB5BA4, 0x6BA4),
    (0xEB5B02, 0x6B02),
    (0xEAA52A, 0xF52A),
    (0xEB5131, 0x6131),
    (0xEA82CA, 0xD2CA),
    (0xEB6C47, 0x7C47),
    (0xEB50C2, 0x60C2),
    (0xEB50CC, 0x60CC),
    (0xEB7822, 0x8822),
    (0xEB50D3, 0x60D3),
    (0xEB50DA, 0x60DA),
    (0xEAA549, 0xF549),
    (0xEB5C18, 0x6C18),
    (0xEB8CDA, 0x5CDA),
    (0xEA8572, 0xD572),
    (0xEB8C8C, 0x5C8C),
    (0xEB8D99, 0x5D99),
    (0xEB6C38, 0x7C38),
    (0xEB002B, 0x502B),
    (0xEB8D17, 0x5D17),
    (0xEA8596, 0xD596),
    (0xEB5086, 0x6086),
    (0xEB6BFA, 0x7BFA),
    (0xEB553F, 0x653F),
    (0xEA8302, 0xD302),
    (0xEAA4A5, 0xF4A5),
    (0xEB77C8, 0x87C8),
    (0xEB5549, 0x6549),
    (0xEB56E5, 0x66E5),
    (0xEB61FB, 0x71FB),
    (0xEB6202, 0x7202),
    (0xEB5550, 0x6550),
    (0xEB5557, 0x6557),
    (0xEB55BA, 0x65BA),
    (0xEB550B, 0x650B),
    (0xEB77ED, 0x87ED),
    (0xEAA53A, 0xF53A),
    (0xEB4988, 0x5988),
    (0xEB6C29, 0x7C29),
    (0xEB503A, 0x603A),
    (0xEB49BA, 0x59BA),
    (0xEB564F, 0x664F),
    (0xEB54BC, 0x64BC),
    (0xEB4D31, 0x5D31),
    (0xEB48FC, 0x58FC),
    (0xEB5412, 0x6412),
    (0xEA8362, 0xD362),
    (0xEB5628, 0x6628),
    (0xEB77BD, 0x87BD),
    (0xEB4851, 0x5851),
    (0xEB56DC, 0x66DC),
    (0xEB55C1, 0x65C1),
    (0xEA838E, 0xD38E),
    (0xEB13F6, 0x63F6),
    (0xEB7819, 0x8819),
    (0xEA82FA, 0xD2FA),
    (0xEB5072, 0x6072),
    (0xEB564D, 0x664D),
    (0xEA8384, 0xD384),
    (0xEA837A, 0xD37A),
    (0xEA8370, 0xD370),
    (0xEB5612, 0x6612),
    (0xEB5653, 0x6653),
    (0xEB6BCA, 0x7BCA),
    (0xEB519B, 0x619B),
    (0xEB4846, 0x5846),
)

# Keep the formal blocks in the layout that avoids excessive live ranges in
# the S1C33 backend.  Selection still comes from the complete trace above;
# ordering here is a code-generation constraint, not a heat ranking.
FORMAL_HOTSPOTS = (
    (0xEB5678, 0x6678),
    (0xEAA55B, 0xF55B),
    (0xEB508A, 0x608A),
    (0xEBE937, 0x7937),
    (0xEB5111, 0x6111),
    (0xEB550F, 0x650F),
    (0xEB5A75, 0x6A75),
    (0xEB5655, 0x6655),
    (0xEB6233, 0x7233),
    (0xEB5AE0, 0x6AE0),
    (0xEB782A, 0x882A),
    (0xEB55A0, 0x65A0),
    (0xEBE933, 0x7933),
    (0xEB5646, 0x6646),
    (0xEB6BD4, 0x7BD4),
    (0xEB6C30, 0x7C30),
    (0xEAA4CA, 0xF4CA),
    (0xEB5131, 0x6131),
    (0xEB6C47, 0x7C47),
    (0xEA81A2, 0xD1A2),
    (0xEA82CA, 0xD2CA),
    (0xEAA52A, 0xF52A),
    (0xEB50C2, 0x60C2),
    (0xEB7822, 0x8822),
    (0xEB50CC, 0x60CC),
    (0xEB50D3, 0x60D3),
    (0xEB50DA, 0x60DA),
    (0xEB6C38, 0x7C38),
    (0xEA8572, 0xD572),
    (0xEB5AA7, 0x6AA7),
    (0xEB5AAE, 0x6AAE),
    (0xEA8596, 0xD596),
    (0xEA8340, 0xD340),
    (0xEA8349, 0xD349),
    (0xEA8352, 0xD352),
    (0xEA835D, 0xD35D),
    (0xEB5B02, 0x6B02),
    (0xEB5B1A, 0x6B1A),
    (0xEB5BA4, 0x6BA4),
    (0xEB5C18, 0x6C18),
    (0xEAA549, 0xF549),
    (0xEB002B, 0x502B),
    (0xEB8C8C, 0x5C8C),
    (0xEB8CB3, 0x5CB3),
    (0xEB8CDA, 0x5CDA),
    (0xEB8CE5, 0x5CE5),
    (0xEB8D17, 0x5D17),
    (0xEB8D99, 0x5D99),
)
# More ranked candidates remain available for tuning.  The 47 formal blocks
# include the guarded entry needed by the sixth firmware HLE.
DEFAULT_BLOCK_LIMIT = len(FORMAL_HOTSPOTS)
DEFAULT_CHAIN_BLOCK_LIMIT = 150

# Some public-ROM HLE boundaries are cold in the original single-game trace
# but must still have guarded dispatcher entries so other GAMs can use them.
REQUIRED_HLE_HOTSPOTS = (
    (0xEB7039, 0x8039),
    (0xEB8351, 0x5351),
    (0xEB8801, 0x5801),
    (0xEB759E, 0x859E),
)

OPCODE_LENGTHS = (
    1,2,2,1,2,2,2,2,1,2,1,1,3,3,3,3,
    2,2,2,1,2,2,2,2,1,3,1,1,3,3,3,3,
    3,2,2,1,2,2,2,2,1,2,1,1,3,3,3,3,
    2,2,2,1,2,2,2,2,1,3,1,1,3,3,3,3,
    1,2,2,1,2,2,2,2,1,2,1,1,3,3,3,3,
    2,2,2,1,2,2,2,2,1,3,1,1,3,3,3,3,
    1,2,2,1,2,2,2,2,1,2,1,1,3,3,3,3,
    2,2,2,1,2,2,2,2,1,3,1,1,3,3,3,3,
    2,2,2,1,2,2,2,2,1,2,1,1,3,3,3,3,
    2,2,2,1,2,2,2,2,1,3,1,1,3,3,3,3,
    2,2,2,1,2,2,2,2,1,2,1,1,3,3,3,3,
    2,2,2,1,2,2,2,2,1,3,1,1,3,3,3,3,
    2,2,2,1,2,2,2,2,1,2,1,1,3,3,3,3,
    2,2,2,1,2,2,2,2,1,3,1,1,3,3,3,3,
    2,2,2,1,2,2,2,2,1,2,1,1,3,3,3,3,
    2,2,2,1,2,2,2,2,1,3,1,1,3,3,3,3,
)

TERMINATORS = {
    0x00, 0x20, 0x40, 0x4C, 0x60, 0x6C, 0x7C, 0x80,
    0x10, 0x30, 0x50, 0x70, 0x90, 0xB0, 0xD0, 0xF0,
    0x0F, 0x1F, 0x2F, 0x3F, 0x4F, 0x5F, 0x6F, 0x7F,
    0x8F, 0x9F, 0xAF, 0xBF, 0xCF, 0xDF, 0xEF, 0xFF,
}

# Entering these virtual PCs through the normal dispatcher is intentional:
# it runs the guarded firmware HLE hooks before falling back to the AOT block.
# A direct AOT-to-AOT chain would be semantically correct but can silently
# replace a much faster HLE call with the lower-level translated block.
HLE_ENTRY_PCS = {
    0x5351, 0x5801,
    0x5C5D, 0x5CB3, 0x5CE5,
    0x608A,
    0x650F,
    0x682D, 0x690F,
    0x6988,
    0x6A75, 0x6AA7, 0x6AE0,
    0x6B1A, 0x6BA4,
    0x7937,
    0x8039,
    0x859E,
    0x876B,
    0xD1A2, 0xD2CA,
    0xD340, 0xD349, 0xD352, 0xD35D, 0xD35F,
    0xD362,
    0xD572, 0xD596,
    0xF52A, 0xF549, 0xF55B,
}

PAGE0_SPECIAL_READ = {0x00, 0x01, 0x02, 0x03, 0x0C, 0x0D, 0x0E}
PAGE0_SPECIAL_WRITE = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x0C, 0x0D, 0x0E}


class CpuFlag(IntFlag):
    """6502 status bits tracked by the offline AOT IR."""

    C = 0x01
    Z = 0x02
    I = 0x04
    D = 0x08
    B = 0x10
    U = 0x20
    V = 0x40
    N = 0x80

    NZ = N | Z
    NZC = N | Z | C
    NZCV = N | Z | C | V
    ALL = 0xFF


@dataclass
class InstructionIR:
    """One decoded 6502 operation plus its status data-flow contract."""

    pc: int
    data: bytes
    reads: CpuFlag
    writes: CpuFlag
    live_writes: CpuFlag = CpuFlag.ALL


BRANCH_FLAG_READS = {
    0x10: CpuFlag.N,
    0x30: CpuFlag.N,
    0x50: CpuFlag.V,
    0x70: CpuFlag.V,
    0x90: CpuFlag.C,
    0xB0: CpuFlag.C,
    0xD0: CpuFlag.Z,
    0xF0: CpuFlag.Z,
}

NZ_WRITERS = {
    0x05, 0x09, 0x0D, 0x11, 0x1D,       # ORA
    0x25, 0x29, 0x2D, 0x31,             # AND
    0x45, 0x49, 0x51,                   # EOR
    0x68,                               # PLA
    0x88, 0x8A, 0x98,                   # DEY/TXA/TYA
    0xA0, 0xA2, 0xA4, 0xA5, 0xA6, 0xA8, # loads/transfers
    0xA9, 0xAA, 0xAC, 0xAD, 0xAE,
    0xB1, 0xBA, 0xBD,
    0xC6, 0xC8, 0xCA, 0xCE, 0xDE,       # DEC/INY/DEX
    0xE6, 0xE8, 0xEE,                   # INC/INX
}
NZC_WRITERS = {
    0x06, 0x0A, 0x0E, 0x1E,             # ASL
    0x26, 0x2A, 0x2E, 0x3E,             # ROL
    0x46, 0x4A, 0x4E,                   # LSR
    0x66, 0x6A, 0x6E,                   # ROR
    0xC0, 0xC5, 0xC9, 0xCD,             # CMP/CPY
    0xE0, 0xEC,                         # CPX
}
ARITHMETIC_WRITERS = {0x65, 0x69, 0x6D, 0x71, 0xE5, 0xE9, 0xED, 0xF1}
ROTATE_READERS = {0x26, 0x2A, 0x2E, 0x3E, 0x66, 0x6A, 0x6E}

def flag_effects(opcode: int) -> tuple[CpuFlag, CpuFlag]:
    """Return status bits read and written by a supported opcode."""

    reads = BRANCH_FLAG_READS.get(opcode, CpuFlag(0))
    writes = CpuFlag(0)
    if opcode in NZ_WRITERS:
        writes = CpuFlag.NZ
    elif opcode in NZC_WRITERS:
        writes = CpuFlag.NZC
    elif opcode in ARITHMETIC_WRITERS:
        reads |= CpuFlag.C | CpuFlag.D
        writes = CpuFlag.NZCV
    elif opcode == 0x18 or opcode == 0x38:
        writes = CpuFlag.C
    elif opcode == 0x78:
        writes = CpuFlag.I
    elif opcode == 0x08:
        reads = CpuFlag.ALL
    elif opcode == 0x28 or opcode == 0x40:
        writes = CpuFlag.ALL
    if opcode in ROTATE_READERS:
        reads |= CpuFlag.C
    return reads, writes


def analyze_flag_liveness(instructions: list[InstructionIR]) -> None:
    """Remove status writes hidden by a later write inside one basic block.

    All status bits are conservatively live at every block exit because the
    dispatcher, an interrupt, or an unchained successor can observe them.  N
    and Z share one compact materialization in the native backend, so retain
    them as a pair whenever either result remains live.
    """

    live = CpuFlag.ALL
    for instruction in reversed(instructions):
        needed = instruction.writes & live
        if needed & CpuFlag.NZ:
            needed |= instruction.writes & CpuFlag.NZ
        instruction.live_writes = needed
        live = (live & ~instruction.writes) | instruction.reads


def u16(data: bytes) -> int:
    return data[0] | data[1] << 8


def branch_target(pc: int, displacement: int) -> int:
    signed = displacement if displacement < 0x80 else displacement - 0x100
    return (pc + 2 + signed) & 0xFFFF


def read_expr(addr: int) -> str:
    if addr < 0x100 and addr not in PAGE0_SPECIAL_READ:
        return f"S6502_AOT_ZP_READ(0x{addr:02x}u)"
    if 0x2000 <= addr < 0x3000:
        return f"S6502_AOT_RAM_READ(0x{addr:04x}u)"
    if 0x0300 <= addr < 0x0400:
        return f"S6502_AOT_PAGE3_READ(0x{addr:04x}u)"
    return f"READ8(0x{addr:04x}u)"


def indirect_base_expr(zp: int) -> str:
    next_zp = (zp + 1) & 0xFF
    if zp not in PAGE0_SPECIAL_READ and next_zp not in PAGE0_SPECIAL_READ:
        return f"S6502_AOT_ZP16(0x{zp:02x}u)"
    return f"READ16W(0x{zp:02x}u)"


def store_line(register: str, addr: int, cost: int) -> str:
    if addr < 0x100 and addr not in PAGE0_SPECIAL_WRITE:
        return f"S6502_AOT_ST{register}_ZP(0x{addr:02x}u, {cost});"
    if 0x2000 <= addr < 0x3000 and addr not in {0x2028}:
        return f"S6502_AOT_ST{register}_RAM(0x{addr:04x}u, {cost});"
    return f"S6502_AOT_ST{register}(0x{addr:04x}u, {cost});"


def rmw_line(
    operation: str, addr: int, flags: str, cost: int | None = None,
) -> str:
    suffix = f", {cost}, {flags}" if cost is not None else f", {flags}"
    if 0x2000 <= addr < 0x3000 and addr not in {0x2028}:
        return f"S6502_AOT_{operation}_RAM(0x{addr:04x}u{suffix});"
    return f"S6502_AOT_{operation}(0x{addr:04x}u{suffix});"


def direct_ram_address(addr: int) -> bool:
    """Return whether an address is side-effect-free direct guest RAM."""

    if addr < 0x100:
        return addr not in PAGE0_SPECIAL_READ | PAGE0_SPECIAL_WRITE
    return 0x2000 <= addr < 0x3000 and addr != 0x2028


def emit_add_sub16_peephole(
    instructions: list[InstructionIR], index: int,
) -> tuple[str, int] | None:
    """Fuse a carry-linked little-endian 16-bit add/subtract sequence.

    The fast form is valid only in binary mode; the generated block guard
    returns decimal-mode execution to the interpreter before consuming any
    instruction.  Exact cycle count, final A/status, and low-then-high store
    order are retained.
    """

    if index + 7 > len(instructions):
        return None
    window = instructions[index:index + 7]
    opcodes = tuple(item.data[0] for item in window)
    operation: str
    low_addr: int
    high_addr: int
    value: int

    if opcodes == (0x18, 0xAD, 0x69, 0x8D, 0xAD, 0x69, 0x8D):
        operation = "ADD16_IMM"
        low_addr = u16(window[1].data[1:3])
        high_addr = u16(window[4].data[1:3])
        value = window[2].data[1] | (window[5].data[1] << 8)
    elif opcodes == (0x18, 0xA9, 0x6D, 0x8D, 0xA9, 0x6D, 0x8D):
        operation = "ADD16_IMM"
        low_addr = u16(window[2].data[1:3])
        high_addr = u16(window[5].data[1:3])
        value = window[1].data[1] | (window[4].data[1] << 8)
    elif opcodes == (0x38, 0xAD, 0xE9, 0x8D, 0xAD, 0xE9, 0x8D):
        operation = "SUB16_IMM"
        low_addr = u16(window[1].data[1:3])
        high_addr = u16(window[4].data[1:3])
        value = window[2].data[1] | (window[5].data[1] << 8)
    else:
        return None

    low_store = u16(window[3].data[1:3])
    high_store = u16(window[6].data[1:3])
    if (
        low_store != low_addr or high_store != high_addr or
        high_addr != ((low_addr + 1) & 0xFFFF) or
        not direct_ram_address(low_addr) or
        not direct_ram_address(high_addr)
    ):
        return None
    flags = f"0x{int(window[5].live_writes):02x}u"
    return (
        f"S6502_AOT_{operation}(0x{low_addr:04x}u, 0x{value:04x}u, "
        f"{flags});",
        7,
    )


def emit_block_ir(
    instructions: list[InstructionIR],
) -> tuple[list[str], bool, int]:
    """Lower analyzed IR, applying safe block-local phrase fusion."""

    emitted: list[str] = []
    requires_binary = False
    fusion_count = 0
    index = 0
    while index < len(instructions):
        fusion = emit_add_sub16_peephole(instructions, index)
        if fusion is not None:
            line, consumed = fusion
            emitted.append(line)
            requires_binary = True
            fusion_count += 1
            index += consumed
            continue
        emitted.extend(emit_instruction(instructions[index])[0])
        index += 1
    return emitted, requires_binary, fusion_count


def emit_instruction(ir: InstructionIR) -> tuple[list[str], bool]:
    pc = ir.pc
    instruction = ir.data
    opcode = instruction[0]
    byte = instruction[1] if len(instruction) > 1 else 0
    word = u16(instruction[1:3]) if len(instruction) > 2 else 0
    flags = f"0x{int(ir.live_writes):02x}u"
    line: str

    if opcode == 0x05:
        line = f"S6502_AOT_ORA({read_expr(byte)}, 3, {flags});"
    elif opcode == 0x06:
        line = f"S6502_AOT_ASL_ZP(0x{byte:02x}u, {flags});"
    elif opcode == 0x08:
        line = "S6502_AOT_PHP();"
    elif opcode == 0x09:
        line = f"S6502_AOT_ORA(0x{byte:02x}u, 2, {flags});"
    elif opcode == 0x0A:
        line = f"S6502_AOT_ASL_A({flags});"
    elif opcode == 0x0D:
        line = f"S6502_AOT_ORA({read_expr(word)}, 4, {flags});"
    elif opcode == 0x0E:
        line = rmw_line("ASL_M", word, flags)
    elif opcode == 0x10:
        target = branch_target(pc, byte)
        line = (
            f"S6502_AOT_BRANCH(!NEGATIVE_p, 0x{(pc + 2) & 0xffff:04x}u, "
            f"0x{target:04x}u);"
        )
    elif opcode == 0x11:
        line = (
            f"S6502_AOT_ORA_INDY({indirect_base_expr(byte)}, {flags});"
        )
    elif opcode == 0x18:
        line = f"S6502_AOT_CLC({flags});"
    elif opcode == 0x1E:
        line = f"S6502_AOT_ASL_ABSX(0x{word:04x}u, {flags});"
    elif opcode == 0x1D:
        line = f"S6502_AOT_ORA_ABSX(0x{word:04x}u, {flags});"
    elif opcode == 0x20:
        line = f"S6502_AOT_JSR(0x{(pc + 2) & 0xffff:04x}u, 0x{word:04x}u);"
    elif opcode == 0x25:
        line = f"S6502_AOT_AND({read_expr(byte)}, 3, {flags});"
    elif opcode == 0x26:
        line = f"S6502_AOT_ROL_ZP(0x{byte:02x}u, {flags});"
    elif opcode == 0x28:
        line = "S6502_AOT_PLP();"
    elif opcode == 0x29:
        line = f"S6502_AOT_AND(0x{byte:02x}u, 2, {flags});"
    elif opcode == 0x2A:
        line = f"S6502_AOT_ROL_A({flags});"
    elif opcode == 0x2D:
        line = f"S6502_AOT_AND({read_expr(word)}, 4, {flags});"
    elif opcode == 0x2E:
        line = rmw_line("ROL_M", word, flags)
    elif opcode == 0x30:
        target = branch_target(pc, byte)
        line = (
            f"S6502_AOT_BRANCH(NEGATIVE_p, 0x{(pc + 2) & 0xffff:04x}u, "
            f"0x{target:04x}u);"
        )
    elif opcode == 0x31:
        line = f"S6502_AOT_AND_INDY({indirect_base_expr(byte)}, {flags});"
    elif opcode == 0x38:
        line = f"S6502_AOT_SEC({flags});"
    elif opcode == 0x3E:
        line = f"S6502_AOT_ROL_ABSX(0x{word:04x}u, {flags});"
    elif opcode == 0x40:
        line = "S6502_AOT_RTI();"
    elif opcode == 0x45:
        line = f"S6502_AOT_EOR({read_expr(byte)}, 3, {flags});"
    elif opcode == 0x46:
        line = f"S6502_AOT_LSR_ZP(0x{byte:02x}u, {flags});"
    elif opcode == 0x48:
        line = "S6502_AOT_PHA();"
    elif opcode == 0x49:
        line = f"S6502_AOT_EOR(0x{byte:02x}u, 2, {flags});"
    elif opcode == 0x51:
        line = (
            f"S6502_AOT_EOR_INDY({indirect_base_expr(byte)}, {flags});"
        )
    elif opcode == 0x4A:
        line = f"S6502_AOT_LSR_A({flags});"
    elif opcode == 0x4E:
        line = rmw_line("LSR_M", word, flags)
    elif opcode == 0x4C:
        line = f"S6502_AOT_JMP(0x{word:04x}u);"
    elif opcode == 0x50:
        target = branch_target(pc, byte)
        line = (
            f"S6502_AOT_BRANCH(!OVERFLOW_p, 0x{(pc + 2) & 0xffff:04x}u, "
            f"0x{target:04x}u);"
        )
    elif opcode == 0x60:
        line = "S6502_AOT_RTS();"
    elif opcode == 0x65:
        line = f"S6502_AOT_ADC({read_expr(byte)}, 3, {flags});"
    elif opcode == 0x66:
        line = f"S6502_AOT_ROR_ZP(0x{byte:02x}u, {flags});"
    elif opcode == 0x68:
        line = f"S6502_AOT_PLA({flags});"
    elif opcode == 0x69:
        line = f"S6502_AOT_ADC(0x{byte:02x}u, 2, {flags});"
    elif opcode == 0x6A:
        line = f"S6502_AOT_ROR_A({flags});"
    elif opcode == 0x6C:
        line = f"S6502_AOT_JMP_INDIRECT(0x{word:04x}u);"
    elif opcode == 0x6D:
        line = f"S6502_AOT_ADC({read_expr(word)}, 4, {flags});"
    elif opcode == 0x6E:
        line = rmw_line("ROR_M", word, flags)
    elif opcode == 0x70:
        target = branch_target(pc, byte)
        line = (
            f"S6502_AOT_BRANCH(OVERFLOW_p, 0x{(pc + 2) & 0xffff:04x}u, "
            f"0x{target:04x}u);"
        )
    elif opcode == 0x71:
        line = f"S6502_AOT_ADC_INDY({indirect_base_expr(byte)}, {flags});"
    elif opcode == 0x78:
        line = f"S6502_AOT_SEI({flags});"
    elif opcode == 0x84:
        line = f"S6502_AOT_STY(0x{byte:02x}u, 3);"
    elif opcode == 0x85:
        line = store_line("A", byte, 3)
    elif opcode == 0x86:
        line = store_line("X", byte, 3)
    elif opcode == 0x88:
        line = f"S6502_AOT_DEY({flags});"
    elif opcode == 0x8A:
        line = f"S6502_AOT_TXA({flags});"
    elif opcode == 0x8C:
        line = f"S6502_AOT_STY(0x{word:04x}u, 4);"
    elif opcode == 0x8D:
        line = store_line("A", word, 4)
    elif opcode == 0x8E:
        line = store_line("X", word, 4)
    elif opcode == 0x90:
        target = branch_target(pc, byte)
        line = (
            f"S6502_AOT_BRANCH(!CARRY_p, 0x{(pc + 2) & 0xffff:04x}u, "
            f"0x{target:04x}u);"
        )
    elif opcode == 0x91:
        line = f"S6502_AOT_STA_INDY({indirect_base_expr(byte)});"
    elif opcode == 0x98:
        line = f"S6502_AOT_TYA({flags});"
    elif opcode == 0x99:
        line = f"S6502_AOT_STA_ABSY(0x{word:04x}u);"
    elif opcode == 0x9A:
        line = "S6502_AOT_TXS();"
    elif opcode == 0x9D:
        line = f"S6502_AOT_STA_ABSX(0x{word:04x}u);"
    elif opcode == 0xA0:
        line = f"S6502_AOT_LDY(0x{byte:02x}u, 2, {flags});"
    elif opcode == 0xA2:
        line = f"S6502_AOT_LDX(0x{byte:02x}u, 2, {flags});"
    elif opcode == 0xA4:
        line = f"S6502_AOT_LDY({read_expr(byte)}, 3, {flags});"
    elif opcode == 0xA5:
        line = f"S6502_AOT_LDA({read_expr(byte)}, 3, {flags});"
    elif opcode == 0xA6:
        line = f"S6502_AOT_LDX({read_expr(byte)}, 3, {flags});"
    elif opcode == 0xA8:
        line = f"S6502_AOT_TAY({flags});"
    elif opcode == 0xA9:
        line = f"S6502_AOT_LDA(0x{byte:02x}u, 2, {flags});"
    elif opcode == 0xAA:
        line = f"S6502_AOT_TAX({flags});"
    elif opcode == 0xAC:
        line = f"S6502_AOT_LDY({read_expr(word)}, 4, {flags});"
    elif opcode == 0xAD:
        line = f"S6502_AOT_LDA({read_expr(word)}, 4, {flags});"
    elif opcode == 0xAE:
        line = f"S6502_AOT_LDX({read_expr(word)}, 4, {flags});"
    elif opcode == 0xB1:
        line = f"S6502_AOT_LDA_INDY({indirect_base_expr(byte)}, {flags});"
    elif opcode == 0xBA:
        line = f"S6502_AOT_TSX({flags});"
    elif opcode == 0xBD:
        line = f"S6502_AOT_LDA_ABSX(0x{word:04x}u, {flags});"
    elif opcode == 0xB0:
        target = branch_target(pc, byte)
        line = (
            f"S6502_AOT_BRANCH(CARRY_p, 0x{(pc + 2) & 0xffff:04x}u, "
            f"0x{target:04x}u);"
        )
    elif opcode == 0xC0:
        line = f"S6502_AOT_COMPARE(iy, 0x{byte:02x}u, 2, {flags});"
    elif opcode == 0xC5:
        line = f"S6502_AOT_COMPARE(ac, {read_expr(byte)}, 3, {flags});"
    elif opcode == 0xC6:
        line = rmw_line("DEC", byte, flags, 5)
    elif opcode == 0xC8:
        line = f"S6502_AOT_INY({flags});"
    elif opcode == 0xC9:
        line = f"S6502_AOT_COMPARE(ac, 0x{byte:02x}u, 2, {flags});"
    elif opcode == 0xCA:
        line = f"S6502_AOT_DEX({flags});"
    elif opcode == 0xCD:
        line = f"S6502_AOT_COMPARE(ac, {read_expr(word)}, 4, {flags});"
    elif opcode == 0xCE:
        line = rmw_line("DEC", word, flags, 6)
    elif opcode == 0xD0:
        target = branch_target(pc, byte)
        line = (
            f"S6502_AOT_BRANCH(!ZERO_p, 0x{(pc + 2) & 0xffff:04x}u, "
            f"0x{target:04x}u);"
        )
    elif opcode == 0xDE:
        line = f"S6502_AOT_DEC_ABSX(0x{word:04x}u, {flags});"
    elif opcode == 0xE0:
        line = f"S6502_AOT_COMPARE(ix, 0x{byte:02x}u, 2, {flags});"
    elif opcode == 0xE5:
        line = f"S6502_AOT_SBC({read_expr(byte)}, 3, {flags});"
    elif opcode == 0xE6:
        line = f"S6502_AOT_INC(0x{byte:02x}u, 5, {flags});"
    elif opcode == 0xE8:
        line = f"S6502_AOT_INX({flags});"
    elif opcode == 0xE9:
        line = f"S6502_AOT_SBC(0x{byte:02x}u, 2, {flags});"
    elif opcode == 0xEA:
        line = "S6502_AOT_NOP();"
    elif opcode == 0xEC:
        line = f"S6502_AOT_COMPARE(ix, {read_expr(word)}, 4, {flags});"
    elif opcode == 0xED:
        line = f"S6502_AOT_SBC({read_expr(word)}, 4, {flags});"
    elif opcode == 0xEE:
        line = rmw_line("INC", word, flags, 6)
    elif opcode == 0xF0:
        target = branch_target(pc, byte)
        line = (
            f"S6502_AOT_BRANCH(ZERO_p, 0x{(pc + 2) & 0xffff:04x}u, "
            f"0x{target:04x}u);"
        )
    elif opcode == 0xF1:
        line = f"S6502_AOT_SBC_INDY({indirect_base_expr(byte)}, {flags});"
    else:
        raise ValueError(f"unsupported opcode 0x{opcode:02x} at 0x{pc:04x}")
    return [line], opcode in TERMINATORS


def decode_block(rom: bytes, physical_pc: int, virtual_pc: int) -> dict[str, object]:
    offset = physical_pc - 0xE00000
    cursor = offset
    pc = virtual_pc
    ir: list[InstructionIR] = []
    instruction_count = 0
    may_change_mapping = False

    if offset < 0 or offset >= len(rom):
        raise ValueError(f"physical PC outside E.BIN: 0x{physical_pc:06x}")
    while True:
        opcode = rom[cursor]
        length = OPCODE_LENGTHS[opcode]
        instruction = rom[cursor : cursor + length]
        if len(instruction) != length:
            raise ValueError(f"truncated instruction at physical 0x{cursor + 0xe00000:06x}")
        reads, writes = flag_effects(opcode)
        ir.append(InstructionIR(pc, instruction, reads, writes))
        terminates = opcode in TERMINATORS
        if opcode == 0x91:
            may_change_mapping = True
        elif opcode in {
            0x85, 0x8D, 0x8E, 0x0E, 0x2E, 0x4E, 0x6E, 0xCE, 0xE6, 0xEE
        }:
            address = instruction[1] if length == 2 else u16(instruction[1:3])
            if address in {0x0D, 0x0E}:
                may_change_mapping = True
        instruction_count += 1
        cursor += length
        pc = (pc + length) & 0xFFFF
        if terminates:
            break
        if instruction_count > 512:
            raise ValueError(f"unterminated block at virtual 0x{virtual_pc:04x}")
    analyze_flag_liveness(ir)
    emitted, requires_binary, fusion_count = emit_block_ir(ir)
    return {
        "physical_pc": physical_pc,
        "virtual_pc": virtual_pc,
        "signature": rom[offset:cursor],
        "instructions": emitted,
        "ir": ir,
        "instruction_count": instruction_count,
        "requires_binary": requires_binary,
        "fusion_count": fusion_count,
        "requires_bank2": any(
            "_RAM(" in line or "RAM_READ(" in line
            for line in emitted
        ),
        "may_change_mapping": may_change_mapping,
    }


MACROS = r"""
#define S6502_AOT_CHAIN(id, label) do {                                      \
    if ((executed >= cycles) || sys_halt_p()) goto _aot_return;              \
    if (s6502_aot_match(id)) goto label;                                     \
    goto _next;                                                              \
} while (0)
#define S6502_AOT_CHAIN_FAST(id, label) do {                                 \
    if ((executed >= cycles) || sys_halt_p()) goto _aot_return;              \
    if (s6502_aot_validation[id] == 1u) goto label;                          \
    if (s6502_aot_match(id)) goto label;                                     \
    goto _next;                                                              \
} while (0)
#define S6502_AOT_TOKEN(id) do {                                             \
    aot_next_token = (uint16_t)((id) + 1u);                                 \
    goto _exit;                                                              \
} while (0)
#define S6502_AOT_ZP_READ(addr) S6502_FAST_STACK_RAM[(uint8_t)(addr)]
#define S6502_AOT_RAM_READ(addr) S6502_FAST_STACK_RAM[(uint16_t)(addr)]
#define S6502_AOT_PAGE3_READ(addr) s6502_page3[(uint8_t)(addr)]
#define S6502_AOT_ZP16(addr) (                                              \
    (uint16_t)S6502_FAST_STACK_RAM[(uint8_t)(addr)] |                        \
    ((uint16_t)S6502_FAST_STACK_RAM[(uint8_t)((addr) + 1u)] << 8)            \
)
#define S6502_AOT_SET_NZ_MASK(value, flags) do {                            \
    if ((flags) & (FLAG_N | FLAG_Z)) { SET_NZ(value); }                     \
} while (0)
#define S6502_AOT_SET_C_MASK(value, flags) do {                             \
    if ((flags) & FLAG_C) { SET_C(value); }                                 \
} while (0)
#define S6502_AOT_SET_V_MASK(value, flags) do {                             \
    if ((flags) & FLAG_V) { SET_V(value); }                                 \
} while (0)
#define S6502_AOT_SET_I_MASK(value, flags) do {                             \
    if ((flags) & FLAG_I) { SET_I(value); }                                 \
} while (0)
#define S6502_AOT_ADD16_IMM(addr, value, flags) do {                        \
    uint8_t aot_low = S6502_FAST_STACK_RAM[(uint16_t)(addr)];               \
    uint8_t aot_high = S6502_FAST_STACK_RAM[(uint16_t)((addr) + 1u)];       \
    uint8_t aot_value_low = (uint8_t)(value);                               \
    uint8_t aot_value_high = (uint8_t)((uint16_t)(value) >> 8);             \
    uint16_t aot_low_sum = (uint16_t)(aot_low + aot_value_low);             \
    uint16_t aot_high_sum = (uint16_t)(                                    \
        aot_high + aot_value_high + (aot_low_sum > 0xffu)                  \
    );                                                                      \
    uint8_t aot_result_low = (uint8_t)aot_low_sum;                          \
    uint8_t aot_result_high = (uint8_t)aot_high_sum;                        \
    S6502_FAST_STACK_RAM[(uint16_t)(addr)] = aot_result_low;                \
    S6502_FAST_STACK_RAM[(uint16_t)((addr) + 1u)] = aot_result_high;        \
    S6502_AOT_SET_C_MASK((aot_high_sum > 0xffu), flags);                    \
    S6502_AOT_SET_V_MASK(                                                   \
        ((aot_high ^ aot_high_sum) &                                       \
            (aot_value_high ^ aot_high_sum) & 0x80u), flags                \
    );                                                                      \
    ac = aot_result_high; S6502_AOT_SET_NZ_MASK(ac, flags); CYCLES(22);     \
} while (0)
#define S6502_AOT_SUB16_IMM(addr, value, flags) do {                        \
    uint8_t aot_low = S6502_FAST_STACK_RAM[(uint16_t)(addr)];               \
    uint8_t aot_high = S6502_FAST_STACK_RAM[(uint16_t)((addr) + 1u)];       \
    uint8_t aot_value_low = (uint8_t)(value);                               \
    uint8_t aot_value_high = (uint8_t)((uint16_t)(value) >> 8);             \
    uint16_t aot_low_sum = (uint16_t)(                                     \
        aot_low + (uint8_t)~aot_value_low + 1u                             \
    );                                                                      \
    uint16_t aot_high_sum = (uint16_t)(                                    \
        aot_high + (uint8_t)~aot_value_high + (aot_low_sum > 0xffu)        \
    );                                                                      \
    uint8_t aot_result_low = (uint8_t)aot_low_sum;                          \
    uint8_t aot_result_high = (uint8_t)aot_high_sum;                        \
    S6502_FAST_STACK_RAM[(uint16_t)(addr)] = aot_result_low;                \
    S6502_FAST_STACK_RAM[(uint16_t)((addr) + 1u)] = aot_result_high;        \
    S6502_AOT_SET_C_MASK((aot_high_sum > 0xffu), flags);                    \
    S6502_AOT_SET_V_MASK(                                                   \
        ((aot_high ^ aot_high_sum) &                                       \
            ((uint8_t)~aot_value_high ^ aot_high_sum) & 0x80u), flags      \
    );                                                                      \
    ac = aot_result_high; S6502_AOT_SET_NZ_MASK(ac, flags); CYCLES(22);     \
} while (0)
#define S6502_AOT_LDA(value, cost, flags) do {                               \
    ac = (uint8_t)(value); S6502_AOT_SET_NZ_MASK(ac, flags);                \
    CYCLES(cost);                                           \
} while (0)
#define S6502_AOT_LDY(value, cost, flags) do {                               \
    iy = (uint8_t)(value); S6502_AOT_SET_NZ_MASK(iy, flags);                \
    CYCLES(cost);                                           \
} while (0)
#define S6502_AOT_LDX(value, cost, flags) do {                               \
    ix = (uint8_t)(value); S6502_AOT_SET_NZ_MASK(ix, flags);                \
    CYCLES(cost);                                           \
} while (0)
#define S6502_AOT_STA(addr, cost) do {                                       \
    WRITE8((uint16_t)(addr), ac); CYCLES(cost);             \
} while (0)
#define S6502_AOT_STX(addr, cost) do {                                       \
    WRITE8((uint16_t)(addr), ix); CYCLES(cost);             \
} while (0)
#define S6502_AOT_STY(addr, cost) do {                                       \
    WRITE8((uint16_t)(addr), iy); CYCLES(cost);             \
} while (0)
#define S6502_AOT_STA_ZP(addr, cost) do {                                    \
    S6502_FAST_STACK_RAM[(uint8_t)(addr)] = ac;                             \
    CYCLES(cost);                                           \
} while (0)
#define S6502_AOT_STX_ZP(addr, cost) do {                                    \
    S6502_FAST_STACK_RAM[(uint8_t)(addr)] = ix;                             \
    CYCLES(cost);                                           \
} while (0)
#define S6502_AOT_STA_RAM(addr, cost) do {                                   \
    S6502_FAST_STACK_RAM[(uint16_t)(addr)] = ac;                            \
    CYCLES(cost);                                           \
} while (0)
#define S6502_AOT_STX_RAM(addr, cost) do {                                   \
    S6502_FAST_STACK_RAM[(uint16_t)(addr)] = ix;                            \
    CYCLES(cost);                                           \
} while (0)
#define S6502_AOT_LDA_INDY(base, flags) do {                                 \
    et = (uint16_t)(base); ea = (uint16_t)(et + iy);                         \
    CYCLES((!!(0xff00 & (et ^ ea))));                                       \
    ac = READ8(ea); S6502_AOT_SET_NZ_MASK(ac, flags);                       \
    CYCLES(5);                                              \
} while (0)
#define S6502_AOT_LDA_ABSX(base, flags) do {                                 \
    et = (uint16_t)(base); ea = (uint16_t)(et + ix);                         \
    CYCLES((!!(0xff00 & (et ^ ea))));                                       \
    ac = READ8(ea); S6502_AOT_SET_NZ_MASK(ac, flags);                       \
    CYCLES(4);                                              \
} while (0)
#define S6502_AOT_STA_INDY(base) do {                                        \
    et = (uint16_t)(base); ea = (uint16_t)(et + iy);                         \
    WRITE8(ea, ac); CYCLES(6);                              \
} while (0)
#define S6502_AOT_STA_ABSX(base) do {                                        \
    ea = (uint16_t)((uint16_t)(base) + ix); WRITE8(ea, ac);                 \
    CYCLES(5);                                              \
} while (0)
#define S6502_AOT_STA_ABSY(base) do {                                        \
    ea = (uint16_t)((uint16_t)(base) + iy); WRITE8(ea, ac);                 \
    CYCLES(5);                                              \
} while (0)
#define S6502_AOT_AND(value, cost, flags) do {                               \
    ac = (uint8_t)(ac & (uint8_t)(value));                                  \
    S6502_AOT_SET_NZ_MASK(ac, flags); CYCLES(cost);         \
} while (0)
#define S6502_AOT_AND_INDY(base, flags) do {                                 \
    et = (uint16_t)(base); ea = (uint16_t)(et + iy);                         \
    CYCLES((!!(0xff00 & (et ^ ea))));                                       \
    ac = (uint8_t)(ac & READ8(ea));                                         \
    S6502_AOT_SET_NZ_MASK(ac, flags); CYCLES(5);            \
} while (0)
#define S6502_AOT_ORA(value, cost, flags) do {                               \
    ac = (uint8_t)(ac | (uint8_t)(value));                                  \
    S6502_AOT_SET_NZ_MASK(ac, flags); CYCLES(cost);         \
} while (0)
#define S6502_AOT_ORA_INDY(base, flags) do {                                 \
    et = (uint16_t)(base); ea = (uint16_t)(et + iy);                         \
    CYCLES((!!(0xff00 & (et ^ ea))));                                       \
    ac = (uint8_t)(ac | READ8(ea));                                         \
    S6502_AOT_SET_NZ_MASK(ac, flags); CYCLES(5);            \
} while (0)
#define S6502_AOT_ORA_ABSX(base, flags) do {                                 \
    et = (uint16_t)(base); ea = (uint16_t)(et + ix);                         \
    CYCLES((!!(0xff00 & (et ^ ea))));                                       \
    ac = (uint8_t)(ac | READ8(ea));                                         \
    S6502_AOT_SET_NZ_MASK(ac, flags); CYCLES(4);            \
} while (0)
#define S6502_AOT_EOR(value, cost, flags) do {                               \
    ac = (uint8_t)(ac ^ (uint8_t)(value));                                  \
    S6502_AOT_SET_NZ_MASK(ac, flags); CYCLES(cost);         \
} while (0)
#define S6502_AOT_EOR_INDY(base, flags) do {                                 \
    et = (uint16_t)(base); ea = (uint16_t)(et + iy);                         \
    CYCLES((!!(0xff00 & (et ^ ea))));                                       \
    ac = (uint8_t)(ac ^ READ8(ea));                                         \
    S6502_AOT_SET_NZ_MASK(ac, flags); CYCLES(5);            \
} while (0)
#define S6502_AOT_COMPARE(reg, value, cost, flags) do {                      \
    dt = (uint8_t)~(uint8_t)(value); et = (uint16_t)((reg) + dt + 1u);       \
    S6502_AOT_SET_C_MASK((et > 0xff), flags);                               \
    S6502_AOT_SET_NZ_MASK((uint8_t)et, flags);                              \
    CYCLES(cost);                                           \
} while (0)
#define S6502_AOT_CLC(flags) do { S6502_AOT_SET_C_MASK(0, flags); CYCLES(2); } while (0)
#define S6502_AOT_SEC(flags) do { S6502_AOT_SET_C_MASK(1, flags); CYCLES(2); } while (0)
#define S6502_AOT_SEI(flags) do { S6502_AOT_SET_I_MASK(1, flags); CYCLES(2); } while (0)
#define S6502_AOT_TAX(flags) do { ix = ac; S6502_AOT_SET_NZ_MASK(ix, flags); CYCLES(2); } while (0)
#define S6502_AOT_TAY(flags) do { iy = ac; S6502_AOT_SET_NZ_MASK(iy, flags); CYCLES(2); } while (0)
#define S6502_AOT_TXA(flags) do { ac = ix; S6502_AOT_SET_NZ_MASK(ac, flags); CYCLES(2); } while (0)
#define S6502_AOT_TYA(flags) do { ac = iy; S6502_AOT_SET_NZ_MASK(ac, flags); CYCLES(2); } while (0)
#define S6502_AOT_TSX(flags) do { ix = sp; S6502_AOT_SET_NZ_MASK(ix, flags); CYCLES(2); } while (0)
#define S6502_AOT_TXS() do { sp = ix; CYCLES(2); } while (0)
#define S6502_AOT_INY(flags) do { iy += 1; S6502_AOT_SET_NZ_MASK(iy, flags); CYCLES(2); } while (0)
#define S6502_AOT_INX(flags) do { ix += 1; S6502_AOT_SET_NZ_MASK(ix, flags); CYCLES(2); } while (0)
#define S6502_AOT_DEX(flags) do { ix -= 1; S6502_AOT_SET_NZ_MASK(ix, flags); CYCLES(2); } while (0)
#define S6502_AOT_DEY(flags) do { iy -= 1; S6502_AOT_SET_NZ_MASK(iy, flags); CYCLES(2); } while (0)
#define S6502_AOT_PHP() do { PUSH((status | FLAG_B | FLAG_U)); CYCLES(3); } while (0)
#define S6502_AOT_PHA() do { PUSH(ac); CYCLES(3); } while (0)
#define S6502_AOT_PLA(flags) do { ac = POP(); S6502_AOT_SET_NZ_MASK(ac, flags); CYCLES(4); } while (0)
#define S6502_AOT_PLP() do {                                                 \
    status = (uint8_t)(POP() | FLAG_U | FLAG_B);                            \
    CYCLES(4);                                              \
} while (0)
#define S6502_AOT_ASL_A(flags) do {                                          \
    S6502_AOT_SET_C_MASK((0x80 & ac), flags); ac = (uint8_t)(ac << 1);       \
    S6502_AOT_SET_NZ_MASK(ac, flags); CYCLES(2);            \
} while (0)
#define S6502_AOT_LSR_A(flags) do {                                          \
    S6502_AOT_SET_C_MASK((0x01 & ac), flags); ac = (uint8_t)(ac >> 1);       \
    S6502_AOT_SET_NZ_MASK(ac, flags); CYCLES(2);            \
} while (0)
#define S6502_AOT_ROL_A(flags) do {                                          \
    dt = (uint8_t)(ac & 0x80); ac = (uint8_t)(CARRY | (ac << 1));            \
    S6502_AOT_SET_C_MASK(dt, flags); S6502_AOT_SET_NZ_MASK(ac, flags);       \
    CYCLES(2);                                              \
} while (0)
#define S6502_AOT_ROR_A(flags) do {                                          \
    dt = (uint8_t)(ac & 0x01);                                              \
    ac = (uint8_t)((0x80 * CARRY) | (ac >> 1));                            \
    S6502_AOT_SET_C_MASK(dt, flags); S6502_AOT_SET_NZ_MASK(ac, flags);       \
    CYCLES(2);                                              \
} while (0)
#define S6502_AOT_ASL_M(addr, flags) do {                                    \
    dt = READ8((uint16_t)(addr)); S6502_AOT_SET_C_MASK((0x80 & dt), flags); \
    dt = (uint8_t)(dt << 1); S6502_AOT_SET_NZ_MASK(dt, flags);              \
    WRITE8((uint16_t)(addr), dt);                                            \
    CYCLES(6);                                              \
} while (0)
#define S6502_AOT_ASL_ZP(addr, flags) do {                                   \
    dt = READ8((uint8_t)(addr)); S6502_AOT_SET_C_MASK((0x80 & dt), flags);  \
    dt = (uint8_t)(dt << 1); S6502_AOT_SET_NZ_MASK(dt, flags);              \
    WRITE8((uint8_t)(addr), dt);                                             \
    CYCLES(5);                                              \
} while (0)
#define S6502_AOT_ASL_ABSX(base, flags) do {                                 \
    et = (uint16_t)(base); ea = (uint16_t)(et + ix);                         \
    CYCLES((!!(0xff00 & (et ^ ea))));                                       \
    dt = READ8(ea); S6502_AOT_SET_C_MASK((0x80 & dt), flags);               \
    dt = (uint8_t)(dt << 1); S6502_AOT_SET_NZ_MASK(dt, flags);              \
    WRITE8(ea, dt); CYCLES(6);                              \
} while (0)
#define S6502_AOT_ASL_M_RAM(addr, flags) do {                                \
    dt = S6502_AOT_RAM_READ(addr);                                           \
    S6502_AOT_SET_C_MASK((0x80 & dt), flags);                               \
    dt = (uint8_t)(dt << 1); S6502_AOT_SET_NZ_MASK(dt, flags);              \
    S6502_FAST_STACK_RAM[(uint16_t)(addr)] = dt;                            \
    CYCLES(6);                                              \
} while (0)
#define S6502_AOT_LSR_M(addr, flags) do {                                    \
    dt = READ8((uint16_t)(addr)); S6502_AOT_SET_C_MASK((0x01 & dt), flags); \
    dt = (uint8_t)(dt >> 1); S6502_AOT_SET_NZ_MASK(dt, flags);              \
    WRITE8((uint16_t)(addr), dt);                                            \
    CYCLES(6);                                              \
} while (0)
#define S6502_AOT_LSR_ZP(addr, flags) do {                                   \
    dt = READ8((uint8_t)(addr)); S6502_AOT_SET_C_MASK((0x01 & dt), flags);  \
    dt = (uint8_t)(dt >> 1); S6502_AOT_SET_NZ_MASK(dt, flags);              \
    WRITE8((uint8_t)(addr), dt);                                             \
    CYCLES(5);                                              \
} while (0)
#define S6502_AOT_LSR_M_RAM(addr, flags) do {                                \
    dt = S6502_AOT_RAM_READ(addr);                                           \
    S6502_AOT_SET_C_MASK((0x01 & dt), flags);                               \
    dt = (uint8_t)(dt >> 1); S6502_AOT_SET_NZ_MASK(dt, flags);              \
    S6502_FAST_STACK_RAM[(uint16_t)(addr)] = dt;                            \
    CYCLES(6);                                              \
} while (0)
#define S6502_AOT_ROL_M(addr, flags) do {                                    \
    dt = READ8((uint16_t)(addr)); et = (uint16_t)(dt & 0x80);               \
    dt = (uint8_t)(CARRY | (dt << 1));                                      \
    S6502_AOT_SET_C_MASK(et, flags); S6502_AOT_SET_NZ_MASK(dt, flags);      \
    WRITE8((uint16_t)(addr), dt); CYCLES(6);                \
} while (0)
#define S6502_AOT_ROL_ZP(addr, flags) do {                                   \
    dt = READ8((uint8_t)(addr)); et = (uint16_t)(dt & 0x80);                \
    dt = (uint8_t)(CARRY | (dt << 1));                                      \
    S6502_AOT_SET_C_MASK(et, flags); S6502_AOT_SET_NZ_MASK(dt, flags);      \
    WRITE8((uint8_t)(addr), dt); CYCLES(5);                 \
} while (0)
#define S6502_AOT_ROL_ABSX(base, flags) do {                                 \
    et = (uint16_t)(base); ea = (uint16_t)(et + ix);                         \
    CYCLES((!!(0xff00 & (et ^ ea))));                                       \
    dt = READ8(ea); et = (uint16_t)(dt & 0x80);                             \
    dt = (uint8_t)(CARRY | (dt << 1));                                      \
    S6502_AOT_SET_C_MASK(et, flags); S6502_AOT_SET_NZ_MASK(dt, flags);      \
    WRITE8(ea, dt); CYCLES(6);                              \
} while (0)
#define S6502_AOT_ROL_M_RAM(addr, flags) do {                                \
    dt = S6502_AOT_RAM_READ(addr); et = (uint16_t)(dt & 0x80);              \
    dt = (uint8_t)(CARRY | (dt << 1));                                      \
    S6502_AOT_SET_C_MASK(et, flags); S6502_AOT_SET_NZ_MASK(dt, flags);      \
    S6502_FAST_STACK_RAM[(uint16_t)(addr)] = dt;                            \
    CYCLES(6);                                              \
} while (0)
#define S6502_AOT_ROR_M(addr, flags) do {                                    \
    dt = READ8((uint16_t)(addr)); et = (uint16_t)(dt & 0x01);               \
    dt = (uint8_t)((0x80 * CARRY) | (dt >> 1));                             \
    S6502_AOT_SET_C_MASK(et, flags); S6502_AOT_SET_NZ_MASK(dt, flags);      \
    WRITE8((uint16_t)(addr), dt); CYCLES(6);                \
} while (0)
#define S6502_AOT_ROR_ZP(addr, flags) do {                                   \
    dt = READ8((uint8_t)(addr)); et = (uint16_t)(dt & 0x01);                \
    dt = (uint8_t)((0x80 * CARRY) | (dt >> 1));                             \
    S6502_AOT_SET_C_MASK(et, flags); S6502_AOT_SET_NZ_MASK(dt, flags);      \
    WRITE8((uint8_t)(addr), dt); CYCLES(5);                 \
} while (0)
#define S6502_AOT_ROR_M_RAM(addr, flags) do {                                \
    dt = S6502_AOT_RAM_READ(addr); et = (uint16_t)(dt & 0x01);              \
    dt = (uint8_t)((0x80 * CARRY) | (dt >> 1));                             \
    S6502_AOT_SET_C_MASK(et, flags); S6502_AOT_SET_NZ_MASK(dt, flags);      \
    S6502_FAST_STACK_RAM[(uint16_t)(addr)] = dt;                            \
    CYCLES(6);                                              \
} while (0)
#define S6502_AOT_INC(addr, cost, flags) do {                                \
    dt = (uint8_t)(READ8((uint16_t)(addr)) + 1);                            \
    S6502_AOT_SET_NZ_MASK(dt, flags);                                        \
    WRITE8((uint16_t)(addr), dt); CYCLES(cost);             \
} while (0)
#define S6502_AOT_INC_RAM(addr, cost, flags) do {                            \
    dt = (uint8_t)(S6502_AOT_RAM_READ(addr) + 1);                           \
    S6502_AOT_SET_NZ_MASK(dt, flags);                                        \
    S6502_FAST_STACK_RAM[(uint16_t)(addr)] = dt;                            \
    CYCLES(cost);                                           \
} while (0)
#define S6502_AOT_DEC(addr, cost, flags) do {                                \
    dt = (uint8_t)(READ8((uint16_t)(addr)) - 1);                            \
    S6502_AOT_SET_NZ_MASK(dt, flags);                                        \
    WRITE8((uint16_t)(addr), dt); CYCLES(cost);             \
} while (0)
#define S6502_AOT_DEC_RAM(addr, cost, flags) do {                            \
    dt = (uint8_t)(S6502_AOT_RAM_READ(addr) - 1);                           \
    S6502_AOT_SET_NZ_MASK(dt, flags);                                        \
    S6502_FAST_STACK_RAM[(uint16_t)(addr)] = dt;                            \
    CYCLES(cost);                                           \
} while (0)
#define S6502_AOT_DEC_ABSX(base, flags) do {                                 \
    ea = (uint16_t)((uint16_t)(base) + ix);                                  \
    dt = (uint8_t)(READ8(ea) - 1); S6502_AOT_SET_NZ_MASK(dt, flags);        \
    WRITE8(ea, dt); CYCLES(7);                              \
} while (0)
#define S6502_AOT_ADC(value, cost, flags) do {                               \
    dt = (uint8_t)(value); CYCLES(cost);                    \
    if (DECIMAL_p) {                                                         \
        uint8_t vu = (uint8_t)(dt & 0x0f);                                   \
        uint8_t vt = (uint8_t)((dt & 0xf0) >> 4);                            \
        uint8_t au = (uint8_t)(ac & 0x0f);                                   \
        uint8_t at = (uint8_t)((ac & 0xf0) >> 4);                            \
        uint8_t units = (uint8_t)(vu + au + CARRY);                          \
        uint8_t tens = (uint8_t)(vt + at);                                   \
        uint8_t tc = 0; CYCLES(1);                                           \
        if (units > 0x09) { tc = 1; tens += 1; units += 0x06; }              \
        if (tens > 0x09) tens += 0x06;                                       \
        if (at & 0x08) at |= 0xf0;                                           \
        if (vt & 0x08) vt |= 0xf0;                                           \
        { int8_t res = (int8_t)(at + vt + tc);                              \
          S6502_AOT_SET_V_MASK(((res < -8) || (res > 7)), flags); }          \
        ac = (uint8_t)((tens << 4) | (units & 0x0f));                       \
        S6502_AOT_SET_NZ_MASK(ac, flags);                                    \
        S6502_AOT_SET_C_MASK((tens & 0xf0), flags);                          \
    } else {                                                                 \
        et = (uint16_t)(ac + dt + CARRY);                                   \
        S6502_AOT_SET_C_MASK((et > 0xff), flags);                            \
        S6502_AOT_SET_V_MASK(((ac ^ et) & (dt ^ et) & 0x80), flags);        \
        ac = (uint8_t)et; S6502_AOT_SET_NZ_MASK(ac, flags);                 \
    }                                                                        \
} while (0)
#define S6502_AOT_ADC_INDY(base, flags) do {                                 \
    et = (uint16_t)(base); ea = (uint16_t)(et + iy);                         \
    CYCLES((!!(0xff00 & (et ^ ea))));                                       \
    S6502_AOT_ADC(READ8(ea), 5, flags);                                      \
} while (0)
#define S6502_AOT_SBC(value, cost, flags) do {                               \
    dt = (uint8_t)(value); CYCLES(cost);                    \
    if (DECIMAL_p) {                                                         \
        et = (uint16_t)(ac + ~dt + CARRY);                                   \
        ea = (uint16_t)(ac - dt - !CARRY); CYCLES(1);                        \
        if (ea & 0x8000) ea -= 0x60;                                         \
        if (((ac & 0x0f) - (dt & 0x0f) - !CARRY) & 0x8000) ea -= 0x06;      \
        S6502_AOT_SET_V_MASK(((ac ^ et) & (~dt ^ et) & 0x80), flags);       \
        S6502_AOT_SET_NZ_MASK((uint8_t)ea, flags);                           \
        S6502_AOT_SET_C_MASK(                                                \
            ((ea <= (uint16_t)ac) || ((ea & 0xff0) == 0xff0)), flags        \
        );                                                                   \
        ac = (uint8_t)ea;                                                    \
    } else {                                                                 \
        dt = (uint8_t)~dt; et = (uint16_t)(ac + dt + CARRY);                 \
        S6502_AOT_SET_C_MASK((et > 0xff), flags);                            \
        S6502_AOT_SET_V_MASK(((ac ^ et) & (dt ^ et) & 0x80), flags);        \
        ac = (uint8_t)et; S6502_AOT_SET_NZ_MASK(ac, flags);                 \
    }                                                                        \
} while (0)
#define S6502_AOT_SBC_INDY(base, flags) do {                                 \
    et = (uint16_t)(base); ea = (uint16_t)(et + iy);                         \
    CYCLES((!!(0xff00 & (et ^ ea))));                                       \
    S6502_AOT_SBC(READ8(ea), 5, flags);                                      \
} while (0)
#define S6502_AOT_BRANCH(condition, fallthrough, target) do {                \
    pc = (uint16_t)(fallthrough);                                            \
    if (condition) {                                                         \
        CYCLES(1); CYCLES((!!(0xff00 & (pc ^ (uint16_t)(target)))));         \
        pc = (uint16_t)(target);                                             \
    }                                                                        \
    CYCLES(2); goto _exit;                                  \
} while (0)
#define S6502_AOT_BRANCH_TARGET(chain, condition, fallthrough, target, id, label) do { \
    pc = (uint16_t)(fallthrough);                                            \
    if (condition) {                                                         \
        CYCLES(1); CYCLES((!!(0xff00 & (pc ^ (uint16_t)(target)))));         \
        pc = (uint16_t)(target); CYCLES(2);                 \
        chain(id, label);                                                    \
    }                                                                        \
    CYCLES(2); goto _exit;                                  \
} while (0)
#define S6502_AOT_BRANCH_FALL(chain, condition, fallthrough, target, id, label) do { \
    pc = (uint16_t)(fallthrough);                                            \
    if (condition) {                                                         \
        CYCLES(1); CYCLES((!!(0xff00 & (pc ^ (uint16_t)(target)))));         \
        pc = (uint16_t)(target); CYCLES(2);                 \
        goto _exit;                                                         \
    }                                                                        \
    CYCLES(2); chain(id, label);                            \
} while (0)
#define S6502_AOT_BRANCH_BOTH(fall_chain, target_chain, condition, fallthrough, target, fall_id, fall_label, target_id, target_label) do { \
    pc = (uint16_t)(fallthrough);                                            \
    if (condition) {                                                         \
        CYCLES(1); CYCLES((!!(0xff00 & (pc ^ (uint16_t)(target)))));         \
        pc = (uint16_t)(target); CYCLES(2);                 \
        target_chain(target_id, target_label);                              \
    }                                                                        \
    CYCLES(2); fall_chain(fall_id, fall_label);             \
} while (0)
#define S6502_AOT_BRANCH_TOKEN_TARGET(condition, fallthrough, target, target_id) do { \
    pc = (uint16_t)(fallthrough);                                            \
    if (condition) {                                                         \
        CYCLES(1); CYCLES((!!(0xff00 & (pc ^ (uint16_t)(target)))));         \
        pc = (uint16_t)(target); CYCLES(2);                                 \
        S6502_AOT_TOKEN(target_id);                                          \
    }                                                                        \
    CYCLES(2); goto _exit;                                                   \
} while (0)
#define S6502_AOT_BRANCH_TOKEN_FALL(condition, fallthrough, target, fall_id) do { \
    pc = (uint16_t)(fallthrough);                                            \
    if (condition) {                                                         \
        CYCLES(1); CYCLES((!!(0xff00 & (pc ^ (uint16_t)(target)))));         \
        pc = (uint16_t)(target); CYCLES(2); goto _exit;                     \
    }                                                                        \
    CYCLES(2); S6502_AOT_TOKEN(fall_id);                                    \
} while (0)
#define S6502_AOT_BRANCH_TOKEN_BOTH(condition, fallthrough, target, fall_id, target_id) do { \
    pc = (uint16_t)(fallthrough);                                            \
    if (condition) {                                                         \
        CYCLES(1); CYCLES((!!(0xff00 & (pc ^ (uint16_t)(target)))));         \
        pc = (uint16_t)(target); CYCLES(2);                                 \
        S6502_AOT_TOKEN(target_id);                                          \
    }                                                                        \
    CYCLES(2); S6502_AOT_TOKEN(fall_id);                                    \
} while (0)
#define S6502_AOT_JSR(return_pc, target) do {                                \
    PUSH((uint16_t)(return_pc) >> 8); PUSH((uint16_t)(return_pc) & 0xff);    \
    pc = (uint16_t)(target); CYCLES(6);                    \
    goto _exit;                                                             \
} while (0)
#define S6502_AOT_JSR_CHAIN(chain, return_pc, target, id, label) do {        \
    PUSH((uint16_t)(return_pc) >> 8); PUSH((uint16_t)(return_pc) & 0xff);    \
    pc = (uint16_t)(target); CYCLES(6);                    \
    chain(id, label);                                                        \
} while (0)
#define S6502_AOT_JSR_TOKEN(return_pc, target, id) do {                      \
    PUSH((uint16_t)(return_pc) >> 8); PUSH((uint16_t)(return_pc) & 0xff);    \
    pc = (uint16_t)(target); CYCLES(6);                                     \
    S6502_AOT_TOKEN(id);                                                     \
} while (0)
#define S6502_AOT_JMP(target) do {                                           \
    pc = (uint16_t)(target); CYCLES(3);                    \
    goto _exit;                                                             \
} while (0)
#define S6502_AOT_JMP_INDIRECT(pointer) do {                                 \
    pc = READ16((uint16_t)(pointer)); CYCLES(6);           \
    goto _exit;                                                             \
} while (0)
#define S6502_AOT_JMP_CHAIN(chain, target, id, label) do {                   \
    pc = (uint16_t)(target); CYCLES(3);                    \
    chain(id, label);                                                        \
} while (0)
#define S6502_AOT_JMP_TOKEN(target, id) do {                                \
    pc = (uint16_t)(target); CYCLES(3);                                     \
    S6502_AOT_TOKEN(id);                                                     \
} while (0)
#define S6502_AOT_RTS() do {                                                 \
    pc = POP(); pc = (uint16_t)(pc | (POP() << 8)); pc += 1;                \
    CYCLES(6); goto _exit;                                  \
} while (0)
#define S6502_AOT_RTI() do {                                                 \
    status = (uint8_t)(POP() | FLAG_U | FLAG_B);                             \
    pc = POP(); pc = (uint16_t)(pc | (POP() << 8));                          \
    CYCLES(6); goto _exit;                                  \
} while (0)
#define S6502_AOT_NOP() do { CYCLES(2); } while (0)
"""


MACRO_NAMES = (
    "S6502_AOT_DISPATCH", "S6502_AOT_CHAIN", "S6502_AOT_CHAIN_FAST",
    "S6502_AOT_TOKEN",
    "S6502_AOT_ZP_READ",
    "S6502_AOT_RAM_READ", "S6502_AOT_PAGE3_READ",
    "S6502_AOT_ZP16",
    "S6502_AOT_SET_NZ_MASK", "S6502_AOT_SET_C_MASK",
    "S6502_AOT_SET_V_MASK", "S6502_AOT_SET_I_MASK",
    "S6502_AOT_ADD16_IMM", "S6502_AOT_SUB16_IMM",
    "S6502_AOT_LDA", "S6502_AOT_LDY", "S6502_AOT_LDX",
    "S6502_AOT_STA", "S6502_AOT_STX", "S6502_AOT_STY", "S6502_AOT_STA_ZP",
    "S6502_AOT_STX_ZP", "S6502_AOT_STA_RAM", "S6502_AOT_STX_RAM",
    "S6502_AOT_LDA_INDY", "S6502_AOT_LDA_ABSX",
    "S6502_AOT_STA_INDY", "S6502_AOT_STA_ABSX", "S6502_AOT_STA_ABSY",
    "S6502_AOT_AND", "S6502_AOT_AND_INDY",
    "S6502_AOT_ORA", "S6502_AOT_ORA_INDY", "S6502_AOT_ORA_ABSX",
    "S6502_AOT_EOR",
    "S6502_AOT_EOR_INDY",
    "S6502_AOT_COMPARE", "S6502_AOT_CLC",
    "S6502_AOT_SEC", "S6502_AOT_SEI", "S6502_AOT_TAX", "S6502_AOT_TAY",
    "S6502_AOT_TXA", "S6502_AOT_TYA", "S6502_AOT_TSX", "S6502_AOT_TXS",
    "S6502_AOT_INY",
    "S6502_AOT_INX", "S6502_AOT_DEX", "S6502_AOT_DEY",
    "S6502_AOT_PHP", "S6502_AOT_PHA", "S6502_AOT_PLA", "S6502_AOT_PLP",
    "S6502_AOT_ASL_A", "S6502_AOT_LSR_A", "S6502_AOT_ROL_A",
    "S6502_AOT_ROR_A", "S6502_AOT_ASL_M", "S6502_AOT_ASL_ZP",
    "S6502_AOT_ASL_ABSX", "S6502_AOT_ASL_M_RAM",
    "S6502_AOT_LSR_M", "S6502_AOT_LSR_ZP", "S6502_AOT_LSR_M_RAM",
    "S6502_AOT_ROL_M", "S6502_AOT_ROL_ZP", "S6502_AOT_ROL_ABSX",
    "S6502_AOT_ROL_M_RAM",
    "S6502_AOT_INC",
    "S6502_AOT_ROR_M", "S6502_AOT_ROR_ZP", "S6502_AOT_ROR_M_RAM",
    "S6502_AOT_INC_RAM", "S6502_AOT_DEC", "S6502_AOT_DEC_RAM",
    "S6502_AOT_DEC_ABSX",
    "S6502_AOT_ADC", "S6502_AOT_ADC_INDY", "S6502_AOT_SBC",
    "S6502_AOT_SBC_INDY",
    "S6502_AOT_BRANCH", "S6502_AOT_BRANCH_TARGET",
    "S6502_AOT_BRANCH_FALL", "S6502_AOT_BRANCH_BOTH",
    "S6502_AOT_BRANCH_TOKEN_TARGET", "S6502_AOT_BRANCH_TOKEN_FALL",
    "S6502_AOT_BRANCH_TOKEN_BOTH",
    "S6502_AOT_JSR", "S6502_AOT_JSR_CHAIN", "S6502_AOT_JSR_TOKEN",
    "S6502_AOT_JMP", "S6502_AOT_JMP_INDIRECT", "S6502_AOT_JMP_CHAIN",
    "S6502_AOT_JMP_TOKEN",
    "S6502_AOT_RTS", "S6502_AOT_RTI", "S6502_AOT_NOP",
)


def chain_terminator(
    line: str, entries: dict[int, int], blocks: list[dict[str, object]],
    source: dict[str, object],
) -> str:
    def entry_for_any(target: int) -> int | None:
        if target in HLE_ENTRY_PCS:
            return None
        return entries.get(target)

    def entry_for_direct(target: int) -> int | None:
        target_id: int | None

        target_id = entry_for_any(target)
        if target_id is None:
            return None
        # Every direct edge still performs the same cycle/halt boundary check
        # as _exit.  Ambiguous physical mappings return through dispatch, and
        # every HLE entry above deliberately remains visible to its hook.
        if not source["chain_enabled"]:
            return None
        if not blocks[target_id]["chain_enabled"]:
            return None
        return target_id

    def chain_for(target_id: int) -> str:
        target = blocks[target_id]
        same_bank = (
            int(source["virtual_pc"]) >> 12 == int(target["virtual_pc"]) >> 12
        )
        bank2_safe = (
            not target["requires_bank2"] or source["requires_bank2"]
        )
        if same_bank and bank2_safe and not source["may_change_mapping"]:
            return "S6502_AOT_CHAIN_FAST"
        return "S6502_AOT_CHAIN"

    branch = re.fullmatch(
        r"S6502_AOT_BRANCH\((.+), 0x([0-9a-f]+)u, 0x([0-9a-f]+)u\);",
        line,
    )
    if branch:
        condition, fall_text, target_text = branch.groups()
        fallthrough = int(fall_text, 16)
        target = int(target_text, 16)
        fall_id = entry_for_direct(fallthrough)
        target_id = entry_for_direct(target)
        if fall_id is not None and target_id is not None:
            return (
                f"S6502_AOT_BRANCH_BOTH({chain_for(fall_id)}, "
                f"{chain_for(target_id)}, {condition}, 0x{fallthrough:04x}u, "
                f"0x{target:04x}u, {fall_id}u, _aot_{fall_id:02d}, "
                f"{target_id}u, _aot_{target_id:02d});"
            )
        if target_id is not None:
            return (
                f"S6502_AOT_BRANCH_TARGET({chain_for(target_id)}, {condition}, "
                f"0x{fallthrough:04x}u, "
                f"0x{target:04x}u, {target_id}u, _aot_{target_id:02d});"
            )
        if fall_id is not None:
            return (
                f"S6502_AOT_BRANCH_FALL({chain_for(fall_id)}, {condition}, "
                f"0x{fallthrough:04x}u, "
                f"0x{target:04x}u, {fall_id}u, _aot_{fall_id:02d});"
            )
        fall_id = entry_for_any(fallthrough)
        target_id = entry_for_any(target)
        if fall_id is not None and target_id is not None:
            return (
                f"S6502_AOT_BRANCH_TOKEN_BOTH({condition}, "
                f"0x{fallthrough:04x}u, 0x{target:04x}u, "
                f"{fall_id}u, {target_id}u);"
            )
        if target_id is not None:
            return (
                f"S6502_AOT_BRANCH_TOKEN_TARGET({condition}, "
                f"0x{fallthrough:04x}u, 0x{target:04x}u, {target_id}u);"
            )
        if fall_id is not None:
            return (
                f"S6502_AOT_BRANCH_TOKEN_FALL({condition}, "
                f"0x{fallthrough:04x}u, 0x{target:04x}u, {fall_id}u);"
            )
        return line

    jsr = re.fullmatch(
        r"S6502_AOT_JSR\(0x([0-9a-f]+)u, 0x([0-9a-f]+)u\);", line
    )
    if jsr:
        return_pc, target = (int(value, 16) for value in jsr.groups())
        target_id = entry_for_direct(target)
        if target_id is not None:
            return (
                f"S6502_AOT_JSR_CHAIN({chain_for(target_id)}, "
                f"0x{return_pc:04x}u, 0x{target:04x}u, "
                f"{target_id}u, _aot_{target_id:02d});"
            )
        target_id = entry_for_any(target)
        if target_id is not None:
            return (
                f"S6502_AOT_JSR_TOKEN(0x{return_pc:04x}u, "
                f"0x{target:04x}u, {target_id}u);"
            )
        return line

    jump = re.fullmatch(r"S6502_AOT_JMP\(0x([0-9a-f]+)u\);", line)
    if jump:
        target = int(jump.group(1), 16)
        target_id = entry_for_direct(target)
        if target_id is not None:
            return (
                f"S6502_AOT_JMP_CHAIN({chain_for(target_id)}, "
                f"0x{target:04x}u, {target_id}u, "
                f"_aot_{target_id:02d});"
            )
        target_id = entry_for_any(target)
        if target_id is not None:
            return (
                f"S6502_AOT_JMP_TOKEN(0x{target:04x}u, {target_id}u);"
            )
    return line


def render(
    rom: bytes,
    hotspots: tuple[tuple[int, int], ...] = FORMAL_HOTSPOTS,
) -> str:
    blocks = [decode_block(rom, physical, virtual) for physical, virtual in hotspots]
    for index, block in enumerate(blocks):
        block["chain_enabled"] = index < DEFAULT_CHAIN_BLOCK_LIMIT
    entry_groups: dict[int, list[int]] = {}
    for index, block in enumerate(blocks):
        entry_groups.setdefault(int(block["virtual_pc"]), []).append(index)
    # Direct chains have only one target label.  Ambiguous virtual PCs must
    # return through dispatch so every physical-bank variant gets a match.
    entries = {
        virtual_pc: indices[0]
        for virtual_pc, indices in entry_groups.items()
        if len(indices) == 1
    }
    signature = b"".join(block["signature"] for block in blocks)
    offsets: list[int] = []
    cursor = 0
    for block in blocks:
        offsets.append(cursor)
        cursor += len(block["signature"])

    out = [
        "/* Generated by tools/generate_aot_ebin.py; do not edit manually.",
        f" * E.BIN sha256: {hashlib.sha256(rom).hexdigest()}",
        " */",
        "",
        "#if defined(S6502_AOT_DEFINE_DATA)",
        "typedef struct s6502_aot_block {",
        "    uint32_t physical_pc;",
        "    uint16_t virtual_pc;",
        "    uint16_t signature_offset;",
        "    uint16_t signature_size;",
        "    uint16_t instruction_count;",
        "    uint8_t requires_bank2;",
        "} s6502_aot_block_t;",
        "",
        f"#define S6502_AOT_BLOCK_COUNT {len(blocks)}u",
        "static const uint8_t s6502_aot_signature[] = {",
    ]
    for index in range(0, len(signature), 12):
        chunk = signature[index : index + 12]
        out.append("    " + ", ".join(f"0x{byte:02x}" for byte in chunk) + ",")
    out.extend(("};", "", "static const s6502_aot_block_t s6502_aot_blocks[] = {"))
    for index, block in enumerate(blocks):
        out.append(
            "    {0x%06xu, 0x%04xu, %uu, %uu, %uu, %uu},"
            % (
                block["physical_pc"], block["virtual_pc"], offsets[index],
                len(block["signature"]), block["instruction_count"],
                1 if block["requires_bank2"] else 0,
            )
        )
    out.extend(("};", "", "#elif defined(S6502_AOT_DEFINE_TOKEN_TABLE)", ""))
    out.append(
        "static void *const s6502_aot_token_table[S6502_AOT_BLOCK_COUNT] = {"
    )
    for index in range(len(blocks)):
        out.append(f"    &&_aot_{index:02d},")
    out.extend(
        (
            "};",
            "",
            "#elif defined(S6502_AOT_DEFINE_DISPATCH)",
            "",
            "#define S6502_AOT_DISPATCH() do {                                      \\",
            "    switch (pc) {                                                        \\",
        )
    )
    for virtual_pc, indices in entry_groups.items():
        out.append(
            f"    case 0x{virtual_pc:04x}u:                                      \\"
        )
        if virtual_pc == 0x5351:
            out.append(
                "        S6502_AOT_ENTRY_5351_HOOK();                              \\"
            )
        if virtual_pc == 0x5801:
            out.append(
                "        S6502_AOT_ENTRY_5801_HOOK();                              \\"
            )
        if virtual_pc == 0x5C5D:
            out.append(
                "        S6502_AOT_ENTRY_5C5D_HOOK();                              \\"
            )
        if virtual_pc == 0x5CB3:
            out.append(
                "        S6502_AOT_ENTRY_5CB3_HOOK();                              \\"
            )
        if virtual_pc == 0x5CE5:
            out.append(
                "        S6502_AOT_ENTRY_5CE5_HOOK();                              \\"
            )
        if virtual_pc == 0x608A:
            out.append(
                "        S6502_AOT_ENTRY_608A_HOOK();                              \\"
            )
        if virtual_pc == 0x650F:
            out.append(
                "        S6502_AOT_ENTRY_650F_HOOK();                              \\"
            )
        if virtual_pc == 0x682D:
            out.append(
                "        S6502_AOT_ENTRY_682D_HOOK();                              \\"
            )
        if virtual_pc == 0x690F:
            out.append(
                "        S6502_AOT_ENTRY_690F_HOOK();                              \\"
            )
        if virtual_pc == 0x6988:
            out.append(
                "        S6502_AOT_ENTRY_6988_HOOK();                              \\"
            )
        if virtual_pc == 0x6A75:
            out.append(
                "        S6502_AOT_ENTRY_6A75_HOOK();                              \\"
            )
        if virtual_pc == 0x6AA7:
            out.append(
                "        S6502_AOT_ENTRY_6AA7_HOOK();                              \\"
            )
        if virtual_pc == 0x6AE0:
            out.append(
                "        S6502_AOT_ENTRY_6AE0_HOOK();                              \\"
            )
        if virtual_pc == 0x6B1A:
            out.append(
                "        S6502_AOT_ENTRY_6B1A_HOOK();                              \\"
            )
        if virtual_pc == 0x6BA4:
            out.append(
                "        S6502_AOT_ENTRY_6BA4_HOOK();                              \\"
            )
        if virtual_pc == 0x7937:
            out.append(
                "        S6502_AOT_ENTRY_7937_HOOK();                              \\"
            )
        if virtual_pc == 0x8039:
            out.append(
                "        S6502_AOT_ENTRY_8039_HOOK();                              \\"
            )
        if virtual_pc == 0x859E:
            out.append(
                "        S6502_AOT_ENTRY_859E_HOOK();                              \\"
            )
        if virtual_pc == 0x876B:
            out.append(
                "        S6502_AOT_ENTRY_876B_HOOK();                              \\"
            )
        if virtual_pc == 0xD1A2:
            out.append(
                "        S6502_AOT_ENTRY_D1A2_HOOK();                              \\"
            )
        if virtual_pc == 0xD2CA:
            out.append(
                "        S6502_AOT_ENTRY_D2CA_HOOK();                              \\"
            )
        if virtual_pc == 0xD340:
            out.append(
                "        S6502_AOT_ENTRY_D340_HOOK();                              \\"
            )
        if virtual_pc == 0xD349:
            out.append(
                "        S6502_AOT_ENTRY_D349_HOOK();                              \\"
            )
        if virtual_pc == 0xD352:
            out.append(
                "        S6502_AOT_ENTRY_D352_HOOK();                              \\"
            )
        if virtual_pc == 0xD35D:
            out.append(
                "        S6502_AOT_ENTRY_D35D_HOOK();                              \\"
            )
        if virtual_pc == 0xD35F:
            out.append(
                "        S6502_AOT_ENTRY_D35F_HOOK();                              \\"
            )
        if virtual_pc == 0xD362:
            out.append(
                "        S6502_AOT_ENTRY_D362_HOOK();                              \\"
            )
        if virtual_pc == 0xD596:
            out.append(
                "        S6502_AOT_ENTRY_D596_HOOK();                              \\"
            )
        if virtual_pc == 0xD572:
            out.append(
                "        S6502_AOT_ENTRY_D572_HOOK();                              \\"
            )
        if virtual_pc == 0xF52A:
            out.append(
                "        S6502_AOT_ENTRY_F52A_HOOK();                              \\"
            )
        if virtual_pc == 0xF549:
            out.append(
                "        S6502_AOT_ENTRY_F549_HOOK();                              \\"
            )
        if virtual_pc == 0xF55B:
            out.append(
                "        S6502_AOT_ENTRY_F55B_HOOK();                              \\"
            )
        for index in indices:
            out.append(
                f"        if (s6502_aot_match({index}u)) goto _aot_{index:02d};                \\"
            )
        out.append("        break;                                                         \\")
    out.extend(('    }                                                                      \\', "} while (0)", MACROS.strip(), ""))
    out.append("#elif defined(S6502_AOT_EMIT_BLOCKS)")
    for index, block in enumerate(blocks):
        out.append(f"  _aot_{index:02d}:")
        if block["requires_binary"]:
            out.append("    if (DECIMAL_p) goto _next;")
        if block["virtual_pc"] == 0x7C30 and all(
            pc in entries for pc in (0x7C38, 0x7C47, 0x7C44, 0x7C4A)
        ):
            out.append(
                "    S6502_AOT_NATIVE_TRACE_7C30("
                f"{index}u, {entries[0x7C38]}u, {entries[0x7C47]}u, "
                f"{entries[0x7C44]}u, {entries[0x7C4A]}u);"
            )
        out.append(
            f"    S6502_AOT_HIT({index}u, {block['instruction_count']}u);"
        )
        for line in block["instructions"]:
            out.append(f"    {chain_terminator(line, entries, blocks, block)}")
        out.append("")
    out.append("#elif defined(S6502_AOT_UNDEFINE)")
    for name in MACRO_NAMES:
        out.append(f"#undef {name}")
    out.extend(("#endif", ""))
    return "\n".join(out)


def hotspots_from_profile(
    rom: bytes, profile_path: Path, limit: int,
) -> tuple[tuple[int, int], ...]:
    # The formal set is the already verified baseline.  Profile expansion must
    # only append to it: replacing baseline blocks changes the AOT-to-AOT chain
    # graph and makes a larger candidate needlessly differ from the proven one.
    selected: list[tuple[int, int]] = list(FORMAL_HOTSPOTS)
    seen_hotspots: set[tuple[int, int]] = set(FORMAL_HOTSPOTS)
    if limit == len(selected):
        return tuple(selected)

    for hotspot in REQUIRED_HLE_HOTSPOTS:
        if hotspot in seen_hotspots:
            continue
        decode_block(rom, *hotspot)
        selected.append(hotspot)
        seen_hotspots.add(hotspot)

    with profile_path.open(encoding="utf-8", newline="") as profile_file:
        rows = csv.DictReader(profile_file)
        required = {"region", "physical_pc", "virtual_pc", "instructions"}

        if rows.fieldnames is None or not required.issubset(rows.fieldnames):
            raise ValueError(f"invalid AOT profile columns: {profile_path}")
        ranked = sorted(
            rows,
            key=lambda row: int(row["instructions"]),
            reverse=True,
        )

    for row in ranked:
        if row["region"] != "ROME":
            continue
        hotspot = (int(row["physical_pc"], 0), int(row["virtual_pc"], 0))
        if hotspot in seen_hotspots:
            continue
        try:
            decode_block(rom, *hotspot)
        except ValueError:
            continue
        selected.append(hotspot)
        seen_hotspots.add(hotspot)
        if len(selected) == limit:
            break

    if len(selected) != limit:
        raise ValueError(
            f"profile contains only {len(selected)} translatable unique blocks; "
            f"requested {limit}"
        )
    return tuple(selected)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, default=DEFAULT_ROM)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument(
        "--limit", type=int,
        help="generate only the first N ranked blocks for performance tuning",
    )
    parser.add_argument(
        "--formal-limit", type=int,
        help="generate a prefix of the formal code-layout order for tuning",
    )
    parser.add_argument(
        "--skip", type=int, action="append", default=[],
        help="skip a zero-based ranked block index while tuning; may repeat",
    )
    parser.add_argument(
        "--profile", type=Path,
        help="select translatable E.BIN blocks from a profiler CSV",
    )
    parser.add_argument(
        "--profile-limit", type=int,
        help="number of unique E.BIN blocks selected from --profile",
    )
    parser.add_argument(
        "--check", action="store_true",
        help="fail if the generated file is missing or out of date",
    )
    args = parser.parse_args()
    rom = args.rom.read_bytes()
    if len(rom) != 0x200000:
        parser.error(f"E.BIN must be exactly 2 MiB: {args.rom}")
    if args.limit is not None and args.formal_limit is not None:
        parser.error("--limit and --formal-limit are mutually exclusive")
    if args.profile is not None and (
        args.limit is not None or args.formal_limit is not None or args.skip
    ):
        parser.error("--profile cannot be combined with block-list tuning options")
    if (args.profile is None) != (args.profile_limit is None):
        parser.error("--profile and --profile-limit must be specified together")
    if (
        args.profile_limit is not None
        and args.profile_limit < len(FORMAL_HOTSPOTS)
    ):
        parser.error(
            "--profile-limit cannot be smaller than the formal AOT baseline"
        )
    if args.formal_limit is not None and args.skip:
        parser.error("--formal-limit and --skip are mutually exclusive")
    if args.limit is not None and not 1 <= args.limit <= len(HOTSPOTS):
        parser.error(f"--limit must be between 1 and {len(HOTSPOTS)}")
    if args.formal_limit is not None and not (
        1 <= args.formal_limit <= len(FORMAL_HOTSPOTS)
    ):
        parser.error(
            f"--formal-limit must be between 1 and {len(FORMAL_HOTSPOTS)}"
        )
    limit = args.limit if args.limit is not None else DEFAULT_BLOCK_LIMIT
    if any(index < 0 or index >= len(HOTSPOTS) for index in args.skip):
        parser.error(f"--skip must be between 0 and {len(HOTSPOTS) - 1}")
    if args.profile is not None:
        try:
            hotspots = hotspots_from_profile(
                rom, args.profile, args.profile_limit
            )
        except (OSError, ValueError) as exc:
            parser.error(str(exc))
    elif args.formal_limit is not None:
        hotspots = FORMAL_HOTSPOTS[:args.formal_limit]
    elif args.limit is None and not args.skip:
        try:
            hotspots = hotspots_from_profile(
                rom, DEFAULT_PROFILE, DEFAULT_PROFILE_LIMIT
            )
        except (OSError, ValueError) as exc:
            parser.error(str(exc))
    else:
        skipped = set(args.skip)
        hotspots = tuple(
            hotspot for index, hotspot in enumerate(HOTSPOTS)
            if index not in skipped
        )[:limit]
        if len(hotspots) != limit:
            parser.error("not enough blocks remain after --skip")
    generated = render(rom, hotspots)
    if args.check:
        if not args.output.is_file() or args.output.read_text(encoding="utf-8") != generated:
            raise SystemExit(f"generated AOT header is out of date: {args.output}")
    else:
        args.output.write_text(generated, encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
