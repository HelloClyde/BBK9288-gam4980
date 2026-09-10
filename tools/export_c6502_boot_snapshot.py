#!/usr/bin/env python3
"""Run the reference A-series boot once and export the native entry state."""

from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_ROM8 = ROOT / "应用" / "数据" / "游戏" / "gam4980" / "8.BIN"
DEFAULT_ROME = ROOT / "应用" / "数据" / "游戏" / "gam4980" / "E.BIN"


def export_snapshot(
    game: Path, output: Path, rom8: Path = DEFAULT_ROM8,
    rome: Path = DEFAULT_ROME, cc: str = "gcc",
) -> None:
    executable = ROOT / "build" / "export_native_boot_snapshot.exe"
    sources = [
        ROOT / "tests" / "export_native_boot_snapshot.c",
        ROOT / "src" / "gam4980_core.c",
    ]
    newest_source = max(path.stat().st_mtime_ns for path in sources)
    if not executable.is_file() or executable.stat().st_mtime_ns < newest_source:
        executable.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run([
            cc, "-O2", "-std=gnu11",
            "-DGAM4980_ENABLE_AOT",
            "-DGAM4980_ENABLE_NATIVE_TRACE_AOT",
            "-DGAM4980_ENABLE_FIRMWARE_HLE",
            "-DGAM4980_FIRMWARE_HLE_MASK=1023",
            "-DGAM4980_ENABLE_AGGRESSIVE_REGION_HLE",
            "-DGAM4980_ENABLE_GAME_LOAD_AOT",
            "-DGAM4980_ENABLE_IRAM_EXEC_ENGINE",
            "-DGAM4980_IRAM_EXEC_NATIVE_TEST",
            "-I", str(ROOT / "src"),
            *(str(path) for path in sources), "-o", str(executable),
        ], check=True)
    output.parent.mkdir(parents=True, exist_ok=True)
    # The small host probe uses the C runtime's narrow fopen().  On the
    # Windows build host that cannot reliably open Chinese game names, so use
    # an ASCII-only staging path and move the generated snapshot afterwards.
    staging = ROOT / "build" / "native_boot_snapshot_stage"
    staging.mkdir(parents=True, exist_ok=True)
    staged_game = staging / "game.gam"
    staged_output = staging / "boot.bin"
    shutil.copyfile(game, staged_game)
    subprocess.run([
        str(executable), str(rom8), str(rome), str(staged_game),
        str(staged_output),
    ], check=True)
    shutil.copyfile(staged_output, output)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--rom8", type=Path, default=DEFAULT_ROM8)
    parser.add_argument("--rome", type=Path, default=DEFAULT_ROME)
    parser.add_argument("--cc", default="gcc")
    args = parser.parse_args()
    export_snapshot(args.game, args.output, args.rom8, args.rome, args.cc)
    print(f"snapshot: {args.output} ({args.output.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
