#!/usr/bin/env python3
"""Minimal GDB-remote probe for the 9288 launcher's physical key callback."""

from __future__ import annotations

import argparse
import socket
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from emulator_qmp_smoke import QmpClient


class Remote:
    def __init__(self, host: str, port: int) -> None:
        self.sock = socket.create_connection((host, port), timeout=10.0)
        self.sock.settimeout(10.0)

    def close(self) -> None:
        self.sock.close()

    def _recv_packet(self) -> str:
        while True:
            byte = self.sock.recv(1)
            if not byte:
                raise RuntimeError("GDB remote closed")
            if byte == b"$":
                break
        payload = bytearray()
        while True:
            byte = self.sock.recv(1)
            if byte == b"#":
                break
            payload.extend(byte)
        checksum = self.sock.recv(2)
        expected = sum(payload) & 0xFF
        if int(checksum, 16) != expected:
            self.sock.sendall(b"-")
            raise RuntimeError("bad GDB checksum")
        self.sock.sendall(b"+")
        return payload.decode("ascii")

    def command(self, payload: str) -> str:
        raw = payload.encode("ascii")
        frame = b"$" + raw + b"#" + f"{sum(raw) & 0xFF:02x}".encode()
        self.sock.sendall(frame)
        ack = self.sock.recv(1)
        if ack != b"+":
            raise RuntimeError(f"GDB command was not acknowledged: {ack!r}")
        return self._recv_packet()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--gdb-port", type=int, required=True)
    parser.add_argument("--qmp-port", type=int, required=True)
    parser.add_argument("--address", type=lambda value: int(value, 0), required=True)
    parser.add_argument("--key", default="1")
    args = parser.parse_args()

    remote = Remote("127.0.0.1", args.gdb_port)
    try:
        print("supported:", remote.command("qSupported:multiprocess+"))
        print("stop:", remote.command("?"))
        # Firmware code is backed by ROM.  QEMU may acknowledge a software
        # breakpoint even though that storage cannot be patched, so prefer a
        # hardware breakpoint and only fall back when the target rejects it.
        response = remote.command(f"Z1,{args.address:x},2")
        if response != "OK":
            response = remote.command(f"Z0,{args.address:x},2")
        print("breakpoint:", response)
        if response != "OK":
            raise RuntimeError("QEMU rejected both software and hardware breakpoints")

        def inject() -> None:
            time.sleep(0.5)
            qmp = QmpClient("127.0.0.1", args.qmp_port)
            try:
                qmp.key(args.key, hold=0.5)
            finally:
                qmp.close()

        thread = threading.Thread(target=inject, daemon=True)
        thread.start()
        stopped = remote.command("c")
        thread.join(timeout=2.0)
        print("stopped:", stopped)
        print("registers:", remote.command("g"))
        print("stack:", remote.command("m225c000,200"))
    finally:
        remote.close()


if __name__ == "__main__":
    main()
