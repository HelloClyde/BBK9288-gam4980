"""Test actual inline NAT graphics I/O against independent guest/pixel models.

CC selects a host compiler; GAM4980_TEST_ASAN=1 enables ASAN/UBSAN.  No ROM,
game assets, target emulator or SDK is required.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

HARNESS = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "firmware_native_graphics_io.h"
#define RAM_BYTES 0x1100u
#define FRAME_WORDS 4800u
static uint8_t actual_ram[RAM_BYTES + 32u], expected_ram[RAM_BYTES + 32u];
static uint32_t actual_frame[FRAME_WORDS + 8u], expected_frame[FRAME_WORDS + 8u];
static uint32_t lut[2][256], physical[96][20];
static int inverse_y[0x1001], inverse_x[0x1001];
static uint32_t rng = 0x92886502u;
static unsigned checks, aliases;
#define RAM (actual_ram + 16u)
#define REF_RAM (expected_ram + 16u)
#define FRAME (actual_frame + 4u)
#define REF_FRAME (expected_frame + 4u)
#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "failure line %d, check %u: %s\n", __LINE__, checks, #x); \
    exit(1); } } while (0)
static uint32_t random32(void) {
    uint32_t x = rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return rng = x;
}
static void host_pixel(uint32_t *frame, unsigned x, unsigned y, unsigned shade) {
    uint8_t *p = (uint8_t *)frame + y * 80u + (x >> 2);
    unsigned shift = 6u - 2u * (x & 3u);
    *p = (uint8_t)((*p & ~(3u << shift)) | (shade << shift));
}
/* Build mapping from the emulator's independent scan order, not the helper's
 * address formula or inverse decoder.  The $400 source is replaced by $1000. */
static void init_reference(void) {
    unsigned a, x, y, bit, white;
    int j;
    for (a = 0u; a <= 0x1000u; ++a) inverse_y[a] = inverse_x[a] = -1;
    a = 0x400u;
    for (j = 65; j >= -30; --j) {
        y = (unsigned)(j >= 0 ? j : 65 - j);
        for (x = 1u; x < 20u; ++x) physical[y][x] = a++;
        a += 13u;
    }
    a = 0x413u;
    for (j = 64; j >= -30; --j) {
        y = (unsigned)(j >= 0 ? j : 65 - j);
        physical[y][0] = a; a += 32u;
    }
    physical[65][0] = 0x0ff3u;
    physical[65][1] = 0x1000u;
    for (y = 0u; y < 96u; ++y) for (x = 0u; x < 20u; ++x) {
        a = physical[y][x]; CHECK(a > 0x400u && a <= 0x1000u);
        CHECK(inverse_y[a] == -1); inverse_y[a] = (int)y; inverse_x[a] = (int)x;
        CHECK(fw_gfx_lcd_address(x, y) == a);
    }
    for (white = 0u; white < 2u; ++white) for (a = 0u; a < 256u; ++a) {
        uint8_t *out = (uint8_t *)&lut[white][a];
        memset(out, 0, 4u);
        for (bit = 0u; bit < 16u; ++bit) {
            unsigned shade = bit ? ((a & (128u >> ((bit - 1u) >> 1))) ? 0u : 3u)
                                 : (white ? 3u : 0u);
            out[bit >> 2] |= (uint8_t)(shade << (6u - 2u * (bit & 3u)));
        }
        CHECK(fw_gfx_expand_word((uint8_t)a, white, 0) == lut[white][a]);
        CHECK(fw_gfx_expand_word((uint8_t)a, white, lut) == lut[white][a]);
    }
}
static void reference_byte(unsigned address) {
    unsigned bit, dx, dy, x, y;
    if (address > 0x1000u || inverse_y[address] < 0) return;
    y = (unsigned)inverse_y[address]; x = (unsigned)inverse_x[address] * 8u;
    for (bit = 0u; bit < 8u && x + bit < 159u; ++bit)
        for (dy = 0u; dy < 2u; ++dy) for (dx = 0u; dx < 2u; ++dx)
            host_pixel(REF_FRAME, 1u + (x + bit) * 2u + dx, 24u + y * 2u + dy,
                       REF_RAM[address] & (128u >> bit) ? 0u : 3u);
}
static void reset_surfaces(void) {
    unsigned i, y, x;
    for (i = 0u; i < sizeof(actual_ram); ++i) actual_ram[i] = (uint8_t)random32();
    memcpy(expected_ram, actual_ram, sizeof(actual_ram));
    for (i = 0u; i < FRAME_WORDS + 8u; ++i) expected_frame[i] = 0x55aa33ccu;
    memset(REF_FRAME, 0xff, FRAME_WORDS * sizeof(uint32_t));
    for (y = 0u; y < 96u; ++y) for (x = 0u; x < 20u; ++x)
        reference_byte(physical[y][x]);
    memcpy(actual_frame, expected_frame, sizeof(actual_frame));
}
static void equal_surfaces(void) {
    ++checks;
    CHECK(!memcmp(actual_ram, expected_ram, sizeof(actual_ram)));
    CHECK(!memcmp(actual_frame, expected_frame, sizeof(actual_frame)));
}
static unsigned expected_stores(unsigned address) {
    return address <= 0x1000u && inverse_y[address] >= 0
        ? (inverse_x[address] == 19 ? 8u : 10u) : 0u;
}
static void check_store(unsigned address, uint8_t value, uint8_t mask, int table) {
    unsigned written;
    if (address >= 0x400u && address <= 0x1000u) {
        REF_RAM[address] = (uint8_t)((REF_RAM[address] & (uint8_t)~mask) | (value & mask));
        reference_byte(address);
    }
    written = fw_gfx_lcd_store(RAM, FRAME, table ? lut : 0, address, value, mask);
    CHECK(written == expected_stores(address)); equal_surfaces();
}
static void check_span(unsigned first, unsigned last, unsigned y,
                       const uint8_t *source, int overlap, uint8_t left, uint8_t right,
                       int table, int visible) {
    uint8_t snapshot[20];
    unsigned i, count = last - first + 1u, result;
    const uint8_t *actual_source = source;
    if (overlap) actual_source = RAM + physical[y][first];
    memcpy(snapshot, actual_source, count);
    for (i = 0u; i < count; ++i) {
        unsigned a = physical[y][first + i]; uint8_t mask = 0xffu;
        if (!i) mask &= left;
        if (i == count - 1u) mask &= right;
        REF_RAM[a] = (uint8_t)((REF_RAM[a] & (uint8_t)~mask) | (snapshot[i] & mask));
        if (visible) reference_byte(a);
    }
    result = fw_gfx_lcd_span(RAM, visible ? FRAME : 0, table ? lut : 0,
                            first, last, y, actual_source, left, right);
    CHECK(result == (visible ? count * 8u + (last < 19u ? 2u : 0u) : 0u));
    equal_surfaces();
    if (!visible) {
        /* Mirror committed RAM later, exactly as a RAM-only SDK service does. */
        for (i = 0u; i < count; ++i) {
            unsigned a = physical[y][first + i];
            reference_byte(a);
            CHECK(fw_gfx_lcd_mirror(RAM, FRAME, table ? lut : 0, a) == expected_stores(a));
        }
        equal_surfaces();
    }
}
/* Deliberately external alias service: the primitive receives its resolved
 * physical identity, never mistakes guest virtual $5xxx for LCD memory, and
 * cannot inspect banks or call into an EXE renderer itself. */
static unsigned alias_write(unsigned virtual_address, unsigned physical_base,
                            uint8_t value, int table) {
    unsigned resolved = physical_base + (virtual_address & 0xfffu);
    ++aliases;
    CHECK(resolved < RAM_BYTES);
    RAM[resolved] = value;
    return fw_gfx_lcd_mirror(RAM, FRAME, table ? lut : 0, resolved);
}
int main(void) {
    unsigned a, x, y, table, i; uint8_t source[20];
    init_reference(); reset_surfaces();
    for (a = 0u; a <= 0x1000u; ++a) {
        unsigned col = 1234u, row = 5678u;
        int visible = fw_gfx_lcd_position(a, &col, &row);
        CHECK(visible == (inverse_y[a] >= 0));
        if (visible) { CHECK(col == (unsigned)inverse_x[a]); CHECK(row == (unsigned)inverse_y[a]); }
        else { CHECK(col == 1234u); CHECK(row == 5678u); }
    }
    CHECK(fw_gfx_lcd_address(20u, 0u) == 0u);
    CHECK(fw_gfx_lcd_address(0u, 96u) == 0u);
    CHECK(!fw_gfx_lcd_position(0x1001u, &x, &y));
    CHECK(!fw_gfx_lcd_position(0xffffffffu, &x, &y));
    CHECK(!fw_gfx_lcd_position(0x1000u, 0, &y));
    for (table = 0u; table < 2u; ++table) {
        for (a = 0x400u; a <= 0x1000u; ++a)
            check_store(a, (uint8_t)random32(), (uint8_t)random32(), (int)table);
        /* The hidden 160th bit survives both single-byte and span stores. */
        for (y = 0u; y < 96u; ++y) {
            a = physical[y][19]; check_store(a, 1u, 1u, (int)table); CHECK(RAM[a] & 1u);
            for (i = 0u; i < 20u; ++i) source[i] = (uint8_t)random32();
            source[19] |= 1u;
            check_span(0u, 19u, y, source, 0, 255u, 255u, (int)table, 1);
            CHECK(RAM[a] & 1u);
        }
        /* Every mask value with complementary edges, including first==last
         * and both special row65 bytes. Other mask pairs are randomized below. */
        for (i = 0u; i < 256u; ++i) {
            source[0] = (uint8_t)random32();
            check_span(i % 20u, i % 20u, 65u, source, 0, (uint8_t)i,
                       (uint8_t)~i, (int)table, 1);
        }
        for (i = 0u; i < 3000u; ++i) {
            unsigned first = random32() % 20u, last = first + random32() % (20u - first);
            y = i % 4u ? random32() % 96u : 65u;
            for (x = 0u; x < 20u; ++x) source[x] = (uint8_t)random32();
            check_span(first, last, y, source, (i % 3u) == 0u,
                       (uint8_t)random32(), (uint8_t)random32(), (int)table, i % 7u != 0u);
        }
        for (i = 0u; i < 500u; ++i) {
            unsigned resolved, result; uint8_t value = (uint8_t)random32();
            y = random32() % 96u; x = random32() % 20u; resolved = physical[y][x];
            REF_RAM[resolved] = value; reference_byte(resolved);
            result = alias_write(0x5000u | (resolved & 0xfffu), resolved & ~0xfffu,
                                 value, (int)table);
            CHECK(result == expected_stores(resolved)); equal_surfaces();
        }
    }
    reset_surfaces();
    CHECK(fw_gfx_lcd_span(RAM, FRAME, lut, 2u, 1u, 65u, source, 255u, 255u) == 0u);
    CHECK(fw_gfx_lcd_span(RAM, FRAME, lut, 0u, 20u, 65u, source, 255u, 255u) == 0u);
    CHECK(fw_gfx_lcd_span(RAM, FRAME, lut, 0u, 19u, 96u, source, 255u, 255u) == 0u);
    CHECK(fw_gfx_lcd_span(RAM, FRAME, lut, 0u, 19u, 65u, 0, 255u, 255u) == 0u);
    CHECK(fw_gfx_lcd_span(0, FRAME, lut, 0u, 19u, 65u, source, 255u, 255u) == 0u);
    CHECK(fw_gfx_lcd_store(RAM, FRAME, lut, 0x1001u, 255u, 255u) == 0u);
    CHECK(fw_gfx_lcd_store(RAM, FRAME, lut, 0xffffffffu, 255u, 255u) == 0u);
    CHECK(fw_gfx_lcd_store(RAM, FRAME, lut, 0x3ffu, 255u, 255u) == 0u);
    CHECK(fw_gfx_lcd_store(0, FRAME, lut, 0x1000u, 255u, 255u) == 0u);
    CHECK(fw_gfx_lcd_mirror(0, FRAME, lut, 0x1000u) == 0u);
    equal_surfaces();
    REF_RAM[0x1000u] = 0xa5u;
    CHECK(fw_gfx_lcd_store(RAM, 0, lut, 0x1000u, 0xa5u, 255u) == 0u);
    equal_surfaces();
    reference_byte(0x1000u); CHECK(fw_gfx_lcd_mirror(RAM, FRAME, lut, 0x1000u) == 10u);
    equal_surfaces();
    printf("native graphics IO PASS: %u cases, %u callback-resolved aliases, all folded bytes and masks\n", checks, aliases);
    return 0;
}
'''

PIC_PROBE = r'''
#include <stdint.h>
#include "firmware_native_graphics_io.h"
uint32_t exported_span(uint8_t *r, volatile uint32_t *f, const uint32_t (*l)[256],
                       uint32_t a, uint32_t b, uint32_t y, const uint8_t *s,
                       uint8_t m, uint8_t n) {
    return fw_gfx_lcd_span(r, f, l, a, b, y, s, m, n);
}
uint32_t exported_store(uint8_t *r, volatile uint32_t *f, const uint32_t (*l)[256],
                        uint32_t a, uint8_t v, uint8_t m) {
    return fw_gfx_lcd_store(r, f, l, a, v, m);
}
'''


class NativeGraphicsIOTest(unittest.TestCase):
    def test_actual_inline_helpers(self):
        cc = os.environ.get("CC") or shutil.which("gcc")
        if not cc and Path("C:/msys64/ucrt64/bin/gcc.exe").exists():
            cc = "C:/msys64/ucrt64/bin/gcc.exe"
        if not cc:
            self.skipTest("host C compiler unavailable")
        env = os.environ.copy()
        env["PATH"] = str(Path(cc).parent) + os.pathsep + env.get("PATH", "")
        flags = ["-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "src")]
        if os.environ.get("GAM4980_TEST_ASAN") == "1":
            flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        with tempfile.TemporaryDirectory(prefix="9288-native-graphics-") as tmp:
            tmp = Path(tmp)
            c, exe = tmp / "test.c", tmp / "test.exe"
            c.write_text(HARNESS, encoding="utf-8")
            built = subprocess.run([cc, *flags, str(c), "-o", str(exe)],
                                   env=env, capture_output=True, text=True, timeout=90)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(exe)], env=env, capture_output=True, text=True, timeout=90)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("native graphics IO PASS", run.stdout)
            print(run.stdout.strip())
            # Helpers must remain self-contained code when inlined into a NAT.
            c.write_text(PIC_PROBE, encoding="utf-8")
            obj = tmp / "pic.o"
            built = subprocess.run([cc, "-std=c99", "-O2", "-ffreestanding", "-fno-builtin",
                                    "-fPIC", "-I", str(ROOT / "src"), "-c", str(c), "-o", str(obj)],
                                   env=env, capture_output=True, text=True, timeout=90)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            nm = shutil.which("nm", path=env["PATH"])
            if nm:
                symbols = subprocess.run([nm, "-u", str(obj)], env=env,
                                         capture_output=True, text=True, timeout=30)
                self.assertEqual(symbols.returncode, 0, symbols.stderr)
                self.assertEqual(symbols.stdout.strip(), "", "unexpected external reference: " + symbols.stdout)


if __name__ == "__main__":
    unittest.main()
