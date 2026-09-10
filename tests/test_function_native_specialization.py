"""Every entry specialization must preserve its original handwritten contract.

This is not a 6502 differential test (the separate firmware contract tests do
that). It specifically detects changes introduced while splitting a family
implementation into independently resident functions, including PC stores,
bank continuations, cycle-budget rejection and callback ordering/counts.
"""
import ast
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from build_firmware_native import authored_entries, specialize_source


def contracts():
    tree = ast.parse((ROOT / 'tools/build_firmware_native.py').read_text())
    loop = next(node for node in ast.walk(tree) if isinstance(node, ast.For)
                and isinstance(node.target, ast.Tuple)
                and all(isinstance(x, ast.Name) for x in node.target.elts)
                and [x.id for x in node.target.elts] == ['name', 'bindings'])
    return ast.literal_eval(loop.iter)


class FunctionSpecializationTest(unittest.TestCase):
    def test_all_sources_have_safe_entry_specializations(self):
        seen = set()
        for name, bindings in contracts():
            source = (ROOT / f'src/firmware_native_{name}.c').read_text()
            for physical, entry, _ in authored_entries(source, bindings):
                if name == 'graphics' and entry == 0x682d:
                    continue  # Replaced by the complete public graphics service.
                specialized, symbol = specialize_source(source, name, entry)
                self.assertNotIn(physical, seen)
                seen.add(physical)
                self.assertIn(f'if (c->pc != 0x{entry:04x}u) return 0u;', specialized)
                self.assertIn(f'uint32_t {symbol}(', specialized)
        self.assertGreater(len(seen), 100)

    def test_randomized_original_versus_each_specialized_contract(self):
        compiler = os.environ.get('CC') or shutil.which('gcc')
        if not compiler:
            self.skipTest('host gcc unavailable (set CC to run contract splitting test)')
        source = ['''#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
 uint32_t pc,ac,ix,iy,sp,status,cycle_budget,cycles;
 uintptr_t ram,read8,write8;
} s6502_iram_asm_context_t;
#define FIRMWARE_NATIVE_HOST_TEST
''']
        rows = []
        for name, bindings in contracts():
            # Graphics need ABI5 services and their own pixel/6502 reference
            # harnesses. This compact ABI4 runtime fixture cannot model those
            # callbacks; their source-specialization safety is tested above.
            if name in ('graphics', 'text', 'public_graphics'):
                continue
            original = (ROOT / f'src/firmware_native_{name}.c').read_text()
            source.append(original)
            for _, entry, _ in authored_entries(original, bindings):
                specialized, symbol = specialize_source(original, name, entry)
                # Standalone target objects have private helpers. Give the
                # division helper a private name in this combined host TU too.
                specialized = re.sub(r'\bfd_quotient\b',
                                     f'fd_quotient_{entry:04x}', specialized)
                source.append(specialized)
                rows.append(f'{{0x{entry:04x}u, firmware_native_{name}, {symbol}}}')
        source.append('''
typedef uint32_t (*entry_fn)(s6502_iram_asm_context_t *);
static const struct {unsigned pc;entry_fn original,specialized;} entries[]={
''' + ',\n'.join(rows) + '\n};\n' + r'''
static uint8_t ram[65536], initial[65536], expected[65536];
static uint32_t seed=0x49809288u,reads,writes,read_trace,write_trace;
static uint32_t random_value(void) {
 seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;
}
static uint8_t read8(uint16_t address) {
 ++reads;read_trace=(read_trace^address)*16777619u;return ram[address];
}
static void write8(uint16_t address,uint8_t value) {
 ++writes;write_trace=(write_trace^(address|((uint32_t)value<<16)))*16777619u;
 ram[address]=value;
}
static void put16(unsigned address,unsigned value) {
 initial[address]=(uint8_t)value;initial[address+1u]=(uint8_t)(value>>8);
}
int main(void) {
 unsigned index,t,i,accepted_total=0;
 for(index=0;index<sizeof(entries)/sizeof(entries[0]);++index) {
  unsigned accepted=0;
  for(t=0;t<2048u;++t) {
   s6502_iram_asm_context_t input={0},left,right;
   uint32_t a,b,ref_reads,ref_writes,ref_read_trace,ref_write_trace;
   for(i=0;i<0x2000u;++i) initial[i]=(uint8_t)random_value();
   put16(0x20,0x1100u+(t&127u));put16(0x23,0xa00u+(t&127u));
   put16(0x26,0xb00u);put16(0x28,0x900u);put16(0x2a,0x600u);
   put16(0x900,0x1200u);put16(0x902,0x1600u);put16(0x904,t%33u);
   initial[0x1200u+t%33u]=initial[0x1600u+t%33u]=0;
   input.pc=entries[index].pc;input.ac=(uint8_t)random_value();
   input.ix=1u+t%16u;input.iy=0;input.sp=32u+t%192u;
   input.status=(uint8_t)random_value()&~8u;
   if(t%17u==0)input.status|=8u;
   input.cycles=t&7u;
   input.cycle_budget=t%7u==0?input.cycles:t%7u==1?60u:65535u;
   input.ram=(uintptr_t)ram;input.read8=(uintptr_t)read8;input.write8=(uintptr_t)write8;
   initial[0x100u|(uint8_t)(input.sp+1u)]=0x43;
   initial[0x100u|(uint8_t)(input.sp+2u)]=0x44;
   for(i=0;i<16u;++i) {
    put16(0xb00u+2u*i,i);put16(0xb20u+2u*i,0x4444u);
   }
   if(input.pc==0xf457u)put16(0x900,0x800u);
   if(input.pc==0xf475u) {initial[0x900]=1u+t%8u;input.ac=5u;}
   if(input.pc==0xf48bu) {initial[0x0c]=5u;input.ix=t%8u;}
   left=input;memcpy(ram,initial,sizeof(ram));
   reads=writes=0;read_trace=write_trace=2166136261u;
   a=entries[index].original(&left);
   memcpy(expected,ram,sizeof(ram));ref_reads=reads;ref_writes=writes;
   ref_read_trace=read_trace;ref_write_trace=write_trace;
   right=input;memcpy(ram,initial,sizeof(ram));
   reads=writes=0;read_trace=write_trace=2166136261u;
   b=entries[index].specialized(&right);
   if(a!=b || memcmp(&left,&right,sizeof(left)) || memcmp(expected,ram,sizeof(ram)) ||
      reads!=ref_reads || writes!=ref_writes || read_trace!=ref_read_trace || write_trace!=ref_write_trace) {
    fprintf(stderr,"specialization mismatch PC=%04x case=%u result=%u/%u cycles=%u/%u outPC=%x/%x\n",
      input.pc,t,a,b,left.cycles,right.cycles,left.pc,right.pc);return 1;
   }
   if(a)++accepted;
   right=input;right.pc=0;left=right;memcpy(ram,initial,sizeof(ram));
   reads=writes=0;
   if(entries[index].specialized(&right) || memcmp(&left,&right,sizeof(left)) ||
      memcmp(ram,initial,sizeof(ram)) || reads || writes) {
    fprintf(stderr,"wrong-entry guard mutated PC=%04x case=%u\n",input.pc,t);return 1;
   }
  }
  if(!accepted) {fprintf(stderr,"no accepted cases PC=%04x\n",entries[index].pc);return 1;}
  accepted_total+=accepted;
 }
 printf("function specialization: %u entries, %u equivalent cases, %u accepted, wrong-entry guards passed\n",
   (unsigned)(sizeof(entries)/sizeof(entries[0])),
   (unsigned)(sizeof(entries)/sizeof(entries[0]))*2048u,accepted_total);
 return 0;
}
''')
        with tempfile.TemporaryDirectory(prefix='9288-function-equivalence-') as temp:
            folder = Path(temp)
            generated = folder / 'function_equivalence.c'
            executable = folder / ('function_equivalence.exe' if os.name == 'nt' else 'function_equivalence')
            generated.write_text('\n'.join(source), encoding='utf-8')
            compiled = subprocess.run([compiler, '-std=c99', '-O2', '-I', str(ROOT/'src'),
                str(generated), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stdout+compiled.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
            print(result.stdout.strip())


if __name__ == '__main__':
    unittest.main()
