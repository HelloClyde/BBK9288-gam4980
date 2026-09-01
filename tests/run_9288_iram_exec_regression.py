#!/usr/bin/env python3
"""Build and statically regress the 9288 resident assembly engine."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys


PROJECT_ROOT = Path(__file__).resolve().parents[1]
BUILD_ROOT = PROJECT_ROOT / "build" / "9288"


def run(command: list[str]) -> None:
    print("+", " ".join(command))
    subprocess.run(command, cwd=PROJECT_ROOT, check=True)


def target_build_command(
    sdk: Path,
    toolchain: Path,
    output: Path,
    assembly: bool,
    rebuild: bool,
) -> list[str]:
    command = [
        sys.executable,
        str(PROJECT_ROOT / "build_9288.py"),
        "--sdk",
        str(sdk),
        "--toolchain",
        str(toolchain),
        "--lightweight-performance",
        "--aggressive-region-hle",
        "--iram-hot-core",
        "--bare-session",
        "--iram-exec-engine",
        "--iram-exec-asm" if assembly else "--iram-exec-c",
        "--optimization",
        "2",
        "--output",
        str(output),
    ]
    if rebuild:
        command.append("--rebuild")
    return command


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, default=PROJECT_ROOT / "sdk")
    parser.add_argument("--toolchain", type=Path, required=True)
    parser.add_argument(
        "--output",
        type=Path,
        default=BUILD_ROOT / "GAM4980-IRAM-ASM.exe",
    )
    parser.add_argument(
        "--also-build-c-control",
        action="store_true",
        help="also build the previous C IRAM engine for a true-device A/B",
    )
    parser.add_argument("--rebuild", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    sdk = args.sdk.resolve()
    toolchain = args.toolchain.resolve()
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)

    run([sys.executable, str(PROJECT_ROOT / "tests" / "test_audit_9288_iram_exec.py")])
    run(
        target_build_command(
            sdk, toolchain, output, assembly=True, rebuild=args.rebuild
        )
    )
    if not output.is_file():
        raise SystemExit(f"target build did not produce {output}")

    if args.also_build_c_control:
        control = output.with_name(output.stem + "-C-CONTROL" + output.suffix)
        run(
            target_build_command(
                sdk, toolchain, control, assembly=False, rebuild=args.rebuild
            )
        )
    print(f"IRAM assembly build regression passed: {output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
