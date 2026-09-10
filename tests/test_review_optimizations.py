"""Execute production control-flow fragments, not just source-name checks."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_direct_framebuffer import function_source

ROOT = Path(__file__).resolve().parents[1]


class ReviewOptimizations(unittest.TestCase):
    def test_slow_handoff_and_resident_chain(self):
        gcc = Path('C:/msys64/ucrt64/bin/gcc.exe')
        if not gcc.exists():
            self.skipTest('host GCC unavailable')
        source = (ROOT / 'src/s6502.c').read_text()
        start = source.index('    {\n      uint32_t iram_budget')
        handoff = source[start:source.index('#endif', start)]
        chain = function_source((ROOT / 'src/gam4980_core.c').read_text(encoding='utf-8'),
                                'native_module_firmware_entry')
        prefix = r'''
#include <stdint.h>
#include <assert.h>
static unsigned calls, mode, slow_calls;
static uint32_t burst(void) {
    ++calls; assert(calls < 3);
    return mode == 0 ? (0x40000000u | 10u) : 0x80000000u;
}
#define S6502_IRAM_EXEC_BURST(...) burst()
static unsigned run(void) {
    uint32_t executed=0, cycles=100;
_next:
'''
        middle = r'''
    ++slow_calls;
_exit:
    return executed;
}
typedef struct { uint32_t cycles, cycle_budget, pc; } s6502_iram_asm_context_t;
static uint32_t native_module_page_entries[256];
static struct { uint32_t diagnostics_enabled, chain_links, max_chain; }
    s6502_native_shared_metrics;
static int halt;
static int sys_halt_p(void) { return halt; }
static uint32_t native_module_firmware_entry_once(s6502_iram_asm_context_t *c) {
    ++calls;
    if(mode==1)return 0;
    c->cycles+=5;
    return 5;
}
'''
        suffix = r'''
int main(void) {
    s6502_iram_asm_context_t c={0,1000,0x5000};
    mode=0;calls=slow_calls=0;
    assert(run()==10 && calls==1 && slow_calls==1);
    mode=1;calls=slow_calls=0;
    assert(run()==0 && calls==1 && slow_calls==0);
    native_module_page_entries[0x50]=(uint32_t)(uintptr_t)native_module_firmware_entry;
    s6502_native_shared_metrics.diagnostics_enabled=1;
    mode=0;calls=0;
    assert(native_module_firmware_entry(&c)==160 && calls==32);
    assert(s6502_native_shared_metrics.chain_links==31);
    assert(s6502_native_shared_metrics.max_chain==32);
    c.cycles=0;c.cycle_budget=15;calls=0;
    assert(native_module_firmware_entry(&c)==15 && calls==3);
    c.cycles=0;c.cycle_budget=1000;calls=0;halt=1;
    assert(native_module_firmware_entry(&c)==5 && calls==1);
    c.cycles=0;calls=0;halt=0;mode=1;
    assert(native_module_firmware_entry(&c)==0 && calls==1 && !c.cycles);
    c.cycles=0;calls=0;mode=0;native_module_page_entries[0x50]=0;
    assert(native_module_firmware_entry(&c)==5 && calls==1);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as temp:
            cfile = Path(temp) / 'review.c'
            exe = Path(temp) / 'review.exe'
            cfile.write_text(prefix + handoff + middle + chain + suffix)
            subprocess.run([str(gcc), '-O2', str(cfile), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()
