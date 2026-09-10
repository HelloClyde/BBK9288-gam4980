#!/usr/bin/env python3
"""Set one QEMU GDB breakpoint and report the first stop.

This deliberately implements only the small remote-protocol subset needed by
the 9288 emulator.  It keeps native startup debugging independent of a host
GDB build with S1C33 register support.
"""

from __future__ import annotations

import argparse
import socket


def checksum(payload: bytes) -> bytes:
    return f"{sum(payload) & 0xff:02x}".encode("ascii")


class Remote:
    def __init__(self, host: str, port: int, timeout: float) -> None:
        self.socket = socket.create_connection((host, port), timeout=10.0)
        self.socket.settimeout(timeout)

    def close(self) -> None:
        self.socket.close()

    def receive(self) -> str:
        while self.socket.recv(1) != b"$":
            pass
        payload = bytearray()
        while True:
            byte = self.socket.recv(1)
            if byte == b"#":
                break
            if byte == b"}":
                escaped = self.socket.recv(1)
                payload.append(escaped[0] ^ 0x20)
            else:
                payload.extend(byte)
        received_sum = self.socket.recv(2)
        if checksum(payload) != received_sum.lower():
            self.socket.sendall(b"-")
            raise RuntimeError("GDB remote checksum mismatch")
        self.socket.sendall(b"+")
        return payload.decode("ascii", errors="replace")

    def command(self, text: str) -> str:
        payload = text.encode("ascii")
        self.socket.sendall(b"$" + payload + b"#" + checksum(payload))
        acknowledgement = self.socket.recv(1)
        if acknowledgement != b"+":
            raise RuntimeError(f"GDB remote rejected {text!r}: {acknowledgement!r}")
        return self.receive()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("address", type=lambda value: int(value, 0))
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=1234)
    parser.add_argument("--timeout", type=float, default=300.0)
    args = parser.parse_args()

    remote = Remote(args.host, args.port, args.timeout)
    try:
        supported = remote.command("qSupported:multiprocess+")
        print("supported=" + supported, flush=True)
        result = remote.command(f"Z0,{args.address:x},2")
        if result != "OK":
            raise RuntimeError(f"cannot set breakpoint: {result}")
        print(f"breakpoint=0x{args.address:08x}", flush=True)
        stopped = remote.command("c")
        print("stopped=" + stopped, flush=True)
        registers = remote.command("g")
        print("registers=" + registers, flush=True)
    finally:
        remote.close()


if __name__ == "__main__":
    main()
