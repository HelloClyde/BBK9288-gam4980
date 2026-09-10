"""Run indexed-dispatch equivalence with authored and dynamic NAT on/off."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class FirmwareBankIndexTest(unittest.TestCase):
    def test_feature_matrix(self):
        compiler = os.environ.get('CC') or shutil.which('gcc')
        if not compiler:
            self.skipTest('host gcc unavailable (set CC to exercise bank index)')
        with tempfile.TemporaryDirectory(prefix='9288-bank-index-') as directory:
            folder = Path(directory)
            for authored in (False, True):
                for dynamic in (False, True):
                    with self.subTest(authored=authored, dynamic=dynamic):
                        output = folder / ('index.exe' if os.name == 'nt' else 'index')
                        command = [compiler, '-std=c99', '-O2',
                            '-Wno-pointer-to-int-cast', '-Wno-int-to-pointer-cast',
                            '-I', str(ROOT/'src')]
                        if authored:
                            command.append('-DGAM4980_AUTHORED_FIRMWARE')
                        if dynamic:
                            command.append('-DGAM4980_DYNAMIC_NATIVE_ALL')
                        command.extend([str(ROOT/'tests/firmware_hle_bank_index_test.c'),
                                        '-o', str(output)])
                        compiled = subprocess.run(command, capture_output=True, text=True)
                        self.assertEqual(compiled.returncode, 0, compiled.stdout+compiled.stderr)
                        checked = subprocess.run([str(output)], capture_output=True, text=True)
                        self.assertEqual(checked.returncode, 0, checked.stdout+checked.stderr)
                        print(f'authored={int(authored)} dynamic={int(dynamic)}: '
                              + checked.stdout.strip())


if __name__ == '__main__':
    unittest.main()
