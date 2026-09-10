"""Actual mapper and native far-call contracts against the original ROM."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class BankBatchTest(unittest.TestCase):
    def test_native_far_call_batch_and_stable_descriptors(self):
        gcc=Path('C:/msys64/ucrt64/bin/gcc.exe')
        rom=ROOT/'build/emulator-c6502-engine-stage/gam4980'
        if not gcc.exists() or not all((rom/n).exists() for n in ('8.BIN','E.BIN')):
            self.skipTest('local compiler / reference ROM unavailable')
        env=os.environ.copy();env['PATH']=str(gcc.parent)+os.pathsep+env.get('PATH','')
        with tempfile.TemporaryDirectory() as tmp:
            exe=Path(tmp)/'bank.exe'
            for enabled in (False,True):
                flags=['-DFW_NATIVE_BANK_TEST_SERVICES'] if enabled else []
                subprocess.run([str(gcc),'-std=c99','-O2','-Wno-pointer-to-int-cast',
                    '-Wno-int-to-pointer-cast',*flags,str(ROOT/'tests/firmware_native_far_call_test.c'),
                    '-o',str(exe)],env=env,check=True,capture_output=True)
                result=subprocess.run([str(exe),str(rom/'8.BIN'),str(rom/'E.BIN')],
                    env=env,check=True,capture_output=True,text=True)
                self.assertIn('10000 native call/return CPU/RAM/bank cases passed',result.stdout)
                if enabled:self.assertIn('batch accepted=20256 extended accepted=7500 budget rejected=10000',result.stdout)

if __name__=='__main__':unittest.main()
