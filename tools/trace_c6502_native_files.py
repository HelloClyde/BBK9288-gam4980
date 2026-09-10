"""Trace native file adapters in the isolated test QEMU (never user ports)."""
import argparse
import json
from pathlib import Path
import re
import struct
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tests'))
from emulator_qmp_smoke import QmpClient
from s1c33_gdb_break import Remote


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--map', type=Path, required=True)
    ap.add_argument('--seconds', type=float, default=25)
    ap.add_argument('--keys', nargs='*', default=[])
    ap.add_argument('--functions', nargs='*', default=[])
    args = ap.parse_args()
    symbols = {}
    for line in args.map.read_text(encoding='utf-8').splitlines():
        m = re.fullmatch(r'\s*([0-9a-f]+)\s+[0-9a-f]+\s+[0-9a-f]+\s+\d+\s+(\w+)\s*', line)
        if m:
            symbols[m[2]] = int(m[1], 16)
    points = {address: name for name, address in symbols.items()
              if name.startswith('c6502_adapter_file')}
    points.update({symbols[name]: name for name in
        ('c6502_game_fn_0f2f6', 'c6502_game_fn_0f424')})
    points.update({symbols[name]: name for name in args.functions})
    q = QmpClient('127.0.0.1', 6172)
    r = Remote('127.0.0.1', 6173, 5)
    returns = {}
    def mem(addr, size):
        return bytes.fromhex(r.command(f'm{addr:x},{size:x}'))
    try:
        q.command('stop')
        for pc in points:
            assert r.command(f'Z0,{pc:x},2') == 'OK'
        print('ready', flush=True)
        start = time.monotonic()
        deadline = start + args.seconds
        events = [(start + 1 + i * 1.5 + (0 if down else .3), key, down)
                  for i, key in enumerate(args.keys) for down in (True, False)]
        first = True
        while time.monotonic() < deadline:
            if first or not q.command('query-status')['running']:
                first = False
                regs = struct.unpack('<21I', bytes.fromhex(r.command('g')))
                pc = regs[16]
                if pc in points or pc in returns:
                    name = returns.pop(pc) if pc in returns else points[pc]
                    row = dict(name=name, pc=hex(pc), a=regs[4], x=regs[5], y=regs[6],
                        p=regs[9], soft_sp=hex(regs[10]),
                        ram_base=hex(regs[14]), state_ram=mem(symbols['c6502_native_state'],4).hex(),
                        stack=mem(regs[14]+regs[10], 48).hex(),
                        zp=mem(regs[14]+0x20, 32).hex(),
                        error=mem(regs[14]+0x1937,1).hex(),
                        host_stack=mem(regs[18],64).hex())
                    if name.startswith('c6502_adapter_file'):
                        ret = int.from_bytes(mem(regs[18], 4), 'little')
                        returns[ret] = 'return:' + name
                        r.command(f'Z0,{ret:x},2')
                    print(json.dumps(row), flush=True)
                    r.command(f'z0,{pc:x},2')
                    r.command('s')
                    if pc in points:
                        r.command(f'Z0,{pc:x},2')
                q.command('cont')
            if events and time.monotonic() >= events[0][0]:
                _, key, down = events[0]
                try:
                    q.command('input-send-event', {'events': [{'type': 'key',
                        'data': {'down': down, 'key': {'type': 'qcode', 'data': key}}}]})
                    events.pop(0)
                except RuntimeError:
                    pass  # Breakpoint raced the input; process it then retry.
            time.sleep(.005)
    finally:
        q.command('stop')
        for pc in set(points) | set(returns):
            r.command(f'z0,{pc:x},2')
        r.close()
        q.close()


if __name__ == '__main__':
    main()
