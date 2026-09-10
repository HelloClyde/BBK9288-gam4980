#!/usr/bin/env python3
"""Read validation state from the isolated native-recompiler QEMU instance.

No input is sent and no state is changed unless --keys/--stop is requested.
The default port deliberately differs from the user's interactive emulator.
"""
from pathlib import Path
import argparse
import json
import re
import struct
import time

from emulator_qmp_smoke import QmpClient


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", type=int, default=6172)
    ap.add_argument("--map", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--keys", nargs="*", default=[])
    ap.add_argument("--wait", type=float, default=0)
    ap.add_argument("--stop", action="store_true")
    ap.add_argument("--boot", action="store_true")
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    symbols = {}
    for line in args.map.read_text(encoding="utf-8").splitlines():
        match = re.fullmatch(r"\s*([0-9a-f]+)\s+[0-9a-f]+\s+[0-9a-f]+\s+\d+\s+(\w+)\s*", line)
        if match:
            symbols[match[2]] = int(match[1], 16)
    q = QmpClient("127.0.0.1", args.port)
    try:
        if args.boot:
            time.sleep(10)
            for key in ("ret", "esc", "f5", "9", "k"):
                q.key(key, hold=1.0)
                time.sleep(1.0)
        for key in args.keys:
            q.key(key)
            time.sleep(.7)
        time.sleep(args.wait)
        if args.stop:
            q.command("stop")
        # Check RAM against this build, not only the NAND-installed filename.
        # A missed Enter event can leave another desktop image at 0x02700000.
        exe = args.map.with_suffix(".exe")
        if exe.is_file():
            payload = exe.read_bytes()
            code_offset = struct.unpack_from("<I", payload, 28)[0]
            path = (args.output / "loaded-entry.bin").resolve()
            q.command("human-monitor-command", {"command-line":
                f"pmemsave 0x2700000 0x40 {path.as_posix()}"})
            if path.read_bytes() != payload[code_offset:code_offset+64]:
                raise RuntimeError("The current KF2 has not started: RAM entry differs from the supplied build")
        registers = q.command("human-monitor-command", {"command-line": "info registers"})
        print(registers)
        (args.output / "registers.txt").write_text(registers, encoding="utf-8")
        image = args.output / "screen.ppm"
        q.capture(image)
        from PIL import Image
        Image.open(image).save(image.with_suffix(".png"))
        # A correctly drawn menu can remain on LCD after an unhandled IRQ.
        # Never count that stale picture as a successful gameplay check.
        pc = re.search(r"pc=0x([0-9a-f]+)", registers)
        if pc and int(pc[1], 16) in (0x020f5e68, 0x020f5e6c, 0x020f5e70, 0x020f5e74):
            raise RuntimeError("Native app is in the V1.5 default trap loop; screenshot is stale")
        for symbol in ("c6502_native_state", "c6502_bridge_regs"):
            print(symbol, q.command("human-monitor-command", {
                "command-line": f"xp /18wx 0x{symbols[symbol]:x}"}))
        if "c6502_validation_keys" in symbols:
            print("key events", q.command("human-monitor-command", {
                "command-line": f"xp /128wx 0x{symbols['c6502_validation_keys']:x}"}))
        if "c6502_validation_trace" in symbols:
            path = (args.output / "calls.bin").resolve()
            q.command("human-monitor-command", {"command-line":
                f"pmemsave 0x{symbols['c6502_validation_trace']:x} 0x1400 {path.as_posix()}"})
            header = Path("build/9288/c6502-native-runtime/c6502_native_bridge_ids.h")
            names = {int(value): name for name, value in re.findall(
                r"C6502_BRIDGE_c6502_(\w+) = (\d+)", header.read_text())}
            raw = path.read_bytes()
            records = []
            for index in range(128):
                row = struct.unpack_from("<10I", raw, index * 40)
                if not row[0]:
                    break
                records.append(dict(index=index, api=names[row[0]],
                    a=row[1], sp=row[2], stack=struct.pack("<4I", *row[3:7]).hex(),
                    bank5=row[7], bank9=row[8], oper1=row[9]))
            (args.output / "calls.json").write_text(json.dumps(records, indent=2), encoding="utf-8")
            print(json.dumps(records[:42], indent=2))
    finally:
        q.close()


if __name__ == "__main__":
    main()
