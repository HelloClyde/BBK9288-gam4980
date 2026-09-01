#!/usr/bin/env python3
"""Compare native resident-IRAM execution with the reference interpreter."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys


STATE_PREFIXES = ("core initialized:", "state hash=", "state cpu=")


def run_case(
    executable: Path,
    rom8: Path,
    rome: Path,
    game: Path,
    frames: int,
    resident: bool,
    story: bool,
    raw_slices: int | None = None,
) -> tuple[tuple[str, ...], str]:
    environment = os.environ.copy()
    environment["GAM4980_SMOKE_FRAMES"] = str(frames)
    if resident:
        environment["GAM4980_SMOKE_IRAM_RESIDENT"] = "1"
    else:
        environment.pop("GAM4980_SMOKE_IRAM_RESIDENT", None)
    if story:
        environment["GAM4980_SMOKE_STORY"] = "1"
    else:
        environment.pop("GAM4980_SMOKE_STORY", None)
    if raw_slices is not None:
        environment["GAM4980_SMOKE_RAW_EXECS"] = str(raw_slices)
    else:
        environment.pop("GAM4980_SMOKE_RAW_EXECS", None)
    result = subprocess.run(
        [str(executable), str(rom8), str(rome), str(game)],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        env=environment,
    )
    state = tuple(
        line for line in result.stdout.splitlines()
        if line.startswith(STATE_PREFIXES)
    )
    return state, result.stdout


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", type=Path)
    parser.add_argument("rom8", type=Path)
    parser.add_argument("rome", type=Path)
    parser.add_argument("game", type=Path)
    parser.add_argument(
        "--frames", default="1,2,3,4,5,10,20,50,100,300,1000,3000,5000"
    )
    parser.add_argument("--story", action="store_true")
    parser.add_argument(
        "--raw-slices",
        help="comma-separated counts of direct one-cycle s6502_exec calls",
    )
    args = parser.parse_args()

    cases = args.raw_slices.split(",") if args.raw_slices else args.frames.split(",")
    for text in cases:
        count = int(text)
        frames = 1 if args.raw_slices else count
        raw_slices = count if args.raw_slices else None
        reference, _ = run_case(
            args.executable, args.rom8, args.rome, args.game,
            frames, False, args.story, raw_slices,
        )
        resident, resident_output = run_case(
            args.executable, args.rom8, args.rome, args.game,
            frames, True, args.story, raw_slices,
        )
        if reference != resident:
            label = "raw_slices" if args.raw_slices else "frames"
            print(f"{label}={count}: DIFFERENT")
            print("reference:")
            print("\n".join(reference))
            print("resident:")
            print("\n".join(resident))
            for line in resident_output.splitlines():
                if line.startswith("iram exec "):
                    print(line)
            return 1
        label = "raw_slices" if args.raw_slices else "frames"
        print(f"{label}={count}: equivalent")
    return 0


if __name__ == "__main__":
    sys.exit(main())
