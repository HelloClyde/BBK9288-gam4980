"""Capture single-instruction QEMU traces; no EXE instrumentation required.

Denominator is IRAM completed guest instructions, NOT HLE-equivalent work.
One S1C33 16-bit instruction word (including EXT) is one translation step.
"""
import argparse
from pathlib import Path
import json
import re
import time
from collections import Counter
from emulator_qmp_smoke import QmpClient


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("output", type=Path)
    p.add_argument("--port", type=int, default=6280)
    p.add_argument("--seconds", type=float, default=2)
    p.add_argument("--disassembly", type=Path,
                   default=Path("build/9288/GAM4980.iram.dis.txt"))
    p.add_argument("--iram-binary", type=Path,
                   default=Path("build/9288/GAM4980.iram.bin"))
    args = p.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    q = QmpClient("127.0.0.1", args.port)
    def hmp(cmd):
        result = q.command("human-monitor-command", {"command-line": cmd})
        if result and str(result).strip():
            raise RuntimeError((cmd, result))
    q.command("stop")
    try:
        hmp("one-insn-per-tb on")
        hmp("logfile " + args.output.resolve().as_posix())
        hmp("log exec,nochain")
        q.command("cont")
        time.sleep(args.seconds)
        q.command("stop")
        hmp("log none")
        q.capture(args.output.with_suffix(".ppm"))
    finally:
        hmp("one-insn-per-tb off")
        q.command("cont")
        q.close()
    commits = set()
    for line in args.disassembly.read_text().splitlines():
        if re.search(r"\badd\s+%r11, 1\s*$", line):
            commits.add(int(line.split(":")[0].strip(),16))
    if len(commits) != 4:
        raise RuntimeError(("unexpected commit sites", commits))
    pcs = Counter()
    for line in args.output.open(errors="replace"):
        m = re.search(r"Trace \d+:.*?\[[0-9a-fA-F]+/([0-9a-fA-F]+)/[0-9a-fA-F]+/([0-9a-fA-F]+)\]",line)
        if m:
            if int(m[2],16) & 0x1ff != 1:
                raise RuntimeError("trace contains multi-instruction TB")
            pcs[int(m[1],16)] += 1
    guest = sum(pcs[pc] for pc in commits)
    iram_end = 0x800 + args.iram_binary.stat().st_size
    iram = sum(n for pc,n in pcs.items() if 0x800 <= pc < iram_end)
    result = dict(trace_steps=sum(pcs.values()), iram_host_steps=iram,
                  iram_guest_completed=guest,
                  iram_ratio=iram/guest if guest else None,
                  external_steps=sum(pcs.values())-iram,
                  iram_entries=pcs[0x800],
                  iram_end=hex(iram_end),
                  commit_pcs=[hex(pc) for pc in sorted(commits)],
                  note="IRAM-only ratio; EXT counts separately; excludes HLE guest equivalents")
    args.output.with_suffix(".json").write_text(json.dumps(result,indent=2))
    print(json.dumps(result,indent=2))


if __name__ == "__main__":
    main()
