"""Exercise the actual C FUNCTION NAT loader without executing S1C33 bytes."""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class NativeFunctionLoaderTest(unittest.TestCase):
    def run_heap_loader(self, static_native=False):
        romdir = ROOT / 'build/emulator-c6502-engine-stage/gam4980'
        package = Path(os.environ.get('GAM4980_TEST_NAT',
                       str(ROOT / 'build/function-native-packer/GAM4980.NAT')))
        inputs = [romdir / '8.BIN', romdir / 'E.BIN', package]
        if not all(path.exists() for path in inputs):
            self.skipTest('build the local FUNCTION NAT and reference ROM stage first')
        flags = []
        if static_native:
            generated = ROOT / 'build/9288/static-native'
            if not (generated / 'gam4980_static_native_generated.h').exists():
                self.skipTest('local static native ABI header unavailable')
            flags = ['-DGAM4980_STATIC_NATIVE_GAME', '-I' + str(generated)]
        legacy = ROOT / 'build/internal-firmware-native/GAM4980.NAT'
        if legacy.exists():
            data = legacy.read_bytes()
            module_offset = struct.unpack_from('<I', data, 24)[0]
            if not struct.unpack_from('<I', data, module_offset + 4)[0] & 0x20:
                inputs.append(legacy)
        compiler = shutil.which('gcc')
        if not compiler and Path('C:/msys64/ucrt64/bin/gcc.exe').exists():
            compiler = 'C:/msys64/ucrt64/bin/gcc.exe'
        if not compiler:
            self.skipTest('host C compiler unavailable')
        env = os.environ.copy()
        env['PATH'] = str(Path(compiler).parent) + os.pathsep + env.get('PATH', '')
        with tempfile.TemporaryDirectory(prefix='9288-function-loader-') as tmp:
            exe = Path(tmp) / 'loader.exe'
            build = subprocess.run([
                compiler, '-std=c99', '-O1', '-Wno-pointer-to-int-cast',
                '-Wno-int-to-pointer-cast', '-I' + str(ROOT / 'src'), *flags,
                str(ROOT / 'tests/native_function_loader_test.c'), '-o', str(exe),
            ], env=env, timeout=180, capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            result = subprocess.run([str(exe), *(str(path) for path in inputs)],
                                    env=env, timeout=60, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('active eviction guard, failure cleanup PASS', result.stdout)

    def test_actual_heap_loader(self):
        self.run_heap_loader()

    def test_static_native_mapping_coexistence(self):
        self.run_heap_loader(static_native=True)


if __name__ == '__main__':
    unittest.main()
