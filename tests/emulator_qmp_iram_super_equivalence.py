#!/usr/bin/env python3
"""Read and assert the true-S1C33 shadow-super equivalence report via QMP."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))

from emulator_qmp_smoke import QmpClient


REPORT_WORDS = 84
REPORT_MAGIC = 0x53555045
IRAM_RESTORED = 2


def map_symbol(path: Path, symbol: str) -> int:
    pattern = re.compile(
        rf"^\s*([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+"
        rf"[0-9a-fA-F]+\s+\d+\s+{re.escape(symbol)}\s*$"
    )
    for line in path.read_text(encoding="utf-8").splitlines():
        match = pattern.match(line)
        if match:
            return int(match.group(1), 16)
    raise RuntimeError(f"missing map symbol: {symbol}")


def read_words(qmp: QmpClient, address: int, count: int) -> list[int]:
    output = str(qmp.command(
        "human-monitor-command",
        {"command-line": f"xp /{count}wx 0x{address:08x}"},
    ))
    values: list[int] = []
    for line in output.splitlines():
        if ":" not in line:
            continue
        payload = line.split(":", 1)[1]
        values.extend(
            int(value, 16)
            for value in re.findall(r"0x([0-9a-fA-F]{8})", payload)
        )
    if len(values) < count:
        raise RuntimeError(
            f"cannot parse {count} words at 0x{address:08x}: {output}"
        )
    return values[:count]


def read_completed(qmp: QmpClient, address: int) -> int:
    return read_words(qmp, address + 4, 1)[0]


def wait_completed(
    qmp: QmpClient, address: int, timeout: float
) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if read_completed(qmp, address) == 1:
            return True
        time.sleep(0.1)
    return False


def wait_completed_dismissing_prompts(
    qmp: QmpClient, address: int, timeout: float
) -> bool:
    """Wait for the probe while dismissing only its finite startup prompts."""
    deadline = time.monotonic() + timeout
    next_key = time.monotonic()
    keys_sent = 0
    while time.monotonic() < deadline:
        if read_completed(qmp, address) == 1:
            return True
        now = time.monotonic()
        # A pristine image can show the RTC-change prompt before the probe's
        # own "Press OK" message.  Two Return presses are therefore expected;
        # allow one spare without creating an unbounded key injector.
        if keys_sent < 3 and now >= next_key:
            qmp.key("ret")
            keys_sent += 1
            next_key = now + 1.0
        time.sleep(0.1)
    return False


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=16173)
    parser.add_argument("--boot-wait", type=float, default=12.0)
    parser.add_argument("--timeout", type=float, default=20.0)
    parser.add_argument("--map", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--keep-running", action="store_true",
        help="leave the isolated emulator running after the assertion",
    )
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    report_address = map_symbol(args.map, "g_super_equiv_report")

    qmp = QmpClient(args.host, args.port)
    try:
        time.sleep(args.boot_wait)
        if not wait_completed(qmp, report_address, 1.0):
            if not wait_completed_dismissing_prompts(
                qmp, report_address, args.timeout
            ):
                raise RuntimeError("target probe did not publish a result")
        words = read_words(qmp, report_address, REPORT_WORDS)
        qmp.capture(args.output / "iram-super-equivalence.ppm")

        magic, completed, passed, fail_mask = words[:4]
        cases_run, cases_passed, iram_status, iram_size = words[4:8]
        hits = words[8:13]
        decimal_fallbacks = words[13]
        errors: list[str] = []
        if magic != REPORT_MAGIC:
            errors.append(f"magic={magic:08x}")
        if completed != 1 or passed != 1 or fail_mask != 0:
            errors.append(
                f"completed={completed} passed={passed} fail={fail_mask:08x}"
            )
        if cases_run != 10 or cases_passed != 10:
            errors.append(f"cases={cases_passed}/{cases_run}")
        if iram_status != IRAM_RESTORED or not (0 < iram_size <= 0x16C8):
            errors.append(
                f"iram_status={iram_status:08x} iram_size={iram_size}"
            )
        if hits != [1, 1, 1, 1, 1]:
            errors.append(f"super_hits={hits}")
        if decimal_fallbacks != 1:
            errors.append(f"decimal_fallbacks={decimal_fallbacks}")

        expected_pc = [
            0x4048, 0x4048, 0x4050, 0x4050, 0x404D, 0x4040,
            0x4101, 0x4045, 0x4045,
            0x7C4A,
        ]
        expected_cycles = [10, 10, 27, 27, 20, 0, 4, 6, 6, 35]
        expected_instructions = [4, 4, 10, 10, 7, 0, 1, 2, 2, 10]
        expected_ac = [
            0x80, 0x00, 0x14, 0x0F, 0x80, 0x5A, 0x80, 0x7F, 0x00,
            0x00,
        ]
        expected_status = [
            0xA5, 0x27, 0x35, 0x34, 0xE4, 0x2D, 0xA5, 0x25, 0x27,
            0x26,
        ]
        observations = words[14:]
        for index in range(10):
            case = observations[index * 7:(index + 1) * 7]
            if len(case) != 7:
                errors.append(f"case{index}=truncated")
                continue
            pc, cycles, instructions, exit_reason, ac, status, result = case
            if (
                pc != expected_pc[index]
                or cycles != expected_cycles[index]
                or instructions != expected_instructions[index]
                or exit_reason != 1
                or ac != expected_ac[index]
                or status != expected_status[index]
                or result != expected_cycles[index]
            ):
                errors.append(
                    f"case{index}: pc={pc:04x} cycles={cycles} "
                    f"instructions={instructions} exit={exit_reason} "
                    f"ac={ac:02x} status={status:02x} "
                    f"result={result}"
                )

        print("[9288 IRAM SUPER EQUIVALENCE 1]")
        print(f"report_address={report_address:08X}")
        print(f"iram_size={iram_size}")
        print(f"cases_passed={cases_passed}")
        print("super_hits=" + ",".join(str(value) for value in hits))
        print(f"decimal_fallbacks={decimal_fallbacks}")
        print("result=" + ("PASS" if not errors else "FAIL"))
        print("[END]")
        if errors:
            raise RuntimeError("; ".join(errors))

        qmp.key("ret")
        time.sleep(2.0)
        qmp.capture(args.output / "desktop-restored.ppm")
    finally:
        if not args.keep_running:
            try:
                qmp.command("quit")
            except (OSError, RuntimeError):
                pass
        qmp.close()


if __name__ == "__main__":
    main()
