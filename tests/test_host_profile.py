from pathlib import Path
import os
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
class HostProfileTest(unittest.TestCase):
    def test_nested_exclusive_and_clock_wrap(self):
        gcc=Path('C:/msys64/ucrt64/bin/gcc.exe')
        if not gcc.exists():self.skipTest('host compiler unavailable')
        source=r'''
#include <stdint.h>
#include <assert.h>
typedef uint32_t u32;
#include "gam4980_host_profile.h"
static u32 now;
static u32 clock_read(void){return now;}
int main(void){
  u32 outer,inner;
  host_profile.clock=clock_read;now=0xfffffff0u;
  host_profile_switch(HP_CORE);
  now+=5;outer=host_profile_enter(HP_IRAM);
  now+=7;inner=host_profile_enter(HP_GRAPHICS);
  now+=11;host_profile_leave(inner);
  now+=13;host_profile_leave(outer);
  now+=17;host_profile_switch(HP_OFF);
  assert(host_profile.ticks[HP_CORE]==22);
  assert(host_profile.ticks[HP_IRAM]==20);
  assert(host_profile.ticks[HP_GRAPHICS]==11);
  assert(host_profile.reads==6);
  now+=100;host_profile_enter(HP_NATIVE);host_profile_leave(HP_OFF);
  assert(host_profile.reads==6 && host_profile.ticks[HP_NATIVE]==0);
  host_profile_switch(HP_CORE);
  outer=host_profile_enter(HP_GRAPHICS);
  host_function_owner=3;
  now+=19;inner=host_profile_enter(HP_IO);
  now+=101;host_profile_leave(inner);
  now+=23;host_profile_leave(outer);
  host_function_owner=HP_FUNCTION_COUNT;
  assert(host_function_profile[3][3]==42);
  assert(host_profile.ticks[HP_IO]==101);
  /* A nested native call belongs to its own owner, not its caller. */
  outer=host_profile_enter(HP_NATIVE);host_function_owner=3;
  now+=7;inner=host_profile_enter(HP_TEXT);host_function_owner=4;
  now+=13;host_profile_leave(inner);host_function_owner=3;
  now+=11;host_profile_leave(outer);host_function_owner=HP_FUNCTION_COUNT;
  assert(host_function_profile[3][3]==60);
  assert(host_function_profile[4][3]==13);
  {
    u32 a[3],b[3],io,reads;
    host_profile_phase(HP_IRAM);
    host_private_profile_begin(5,0xeb582d,HP_GRAPHICS,a);
    now+=9;io=host_profile_enter(HP_IO);
    now+=100;host_profile_leave(io);
    now+=3;host_private_profile_begin(6,0xea8123,HP_NATIVE,b);
    now+=17;host_private_profile_end(b,0);
    now+=11;host_private_profile_end(a,1);
    assert(host_profile.current==HP_IRAM && host_function_owner==HP_FUNCTION_COUNT);
    assert(host_function_profile[5][3]==23 && host_function_profile[6][3]==17);
    assert(host_private_function_attempts[5]==1 && host_private_function_accepted[5]==1);
    assert(host_private_function_attempts[6]==1 && host_private_function_accepted[6]==0);
    assert(host_function_profile[5][0]==0xeb582d);
    host_profile_switch(HP_OFF);reads=host_profile.reads;
    host_private_profile_begin(5,0,HP_NATIVE,a);now+=100;host_private_profile_end(a,1);
    assert(host_profile.reads==reads && host_private_function_attempts[5]==1);
  }
  {
    u32 io, reads;
    host_profile_switch(HP_CORE);host_profile_phase(HP_HLE);
    now+=3;host_hle_event(7,0);
    now+=5;io=host_profile_enter(HP_IO);
    now+=100;host_profile_leave(io);
    now+=7;host_hle_event(7,1);
    now+=2;host_profile_phase(HP_CORE);
    assert(host_hle_profile[7][4]==14 && host_hle_profile[7][0]==1);
    assert(host_hle_profile[7][1]==1 && host_hle_profile[31][4]==3);
    host_profile_phase(HP_HLE);host_hle_event(8,0);
    now+=11;host_hle_event(8,3);
    now+=13;host_profile_phase(HP_CORE);
    assert(host_hle_profile[8][4]==11 && host_hle_profile[8][3]==1);
    assert(host_hle_profile[31][4]==16);
    assert(host_profile.ticks[HP_HLE]==41);
    host_profile_switch(HP_OFF);reads=host_profile.reads;
    host_hle_event(7,0);host_hle_event(7,1);host_hle_select(31);
    assert(host_profile.reads==reads && host_hle_profile[7][0]==1);
  }
  return 0;
}'''
        env=os.environ.copy();env['PATH']=str(gcc.parent)+os.pathsep+env['PATH']
        with tempfile.TemporaryDirectory(dir='D:/Downloads') as tmp:
            c=Path(tmp)/'test.c';exe=Path(tmp)/'test.exe';c.write_text(source)
            subprocess.run([str(gcc),'-std=c99','-I'+str(ROOT/'src'),str(c),'-o',str(exe)],check=True,env=env)
            subprocess.run([str(exe)],check=True,env=env)
