#!/usr/bin/env python3
"""Run the short IRAM benchmark variant in the BBK 9288 emulator."""

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
    parser.add_argument("--port", type=int, default=4445)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--boot-wait", type=float, default=12.0)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    qmp = QmpClient(args.host, args.port)
    try:
        time.sleep(args.boot_wait)
        qmp.capture(args.output / "00-boot.ppm")
        qmp.key("ret")
        time.sleep(2.0)
        qmp.capture(args.output / "01-warning.ppm")
        qmp.key("ret")
        time.sleep(2.0)
        qmp.capture(args.output / "02-external-ready.ppm")
        qmp.key("ret")
        time.sleep(2.0)
        qmp.capture(args.output / "03-iram-ready.ppm")
        qmp.key("ret")
        time.sleep(3.0)
        qmp.capture(args.output / "04-result.ppm")
    finally:
        try:
            qmp.command("quit")
        finally:
            qmp.close()


if __name__ == "__main__":
    main()
