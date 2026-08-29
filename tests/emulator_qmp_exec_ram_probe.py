#!/usr/bin/env python3
"""Run the writable/executable RAM probe in the BBK 9288 emulator."""

from __future__ import annotations

import argparse
from pathlib import Path
import time

from emulator_qmp_smoke import QmpClient


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=4444)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--boot-wait", type=float, default=12.0)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    qmp = QmpClient(args.host, args.port)
    try:
        time.sleep(args.boot_wait)
        qmp.key("ret")
        time.sleep(1.0)
        qmp.key("esc")
        time.sleep(1.0)
        qmp.key("f5")
        time.sleep(2.0)
        qmp.key("9", hold=1.0)
        time.sleep(2.0)
        for _ in range(2):
            qmp.key("down")
            time.sleep(0.2)
        qmp.capture(args.output / "01-probe-selected.ppm")

        qmp.key("ret")
        time.sleep(2.0)
        qmp.capture(args.output / "02-before-execution.ppm")
        qmp.key("ret")
        time.sleep(3.0)
        qmp.capture(args.output / "03-result.ppm")
    finally:
        qmp.close()


if __name__ == "__main__":
    main()
