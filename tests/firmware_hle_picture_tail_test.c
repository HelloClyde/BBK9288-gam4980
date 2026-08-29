#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DL_DOWN
#define _RLS_
#define GAM4980_ENABLE_AOT
#define GAM4980_ENABLE_FIRMWARE_HLE
#define GAM4980_ENABLE_AGGRESSIVE_REGION_HLE
#define GAM4980_FIRMWARE_HLE_MASK 0x04u
#define GAM4980_PICTURE_TAIL_TEST_CONTROL
#include "../src/gam4980_core.c"

#define RANDOM_CASES 3000u

static uint32_t random_state = 0x6b1a6ba4u;

static uint32_t next_random(void)
{
    uint32_t value = random_state;

    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    random_state = value;
    return value;
}

static int load_exact(const char *path, uint8_t *data, size_t size)
{
    FILE *file = fopen(path, "rb");
    int ok;

    if (!file)
        return 0;
    ok = fread(data, 1u, size, file) == size && fgetc(file) == EOF;
    fclose(file);
    return ok;
}

static int cpu_equal(const s6502_t *left, const s6502_t *right)
{
    return left->pc == right->pc && left->ac == right->ac &&
        left->ix == right->ix && left->iy == right->iy &&
        left->sp == right->sp && left->status == right->status;
}

static int run_case(
    uint32_t case_id, int has_next_source, uint8_t shift, uint8_t row,
    uint8_t column, uint8_t rows, uint8_t picture_mode, uint16_t destination,
    uint8_t *initial_ram, uint8_t *reference_ram
)
{
    s6502_t initial_cpu;
    s6502_t reference_cpu;
    s6502_t actual_cpu;
    uint16_t source = (uint16_t)(0x3000u + (next_random() & 0x01ffu));
    uint32_t reference_cycles;
    uint32_t actual_cycles;
    uint32_t index;
    uint32_t difference = 0u;
    int reference_lcd_dirty;
    int actual_lcd_dirty;

    for (index = 0u; index < GAM4980_RAM_SIZE; ++index)
        sys.ram[index] = (uint8_t)next_random();
    sys.ram[_SYSCON] = 0u;
    sys.ram[0x03e5u] = 1u;
    sys.ram[0x03e6u] = 0u;
    sys.ram[0x03e7u] = 4u;
    sys.ram[0x03e8u] = 0u;
    sys.ram[0x03e9u] = 0x10u;
    sys.ram[0x2fu] = (uint8_t)source;
    sys.ram[0x30u] = (uint8_t)(source >> 8);
    sys.ram[0x38u] = (uint8_t)destination;
    sys.ram[0x39u] = (uint8_t)(destination >> 8);
    sys.ram[0x3au] = (uint8_t)destination;
    sys.ram[0x3bu] = (uint8_t)(destination >> 8);
    sys.ram[0x20e9u] = (uint8_t)destination;
    sys.ram[0x20eau] = (uint8_t)(destination >> 8);
    sys.ram[0x2081u] = column;
    sys.ram[0x2082u] = row;
    sys.ram[0x2083u] = has_next_source
        ? (uint8_t)((column & 7u) | 0x18u)
        : (uint8_t)(((column & 7u) - 1u) & 7u);
    if (!has_next_source && (column & 7u) == 0u) {
        sys.ram[0x2081u] = (uint8_t)((column & 0xf8u) | 1u);
        sys.ram[0x2083u] = 0u;
    }
    sys.ram[0x20cfu] = shift;
    sys.ram[0x20dau] = rows;
    sys.ram[0x20e4u] = (uint8_t)next_random();
    sys.ram[0x20e7u] = picture_mode;
    sys.ram[0x4000u] = 0x4cu;
    sys.ram[0x4001u] = 0x00u;
    sys.ram[0x4002u] = 0x40u;

    initial_cpu.pc = has_next_source ? 0x6b1au : 0x6ba4u;
    initial_cpu.ac = (uint8_t)next_random();
    initial_cpu.ix = (uint8_t)next_random();
    initial_cpu.iy = (uint8_t)next_random();
    initial_cpu.sp = (uint8_t)next_random();
    initial_cpu.status = (uint8_t)(next_random() & ~0x08u);
    sys.ram[0x100u | (uint8_t)(initial_cpu.sp + 1u)] = 0xffu;
    sys.ram[0x100u | (uint8_t)(initial_cpu.sp + 2u)] = 0x3fu;
    memcpy(initial_ram, sys.ram, GAM4980_RAM_SIZE);

    reference_cpu = initial_cpu;
    lcd_dirty = 0;
    s6502_hle_shift_cache.valid = 0u;
    memset(s6502_firmware_hle_picture_tail_trace_count, 0,
        sizeof(s6502_firmware_hle_picture_tail_trace_count));
    s6502_firmware_hle_picture_tail_trace_mode = 1;
    gam4980_set_firmware_hle_enabled(1);
    s6502_firmware_hle_picture_tail_test_enabled = 0;
    reference_cycles = s6502_exec(&reference_cpu, 0x800u);
    reference_lcd_dirty = lcd_dirty;
    memcpy(reference_ram, sys.ram, GAM4980_RAM_SIZE);
    memcpy(sys.ram, initial_ram, GAM4980_RAM_SIZE);
    actual_cpu = initial_cpu;
    lcd_dirty = 0;
    s6502_hle_shift_cache.valid = 0u;
    s6502_firmware_hle_picture_tail_trace_mode = 2;
    gam4980_set_firmware_hle_enabled(1);
    s6502_firmware_hle_picture_tail_test_enabled = 1;
    actual_cycles = s6502_exec(&actual_cpu, 0x800u);
    actual_lcd_dirty = lcd_dirty;
    if (actual_cycles == reference_cycles &&
        cpu_equal(&actual_cpu, &reference_cpu) &&
        actual_lcd_dirty == reference_lcd_dirty &&
        memcmp(sys.ram, reference_ram, GAM4980_RAM_SIZE) == 0) {
        uint32_t slice_cycles = reference_cycles + (next_random() & 0x03ffu);

        memcpy(sys.ram, initial_ram, GAM4980_RAM_SIZE);
        reference_cpu = initial_cpu;
        lcd_dirty = 0;
        s6502_hle_shift_cache.valid = 0u;
        memset(s6502_firmware_hle_picture_tail_trace_count, 0,
            sizeof(s6502_firmware_hle_picture_tail_trace_count));
        s6502_firmware_hle_picture_tail_trace_mode = 1;
        gam4980_set_firmware_hle_enabled(1);
        s6502_firmware_hle_picture_tail_test_enabled = 0;
        reference_cycles = s6502_exec(&reference_cpu, slice_cycles);
        reference_lcd_dirty = lcd_dirty;
        memcpy(reference_ram, sys.ram, GAM4980_RAM_SIZE);

        memcpy(sys.ram, initial_ram, GAM4980_RAM_SIZE);
        actual_cpu = initial_cpu;
        lcd_dirty = 0;
        s6502_hle_shift_cache.valid = 0u;
        s6502_firmware_hle_picture_tail_trace_mode = 2;
        gam4980_set_firmware_hle_enabled(1);
        s6502_firmware_hle_picture_tail_test_enabled = 1;
        actual_cycles = s6502_exec(&actual_cpu, slice_cycles);
        actual_lcd_dirty = lcd_dirty;
        if (actual_cycles == reference_cycles &&
            cpu_equal(&actual_cpu, &reference_cpu) &&
            actual_lcd_dirty == reference_lcd_dirty &&
            memcmp(sys.ram, reference_ram, GAM4980_RAM_SIZE) == 0)
            return 1;
    }

    while (difference < GAM4980_RAM_SIZE &&
           sys.ram[difference] == reference_ram[difference])
        ++difference;
    fprintf(stderr,
        "picture-tail mismatch case=%lu kind=%d shift=%u row=%u col=%u "
        "rows=%u mode=%u dst=%04x cycles=%lu/%lu ram=%04lx\n"
        "ref pc=%04x a=%02x x=%02x y=%02x sp=%02x p=%02x dirty=%d\n"
        "got pc=%04x a=%02x x=%02x y=%02x sp=%02x p=%02x dirty=%d\n",
        (unsigned long)case_id, has_next_source, shift, row, column, rows,
        picture_mode, destination, (unsigned long)reference_cycles,
        (unsigned long)actual_cycles, (unsigned long)difference,
        reference_cpu.pc, reference_cpu.ac, reference_cpu.ix,
        reference_cpu.iy, reference_cpu.sp, reference_cpu.status,
        reference_lcd_dirty, actual_cpu.pc, actual_cpu.ac, actual_cpu.ix,
        actual_cpu.iy, actual_cpu.sp, actual_cpu.status, actual_lcd_dirty);
    fprintf(stderr,
        "scratch ref ea=%04x et=%04x dt=%02x ex=%u; "
        "got ea=%04x et=%04x dt=%02x ex=%u\n",
        s6502_firmware_hle_picture_tail_trace_ea[0],
        s6502_firmware_hle_picture_tail_trace_et[0],
        s6502_firmware_hle_picture_tail_trace_dt[0],
        s6502_firmware_hle_picture_tail_trace_executed[0],
        s6502_firmware_hle_picture_tail_trace_ea[1],
        s6502_firmware_hle_picture_tail_trace_et[1],
        s6502_firmware_hle_picture_tail_trace_dt[1],
        s6502_firmware_hle_picture_tail_trace_executed[1]);
    return 0;
}

int main(int argc, char **argv)
{
    gam4980_buffers_t buffers;
    uint8_t *initial_ram;
    uint8_t *reference_ram;
    uint32_t case_id;
    int result = 2;

    if (argc != 3) {
        fprintf(stderr, "usage: %s 8.BIN E.BIN\n", argv[0]);
        return 2;
    }
    memset(&buffers, 0, sizeof(buffers));
    buffers.ram = (uint8_t *)malloc(GAM4980_RAM_SIZE);
    buffers.flash = (uint8_t *)malloc(GAM4980_FLASH_SIZE);
    buffers.rom_8 = (uint8_t *)malloc(GAM4980_ROM_SIZE);
    buffers.rom_e = (uint8_t *)malloc(GAM4980_ROM_SIZE);
    buffers.flash_size = GAM4980_FLASH_SIZE;
    initial_ram = (uint8_t *)malloc(GAM4980_RAM_SIZE);
    reference_ram = (uint8_t *)malloc(GAM4980_RAM_SIZE);
    if (!buffers.ram || !buffers.flash || !buffers.rom_8 || !buffers.rom_e ||
        !initial_ram || !reference_ram ||
        !load_exact(argv[1], buffers.rom_8, GAM4980_ROM_SIZE) ||
        !load_exact(argv[2], buffers.rom_e, GAM4980_ROM_SIZE) ||
        gam4980_init(&buffers) <= 0)
        goto cleanup;
    sys.bk_tab[3] = 0x0003u;
    sys.bk_tab[4] = 0x0004u;
    sys.bk_tab[6] = 0x0eb5u;
    mem_bs(3u);
    mem_bs(4u);
    mem_bs(6u);
    if (!s6502_firmware_hle_shift_region_match())
        goto cleanup_core;

    for (case_id = 0u; case_id < RANDOM_CASES; ++case_id) {
        int has_next_source = (int)(case_id & 1u);
        uint8_t rows = (uint8_t)((next_random() & 1u) + 1u);
        uint8_t picture_mode = case_id % 3u == 0u ? 4u : 3u;
        uint16_t destination = (case_id % 17u) == 0u
            ? 0x0400u
            : (uint16_t)(0x0800u + (next_random() & 0x01ffu));

        if (!run_case(
                case_id, has_next_source,
                (uint8_t)(next_random() & 7u),
                (uint8_t)next_random(), (uint8_t)next_random(), rows,
                picture_mode, destination, initial_ram, reference_ram)) {
            result = 1;
            goto cleanup_core;
        }
    }
    printf("firmware picture-tail HLE: %lu exact-state cases passed\n",
        (unsigned long)RANDOM_CASES);
    result = 0;

cleanup_core:
    gam4980_deinit();
cleanup:
    free(reference_ram);
    free(initial_ram);
    free(buffers.rom_e);
    free(buffers.rom_8);
    free(buffers.flash);
    free(buffers.ram);
    return result;
}
