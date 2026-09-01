#!/usr/bin/env python3
"""Build the C resident-engine reference and run host equivalence checks."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


PROJECT_ROOT = Path(__file__).resolve().parents[1]


def run(
    command: list[str],
    environment: dict[str, str] | None = None,
    echo_output: bool = True,
) -> str:
    print("+", " ".join(command))
    result = subprocess.run(
        command,
        cwd=PROJECT_ROOT,
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        env=environment,
    )
    if echo_output:
        print(result.stdout, end="")
    return result.stdout


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom8", type=Path)
    parser.add_argument("rome", type=Path)
    parser.add_argument("games", type=Path, nargs="+")
    parser.add_argument("--cc", default=os.environ.get("CC", "gcc"))
    parser.add_argument("--raw-slices", default="1,2,3,5,10,20,100,1000")
    parser.add_argument("--frames", default="1,2,5,10,100,1000,5000")
    parser.add_argument("--story", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    with tempfile.TemporaryDirectory() as directory:
        executable = Path(directory) / (
            "gam4980-core-smoke-iram-c.exe"
            if os.name == "nt"
            else "gam4980-core-smoke-iram-c"
        )
        compile_command = [
            args.cc,
            "-std=gnu11",
            "-O2",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wno-unused-parameter",
            "-Wno-unused-function",
            "-Wno-attributes",
            "-Wno-pointer-to-int-cast",
            "-Wno-int-to-pointer-cast",
            "-DDL_DOWN",
            "-D_RLS_",
            "-DGAM4980_ENABLE_AOT",
            "-DGAM4980_ENABLE_NATIVE_TRACE_AOT",
            "-DGAM4980_ENABLE_FIRMWARE_HLE",
            "-DGAM4980_FIRMWARE_HLE_MASK=1023",
            "-DGAM4980_ENABLE_AGGRESSIVE_REGION_HLE",
            "-DGAM4980_ENABLE_GAME_LOAD_AOT",
            "-DGAM4980_ENABLE_IRAM_EXEC_ENGINE",
            "-DGAM4980_IRAM_EXEC_NATIVE_TEST",
            "-Isrc",
            "tests/core_smoke.c",
            "src/gam4980_core.c",
            "-o",
            str(executable),
        ]
        run(compile_command)

        compare_script = str(PROJECT_ROOT / "tests" / "compare_iram_resident.py")
        first_game = args.games[0].resolve()
        run(
            [
                sys.executable,
                compare_script,
                str(executable),
                str(args.rom8.resolve()),
                str(args.rome.resolve()),
                str(first_game),
                "--raw-slices",
                args.raw_slices,
            ]
        )
        for game in args.games:
            command = [
                sys.executable,
                compare_script,
                str(executable),
                str(args.rom8.resolve()),
                str(args.rome.resolve()),
                str(game.resolve()),
                "--frames",
                args.frames,
            ]
            if args.story:
                command.append("--story")
            run(command)

        environment = os.environ.copy()
        environment["GAM4980_SMOKE_FRAMES"] = "1000"
        environment["GAM4980_SMOKE_IRAM_RESIDENT"] = "1"
        activity = run(
            [
                str(executable),
                str(args.rom8.resolve()),
                str(args.rome.resolve()),
                str(first_game),
            ],
            environment=environment,
            echo_output=False,
        )
        match = re.search(
            r"^iram exec calls=(\d+) instructions=(\d+)",
            activity,
            flags=re.MULTILINE,
        )
        if match is None or int(match.group(1)) == 0 or int(match.group(2)) == 0:
            raise SystemExit(
                "resident/reference comparison was vacuous: no IRAM C "
                "engine activity was recorded"
            )
        print(
            "host IRAM C equivalence passed: "
            f"calls={match.group(1)}, instructions={match.group(2)}"
        )
        environment["GAM4980_DISABLE_PERFORMANCE_DEBUG"] = "1"
        disabled_activity = run(
            [
                str(executable),
                str(args.rom8.resolve()),
                str(args.rome.resolve()),
                str(first_game),
            ],
            environment=environment,
            echo_output=False,
        )
        disabled_match = re.search(
            r"^iram exec calls=(\d+) instructions=(\d+)",
            disabled_activity,
            flags=re.MULTILINE,
        )
        if (
            disabled_match is None
            or int(disabled_match.group(1)) == 0
            or int(disabled_match.group(2)) == 0
        ):
            raise SystemExit(
                "debug-off diagnostic check was vacuous: no IRAM engine "
                "activity was recorded"
            )
        print(
            "host IRAM diagnostics debug-off gate passed: "
            f"calls={disabled_match.group(1)}"
        )

        # Splitting a guest frame for true-device key polling must not alter
        # guest-visible execution when the callback itself is observational.
        # Exercise the public callback path and compare both framebuffer and
        # CPU state against the unsliced run.
        poll_environment = os.environ.copy()
        poll_environment["GAM4980_SMOKE_FRAMES"] = "100"
        poll_environment["GAM4980_SMOKE_IRAM_RESIDENT"] = "1"
        control = run(
            [
                str(executable),
                str(args.rom8.resolve()),
                str(args.rome.resolve()),
                str(first_game),
            ],
            environment=poll_environment,
            echo_output=False,
        )
        poll_environment["GAM4980_SMOKE_RUNTIME_POLL"] = "1"
        polled = run(
            [
                str(executable),
                str(args.rom8.resolve()),
                str(args.rome.resolve()),
                str(first_game),
            ],
            environment=poll_environment,
            echo_output=False,
        )
        poll_match = re.search(r"^runtime poll calls=(\d+)", polled, re.M)
        state_pattern = re.compile(
            r"^(?:core initialized:.*|debug cpu pc=.*)$", re.MULTILINE
        )
        if (
            poll_match is None
            or int(poll_match.group(1)) <= 100
            or state_pattern.findall(control) != state_pattern.findall(polled)
        ):
            raise SystemExit(
                "runtime poll slicing changed guest state or did not split "
                "the guest frame"
            )
        print(
            "runtime input-poll slicing equivalence passed: "
            f"callbacks={poll_match.group(1)}"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
