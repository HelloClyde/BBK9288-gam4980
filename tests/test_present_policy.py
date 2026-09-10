import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class PresentPolicyTest(unittest.TestCase):
    def test_boundaries_quiet_timeout_and_clock_wrap(self):
        source = r'''
#include <stdint.h>
#include <assert.h>
typedef uint32_t u32;
#include "gam4980_present_policy.h"
int main(void) {
 gam_present_policy p={0};
 assert(gam_present_decide(&p,0,0,0,0,0)==PRESENT_NONE);
 assert(gam_present_decide(&p,1,1,0,1,0)==PRESENT_NONE);
 assert(gam_present_decide(&p,8,0,0,1,1)==PRESENT_NONE);
 assert(gam_present_decide(&p,10,0,1,1,0)==PRESENT_PICTURE);
 assert(gam_present_decide(&p,12,0,0,0,1)==PRESENT_WAIT);
 p=(gam_present_policy){0};
 assert(gam_present_decide(&p,100,1,0,0,0)==0);
 assert(gam_present_decide(&p,106,0,0,0,0)==0);
 assert(gam_present_decide(&p,107,0,0,0,0)==PRESENT_QUIET);
 assert(gam_present_decide(&p,108,1,0,0,0)==0);
 assert(p.quiet==0);
 assert(gam_present_decide(&p,164,1,0,1,0)==PRESENT_TIMEOUT);
 p=(gam_present_policy){0};
 assert(gam_present_decide(&p,0xfffffff0u,1,0,1,0)==0);
 assert(gam_present_decide(&p,0x30u,1,0,1,0)==PRESENT_TIMEOUT);
 return 0;
}'''
        cc=Path('C:/msys64/ucrt64/bin/gcc.exe')
        if not cc.exists(): self.skipTest('host gcc unavailable')
        with tempfile.TemporaryDirectory() as d:
            c=Path(d)/'test.c';exe=Path(d)/'test.exe';c.write_text(source)
            env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
            subprocess.run([str(cc),'-std=c99','-Wall','-Wextra','-Werror','-I',str(ROOT/'src'),str(c),'-o',str(exe)],check=True,env=env)
            subprocess.run([str(exe)],check=True,env=env)
