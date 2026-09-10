"""Real-ROM regression for native HLE boundary adapters and shared algorithms."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class ConsolidationTest(unittest.TestCase):
    def test_real_rom(self):
        gcc = Path('C:/msys64/ucrt64/bin/gcc.exe')
        rom = ROOT / '应用/数据/游戏/gam4980'
        game = Path('D:/Downloads/伏魔记.gam')
        if not gcc.exists() or not game.exists():
            self.skipTest('compiler/game unavailable')
        env = os.environ.copy()
        env['PATH'] = str(gcc.parent) + os.pathsep + env['PATH']
        flags = ['-DGAM4980_ENABLE_AOT', '-DGAM4980_ENABLE_FIRMWARE_HLE',
                 '-DGAM4980_ENABLE_GAME_LOAD_AOT', '-DGAM4980_ENABLE_AGGRESSIVE_REGION_HLE']
        sources = ['firmware_hle_byte_transfer_test', 'firmware_hle_multiply_test',
                   'firmware_hle_c_runtime_test', 'firmware_hle_bank_switch_test']
        sources += ['game_hle_' + name + '_equivalence' for name in
                    ('record_scan', 'record_reverse', 'table_chain', 'object_flow', 'callback_scan', 'scan')]
        with tempfile.TemporaryDirectory() as tmp:
            for source in sources:
                is_game = source.startswith('game_')
                exe = Path(tmp) / 'case.exe'
                subprocess.run([str(gcc), '-std=c99', '-O2', '-w',
                                *(flags if is_game else []),
                                str(ROOT / 'tests' / (source + '.c')), '-o', str(exe)],
                               check=True, env=env)
                args = [str(rom / '8.BIN'), str(rom / 'E.BIN')]
                if is_game:
                    args.append(str(game))
                with self.subTest(source=source):
                    run = subprocess.run([str(exe), *args], check=True, env=env,
                                         capture_output=True, text=True)
                    print(run.stdout, flush=True)

if __name__ == '__main__':
    unittest.main()
