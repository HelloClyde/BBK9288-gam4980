#!/usr/bin/env python3
"""Break once, then stop on the next write to an S1C33 memory address."""

from __future__ import annotations

import argparse

from s1c33_gdb_break import Remote


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("break_address", type=lambda value: int(value, 0))
    parser.add_argument("watch_address", type=lambda value: int(value, 0))
    parser.add_argument("--watch-size", type=int, default=1)
    parser.add_argument("--count", type=int, default=1)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=1234)
    parser.add_argument("--timeout", type=float, default=300.0)
    args = parser.parse_args()

    remote = Remote(args.host, args.port, args.timeout)
    breakpoint_spec = f"{args.break_address:x},2"
    watchpoint_spec = f"{args.watch_address:x},{args.watch_size}"
    try:
        print("supported=" + remote.command("qSupported:multiprocess+"), flush=True)
        if remote.command("Z0," + breakpoint_spec) != "OK":
            raise RuntimeError("cannot set initial breakpoint")
        print(f"waiting_break=0x{args.break_address:08x}", flush=True)
        print("break_stop=" + remote.command("c"), flush=True)
        print("break_registers=" + remote.command("g"), flush=True)
        print("remove_break=" + remote.command("z0," + breakpoint_spec), flush=True)
        if remote.command("Z2," + watchpoint_spec) != "OK":
            raise RuntimeError("cannot set write watchpoint")
        for index in range(args.count):
            print(
                f"waiting_write[{index}]=0x{args.watch_address:08x}",
                flush=True,
            )
            stopped = remote.command("c")
            print(f"write_stop[{index}]=" + stopped, flush=True)
            if stopped.startswith(("W", "X")):
                return
            print(
                f"write_registers[{index}]=" + remote.command("g"),
                flush=True,
            )
            print(
                f"watch_memory[{index}]=" + remote.command(
                    f"m{args.watch_address:x},{args.watch_size}"
                ),
                flush=True,
            )
    finally:
        remote.close()


if __name__ == "__main__":
    main()
