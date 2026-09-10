"""Target PIC code and host exact-state regression for pageable graphics."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from build_firmware_native import specialize_source

class FirmwareNativeGraphicsTest(unittest.TestCase):
    def test_generated_algorithms_are_current(self):
        subprocess.run([sys.executable,str(ROOT/'tools/generate_native_graphics.py'),'--check'],check=True)

    def test_all_entries_are_relocation_free_s1c33(self):
        tc=ROOT/'build/llvm-s1c33-host/bin'
        if not (tc/'clang.exe').exists():self.skipTest('S1C33 toolchain not available')
        original=(ROOT/'src/firmware_native_graphics.c').read_text(encoding='utf-8')
        with tempfile.TemporaryDirectory(prefix='native-graphics-') as temp:
            for entry in [0x682d,0x690f,0x6988,0x5c5d,0x6b1a,0x6ba4,0x876b,0x8039,0x5351,0x5801,0x859e]:
                text,symbol=specialize_source(original,'graphics',entry)
                src=Path(temp)/(symbol+'.c');obj=src.with_suffix('.o')
                src.write_text(text,encoding='utf-8')
                subprocess.run([str(tc/'clang.exe'),'--target=s1c33-none-elf','-O2','-ffreestanding','-fno-builtin','-fno-jump-tables','-fomit-frame-pointer','-I',str(ROOT/'src'),'-c',str(src),'-o',str(obj)],check=True)
                reloc=subprocess.check_output([str(tc/'llvm-readelf.exe'),'-r',str(obj)],text=True)
                self.assertIn('There are no relocations',reloc)
                print(symbol,'PIC PASS',obj.stat().st_size,'object bytes')

    def test_core_mapper_cpu_cycles_ram_and_lcd(self):
        gcc=Path('C:/msys64/ucrt64/bin/gcc.exe')
        rom=ROOT/'build/emulator-c6502-engine-stage/gam4980'
        if not gcc.exists() or not (rom/'E.BIN').exists():self.skipTest('host GCC or ROM fixtures unavailable')
        with tempfile.TemporaryDirectory(prefix='native-graphics-host-') as temp:
            exe=Path(temp)/'graphics.exe'
            subprocess.run([str(gcc),'-O1','-std=c99','-ffunction-sections','-fdata-sections',str(ROOT/'tests/firmware_native_graphics_test.c'),'-Wl,--gc-sections','-o',str(exe)],check=True)
            subprocess.run([str(exe),str(rom/'8.BIN'),str(rom/'E.BIN')],check=True)

if __name__=='__main__':unittest.main()
