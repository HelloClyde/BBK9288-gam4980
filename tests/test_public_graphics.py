"""Public atomic drawing: original ROM pixels, public stack ABI, target PIC."""
import ctypes as C
import _ctypes
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest
from native_rom_reference import FirmwareMemory
from py65.devices.mpu65c02 import MPU
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from build_firmware_native import specialize_source

def rom_call(mem,pc,a):
    cpu=MPU(memory=mem);cpu.pc=pc;cpu.a=a;cpu.sp=253;cpu.p=48
    banks=[]
    for _ in range(2000000):
        if cpu.pc==0x2000:return
        if cpu.pc==0x3ff0:
            mem.bank=banks.pop();cpu.pc=(cpu.stPopWord()+1)&65535;continue
        if cpu.pc==0xd2f6:
            table=mem.ram[0x26]|mem.ram[0x27]<<8
            if table==0xe749:
                address=int.from_bytes(mem.ram[0x211:0x214],'little')
                dest=mem.ram[0x2f]|mem.ram[0x30]<<8
                for i in range(mem.ram[0x208c]):mem[dest+i]=mem.rom8[address-0x800000+i]
                cpu.pc=(cpu.stPopWord()+1)&65535;continue
            target=mem[table]|mem[table+1]<<8;bank=mem[table+2]
            banks.append(mem.bank);mem.bank=bank;cpu.stPushWord(0x3fef);cpu.pc=target;continue
        if cpu.pc in (0xe8f8,0xe8fb,0xe8fe):
            if cpu.pc==0xe8f8:cpu.a=mem.bank
            else:mem.bank=cpu.a
            cpu.pc=(cpu.stPopWord()+1)&65535;continue
        cpu.step()
    raise AssertionError(f'ROM stuck {mem.bank} {cpu.pc:04x}')

class PublicGraphics(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='atomic-gfx-')
        cls.dll=Path(cls.temp.name)/'public.dll'
        subprocess.run(['C:/msys64/ucrt64/bin/gcc.exe','-O2','-shared',str(ROOT/'tests/public_graphics_host.c'),'-o',str(cls.dll)],check=True)
        cls.lib=C.CDLL(str(cls.dll));cls.lib.run_public.argtypes=[C.c_void_p]*3+[C.c_uint]*2+[C.c_void_p]
        cls.e=(ROOT/'应用/数据/游戏/gam4980/E.BIN').read_bytes()
        cls.r8=(ROOT/'应用/数据/游戏/gam4980/8.BIN').read_bytes()
        cls.earray=(C.c_uint8*len(cls.e)).from_buffer_copy(cls.e)
        cls.array8=(C.c_uint8*len(cls.r8)).from_buffer_copy(cls.r8)

    @classmethod
    def tearDownClass(cls):
        _ctypes.FreeLibrary(cls.lib._handle)
        cls.temp.cleanup()

    def check_case(self,pc,a,args,seed):
        rng=random.Random(seed);m=FirmwareMemory(6 if pc==0x5000 else 5);m.rom8=self.r8
        m.ram[:]=rng.randbytes(65536)
        m.ram[0x3e5:0x3ea]=bytes([1,0,4,0,16]);m.ram[0x3d8]=0x80
        m.ram[0x28:0x2a]=bytes([0,0x18]);m.ram[0x1800:0x1800+len(args)]=bytes(args)
        m.ram[0x1fe:0x200]=bytes([255,31])
        if pc==0x5000:m.ram[0x3000:0x3002]=bytes([80,40])
        original=bytes(m.ram);actual=(C.c_uint8*65536).from_buffer_copy(original);out=(C.c_uint*3)()
        got=self.lib.run_public(actual,self.array8,self.earray,pc,a,out)
        self.assertEqual(got,6,(hex(pc),args));self.assertEqual(list(out),[8192,255,6])
        rom_call(m,pc,a)
        for y in range(96):
            for x in range(20):
                addr=(0xff3 if y==65 else 0x413+(64-y if y<65 else y-1)*32) if x==0 else 0x400+(65-y if y<=65 else y)*32+x-1
                if addr==0x400:addr=0x1000
                self.assertEqual(actual[addr],m.ram[addr],(hex(pc),a,args,x,y,hex(addr)))
        self.assertEqual(bytes(actual[0x1800:0x1800+len(args)]),bytes(args))

    def test_picture(self):
        for flag in (0,1):
            for x in range(8):
                for y in (0,60,64,65,80):
                    self.check_case(0x682d,x,[y,x+17,min(95,y+8),0,0x30,flag],x+y+flag*100)
        self.check_case(0x682d,0,[0,158,95,0,0x30,0],123)
        for x in range(8):
            for w in range(1,17):
                self.check_case(0x682d,x,[65,x+w-1,66,0,0x30,0],x+w)

    def test_fonts(self):
        for x in range(8):
            for y in (0,60,64,65,80):
                self.check_case(0x63d7,x,[y,48+x],x+y)
                self.check_case(0x5c57,x,[y,0xec,0xcc],x+y)
        for high,low in [(0xa1,0xa1),(0xa1,0x40),(0xa8,0x80),(0xaa,0xa1),(0xf8,0xa1),(0x81,0x42),(0xb0,0xa1)]:
            self.check_case(0x5c57,17,[22,low,high],high+low)
        for i in range(228):
            high,low=self.e[0xb7a93+i],self.e[0xb7b77+i]
            if high: self.check_case(0x5c57,i%145,[i%81,low,high],i)

    def test_fallback_does_not_mutate_guest(self):
        for source in (0x400,0x1000,0xffff):
            ram=bytearray(65536);ram[0x3e5:0x3ea]=bytes([1,0,4,0,16])
            ram[0x28:0x2a]=bytes([0,0x18]);ram[0x1800:0x1806]=bytes([0,15,1,source&255,source>>8,0])
            actual=(C.c_uint8*65536).from_buffer_copy(ram);out=(C.c_uint*3)()
            self.assertEqual(self.lib.run_public(actual,self.array8,self.earray,0x682d,0,out),0)
            self.assertEqual(bytes(actual),ram)
            self.assertEqual(list(out),[0x682d,253,0])

    def test_part(self):
        for x in range(8):
            for sx in range(8):
                self.check_case(0x5000,x,[60,sx,3,25,8,0,0x30],x*8+sx)

    def test_pic(self):
        tc=ROOT/'build/llvm-s1c33-host/bin'
        for entry in (0x682d,0x5000,0x63d7,0x5c57):
            source,symbol=specialize_source((ROOT/'src/firmware_native_public_graphics.c').read_text(),'public_graphics',entry)
            unit=Path(self.temp.name)/(symbol+'.c');obj=unit.with_suffix('.o');unit.write_text(source)
            subprocess.run([str(tc/'clang.exe'),'--target=s1c33-none-elf','-O2','-ffreestanding','-fno-builtin','-fno-jump-tables','-fomit-frame-pointer','-I',str(ROOT/'src'),'-c',str(unit),'-o',str(obj)],check=True)
            self.assertIn('There are no relocations',subprocess.check_output([str(tc/'llvm-readelf.exe'),'-r',str(obj)],text=True))

if __name__=='__main__':unittest.main()
