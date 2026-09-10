import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
class AnalysisCacheTest(unittest.TestCase):
    def test_actual_collector(self):
        gcc = Path('C:/msys64/ucrt64/bin/gcc.exe')
        rom = ROOT/'build/emulator-c6502-engine-stage/gam4980'
        game = Path('D:/Downloads/伏魔记.gam')
        if not gcc.exists() or not game.exists(): self.skipTest('local compiler/game unavailable')
        env=os.environ.copy();env['PATH']=str(gcc.parent)+os.pathsep+env['PATH']
        with tempfile.TemporaryDirectory(dir='D:/Downloads') as tmp:
            exe=Path(tmp)/'cache.exe'
            result=subprocess.run([str(gcc),'-O1','-std=c99','-w','-I'+str(ROOT/'src'),
                str(ROOT/'tests/analysis_cache_test.c'),'-o',str(exe)],capture_output=True,text=True,env=env)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([str(exe),str(rom/'8.BIN'),str(rom/'E.BIN'),str(game)],capture_output=True,text=True,env=env)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            print(result.stdout)
