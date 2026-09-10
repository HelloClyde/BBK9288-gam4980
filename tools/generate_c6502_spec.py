#!/usr/bin/env python3
"""Generate the compact C6502 compiler specification used by game AOT.

The old A-series kit is an input to this *offline* generator only.  The 9288
application consumes the generated header and therefore never needs the kit,
its compiler, or any 9588-era SDK at build or run time.
"""

from __future__ import annotations

import argparse
import hashlib
import re
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Region:
    name: str
    first: int
    last: int


@dataclass(frozen=True)
class Locate:
    name: str
    physical: int
    virtual: int


RANGE_RE = re.compile(
    r"^\s*range\s*:\s*([A-Za-z0-9_]+)\s+from\s+([0-9A-Fa-f]+)h"
    r"\s+to\s+([0-9A-Fa-f]+)h",
    re.IGNORECASE,
)
LOCATE_RE = re.compile(
    r"^\s*locate\s*:\s*([A-Za-z0-9_]+)\s+at\s+([0-9A-Fa-f]+)h"
    r"(?:\s+linked\s+to\s+([0-9A-Fa-f]+)h)?",
    re.IGNORECASE,
)
MAP_RE = re.compile(r"^([^\s*][^\s]*)\s+([0-9A-Fa-f]{8})\s*$")
RT_JSR_RE = re.compile(
    r"^\s*\d+\s+([0-9A-Fa-f]{4})\s+20\s+"
    r"([0-9A-Fa-f]{2})\s+([0-9A-Fa-f]{2})\s+.*?jsr\s+([^\s;]+)",
    re.IGNORECASE,
)


REGION_KINDS = {
    "game_uninit_grp": "C6502_REGION_RAM",
    "uninit_data_group": "C6502_REGION_RAM",
    "const_data_group": "C6502_REGION_CONST",
    "psdata_data_group": "C6502_REGION_CONST",
    "program_group": "C6502_REGION_CODE",
    "psnake_program_group": "C6502_REGION_GAME_CODE",
    "psfar_program_group": "C6502_REGION_GAME_CODE",
    "runtime_program_group": "C6502_REGION_RUNTIME",
    "bank_table_grp": "C6502_REGION_BANK_TABLE",
    "lcd_program_group": "C6502_REGION_FIRMWARE",
    "lcd1_program_group": "C6502_REGION_FIRMWARE",
    "bios_program_group": "C6502_REGION_FIRMWARE",
}

ABI_SYMBOLS = (
    "__FUNCTION_RET_ADDR",
    "__OPER1_TEMP_STORE",
    "__OPER1_CONVERSION_STORE",
    "__OPER2_TEMP_STORE",
    "__OPER2_CONVERSION_STORE",
    "__stack_ptr",
    "__oper1",
    "__oper2",
    "__addr_reg",
)

PUBLIC_APIS = (
    "&SysPicture",
    "&SysPartPicture",
    "&SysChinese",
    "&SysAscii",
    "&SysLine",
    "&SysRect",
    "&SysHorizontalLine",
    "&SysFillRect",
    "&SysPutPixel",
    "&SysGetKey",
    "&SysPollKey",
    "&SysClearKeyBuffer",
)


def read_text(path: Path) -> str:
    return path.read_bytes().replace(b"\0", b"").decode(
        "gb18030", errors="replace"
    )


def parse_cfg(text: str) -> tuple[list[Region], list[Locate]]:
    regions: list[Region] = []
    locates: list[Locate] = []
    for line in text.splitlines():
        match = RANGE_RE.match(line)
        if match and match.group(1) in REGION_KINDS:
            regions.append(
                Region(match.group(1), int(match.group(2), 16),
                       int(match.group(3), 16))
            )
            continue
        match = LOCATE_RE.match(line)
        if match and match.group(1) in REGION_KINDS:
            physical = int(match.group(2), 16)
            virtual = int(match.group(3), 16) if match.group(3) else physical
            locates.append(Locate(match.group(1), physical, virtual))
    return regions, locates


def parse_map(text: str) -> dict[str, int]:
    symbols: dict[str, int] = {}
    for line in text.splitlines():
        match = MAP_RE.match(line)
        if match:
            symbols.setdefault(match.group(1), int(match.group(2), 16))
    return symbols


def parse_runtime_calls(text: str) -> dict[str, int]:
    calls: dict[str, int] = {}
    for line in text.splitlines():
        match = RT_JSR_RE.match(line)
        if match:
            calls.setdefault(
                match.group(4),
                int(match.group(2), 16) | (int(match.group(3), 16) << 8),
            )
    return calls


def c_identifier(name: str) -> str:
    name = name.lstrip("&_")
    return re.sub(r"[^A-Za-z0-9]+", "_", name).upper()


def runtime_identifier(name: str) -> str:
    if name.startswith("___"):
        return "CORE_" + c_identifier(name)
    return c_identifier(name)


def source_digest(paths: list[Path]) -> str:
    digest = hashlib.sha256()
    for path in paths:
        digest.update(path.name.lower().encode("utf-8"))
        digest.update(b"\0")
        digest.update(path.read_bytes())
    return digest.hexdigest()


def emit(
    cfg_path: Path,
    map_path: Path,
    rt_path: Path,
    listing_path: Path,
) -> str:
    cfg = read_text(cfg_path)
    map_text = read_text(map_path)
    rt_text = read_text(rt_path)
    listing_text = read_text(listing_path)
    regions, locates = parse_cfg(cfg)
    symbols = parse_map(map_text)
    calls = parse_runtime_calls(rt_text)
    locate_by_name = {item.name: item for item in locates}
    digest = source_digest([cfg_path, map_path, rt_path, listing_path])

    # These are compiler templates, not game signatures.  Operand bytes are
    # masked out so relocated or re-linked GAMs still match.
    templates = [
        ("FAR_CALL", 11,
         "a2 00 86 26 a2 00 86 27 20 f6 d2",
         "ff 00 ff ff ff 00 ff ff ff ff ff"),
        # The compiler uses the same four-instruction phrase for every
        # 16-bit immediate store, not only for the ABI's OPER1 temporary.
        # Both destination bytes are relocatable here; the runtime matcher
        # additionally requires the second byte to be dst+1 (without a
        # zero-page wrap) before publishing the superinstruction.
        ("LOAD_OPER1_IMM16", 8,
         "a9 00 85 00 a9 00 85 00",
         "ff 00 ff 00 ff 00 ff 00"),
        ("LOAD_OPER2_IMM16", 8,
         "a9 00 85 23 a9 00 85 24",
         "ff 00 ff ff ff 00 ff ff"),
        ("STACK_ADD16", 16,
         "08 78 18 a5 28 69 00 85 28 a5 29 69 00 85 29 28",
         "ff ff ff ff ff ff 00 ff ff ff ff ff 00 ff ff ff"),
        ("STACK_SUB16", 16,
         "08 78 38 a5 28 e9 00 85 28 a5 29 e9 00 85 29 28",
         "ff ff ff ff ff ff 00 ff ff ff ff ff 00 ff ff ff"),
        ("STORE_CHAR_ARG_IMM", 5,
         "a9 00 20 aa da", "ff 00 ff ff ff"),
        ("STORE_INT_ARG_OPER1", 3,
         "20 ca da", "ff ff ff"),
        ("LOAD_OPER1_ZP16", 8,
         "a5 00 85 20 a5 00 85 21",
         "ff 00 ff ff ff 00 ff ff"),
        ("LOAD_OPER2_ZP16", 8,
         "a5 00 85 23 a5 00 85 24",
         "ff 00 ff ff ff 00 ff ff"),
        ("ADD16_OPER1_OPER2", 13,
         "18 a5 20 65 23 85 20 a5 21 65 24 85 21",
         "ff ff ff ff ff ff ff ff ff ff ff ff ff"),
        ("SUB16_OPER1_OPER2", 13,
         "38 a5 20 e5 23 85 20 a5 21 e5 24 85 21",
         "ff ff ff ff ff ff ff ff ff ff ff ff ff"),
        ("LOAD_OPER1_INDY16", 11,
         "a0 00 b1 00 85 20 c8 b1 00 85 21",
         "ff 00 ff 00 ff ff ff ff 00 ff ff"),
        ("STORE_OPER1_INDY16", 11,
         "a0 00 a5 20 91 00 c8 a5 21 91 00",
         "ff 00 ff ff ff 00 ff ff ff ff 00"),
        ("ADD16_IMM_GENERIC", 13,
         "18 a5 00 69 00 85 00 a5 00 69 00 85 00",
         "ff ff 00 ff 00 ff 00 ff 00 ff 00 ff 00"),
        ("SUB16_IMM_GENERIC", 13,
         "38 a5 00 e9 00 85 00 a5 00 e9 00 85 00",
         "ff ff 00 ff 00 ff 00 ff 00 ff 00 ff 00"),
        ("ADD16_PRESERVE_GENERIC", 16,
         "08 78 18 a5 00 69 00 85 00 a5 00 69 00 85 00 28",
         "ff ff ff ff 00 ff 00 ff 00 ff 00 ff 00 ff 00 ff"),
        ("SUB16_PRESERVE_GENERIC", 16,
         "08 78 38 a5 00 e9 00 85 00 a5 00 e9 00 85 00 28",
         "ff ff ff ff 00 ff 00 ff 00 ff 00 ff 00 ff 00 ff"),
        ("ADD16_REGS_GENERIC", 13,
         "18 a5 00 65 00 85 00 a5 00 65 00 85 00",
         "ff ff 00 ff 00 ff 00 ff 00 ff 00 ff 00"),
        ("SUB16_REGS_GENERIC", 13,
         "38 a5 00 e5 00 85 00 a5 00 e5 00 85 00",
         "ff ff 00 ff 00 ff 00 ff 00 ff 00 ff 00"),
        ("STORE16_IMM_GENERIC", 8,
         "a9 00 85 00 a9 00 85 00",
         "ff 00 ff 00 ff 00 ff 00"),
        ("COPY16_GENERIC", 8,
         "a5 00 85 00 a5 00 85 00",
         "ff 00 ff 00 ff 00 ff 00"),
        ("LOAD16_INDIRECT_GENERIC", 11,
         "a0 00 b1 00 85 00 c8 b1 00 85 00",
         "ff 00 ff 00 ff 00 ff ff 00 ff 00"),
        ("STORE16_INDIRECT_GENERIC", 11,
         "a0 00 a5 00 91 00 c8 a5 00 91 00",
         "ff 00 ff 00 ff 00 ff ff 00 ff 00"),
        ("LOAD_STACK8_GENERIC", 4,
         "a0 00 b1 28",
         "ff 00 ff ff"),
        ("STORE_STACK8_GENERIC", 4,
         "a0 00 91 28",
         "ff 00 ff ff"),
    ]
    far_call_count = listing_text.lower().count(".bf_call")
    c_start_count = listing_text.lower().count(".c_start")

    out = [
        "/* Generated by tools/generate_c6502_spec.py; do not edit. */",
        "#ifndef S6502_C6502_SPEC_GENERATED_H",
        "#define S6502_C6502_SPEC_GENERATED_H",
        "",
        f'#define C6502_SPEC_SOURCE_SHA256 "{digest}"',
        f"#define C6502_SPEC_LISTING_C_STATEMENTS {c_start_count}u",
        f"#define C6502_SPEC_LISTING_FAR_CALLS {far_call_count}u",
        "",
        "enum c6502_region_kind {",
        "    C6502_REGION_RAM = 1,",
        "    C6502_REGION_CONST,",
        "    C6502_REGION_CODE,",
        "    C6502_REGION_GAME_CODE,",
        "    C6502_REGION_RUNTIME,",
        "    C6502_REGION_BANK_TABLE,",
        "    C6502_REGION_FIRMWARE,",
        "};",
        "typedef struct c6502_region_spec {",
        "    uint32_t physical_first;",
        "    uint32_t physical_last;",
        "    uint32_t load_address;",
        "    uint16_t virtual_address;",
        "    uint8_t kind;",
        "} c6502_region_spec_t;",
        "static const c6502_region_spec_t c6502_region_specs[] = {",
    ]
    for region in regions:
        locate = locate_by_name.get(region.name)
        load = locate.physical if locate else region.first
        virtual = locate.virtual if locate else region.first
        out.append(
            f"    {{0x{region.first:08x}u, 0x{region.last:08x}u, "
            f"0x{load:08x}u, 0x{virtual & 0xffff:04x}u, "
            f"{REGION_KINDS[region.name]}}}, /* {region.name} */"
        )
    out += [
        "};",
        "#define C6502_REGION_SPEC_COUNT "
        "((uint32_t)(sizeof(c6502_region_specs) / sizeof(c6502_region_specs[0])))",
        "",
    ]

    for symbol in ABI_SYMBOLS:
        value = symbols.get(symbol)
        if value is not None:
            out.append(
                f"#define C6502_ABI_MAP_{c_identifier(symbol)} "
                f"0x{value & 0xffff:04x}u"
            )
    out += [
        "/* ABI used by the downloadable GAM compiler listings and 4980 ROM. */",
        "#define C6502_ABI_STACK_PTR 0x0028u",
        "#define C6502_ABI_OPER1 0x0020u",
        "#define C6502_ABI_OPER2 0x0023u",
        "#define C6502_ABI_ADDR_REG 0x0026u",
    ]
    out.append("")
    for symbol, address in sorted(calls.items(), key=lambda item: item[1]):
        out.append(
            f"#define C6502_RT_{runtime_identifier(symbol)} 0x{address:04x}u"
        )
    out.append("")
    for symbol in PUBLIC_APIS:
        value = symbols.get(symbol)
        if value is not None:
            out.append(
                f"#define C6502_API_{c_identifier(symbol)} 0x{value & 0xffff:04x}u"
            )

    out += [
        "",
        "enum c6502_template_kind {",
        "    C6502_TEMPLATE_NONE = 0,",
    ]
    for index, (name, *_rest) in enumerate(templates, start=1):
        out.append(f"    C6502_TEMPLATE_{name} = {index},")
    out += [
        "};",
        "#define C6502_TEMPLATE_MAX_BYTES 20u",
        "typedef struct c6502_template_spec {",
        "    uint8_t kind;",
        "    uint8_t size;",
        "    uint8_t bytes[C6502_TEMPLATE_MAX_BYTES];",
        "    uint8_t mask[C6502_TEMPLATE_MAX_BYTES];",
        "} c6502_template_spec_t;",
        "static const c6502_template_spec_t c6502_template_specs[] = {",
    ]
    for index, (_name, size, byte_text, mask_text) in enumerate(templates, 1):
        byte_values = [int(value, 16) for value in byte_text.split()]
        mask_values = [int(value, 16) for value in mask_text.split()]
        byte_values += [0] * (20 - len(byte_values))
        mask_values += [0] * (20 - len(mask_values))
        out.append(
            f"    {{{index}u, {size}u, "
            "{" + ", ".join(f"0x{x:02x}u" for x in byte_values) + "}, "
            "{" + ", ".join(f"0x{x:02x}u" for x in mask_values) + "}},"
        )
    out += [
        "};",
        "#define C6502_TEMPLATE_SPEC_COUNT "
        "((uint32_t)(sizeof(c6502_template_specs) / sizeof(c6502_template_specs[0])))",
        "",
    ]
    if len(templates) > 32:
        raise ValueError('template first-byte index needs more than 32 bits')
    out.append('static const uint32_t c6502_template_first_index[256] = {')
    for opcode in range(256):
        bits = 0
        for index, (_, _, byte_text, mask_text) in enumerate(templates):
            first = int(byte_text.split()[0], 16)
            mask = int(mask_text.split()[0], 16)
            if opcode & mask == first & mask:
                bits |= 1 << index
        out.append(f'    0x{bits:08x}u,')
    out += ['};', '', '#endif', '']
    return "\n".join(out)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--kit", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--listing", type=Path)
    args = parser.parse_args()
    dwn = args.kit / "dwn"
    if args.listing:
        listing = args.listing
    else:
        clean_listing = dwn / "贪食蛇" / "psnake.lst"
        listing = clean_listing if clean_listing.is_file() else dwn / "psnake.lst"
    generated = emit(
        dwn / "4980.cfg", dwn / "test.map", dwn / "RT.LST", listing
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(generated, encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
