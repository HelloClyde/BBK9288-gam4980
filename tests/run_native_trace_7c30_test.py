#!/usr/bin/env python3
"""Build and run the exact-state EB6C30 native-trace comparison."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom8", type=Path)
    parser.add_argument("rome", type=Path)
    parser.add_argument("--cc", default=os.environ.get("CC", "gcc"))
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as directory:
        executable = Path(directory) / (
            "native-trace-7c30.exe" if os.name == "nt" else "native-trace-7c30"
        )
        command = [
            args.cc,
            "-std=gnu11",
            "-O2",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wno-unused-function",
            "-Wno-unused-parameter",
            "-Wno-attributes",
            "-Isrc",
            "tests/firmware_native_trace_7c30_test.c",
            "-o",
            str(executable),
        ]
        print("+", " ".join(command))
        subprocess.run(command, cwd=ROOT, check=True)
        command = [str(executable), str(args.rom8), str(args.rome)]
        print("+", " ".join(command))
        subprocess.run(command, cwd=ROOT, check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
