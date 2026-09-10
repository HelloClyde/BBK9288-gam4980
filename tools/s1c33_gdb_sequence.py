#!/usr/bin/env python3
"""Stop at an ordered list of S1C33 addresses in one QEMU session."""

from __future__ import annotations

import argparse

from s1c33_gdb_break import Remote


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("addresses", nargs="+", type=lambda value: int(value, 0))
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=1234)
    parser.add_argument("--timeout", type=float, default=300.0)
    args = parser.parse_args()

    remote = Remote(args.host, args.port, args.timeout)
    try:
        print("supported=" + remote.command("qSupported:multiprocess+"), flush=True)
        for address in args.addresses:
            spec = f"{address:x},2"
            result = remote.command("Z0," + spec)
            if result != "OK":
                raise RuntimeError(f"cannot set breakpoint 0x{address:08x}: {result}")
            print(f"waiting=0x{address:08x}", flush=True)
            stopped = remote.command("c")
            print(f"stopped=0x{address:08x}:{stopped}", flush=True)
            if stopped.startswith("W") or stopped.startswith("X"):
                return
            print("registers=" + remote.command("g"), flush=True)
            print("remove=" + remote.command("z0," + spec), flush=True)
    finally:
        remote.close()


if __name__ == "__main__":
    main()
