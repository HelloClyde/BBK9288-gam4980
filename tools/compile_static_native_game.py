#!/usr/bin/env python3
"""Generate statically linked S1C33 modules for one GAM image.

This is the first stage of the game-specific PC recompiler.  It reuses the
validated C6502 CFG/AOT lowering, but emits ordinary ELF objects which are
linked into the KF2 instead of copied through the pageable .GNA arena.
Only the initial four game banks are selected in v1; the complete .GNA is
still produced separately for coverage/fallback experiments.
"""

from __future__ import annotations

import argparse
import json
from collections import defaultdict
from pathlib import Path
import subprocess
import sys


PROJECT_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT_ROOT / "tools"))

import pack_native_module as native  # noqa: E402


def run(command: list[str]) -> None:
    print("+", " ".join(command))
    subprocess.run(command, check=True)


def compile_group(
    *,
    clang: str,
    objcopy: str,
    include_dir: Path,
    output_dir: Path,
    game: bytes,
    index: int,
    blocks: list[tuple[int, tuple[int, ...]]],
    optimization: str,
) -> tuple[Path, int]:
    source_text, section = native.render_module_source(
        index,
        blocks,
        game,
        skip_hle_entries=False,
        defer_dispatch_entries=True,
        module_key=(
            ((blocks[0][1][1] >> 12) << 28)
            | ((blocks[0][1][0] >> 12) << 12)
            | ((blocks[0][1][1] >> 8) << 4)
            | 11
        ),
    )
    source = output_dir / f"static_native_{index:03d}.c"
    object_path = output_dir / f"static_native_{index:03d}.o"
    raw_path = output_dir / f"static_native_{index:03d}.bin"
    source.write_text(source_text, encoding="utf-8")
    run(
        [
            clang,
            "--target=s1c33-none-elf",
            f"-O{optimization}",
            "-ffreestanding",
            "-fno-builtin",
            "-fno-jump-tables",
            "-fomit-frame-pointer",
            "-fno-strict-aliasing",
            "-ffunction-sections",
            "-fdata-sections",
            "-I",
            str(include_dir),
            "-c",
            str(source),
            "-o",
            str(object_path),
        ]
    )
    run(
        [
            objcopy,
            "-O",
            "binary",
            f"--only-section={section}",
            str(object_path),
            str(raw_path),
        ]
    )
    return object_path, raw_path.stat().st_size


def render_header(
    *, game: bytes, stats: dict[str, int], selected: list[dict[str, object]],
    spans: tuple[tuple[int, int], ...], recovered_blocks: int,
    recovered_guest_bytes: int,
) -> str:
    selected_blocks = sum(int(item["block_count"]) for item in selected)
    selected_guest_bytes = sum(int(item["guest_bytes"]) for item in selected)
    selected_native_bytes = sum(int(item["native_bytes"]) for item in selected)
    lines = [
        "#ifndef GAM4980_STATIC_NATIVE_GENERATED_H",
        "#define GAM4980_STATIC_NATIVE_GENERATED_H",
        "",
        '#include "gam4980_types.h"',
        '#include "s6502_iram_exec_abi.h"',
        "",
        "typedef u32 (*gam4980_static_native_entry_fn)(",
        "    s6502_iram_asm_context_t *context);",
        "typedef struct gam4980_static_native_page {",
        "    u16 slot;",
        "    u16 bank;",
        "    u16 page;",
        "    u16 reserved;",
        "    gam4980_static_native_entry_fn entry;",
        "} gam4980_static_native_page_t;",
        "typedef struct gam4980_static_native_span {",
        "    u32 offset;",
        "    u32 size;",
        "} gam4980_static_native_span_t;",
        "",
        f"#define GAM4980_STATIC_NATIVE_GAME_SIZE {len(game)}u",
        f"#define GAM4980_STATIC_NATIVE_GAME_HASH 0x{native.fnv1a(game):08x}u",
        f"#define GAM4980_STATIC_NATIVE_GAME_CODE_SIZE {stats['code_size']}u",
        f"#define GAM4980_STATIC_NATIVE_GAME_ENTRY 0x{stats['entry_pc']:04x}u",
        f"#define GAM4980_STATIC_NATIVE_RECOVERED_BLOCKS {recovered_blocks}u",
        f"#define GAM4980_STATIC_NATIVE_RECOVERED_GUEST_BYTES {recovered_guest_bytes}u",
        f"#define GAM4980_STATIC_NATIVE_SELECTED_BLOCKS {selected_blocks}u",
        f"#define GAM4980_STATIC_NATIVE_SELECTED_GUEST_BYTES {selected_guest_bytes}u",
        f"#define GAM4980_STATIC_NATIVE_SELECTED_CODE_BYTES {selected_native_bytes}u",
        f"#define GAM4980_STATIC_NATIVE_MODULE_COUNT {len(selected)}u",
        f"#define GAM4980_STATIC_NATIVE_PAGE_COUNT {sum(len(item['pages']) for item in selected)}u",
        f"#define GAM4980_STATIC_NATIVE_SPAN_COUNT {len(spans)}u",
        "",
        "extern const gam4980_static_native_page_t",
        "    gam4980_static_native_pages[GAM4980_STATIC_NATIVE_PAGE_COUNT];",
        "extern const gam4980_static_native_span_t",
        "    gam4980_static_native_spans[GAM4980_STATIC_NATIVE_SPAN_COUNT];",
        "",
        "#endif",
        "",
    ]
    return "\n".join(lines)


def render_registry(selected: list[dict[str, object]], spans: tuple[tuple[int, int], ...]) -> str:
    lines = [
        "/* Generated by tools/compile_static_native_game.py; do not edit. */",
        '#include "gam4980_static_native_generated.h"',
        "",
    ]
    for item in selected:
        lines.append(
            f"u32 s6502_native_module_{int(item['index']):02d}("
            "s6502_iram_asm_context_t *context);"
        )
    lines.extend(
        [
            "",
            "const gam4980_static_native_page_t gam4980_static_native_pages[",
            "    GAM4980_STATIC_NATIVE_PAGE_COUNT] = {",
        ]
    )
    for item in selected:
        for page in item["pages"]:
            lines.append(
                "    {"
                f"0x{int(item['slot']):x}u, 0x{int(item['bank']):x}u, "
                f"0x{int(page):02x}u, 0u, "
                f"s6502_native_module_{int(item['index']):02d}" + "},"
            )
    lines.extend(
        [
            "};",
            "",
            "const gam4980_static_native_span_t gam4980_static_native_spans[",
            "    GAM4980_STATIC_NATIVE_SPAN_COUNT] = {",
        ]
    )
    for offset, size in spans:
        lines.append(f"    {{{offset}u, {size}u}},")
    lines.extend(["};", ""])
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Compile one GAM's initial working set into KF2-linkable S1C33 objects"
    )
    parser.add_argument("--clang", required=True)
    parser.add_argument("--objcopy", required=True)
    parser.add_argument("--include-dir", type=Path, required=True)
    parser.add_argument("--game", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--optimization", choices=("2", "3", "s", "z"), default="z")
    parser.add_argument("--max-native-bytes", type=lambda value: int(value, 0), default=0x50000)
    args = parser.parse_args()

    game = args.game.read_bytes()
    records, stats = native.recover_game_blocks(game)
    groups: dict[tuple[int, int], list[tuple[int, tuple[int, ...]]]] = defaultdict(list)
    for block_id, record in enumerate(records):
        physical, virtual = record[:2]
        slot = virtual >> 12
        bank = physical >> 12
        # Version 1 statically links the four banks visible at game startup.
        # Later revisions can use a true-device profile to choose scene banks.
        if 5 <= slot <= 8 and bank == 0x20D + slot - 5:
            groups[(virtual >> 11, physical >> 11)].append((block_id, record))

    args.output_dir.mkdir(parents=True, exist_ok=True)
    for pattern in ("static_native_*.c", "static_native_*.o", "static_native_*.bin"):
        for path in args.output_dir.glob(pattern):
            path.unlink()

    candidates: list[dict[str, object]] = []
    selected: list[dict[str, object]] = []
    used = 0
    ordered_groups = sorted(
        groups.items(),
        key=lambda item: (
            0 if any(record[1] == stats["entry_pc"] for _id, record in item[1]) else 1,
            item[0][0],
            item[0][1],
        ),
    )
    for index, ((_virtual_group, _physical_group), blocks) in enumerate(ordered_groups):
        object_path, native_bytes = compile_group(
            clang=args.clang,
            objcopy=args.objcopy,
            include_dir=args.include_dir,
            output_dir=args.output_dir,
            game=game,
            index=index,
            blocks=blocks,
            optimization=args.optimization,
        )
        record0 = blocks[0][1]
        candidate = {
                "index": index,
                "object": object_path,
                "slot": record0[1] >> 12,
                "bank": record0[0] >> 12,
                "pages": sorted({record[1] >> 8 for _id, record in blocks}),
                "block_count": len(blocks),
                "guest_bytes": sum(record[3] for _id, record in blocks),
                "native_bytes": native_bytes,
                "has_entry": any(record[1] == stats["entry_pc"] for _id, record in blocks),
            }
        candidates.append(candidate)
        size = int(candidate["native_bytes"])
        if used + size <= args.max_native_bytes or not selected:
            selected.append(candidate)
            used += size
        else:
            Path(candidate["object"]).unlink(missing_ok=True)
            break
    if not selected:
        raise SystemExit("static native byte budget selected no modules")

    selected_indexes = {int(item["index"]) for item in selected}
    for candidate in candidates:
        if int(candidate["index"]) not in selected_indexes:
            Path(candidate["object"]).unlink(missing_ok=True)

    # Publish spans only for code that was actually linked.  A guest write to
    # one of these ranges invalidates the static translation before re-entry.
    selected_records: list[tuple[int, ...]] = []
    for item in selected:
        for blocks in groups.values():
            if not blocks:
                continue
            first = blocks[0][1]
            if first[0] >> 12 == int(item["bank"]) and first[1] >> 8 in item["pages"]:
                selected_records.extend(record for _id, record in blocks)
    spans = native.merge_game_code_spans(selected_records)

    header = args.output_dir / "gam4980_static_native_generated.h"
    registry = args.output_dir / "gam4980_static_native_registry.c"
    header.write_text(
        render_header(
            game=game,
            stats=stats,
            selected=selected,
            spans=spans,
            recovered_blocks=len(records),
            recovered_guest_bytes=sum(record[3] for record in records),
        ),
        encoding="utf-8",
    )
    registry.write_text(render_registry(selected, spans), encoding="utf-8")

    report = {
        "format": "gam4980-static-native-v1",
        "game": str(args.game.resolve()),
        "game_size": len(game),
        "game_hash_fnv1a": f"0x{native.fnv1a(game):08x}",
        "entry_pc": f"0x{stats['entry_pc']:04x}",
        "code_size": stats["code_size"],
        "recovered_blocks": len(records),
        "recovered_guest_bytes": sum(record[3] for record in records),
        "selected_blocks": sum(int(item["block_count"]) for item in selected),
        "selected_guest_bytes": sum(int(item["guest_bytes"]) for item in selected),
        "selected_native_bytes": used,
        "selected_module_count": len(selected),
        "candidate_module_count": len(ordered_groups),
        "max_native_bytes": args.max_native_bytes,
        "selected_modules": [
            {key: value for key, value in item.items() if key != "object"}
            for item in selected
        ],
    }
    (args.output_dir / "static-native-report.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(
        "static native v1: "
        f"modules={len(selected)}/{len(ordered_groups)}, "
        f"blocks={report['selected_blocks']}/{len(records)}, "
        f"guest_bytes={report['selected_guest_bytes']}/"
        f"{report['recovered_guest_bytes']}, native_bytes={used}"
    )


if __name__ == "__main__":
    main()
