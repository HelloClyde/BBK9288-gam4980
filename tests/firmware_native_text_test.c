#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DL_DOWN
#define _RLS_
#define GAM4980_ENABLE_AOT
#define GAM4980_ENABLE_FIRMWARE_HLE
#define GAM4980_FIRMWARE_HLE_MASK 0x3ffu
#include "../src/gam4980_core.c"
typedef __UINTPTR_TYPE__ uintptr_t;

/* Pointer-sized host mirrors of the all-32-bit target service ABI. */
typedef struct {
    uint32_t pc, ac, ix, iy, sp, status, cycle_budget, cycles;
    uintptr_t ram, read8, write8, graphics;
    uintptr_t pages, dirty, lcd_write_calls, lcd_changed_writes;
} host_text_context;
typedef struct {
    uint32_t version;
    uintptr_t framebuffer, expand_lut, page3, banks, write8_resolved;
    uintptr_t clock, poll, metrics;
    uintptr_t batch, synced_frame, synced_rows;
} host_text_graphics;
#define FW_GRAPHICS_SERVICE_VERSION 2u
#define FIRMWARE_NATIVE_HOST_TEST
#define s6502_iram_asm_context_t host_text_context
#define firmware_native_graphics_services_t host_text_graphics
#include "../src/firmware_native_text.c"
#undef firmware_native_graphics_services_t
#undef s6502_iram_asm_context_t
#undef FIRMWARE_NATIVE_HOST_TEST

#define FRAME_BYTES (80u * 240u)
struct test_frame {
    uint32_t before[8];
    uint32_t pixels[FRAME_BYTES / 4u];
    uint32_t after[8];
};
static uint32_t random_state = 0x650f608au;
static uint32_t poll_calls;
static uint32_t next_random(void)
{
    uint32_t x = random_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return random_state = x;
}
static int load_exact(const char *path, uint8_t *data, size_t size)
{
    FILE *file = fopen(path, "rb");
    int ok;
    if (!file) return 0;
    ok = fread(data, 1, size, file) == size && fgetc(file) == EOF;
    fclose(file);
    return ok;
}
static uint32_t resolved_write(uint16_t address, uint8_t value)
{
    uint32_t physical = 0xffffffffu;
    if (sys.mem_iw[address >> 8] == ram_write) physical = address;
    else if (sys.mem_iw[address >> 8] == ram_vwrite) physical = PA(address);
    mem_write(address, value);
    return physical;
}
static void input_checkpoint(void) { ++poll_calls; }

/* Read the actual core LCD snapshot and independently place 2x 2-bpp pixels.
 * No NAT mapping/expansion helper is used by this reference. */
static void reference_screen(uint32_t *target)
{
    uint8_t *pixels = (uint8_t *)target;
    uint8_t saved_0400 = sys.ram[0x400u];
    unsigned x, y, dx, dy;
    lcd_frame_valid = 0; lcd_dirty = 1;
    (void)gam4980_render_frame();
    /* Core scanout refreshes an invisible alias scratch byte. That host
     * presentation side effect must not become part of the ROM reference. */
    sys.ram[0x400u] = saved_0400;
    memset(pixels, 0xff, FRAME_BYTES);
    for (y = 0; y < 96u; ++y) for (x = 0; x < 159u; ++x) {
        unsigned black = (lcd_frame[y * 20u + x / 8u] >> (7u - x % 8u)) & 1u;
        if (!black) continue;
        for (dy = 0; dy < 2u; ++dy) for (dx = 0; dx < 2u; ++dx) {
            unsigned px = 1u + x * 2u + dx, py = 24u + y * 2u + dy;
            pixels[py * 80u + px / 4u] &= (uint8_t)~(3u << (6u - 2u * (px % 4u)));
        }
    }
}
static uint16_t row_address(unsigned column, unsigned y)
{
    if (!column) return y == 65u ? 0x0ff3u :
        (uint16_t)(0x0413u + (y < 65u ? 64u - y : y - 1u) * 32u);
    return (uint16_t)(0x0400u + (y <= 65u ? 65u - y : y) * 32u + column - 1u);
}
static int same_cpu(const host_text_context *actual, const s6502_t *expected)
{
    return actual->pc == expected->pc && actual->ac == expected->ac &&
        actual->ix == expected->ix && actual->iy == expected->iy &&
        actual->sp == expected->sp && actual->status == expected->status;
}
int main(int argc, char **argv)
{
    gam4980_buffers_t buffers = {0};
    uint8_t *initial = malloc(GAM4980_RAM_SIZE), *expected = malloc(GAM4980_RAM_SIZE);
    struct test_frame actual_frame, expected_frame, initial_frame;
    unsigned wide, test, i, tests = 0u, flat_rows=0u;
    int result = 1;
    buffers.ram = malloc(GAM4980_RAM_SIZE);
    buffers.flash = calloc(1, GAM4980_FLASH_SIZE);
    buffers.rom_8 = malloc(GAM4980_ROM_SIZE);
    buffers.rom_e = malloc(GAM4980_ROM_SIZE);
    buffers.flash_size = GAM4980_FLASH_SIZE;
    if (argc != 3 || !initial || !expected || !buffers.ram || !buffers.flash ||
        !buffers.rom_8 || !buffers.rom_e ||
        !load_exact(argv[1], buffers.rom_8, GAM4980_ROM_SIZE) ||
        !load_exact(argv[2], buffers.rom_e, GAM4980_ROM_SIZE) ||
        gam4980_init(&buffers) <= 0) return 2;
    for (i = 1; i <= 4; ++i) { sys.bk_tab[i] = (uint16_t)i; mem_bs((uint8_t)i); }
    sys.bk_tab[6] = 0x0eb5u; mem_bs(6u);
    sys.bk_tab[7] = 4u; mem_bs(7u);
    sys.bk_tab[8] = 4u; mem_bs(8u);
    sys.bk_tab[9] = 0u; mem_bs(9u);
    sys.bk_tab[10] = 1u; mem_bs(10u);
    gam4980_set_firmware_hle_enabled(0);
    memset(s6502_aot_validation, 2, sizeof(s6502_aot_validation));
    for (wide = 0; wide < 2u; ++wide) for (test = 0; test < 624u; ++test) {
        s6502_t saved, reference;
        host_text_context c = {0}, unchanged;
        host_text_graphics g = {0};
        uint32_t metrics[4] = {0}, cycles = 0, got, rows, row, boundary;
        unsigned x = next_random() % (wide ? 145u : 153u), y = next_random() % 96u;
        unsigned source = 0x2200u + (next_random() & 255u);
        unsigned destination, alternate, start_x = next_random() % (wide ? 32u : 16u);
        unsigned remaining = ((wide ? 32u : 16u) - start_x + wide) / (1u + wide);
        unsigned bank_alias = (test % 7u) == 0u;
        unsigned show_frame = (test % 11u) != 0u;
        if (test < 8u) { x = (wide ? 144u : 152u); y = 63u + test % 4u; }
        if (test >= 8u && test < 16u) { x = test - 8u; y = 63u + test % 4u; }
        if (test >= 600u) { x = 16u; y = test < 612u ? 64u : 70u; }
        for (i = 0; i < GAM4980_RAM_SIZE; ++i) sys.ram[i] = (uint8_t)next_random();
        sys.ram[_SYSCON] = 0u;
        destination = row_address(x < 8u ? 1u : x >> 3, y);
        alternate = row_address(0u, y);
        if (test % 13u == 0u) source = destination; /* Ordered source/LCD overlap. */
        if (bank_alias) { destination += 0x9000u; alternate += 0x9000u; }
        if (test >= 600u) {
            /* ADC/SBC high-byte overflow and low-byte carry/borrow edges. */
            destination = alternate = test < 612u ?
                0x8000u + ((test - 600u) * 7u) :
                0x7fc0u + ((test - 612u) * 5u);
        }
        sys.ram[0x2f] = (uint8_t)source; sys.ram[0x30] = (uint8_t)(source >> 8);
        sys.ram[0x3a] = (uint8_t)destination; sys.ram[0x3b] = (uint8_t)(destination >> 8);
        sys.ram[0x38] = (uint8_t)alternate; sys.ram[0x39] = (uint8_t)(alternate >> 8);
        sys.ram[0x2081] = (uint8_t)x; sys.ram[0x2082] = (uint8_t)y;
        sys.ram[0x208b] = (uint8_t)(test & 7u);
        /* Exercise disabled aliases, exact alias match, and both mismatch checks. */
        s6502_page3[0xe5] = test % 5u ? 1u : 0u;
        s6502_page3[0xe6] = test % 5u == 2u ? (uint8_t)destination : 0u;
        s6502_page3[0xe7] = test % 5u == 2u ? (uint8_t)(destination >> 8) :
            test % 5u == 3u ? (uint8_t)(destination >> 8) : 4u;
        s6502_page3[0xe8] = 0u; s6502_page3[0xe9] = 0x10u;
        saved.pc = wide ? 0x608au : 0x650fu;
        saved.ac = (uint8_t)next_random(); saved.ix = (uint8_t)start_x;
        saved.iy = (uint8_t)next_random(); saved.sp = (uint8_t)next_random();
        saved.status = (uint8_t)(0x20u | (next_random() & 0xd7u));
        reference = saved;
        memcpy(initial, sys.ram, GAM4980_RAM_SIZE);
        memset(&initial_frame, 0xa5, sizeof(initial_frame));
        reference_screen(initial_frame.pixels);
        actual_frame = expected_frame = initial_frame;
        rows = 1u + (next_random() % remaining);
        if (test >= 600u) rows = 1u;
        boundary = wide ? 0x6086u : 0x650bu;
        for (row = 0; row < rows; ++row) {
            do {
                cycles += s6502_exec(&reference, 1u);
                if (cycles > 50000u) { fprintf(stderr, "reference loop runaway\n"); goto cleanup; }
            } while (reference.pc != boundary);
        }
        memcpy(expected, sys.ram, GAM4980_RAM_SIZE);
        if (show_frame) reference_screen(expected_frame.pixels);
        memcpy(sys.ram, initial, GAM4980_RAM_SIZE);
        g.version = FW_GRAPHICS_SERVICE_VERSION;
        g.framebuffer = show_frame ? (uintptr_t)actual_frame.pixels : 0u;
        g.page3 = (uintptr_t)s6502_page3; g.banks = (uintptr_t)sys.bk_tab;
        g.write8_resolved = (uintptr_t)resolved_write;
        g.poll = (uintptr_t)input_checkpoint; g.metrics = (uintptr_t)metrics;
        c.pc = saved.pc; c.ac = saved.ac; c.ix = saved.ix; c.iy = saved.iy;
        c.sp = saved.sp; c.status = saved.status; c.cycles = 17u;
        c.ram = (uintptr_t)sys.ram; c.read8 = (uintptr_t)mem_read;
        c.pages = (uintptr_t)sys.mem_r; c.dirty = (uintptr_t)&lcd_dirty;
        c.graphics = (uintptr_t)&g;
        c.cycle_budget = c.cycles + fnt_row_cycles(&c, c.ix, (int)wide) - 1u;
        unchanged = c;
        if (firmware_native_text(&c) || memcmp(&unchanged, &c, sizeof(c)) ||
            memcmp(initial, sys.ram, GAM4980_RAM_SIZE) ||
            memcmp(&initial_frame, &actual_frame, sizeof(actual_frame)) || metrics[0] || metrics[1] || metrics[2]) {
            fprintf(stderr, "short budget mutation wide=%u case=%u\n", wide, test); goto cleanup;
        }
        c.cycle_budget = c.cycles + cycles;
        poll_calls = 0u;
        got = firmware_native_text(&c);
        flat_rows+=metrics[3];
        if (got != cycles || c.cycles != 17u + cycles || !same_cpu(&c, &reference) ||
            memcmp(expected, sys.ram, GAM4980_RAM_SIZE) ||
            memcmp(&expected_frame, &actual_frame, sizeof(actual_frame)) ||
            metrics[2] != rows || poll_calls != rows / 4u) {
            unsigned difference = 0;
            while (difference < GAM4980_RAM_SIZE && expected[difference] == sys.ram[difference]) ++difference;
            fprintf(stderr, "text wide=%u case=%u xy=%u,%u rows=%u cyc=%u/%u RAM=%x screen=%d\n"
                    "ref pc=%x a=%x x=%x y=%x sp=%x p=%x\n"
                    "got pc=%x a=%x x=%x y=%x sp=%x p=%x metricsrows=%u\n",
                    wide, test, x, y, rows, got, cycles, difference,
                    memcmp(&expected_frame, &actual_frame, sizeof(actual_frame)),
                    reference.pc, reference.ac, reference.ix, reference.iy, reference.sp, reference.status,
                    c.pc, c.ac, c.ix, c.iy, c.sp, c.status, metrics[2]);
            for (i = difference; i < GAM4980_RAM_SIZE && i < difference + 64u; ++i)
                if (expected[i] != sys.ram[i])
                    fprintf(stderr, "ram[%x]=%02x/%02x initial=%02x\n", i,
                            sys.ram[i], expected[i], initial[i]);
            goto cleanup;
        }
        ++tests;
    }
    if(!flat_rows){fprintf(stderr,"flat glyph path was not exercised\n");goto cleanup;}
    printf("native text: %u exact ROM CPU/RAM/cycle/framebuffer cases passed; flat rows %u\n", tests,flat_rows);
    result = 0;
cleanup:
    gam4980_deinit(); free(initial); free(expected); free(buffers.ram);
    free(buffers.flash); free(buffers.rom_8); free(buffers.rom_e);
    return result;
}
