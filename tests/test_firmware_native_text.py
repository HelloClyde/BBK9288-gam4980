"""Exact-ROM and PIC checks for authored ASCII/Chinese computation in NAT."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from build_firmware_native import authored_entries, specialize_source

TEXT_BINDINGS = ((0xEB550B, 0x650B, 0x1F2), (0xEB5086, 0x6086, 0x13F))


class FirmwareNativeTextTest(unittest.TestCase):
    def test_flat_kernels_are_current(self):
        subprocess.run([sys.executable,str(ROOT/'tools/generate_native_text_flat.py'),'--check'],check=True)

    def test_exact_cpu_ram_cycles_and_lcd(self):
        cc = os.environ.get("CC") or shutil.which("gcc")
        if not cc and Path("C:/msys64/ucrt64/bin/gcc.exe").exists():
            cc = "C:/msys64/ucrt64/bin/gcc.exe"
        if not cc:
            self.skipTest("host gcc unavailable (set CC)")
        rom_dir = ROOT / "build/emulator-c6502-engine-stage/gam4980"
        if not (rom_dir / "E.BIN").exists():
            rom_dir = ROOT / "应用/数据/游戏/gam4980"
        if not all((rom_dir / name).exists() for name in ("8.BIN", "E.BIN")):
            self.skipTest("reference 8.BIN/E.BIN unavailable")
        with tempfile.TemporaryDirectory(prefix="9288-text-native-") as temp:
            folder = Path(temp)
            output = folder / ("native-text.exe" if os.name == "nt" else "native-text")
            args = [cc, "-std=gnu11", "-O2", "-Wno-attributes", "-I", str(ROOT / "src")]
            if os.environ.get("GAM4980_TEST_ASAN") == "1":
                args += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g"]
                if os.name != "nt":
                    args.append("-no-pie")
            args += [str(ROOT / "tests/firmware_native_text_test.c"), "-o", str(output)]
            environment = os.environ.copy()
            environment["PATH"] = str(Path(cc).resolve().parent) + os.pathsep + environment.get("PATH", "")
            compile_result = subprocess.run(args, capture_output=True, text=True,
                                            env=environment, timeout=120)
            self.assertEqual(compile_result.returncode, 0, compile_result.stdout + compile_result.stderr)
            # ASCII temporary names also support a legacy Windows C runtime.
            for name in ("8.BIN", "E.BIN"):
                shutil.copyfile(rom_dir / name, folder / name)
            result = subprocess.run([str(output), str(folder / "8.BIN"), str(folder / "E.BIN")],
                                    capture_output=True, text=True, env=environment, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("1248 exact ROM CPU/RAM/cycle/framebuffer cases passed", result.stdout)
            print(result.stdout.strip())

    def test_exact_entry_guards_and_no_trampoline(self):
        source = (ROOT / "src/firmware_native_text.c").read_text(encoding="utf-8")
        self.assertEqual({pc for _, pc, _ in authored_entries(source, TEXT_BINDINGS)}, {0x650F, 0x608A})
        stripped = re.sub(r"/\*.*?\*/|//[^\n]*", "", source, flags=re.S)
        self.assertNotIn("s6502_firmware_hle_", stripped)
        self.assertNotIn("SysPrintString", stripped)
        self.assertIn("fw_graphics_stage(", stripped)
        self.assertIn("fw_graphics_flush(", stripped)
        self.assertIn("fw_gfx_lcd_span(", (ROOT / "src/firmware_native_graphics_memory.h").read_text())
        self.assertIn("write8_resolved", stripped)
        for entry in (0x650F, 0x608A):
            specialized, symbol = specialize_source(source, "text", entry)
            self.assertIn(f"if (c->pc != 0x{entry:04x}u) return 0u;", specialized)
            self.assertIn(f"uint32_t {symbol}", specialized)

    def test_specialized_s1c33_has_no_external_relocations(self):
        toolchain = ROOT / "build/llvm-s1c33-host/bin"
        compiler = toolchain / "clang.exe"
        readelf = toolchain / "llvm-readelf.exe"
        if os.name != "nt" or not compiler.exists():
            self.skipTest("bundled Windows S1C33 compiler unavailable on this host")
        source = (ROOT / "src/firmware_native_text.c").read_text(encoding="utf-8")
        with tempfile.TemporaryDirectory(prefix="9288-text-pic-") as temp:
            folder = Path(temp)
            for entry in (0x650F, 0x608A):
                with self.subTest(entry=hex(entry)):
                    specialized, symbol = specialize_source(source, "text", entry)
                    unit = folder / (symbol + ".c")
                    obj = folder / (symbol + ".o")
                    unit.write_text(specialized, encoding="utf-8")
                    args = [str(compiler), "--target=s1c33-none-elf", "-O2",
                            "-ffreestanding", "-fno-builtin", "-fno-jump-tables", "-fomit-frame-pointer",
                            "-I", str(ROOT / "src"), "-c", str(unit), "-o", str(obj)]
                    result = subprocess.run(args, capture_output=True, text=True, timeout=60)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    relocs = subprocess.check_output([str(readelf), "-r", str(obj)], text=True)
                    self.assertIn("There are no relocations", relocs)


if __name__ == "__main__":
    unittest.main()
