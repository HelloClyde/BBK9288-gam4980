"""Compare identical target probe suites; exclude restored system IRAM code."""
from collections import Counter
from pathlib import Path
import json
import re
import subprocess

objdump = 'build/llvm-s1c33-host/bin/llvm-objdump.exe'
def analyze(elf, trace):
    dis = subprocess.check_output([objdump, '-d', '--section=.iram', elf], text=True)
    commits = set()
    exit_pc = None
    for line in dis.splitlines():
        if re.search(r'\badd\s+%r11, 1\s*$', line):
            commits.add(int(line.split(':')[0].strip(), 16))
        if re.search(r'\bpopn\s+%r3\s*$', line):
            exit_pc = int(line.split(':')[0].strip(), 16) + 2
    assert len(commits) == 4 and exit_pc is not None
    active = False
    count = Counter()
    calls = 0
    for line in Path(trace).open():
        m = re.search(r'Trace \d+:.*?\[[0-9a-fA-F]+/([0-9a-fA-F]+)/[0-9a-fA-F]+/([0-9a-fA-F]+)\]',line)
        if not m:
            continue
        pc = int(m[1],16)
        if pc == 0x800:
            assert not active, (calls, hex(exit_pc))
            active = True
            calls += 1
        if active:
            assert int(m[2],16) & 0x1ff == 1
            count[pc] += 1
        if pc == exit_pc:
            active = False
            if calls == 524:
                break
    assert not active
    assert calls == 524, calls # 12 individual cases + two 256-input sweeps
    guest = sum(count[pc] for pc in commits)
    return dict(calls=calls, host_instructions=sum(count.values()),
                guest_instructions=guest, ratio=sum(count.values())/guest)

baseline = analyze('build/iram-super-equivalence/IRAM_SUPER_EQ.elf',
                   'build/emulator-exec-base-eq/iram.log')
optimized = analyze('build/exec-tail-eq.elf','build/emulator-exec-tail-eq/iram.log')
assert baseline['guest_instructions'] == optimized['guest_instructions']
print(json.dumps(dict(baseline=baseline, optimized=optimized,
    host_reduction_percent=100*(1-optimized['host_instructions']/baseline['host_instructions'])),indent=2))
