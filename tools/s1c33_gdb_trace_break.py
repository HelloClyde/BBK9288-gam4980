#!/usr/bin/env python3
"""Trace repeated hits of one S1C33 breakpoint through QEMU GDB remote."""

from __future__ import annotations

import argparse

from s1c33_gdb_break import Remote


def registers(packet: str) -> list[int]:
    raw = bytes.fromhex(packet)
    return [int.from_bytes(raw[index:index + 4], "little")
            for index in range(0, len(raw) - 3, 4)]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("address", type=lambda value: int(value, 0))
    parser.add_argument("--count", type=int, default=100)
    parser.add_argument("--tail", type=int, default=32)
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=1234)
    parser.add_argument("--timeout", type=float, default=300.0)
    args = parser.parse_args()

    remote = Remote(args.host, args.port, args.timeout)
    spec = f"{args.address:x},2"
    try:
        print("supported=" + remote.command("qSupported:multiprocess+"),
              flush=True)
        history: list[str] = []
        for index in range(args.count):
            if remote.command("Z0," + spec) != "OK":
                raise RuntimeError("cannot set breakpoint")
            stopped = remote.command("c")
            if stopped.startswith(("W", "X")):
                print(f"exit[{index}]={stopped}", flush=True)
                print("\n".join(history), flush=True)
                return
            values = registers(remote.command("g"))
            shown = " ".join(
                f"r{reg}={values[reg]:08x}"
                for reg in (4, 5, 6, 7, 8, 9, 10, 12, 13)
                if reg < len(values)
            )
            line = f"hit[{index}] {shown}"
            history.append(line)
            history = history[-args.tail:]
            if args.verbose:
                print(line, flush=True)
            remote.command("z0," + spec)
            stepped = remote.command("s")
            if stepped.startswith(("W", "X")):
                print(f"step_exit[{index}]={stepped}", flush=True)
                print("\n".join(history), flush=True)
                return
        print("limit_reached", flush=True)
        print("\n".join(history), flush=True)
    finally:
        remote.close()


if __name__ == "__main__":
    main()
