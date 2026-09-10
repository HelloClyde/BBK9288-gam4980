"""Compare atomic complete contracts against their original stateful contracts."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
from test_function_native_specialization import contracts,ROOT
sys.path.insert(0,str(ROOT/'tools'))
from build_firmware_native import specialize_source,authored_entries
from native_atomic_contracts import eligible,lower
import native_register_abi

class AtomicContracts(unittest.TestCase):
    def test_exclusions_and_fail_closed(self):
        for name in ('bank','switch','graphics','text','public_graphics','wait'):
            self.assertFalse(eligible(name,0x5000))
        self.assertFalse(eligible('runtime',0xd572))
        with self.assertRaises(ValueError):lower('c->cycle_budget -= 1;','runtime',0xd596)
        self.assertIn('ld.w %r3, 6',native_register_abi.source('test',0xd596,atomic=True))

    def test_all_complete_function_states(self):
        units=['''#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct { uint32_t pc,ac,ix,iy,sp,status,cycle_budget,cycles;
 uintptr_t ram,read8,write8; } s6502_iram_asm_context_t;
#define FIRMWARE_NATIVE_HOST_TEST
'''];entries=[]
        for name,bindings in contracts():
            original=(ROOT/f'src/firmware_native_{name}.c').read_text()
            selected=[e for _,e,_ in authored_entries(original,bindings) if eligible(name,e)]
            if not selected:continue
            units.append(original)
            for entry in selected:
                specialized,symbol=specialize_source(original,name,entry)
                specialized=lower(specialized,name,entry)
                specialized=re.sub(r'\bfd_quotient\b',f'fd_quotient_{entry:04x}',specialized)
                units.append(specialized)
                entries.append(f'{{0x{entry:x},firmware_native_{name},{symbol}}}')
        units.append('''
typedef uint32_t(*fn)(s6502_iram_asm_context_t*);
static struct { unsigned pc;fn old,atomic; } entries[]={'''+','.join(entries)+'''};
static uint8_t ram[65536],initial[65536],expected[65536];
static unsigned seed=0x92884980,reads,writes,rhash,whash;
static unsigned rnd(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static uint8_t read8(uint16_t a){++reads;rhash=(rhash^a)*16777619u;return ram[a];}
static void write8(uint16_t a,uint8_t v){++writes;whash=(whash^(a|((unsigned)v<<16)))*16777619u;ram[a]=v;}
static void word(unsigned a,unsigned v){initial[a]=v;initial[a+1]=v>>8;}
int main(void){
 unsigned k,t,i,total=0;
 for(k=0;k<sizeof(entries)/sizeof(entries[0]);++k){
  unsigned accepted=0;
  for(t=0;t<512;++t){
   s6502_iram_asm_context_t in={0},a,b;
   unsigned ra,rb,rr,rw,rh,wh;
   for(i=0;i<65536;++i)initial[i]=rnd();
   word(0x20,0x1100+(t&127));word(0x23,0xa00+(t&127));word(0x26,0xb00);
   word(0x28,0x900);word(0x2a,0x600);
   word(0x900,0x1200);word(0x902,0x1600);word(0x904,t%33);
   initial[0x1200+t%33]=initial[0x1600+t%33]=0;
   if(t>=480){ /* Long bounded memory/string work no longer needs a huge budget. */
    word(0x900,0x3000);word(0x902,0x5000);word(0x904,4096);
    memset(initial+0x3000,42,4096);memset(initial+0x5000,42,4096);
    initial[0x4000]=initial[0x6000]=0;
   }
   in.pc=entries[k].pc;in.ac=(uint8_t)rnd();in.ix=1+t%16;in.sp=32+t%192;
   in.status=(uint8_t)rnd()&~8u;if(t%17==0)in.status|=8;
   in.cycles=t&7;in.cycle_budget=0x10000000;
   in.ram=(uintptr_t)ram;in.read8=(uintptr_t)read8;in.write8=(uintptr_t)write8;
   initial[0x100|(uint8_t)(in.sp+1)]=0x43;initial[0x100|(uint8_t)(in.sp+2)]=0x44;
   a=in;memcpy(ram,initial,sizeof ram);reads=writes=rhash=whash=0;
   ra=entries[k].old(&a);memcpy(expected,ram,sizeof ram);rr=reads;rw=writes;rh=rhash;wh=whash;
   b=in;b.cycle_budget=in.cycles+6;memcpy(ram,initial,sizeof ram);reads=writes=rhash=whash=0;
   rb=entries[k].atomic(&b);
   if(rb!=(ra?6u:0u) || b.cycles!=in.cycles+(ra?6u:0u))goto bad;
   a.cycles=b.cycles=0;a.cycle_budget=b.cycle_budget=0;
   if(memcmp(&a,&b,sizeof a)||memcmp(expected,ram,sizeof ram)||rr!=reads||rw!=writes||rh!=rhash||wh!=whash)goto bad;
   if(ra)++accepted;
   b=in;b.cycle_budget=in.cycles+5;a=b;memcpy(ram,initial,sizeof ram);reads=writes=0;
   if(entries[k].atomic(&b)||memcmp(&a,&b,sizeof a)||memcmp(ram,initial,sizeof ram)||reads||writes)goto bad;
   b=in;b.pc=0;a=b;memcpy(ram,initial,sizeof ram);reads=writes=0;
   if(entries[k].atomic(&b)||memcmp(&a,&b,sizeof a)||memcmp(ram,initial,sizeof ram)||reads||writes)goto bad;
   continue;
bad:fprintf(stderr,"atomic mismatch pc=%04x test=%u return=%u/%u\\n",in.pc,t,ra,rb);return 1;
  }
  if(!accepted){fprintf(stderr,"no accepted pc=%04x\\n",entries[k].pc);return 1;}total+=accepted;
 }
 printf("atomic contracts: %u entries x 512 state/memory/callback cases, %u accepted\\n",(unsigned)(sizeof entries/sizeof entries[0]),total);return 0;
}
''')
        with tempfile.TemporaryDirectory(prefix='atomic-contracts-') as temp:
            src=Path(temp)/'atomic.c';exe=src.with_suffix('.exe');src.write_text('\n'.join(units),encoding='utf-8')
            subprocess.run(['C:/msys64/ucrt64/bin/gcc.exe','-std=c99','-O2','-I',str(ROOT/'src'),str(src),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True,timeout=120)

if __name__=='__main__':unittest.main()
