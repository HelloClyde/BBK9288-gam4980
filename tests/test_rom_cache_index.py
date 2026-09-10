"""Actual-core old/new cache equivalence for all supported cache capacities."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
CONFIGURATIONS = (
    ([], 32),
    (['GAM4980_ENABLE_BARE_SESSION','GAM4980_DYNAMIC_NATIVE_ALL'],64),
    (['GAM4980_ENABLE_BARE_SESSION'],128),
)


class RomCacheIndexTest(unittest.TestCase):
    def compiler(self):
        compiler = os.environ.get('CC') or shutil.which('gcc')
        if not compiler:
            self.skipTest('host gcc unavailable (set CC to run ROM-cache regression)')
        return compiler

    def compile_and_check(self, folder, defines, lines, sanitize=False):
        output = folder / ('rom_cache.exe' if os.name == 'nt' else 'rom_cache')
        command = [self.compiler(), '-std=c99', '-O1' if sanitize else '-O2',
                   '-I', str(ROOT/'src')]
        if sanitize:
            command += ['-g', '-fsanitize=address', '-fno-omit-frame-pointer']
            if sys.platform.startswith('linux'):
                command.append('-no-pie')
        command += ['-D'+define for define in defines]
        command += [str(ROOT/'tests/rom_cache_index_test.c'), '-o', str(output)]
        compiled = subprocess.run(command, capture_output=True, text=True)
        if sanitize and compiled.returncode and re.search(
                r'cannot find -lasan|unsupported.*saniti|unrecognized.*saniti',
                compiled.stderr, re.I):
            self.skipTest('this host compiler has no AddressSanitizer runtime')
        self.assertEqual(compiled.returncode, 0, compiled.stdout+compiled.stderr)
        environment = os.environ.copy()
        if sanitize:
            # Older WSL libasan signal handlers can recurse on startup faults.
            # Keep ASAN's instrumented bounds checks, but let a real SIGSEGV
            # terminate the process instead of emitting an unbounded loop.
            environment['ASAN_OPTIONS'] = (
                environment.get('ASAN_OPTIONS', '') +
                ':abort_on_error=1:halt_on_error=1:handle_segv=0').strip(':')
        result = subprocess.run([str(output)], capture_output=True, text=True,
                                env=environment, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
        self.assertIn(f'lines={lines} ', result.stdout)
        self.assertIn('passed', result.stdout)
        print(('ASAN ' if sanitize else '')+result.stdout.strip())

    def test_gui_dynamic_bare_and_normal_bare(self):
        with tempfile.TemporaryDirectory(prefix='9288-rom-index-') as temp:
            folder = Path(temp)
            for defines, lines in CONFIGURATIONS:
                with self.subTest(lines=lines):
                    self.compile_and_check(folder, defines, lines)

    def test_address_sanitizer_all_cache_sizes(self):
        with tempfile.TemporaryDirectory(prefix='9288-rom-index-asan-') as temp:
            for defines, lines in CONFIGURATIONS:
                with self.subTest(lines=lines):
                    self.compile_and_check(Path(temp), defines, lines, True)


if __name__ == '__main__':
    unittest.main()
