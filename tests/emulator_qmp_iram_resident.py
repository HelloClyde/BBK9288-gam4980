#!/usr/bin/env python3
"""Exercise and inspect the session-resident IRAM engine through QMP."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import time

from emulator_qmp_smoke import QmpClient


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


def optional_map_symbol(path: Path, symbol: str) -> int | None:
    try:
        return map_symbol(path, symbol)
    except RuntimeError:
        return None


def read_word(qmp: QmpClient, address: int) -> int:
    output = str(qmp.command(
        "human-monitor-command",
        {"command-line": f"xp /1wx 0x{address:08x}"},
    ))
    values = re.findall(r"0x([0-9a-fA-F]{8})", output)
    if not values:
        raise RuntimeError(f"cannot parse memory at 0x{address:08x}: {output}")
    return int(values[-1], 16)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=6172)
    parser.add_argument("--boot-wait", type=float, default=2.0)
    parser.add_argument("--game-wait", type=float, default=35.0)
    parser.add_argument("--selector-wait", type=float, default=4.0)
    parser.add_argument(
        "--advance-story", action="store_true",
        help="enter the default new game and capture the opening story",
    )
    parser.add_argument(
        "--from-home", action="store_true",
        help="start with the firmware desktop already visible",
    )
    parser.add_argument(
        "--from-selector", action="store_true",
        help="start with GAME.GAM already highlighted in the file selector",
    )
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--map", type=Path, default=Path("build/9288/GAM4980.map")
    )
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    counters = map_symbol(args.map, "s6502_iram_exec_calls")
    session_state = map_symbol(args.map, "g_iram_size")
    bare_state = map_symbol(args.map, "g_status")
    selector_done = map_symbol(args.map, "g_selector_done")
    rom_suspends = optional_map_symbol(args.map, "g_rom_suspends")

    qmp = QmpClient(args.host, args.port)
    try:
        if not args.from_selector:
            if not args.from_home:
                time.sleep(args.boot_wait)
                # A cold image opens Calendar below its clock prompt.  Return
                # to Start first; only the application selection below uses
                # the launcher's stable direct shortcuts.
                qmp.key("ret")
                time.sleep(1.0)
                qmp.key("esc")
                time.sleep(1.0)
                qmp.key("f5")
                time.sleep(2.0)
            # The 9288 launcher uses digits for category pages and the letter
            # printed beside each icon as its direct application shortcut.
            # GAM4980 is K on page 9; cursor navigation is state-dependent.
            qmp.key("9", hold=0.3)
            time.sleep(1.0)
            qmp.key("k", hold=0.5)
            time.sleep(args.selector_wait)
        for _ in range(3):
            qmp.key("ret", hold=1.0)
            time.sleep(1.0)
            if read_word(qmp, selector_done):
                break
            time.sleep(2.0)
        else:
            raise RuntimeError("file selector did not accept GAME.GAM")
        time.sleep(args.game_wait)
        for _ in range(100):
            if (
                read_word(qmp, bare_state + 4) == 1
                and read_word(qmp, session_state + 4) == 1
            ):
                break
            time.sleep(0.05)
        else:
            raise RuntimeError(
                "resident engine was not active after launching GAME.GAM"
            )
        qmp.capture(args.output / "resident-active.ppm")

        if args.advance_story:
            rom_suspends_before_story = (
                read_word(qmp, rom_suspends)
                if rom_suspends is not None else None
            )
            qmp.key("ret", hold=0.8)
            time.sleep(3.0)
            qmp.key("ret", hold=0.8)
            time.sleep(15.0)
            qmp.capture(args.output / "resident-game-world.ppm")
            qmp.key("ret", hold=0.8)
            time.sleep(8.0)
            qmp.capture(args.output / "resident-story-dialog.ppm")
            if (
                read_word(qmp, bare_state + 4) != 1
                or read_word(qmp, session_state + 4) != 1
            ):
                raise RuntimeError(
                    "IRAM resident/bare session stopped during story advance"
                )
            if rom_suspends_before_story is not None:
                rom_suspends_after_story = read_word(qmp, rom_suspends)
                if rom_suspends_after_story != rom_suspends_before_story:
                    raise RuntimeError(
                        "story advance entered the filesystem gateway: "
                        f"bare ROM suspends {rom_suspends_before_story} -> "
                        f"{rom_suspends_after_story}"
                    )

        print("[resident-active]")
        print(qmp.command(
            "human-monitor-command",
            {"command-line": f"xp /9wx 0x{counters:08x}"},
        ))
        print(qmp.command(
            "human-monitor-command",
            {"command-line": f"xp /8wx 0x{session_state:08x}"},
        ))
        print(qmp.command(
            "human-monitor-command",
            {"command-line": f"xp /4wx 0x{bare_state:08x}"},
        ))
        for address in (0x0004822C, 0x0004823C, 0x0004824C, 0x0004825C):
            print(qmp.command(
                "human-monitor-command",
                {"command-line": f"xp /1bx 0x{address:08x}"},
            ))
        print(qmp.command(
            "human-monitor-command",
            {"command-line": "xp /1bx 0x00003fca"},
        ))

        # F12 is the emulator's verified alias for the 9288 physical EXIT
        # matrix position.  PC Escape is a different ordinary guest key.
        # The bare loop samples the physical matrix directly; a synthetic QMP
        # pulse can land wholly between two scans on a busy emulator host.
        # Retry a bounded number of long presses, just as a user would press
        # EXIT again, and stop immediately once both owners report restored.
        for _ in range(3):
            qmp.key("f12", hold=1.8)
            time.sleep(4.0)
            if (
                read_word(qmp, session_state + 4) == 2
                and read_word(qmp, bare_state + 4) == 0
            ):
                break
        time.sleep(2.0)
        qmp.capture(args.output / "resident-restored.ppm")
        print("[after-exit]")
        print(qmp.command(
            "human-monitor-command",
            {"command-line": f"xp /8wx 0x{session_state:08x}"},
        ))
        if (
            read_word(qmp, session_state + 4) != 2
            or read_word(qmp, bare_state + 4) != 0
        ):
            raise RuntimeError("IRAM/bare state was not restored on exit")
        print(qmp.command(
            "human-monitor-command", {"command-line": "info registers"}
        ))
    finally:
        qmp.close()


if __name__ == "__main__":
    main()
