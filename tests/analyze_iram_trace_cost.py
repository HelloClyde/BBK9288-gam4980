"""Attribute the saved SEMANTIC-ALL single-instruction traces by code region.

Ranges are specific to the ELF identified in semantic-all-instruction-ratio.md.
Handler bodies include inline addressing, flags and operand fetch, not just ALU.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import re

REGIONS = {
    "entry": [(0x800,0x840)],
    "code_mapping": [(0x840,0x864),(0x87e,0x898),(0x8ea,0x8fc),
                     (0x92e,0x940),(0x9a0,0x9ce)],
    "opcode_dispatch": [(0x864,0x87e)],
    "shared_nz": [(0x898,0x8a4),(0x8b8,0x8e8)],
    "completion_control": [(0x8a4,0x8b8),(0x8e8,0x8ea),(0x8fc,0x92e)],
    "exit_writeback": [(0x940,0x9a0)],
    "special_store_post": [(0x9ce,0x9fe)],
    "handler_body_including_inline_helpers": [(0x9fe,0x1c6a)],
}
COMMIT = {0x8a4,0x8b4,0x8e8,0x8fe}
TRACE = re.compile(r"Trace \d+:.*?\[[0-9a-fA-F]+/([0-9a-fA-F]+)/[0-9a-fA-F]+/([0-9a-fA-F]+)\]")

def analyze(path):
    counts, pcs = Counter(), Counter()
    table=Path("build/emulator-ratio/baseline.iram.bin").read_bytes()
    targets={}
    for opcode in range(256):
        offset=0x1c6c-0x800+opcode*2
        target=int.from_bytes(table[offset:offset+2],"little")
        targets.setdefault(target,[]).append(f"{opcode:02X}")
    last_pc=None
    handler="unknown"
    reload_handlers=Counter()
    bursts = []
    active = None
    for line in path.open(errors="replace"):
        match = TRACE.search(line)
        if not match: continue
        pc, flags = (int(value,16) for value in match.groups())
        if flags & 511 != 1: raise ValueError("not one instruction per TB")
        if not 0x800 <= pc < 0x1e7c: continue
        if last_pc in (0x870,0x896):
            handler="/".join(targets.get(pc,[hex(pc)]))
        if pc==0x8e8: reload_handlers[handler]+=1
        last_pc=pc
        pcs[pc] += 1
        group = next((name for name,ranges in REGIONS.items()
                      if any(lo <= pc < hi for lo,hi in ranges)), None)
        if group is None: raise ValueError(f"unclassified executed PC {pc:x}")
        counts[group] += 1
        if pc == 0x800: active = [0,0]
        if active is not None:
            active[0] += 1
            active[1] += pc in COMMIT
            if pc == 0x99e:
                bursts.append(active)
                active = None
    total, guest = sum(counts.values()), sum(pcs[p] for p in COMMIT)
    return dict(sample=path.name, host=total, guest=guest,
                categories=dict(counts), completed_bursts=len(bursts),
                reload_handlers=dict(reload_handlers),
                sites={name:pcs[pc] for name,pc in {
                    "entry":0x800,"map_fetch":0x840,"normal_finish":0x8a4,
                    "remap_finish":0x8b4,"reload_finish":0x8e8,
                    "control_finish":0x8fe,"cross_operand":0x9a0}.items()},
                zero_bursts=sum(g==0 for h,g in bursts),
                zero_burst_host=sum(h for h,g in bursts if g==0),
                completed_burst_guest=sum(g for h,g in bursts),
                top_pcs=[dict(pc=hex(pc),count=n) for pc,n in pcs.most_common(15)])

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("traces",nargs="+",type=Path)
    parser.add_argument("--output",required=True,type=Path)
    args=parser.parse_args()
    results=[analyze(path) for path in args.traces]
    total=Counter()
    for result in results: total.update(result["categories"])
    sites=Counter()
    for result in results: sites.update(result["sites"])
    reloads=Counter()
    for result in results: reloads.update(result["reload_handlers"])
    host=sum(total.values()); guest=sum(r["guest"] for r in results)
    report=dict(samples=results,combined=dict(host=host,guest=guest,
        categories={name:dict(count=n,percent=100*n/host,per_guest=n/guest)
                    for name,n in total.items()},
        completed_bursts=sum(r["completed_bursts"] for r in results),
        zero_bursts=sum(r["zero_bursts"] for r in results),
        zero_burst_host=sum(r["zero_burst_host"] for r in results),sites=dict(sites),
        reload_handlers=dict(reloads.most_common())))
    args.output.write_text(json.dumps(report,indent=2))
    print(json.dumps(report["combined"],indent=2))

if __name__=="__main__": main()
