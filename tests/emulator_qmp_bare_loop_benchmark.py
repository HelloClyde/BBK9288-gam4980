#!/usr/bin/env python3
"""Exercise the interrupt-off bare-loop probe in the BBK 9288 emulator."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))

from emulator_qmp_smoke import QmpClient


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=4455)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--boot-wait", type=float, default=12.0)
    parser.add_argument(
        "--exit-early",
        action="store_true",
        help="press the physical Exit mapping while the bare loop is active",
    )
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    qmp = QmpClient(args.host, args.port)
    samples: list[str] = []

    def sample(label: str) -> None:
        memory = qmp.command(
            "human-monitor-command",
            {"command-line": "xp /1bx 0x3fca"},
        )
        registers = qmp.command(
            "human-monitor-command",
            {"command-line": "info registers"},
        )
        samples.append(f"[{label}]\n{memory}\n{registers}\n")

    try:
        time.sleep(args.boot_wait)
        qmp.capture(args.output / "00-boot.ppm")
        qmp.key("ret")             # dismiss the RTC-change prompt
        time.sleep(2.0)            # firmware automatically launches Time.exe
        qmp.capture(args.output / "01-warning.ppm")
        sample("warning")
        qmp.key("ret")             # accept the risky-test warning
        time.sleep(3.0)
        qmp.capture(args.output / "02-bare-loop.ppm")
        sample("bare_loop")
        if args.exit_early:
            # The 9288's physical Exit is a raw matrix position with no
            # generic PC keycode.  The emulator intentionally exposes F12 as
            # its transport alias; Esc is a different row-0 key.
            qmp.key("f12", hold=0.3)
            time.sleep(3.0)
        else:
            time.sleep(9.0)
        qmp.capture(args.output / "03-result.ppm")
        sample("result")
        qmp.key("ret")             # close the result box and return to desktop
        time.sleep(3.0)
        qmp.capture(args.output / "04-desktop-restored.ppm")
        sample("desktop")
    finally:
        args.output.joinpath("memory-samples.txt").write_text(
            "\n".join(samples), encoding="utf-8"
        )
        try:
            qmp.command("quit")
        finally:
            qmp.close()


if __name__ == "__main__":
    main()
