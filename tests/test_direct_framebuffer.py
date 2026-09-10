"""Run the actual 9288 expansion helper against an independent pixel renderer.

The host harness extracts production functions rather than maintaining a second
optimized renderer. Set CC for a host compiler and GAM4980_TEST_ASAN=1 to enable
address/undefined sanitizers on a supported compiler.
"""

import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src/gam4980_9288.c"


def function_source(source, name):
    """Extract a complete C function, ignoring braces in comments/strings."""
    match = re.search(
        r"^(?:static\s+)?[^;{}\n]*\b" + re.escape(name) + r"\s*\([^;{}]*\)\s*\{",
        source, re.M,
    )
    if not match:
        raise AssertionError(f"production function {name} not found")
    tokens = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', re.S)
    depth = 0
    for token in tokens.finditer(source, match.end() - 1):
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if not depth:
                return source[match.start():token.end()]
    raise AssertionError(f"unterminated production function {name}")


HARNESS_PREFIX = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
typedef uint8_t u8;
typedef uint32_t u32;
#define GAM_SCREEN_WIDTH 320
#define GAM_SCREEN_HEIGHT 240
#define SCREEN_PITCH_BYTES 80
#define SCREEN_FRAME_BYTES (SCREEN_PITCH_BYTES * GAM_SCREEN_HEIGHT)
#define VIEW_X 1
#define VIEW_Y 24
#define GAM4980_LCD_WIDTH 159
#define GAM4980_LCD_HEIGHT 96
#define GAM4980_LCD_PACKED_STRIDE 20
#define PACKED_BYTES (GAM4980_LCD_HEIGHT * GAM4980_LCD_PACKED_STRIDE)
static u32 g_expand_2x_shifted[2][256];
static u32 dirty[3];
static unsigned mask_reads;
static unsigned checks;
static u32 random_state = 0x92884980u;
static u8 native_graphics_synced_frame[1920];
static u32 native_graphics_synced_rows[3];
static u32 gam4980_changed_row_mask(u32 word) {
    if (word >= 3u) { fprintf(stderr, "invalid dirty word %u\n", word); exit(2); }
    ++mask_reads;
    return dirty[word];
}
'''


HARNESS_SUFFIX = r'''
static u32 next_random(void) {
    u32 v = random_state;
    v ^= v << 13; v ^= v >> 17; v ^= v << 5;
    return random_state = v;
}
static void fill_random(void *memory, size_t size) {
    u8 *p = (u8 *)memory;
    while (size--) *p++ = (u8)next_random();
}
static void pixel(u8 *frame, unsigned x, unsigned y, unsigned shade) {
    unsigned shift = 6u - 2u * (x & 3u);
    unsigned offset = y * SCREEN_PITCH_BYTES + x / 4u;
    frame[offset] = (u8)((frame[offset] & ~(3u << shift)) | (shade << shift));
}
static u32 reference(u8 *frame, const u8 *packed, int force_all) {
    unsigned y, x, dy, dx;
    u32 rows = 0u;
    if (!packed) return 0u;
    if (force_all) {
        memset(frame, 0xff, SCREEN_FRAME_BYTES);
        rows = GAM_SCREEN_HEIGHT - GAM4980_LCD_HEIGHT * 2u;
    }
    for (y = 0; y < GAM4980_LCD_HEIGHT; ++y) {
        if (!force_all && !(dirty[y / 32u] & (1u << (y % 32u)))) continue;
        rows += 2u;
        /* Include the two one-pixel margins without using the production LUT. */
        for (dy = 0; dy < 2u; ++dy) {
            pixel(frame, 0u, VIEW_Y + y * 2u + dy, 3u);
            pixel(frame, GAM_SCREEN_WIDTH - 1u, VIEW_Y + y * 2u + dy, 3u);
        }
        for (x = 0; x < GAM4980_LCD_WIDTH; ++x) {
            unsigned black = (packed[y * GAM4980_LCD_PACKED_STRIDE + x / 8u]
                              >> (7u - x % 8u)) & 1u;
            for (dy = 0; dy < 2u; ++dy)
                for (dx = 0; dx < 2u; ++dx)
                    pixel(frame, VIEW_X + x * 2u + dx,
                          VIEW_Y + y * 2u + dy, black ? 0u : 3u);
        }
    }
    return rows;
}
struct GuardedFrame {
    u32 before[16];
    u32 frame[SCREEN_FRAME_BYTES / 4];
    u32 after[16];
};
static void run_case(const char *label, unsigned number, const u8 *packed,
                     int force_all) {
    struct GuardedFrame got, expected;
    u8 source_copy[PACKED_BYTES];
    u32 rows, expected_rows;
    size_t offset;
    fill_random(&got, sizeof(got));
    memcpy(&expected, &got, sizeof(got));
    if (packed) memcpy(source_copy, packed, sizeof(source_copy));
    expected_rows = reference((u8 *)expected.frame, packed, force_all);
    mask_reads = 0u;
    rows = expand_2x_to(packed, got.frame, force_all);
    if (rows != expected_rows) {
        fprintf(stderr, "%s[%u]: row count %u, expected %u\n",
                label, number, rows, expected_rows);
        exit(3);
    }
    if (memcmp(&got, &expected, sizeof(got))) {
        for (offset = 0; offset < sizeof(got); ++offset)
            if (((u8 *)&got)[offset] != ((u8 *)&expected)[offset]) break;
        fprintf(stderr, "%s[%u]: mismatch at guarded byte %zu: %02x != %02x\n",
                label, number, offset, ((u8 *)&got)[offset], ((u8 *)&expected)[offset]);
        exit(4);
    }
    if (packed && memcmp(source_copy, packed, sizeof(source_copy))) {
        fprintf(stderr, "%s[%u]: source modified\n", label, number); exit(5);
    }
    ++checks;
}
static void test_zero_writes(const u8 *packed) {
    void *readonly;
    u32 rows;
    const size_t allocation_size = 24576u;
#ifdef _WIN32
    DWORD old_protection;
    readonly = VirtualAlloc(NULL, allocation_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!readonly) { fprintf(stderr, "VirtualAlloc failed\n"); exit(6); }
    memset(readonly, 0xa5, allocation_size);
    if (!VirtualProtect(readonly, allocation_size, PAGE_READONLY, &old_protection)) exit(6);
#else
    readonly = mmap(NULL, allocation_size, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (readonly == MAP_FAILED) { perror("mmap"); exit(6); }
    memset(readonly, 0xa5, allocation_size);
    if (mprotect(readonly, allocation_size, PROT_READ)) { perror("mprotect"); exit(6); }
#endif
    dirty[0] = dirty[1] = dirty[2] = 0u;
    rows = expand_2x_to(packed, (volatile u32 *)readonly, 0);
    if (rows) { fprintf(stderr, "no-dirty unexpectedly wrote %u rows\n", rows); exit(6); }
#ifdef _WIN32
    VirtualFree(readonly, 0, MEM_RELEASE);
#else
    munmap(readonly, allocation_size);
#endif
    ++checks;
}
int main(void) {
    u8 packed[PACKED_BYTES];
    u32 padding_first[SCREEN_FRAME_BYTES / 4];
    u32 padding_second[SCREEN_FRAME_BYTES / 4];
    const unsigned boundaries[] = {0u, 31u, 32u, 63u, 64u, 95u};
    unsigned value, i, y;
    if (((u8 *)&(u32){1u})[0] != 1u) {
        fprintf(stderr, "9288 LUT requires a little-endian host\n"); return 7;
    }
    init_screen_expansion();
    /* A stale LCD must be repaired even with identical guest/shadow data and
     * no dirty rows. run_case starts with unrelated physical pixel contents. */
    fill_random(packed, sizeof(packed));
    memcpy(native_graphics_synced_frame, packed, sizeof(packed));
    native_graphics_synced_rows[0] = native_graphics_synced_rows[1] = native_graphics_synced_rows[2] = 0xffffffffu;
    dirty[0] = dirty[1] = dirty[2] = 0u;
    run_case("unchanged-full-repair", 0u, packed, 1);
    dirty[0] = dirty[1] = dirty[2] = 0xffffffffu;
    for (value = 0; value < 256u; ++value) {
        memset(packed, (int)value, sizeof(packed));
        run_case("all-byte-values", value, packed, 0);
        /* Alternation exercises the carry across every packed-byte boundary. */
        for (i = 0; i < sizeof(packed); ++i)
            packed[i] = (u8)((i & 1u) ? value : (value ^ 255u));
        run_case("cross-byte-carry", value, packed, 1);
    }
    for (i = 0; i < sizeof(boundaries) / sizeof(boundaries[0]); ++i) {
        fill_random(packed, sizeof(packed));
        dirty[0] = dirty[1] = dirty[2] = 0u;
        y = boundaries[i];
        dirty[y / 32u] = 1u << (y % 32u);
        run_case("dirty-word-boundary", y, packed, 0);
    }
    for (i = 0; i < 1024u; ++i) {
        fill_random(packed, sizeof(packed));
        dirty[0] = next_random(); dirty[1] = next_random(); dirty[2] = next_random();
        run_case("random-dirty", i, packed, 0);
        if (!(i & 15u)) run_case("random-forced", i, packed, 1);
    }
    dirty[0] = dirty[1] = dirty[2] = 0u;
    run_case("no-dirty", 0u, packed, 0);
    run_case("restore-with-no-guest-change", 0u, packed, 1);
    run_case("null-source", 0u, NULL, 0);
    test_zero_writes(packed);
    /* The hidden 160th guest bit must never change the visible right margin. */
    for (y = 0; y < GAM4980_LCD_HEIGHT; ++y)
        packed[y * GAM4980_LCD_PACKED_STRIDE + 19u] &= 0xfeu;
    expand_2x_to(packed, padding_first, 1);
    for (y = 0; y < GAM4980_LCD_HEIGHT; ++y)
        packed[y * GAM4980_LCD_PACKED_STRIDE + 19u] |= 1u;
    expand_2x_to(packed, padding_second, 1);
    if (memcmp(padding_first, padding_second, sizeof(padding_first))) {
        fprintf(stderr, "padding bit changed visible framebuffer\n"); return 8;
    }
    ++checks;
    printf("direct framebuffer: %u cases passed; dirty, carry, margins, padding, guards, zero writes\n", checks);
    return 0;
}
'''


class DirectFramebufferTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE.read_text(encoding="utf-8")

    def test_full_submission_and_no_nat_physical_surface(self):
        bare = function_source(self.source, 'present_2x_bare')
        self.assertIn('force_all = 1;', bare)
        self.assertLess(bare.index('force_all = 1;'), bare.index('expand_2x_to('))
        self.assertRegex(function_source(self.source, 'expand_2x'), r'g_screen_frame,\s*1')
        self.assertNotRegex(self.source, r'gam4980_native_graphics_set_display\(\s*0x003c0000')
        self.assertIn('"native_graphics_direct_mirror", 0u', self.source)

    def test_real_expansion_matches_independent_pixels(self):
        compiler = os.environ.get("CC") or shutil.which("gcc")
        if not compiler and Path("C:/msys64/ucrt64/bin/gcc.exe").is_file():
            compiler = "C:/msys64/ucrt64/bin/gcc.exe"
        if not compiler:
            self.skipTest("host C compiler unavailable; set CC")
        pair = re.search(r"static const u8 k_expand_2x_pair\[4\]\s*=\s*\{[^}]+\};", self.source)
        self.assertIsNotNone(pair, "production expansion pair LUT missing")
        program = "\n".join((
            HARNESS_PREFIX, pair.group(),
            function_source((ROOT / 'src/gam4980_core.c').read_text(encoding='utf-8'), "gam4980_native_graphics_row_synced"),
            function_source((ROOT / 'src/gam4980_core.c').read_text(encoding='utf-8'), "gam4980_native_graphics_row_presented"),
            function_source((ROOT / 'src/gam4980_core.c').read_text(encoding='utf-8'), "gam4980_native_graphics_invalidate_sync"),
            function_source(self.source, "init_screen_expansion"),
            function_source(self.source, "expand_2x_to"), HARNESS_SUFFIX,
        ))
        with tempfile.TemporaryDirectory(prefix="9288-direct-frame-") as temp:
            folder = Path(temp)
            harness = folder / "direct_frame.c"
            executable = folder / ("direct_frame.exe" if os.name == "nt" else "direct_frame")
            harness.write_text(program, encoding="utf-8")
            command = [compiler, "-std=c99", "-D_GNU_SOURCE", "-O2", "-Wall", "-Wextra", "-Werror"]
            if os.environ.get("GAM4980_TEST_ASAN") == "1":
                command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g"]
                if os.name != "nt":
                    command.append("-no-pie")
            command += [str(harness), "-o", str(executable)]
            environment = os.environ.copy()
            environment["PATH"] = str(Path(compiler).resolve().parent) + os.pathsep + environment.get("PATH", "")
            compiled = subprocess.run(command, capture_output=True, text=True, env=environment, timeout=60)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True, env=environment, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("cases passed", result.stdout)
            print(result.stdout.strip())

    def test_bare_direct_path_and_force_repair_contract(self):
        bare = re.sub(r"/\*.*?\*/|//[^\n]*", "", function_source(self.source, "present_2x_bare"), flags=re.S)
        loop = function_source(self.source, "run_bare_emulator_window")
        gui = function_source(self.source, "present_2x")
        wrapper = function_source(self.source, "expand_2x")
        self.assertIn("expand_2x_to", bare)
        self.assertRegex(bare.lower(), r"0x0*3c0000")
        self.assertNotIn("gam4980_9288_bare_submit(", bare)
        self.assertNotIn("g_screen_frame", bare)
        self.assertIn("submit_screen_frame();", gui)
        self.assertIn("g_screen_frame", wrapper)
        self.assertRegex(wrapper, r"expand_2x_to\([^;]+,\s*1\s*\)")
        self.assertRegex(loop, r"force_full_present\s*=\s*1")
        self.assertGreaterEqual(len(re.findall(r"force_full_present\s*=\s*1", loop)), 2)
        self.assertIn("gam_present_decide", loop)
        self.assertRegex(loop, r"if\s*\(\s*reason\s*\|\|\s*force_full_present\s*\)")
        self.assertRegex(loop, r"present_2x_bare\(\s*display,\s*force_full_present\s*\)")
        self.assertRegex(loop, r"force_full_present\s*=\s*0")
        suspend = loop.index("if (last_rom_suspends !=")
        render = loop.index("frame_changed = gam4980_render_frame();", suspend)
        self.assertIn("force_full_present = 1", loop[suspend:render])


if __name__ == "__main__":
    unittest.main()
