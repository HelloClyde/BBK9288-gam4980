"""Actual C CFG/preload behavior, not just source-text expectations."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class NativeFunctionPreloadTest(unittest.TestCase):
    def test_real_cfg_with_reference_rom(self):
        romdir = ROOT / 'build/emulator-c6502-engine-stage/gam4980'
        if not all((romdir / name).exists() for name in ('8.BIN', 'E.BIN')):
            self.skipTest('local reference ROMs unavailable')
        compiler = shutil.which('gcc')
        if not compiler and Path('C:/msys64/ucrt64/bin/gcc.exe').exists():
            compiler = 'C:/msys64/ucrt64/bin/gcc.exe'
        if not compiler:
            self.skipTest('host C compiler unavailable')
        env = os.environ.copy()
        env['PATH'] = str(Path(compiler).parent) + os.pathsep + env.get('PATH', '')
        with tempfile.TemporaryDirectory(prefix='9288-preload-') as tmp:
            exe = Path(tmp) / 'preload.exe'
            subprocess.run([compiler, '-std=c99', '-O2', '-I'+str(ROOT/'src'),
                            str(ROOT/'tests/native_function_preload_test.c'),
                            '-o', str(exe)], check=True, env=env, timeout=180,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            result = subprocess.run([str(exe), str(romdir/'8.BIN'), str(romdir/'E.BIN')],
                                    check=True, env=env, timeout=60,
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            self.assertIn('AOT-off, overflow PASS', result.stdout)


if __name__ == '__main__':
    unittest.main()
