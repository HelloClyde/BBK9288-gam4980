"""Run an isolated prepared NAND probe, optionally tracing only IRAM."""
import argparse
from pathlib import Path
import subprocess
import sys
import time
from emulator_qmp_smoke import QmpClient

p = argparse.ArgumentParser()
p.add_argument('--image', type=Path, required=True)
p.add_argument('--map', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--emulator', type=Path, required=True)
p.add_argument('--port', type=int, default=6280)
p.add_argument('--trace', action='store_true')
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
args = [str(a.emulator.resolve()), '-L', 'share', '-name', 'IRAM-EQ-ISOLATED',
        '-machine', 'bbk9288,nand-image='+a.image.resolve().as_posix()+',usb-connected=on',
        '-cpu', 'c33l05,exit-on-halt=off', '-rtc', 'base=localtime',
        '-display', 'none', '-qmp', f'tcp:127.0.0.1:{a.port},server=on,wait=off',
        '-serial', 'none', '-monitor', 'none', '-dfilter', '0x800..0x1ec7']
with (a.output/'stderr.log').open('w') as err:
    proc = subprocess.Popen(args, cwd=a.emulator.resolve().parent, stderr=err,
                            creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    try:
        time.sleep(5)
        q = QmpClient('127.0.0.1', a.port)
        for key in ('ret','esc','f5','9','k'):
            q.key(key, .2)
            time.sleep(.8)
        q.key('ret', .1)
        if a.trace:
            q.command('stop')
            for cmd in ('one-insn-per-tb on',
                        'logfile '+(a.output/'iram.log').resolve().as_posix(),
                        'log exec,nochain'):
                print(q.command('human-monitor-command', {'command-line':cmd}))
            q.command('cont')
        q.close()
        subprocess.run([sys.executable, str(Path(__file__).with_name('emulator_qmp_iram_super_equivalence.py')),
                        '--port',str(a.port),'--map',str(a.map), '--output',str(a.output),
                        '--boot-wait','0','--timeout','45'], check=True)
    finally:
        if proc.poll() is None:
            try:
                q = QmpClient('127.0.0.1', a.port)
                q.command('quit')
                q.close()
                proc.wait(timeout=5)
            except Exception:
                proc.terminate()
                proc.wait(timeout=5)
