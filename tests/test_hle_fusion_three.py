"""Build and run the three focused equivalence fixtures with the real GAM."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class FusionTest(unittest.TestCase):
    def test_three_paths(self):
        gcc=Path('C:/msys64/ucrt64/bin/gcc.exe')
        rom=ROOT/'应用/数据/游戏/gam4980'
        game=Path('D:/Downloads/伏魔记.gam')
        if not gcc.exists() or not game.exists():self.skipTest('compiler/game unavailable')
        env=os.environ.copy();env['PATH']=str(gcc.parent)+os.pathsep+env['PATH']
        flags=['-DGAM4980_ENABLE_AOT','-DGAM4980_ENABLE_FIRMWARE_HLE',
               '-DGAM4980_ENABLE_GAME_LOAD_AOT','-DGAM4980_ENABLE_AGGRESSIVE_REGION_HLE']
        with tempfile.TemporaryDirectory() as tmp:
            for source,marker in [('native_compare_math_test.c','1000000'),
                                  ('native_runtime_direct_test.c','fast='),
                                  ('native_compare_direct_test.c','fast='),
                                  ('hle_counter_chain_equivalence.c','links='),
                                  ('game_hle_bitmap_equivalence.c','packed pixel groups=')]:
                exe=Path(tmp)/'test.exe'
                extra=[] if source.startswith('native_') else flags
                subprocess.run([str(gcc),'-std=c99','-O2','-w',*extra,
                                str(ROOT/'tests'/source),'-o',str(exe)],check=True,env=env)
                args=[] if not extra else [str(rom/'8.BIN'),str(rom/'E.BIN'),str(game)]
                run=subprocess.run([str(exe),*args],check=True,env=env,capture_output=True,text=True)
                self.assertIn(marker,run.stdout)
                print(run.stdout)

if __name__=='__main__':unittest.main()
