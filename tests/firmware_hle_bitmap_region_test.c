#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DL_DOWN
#define _RLS_
#define GAM4980_ENABLE_AOT
#define GAM4980_ENABLE_FIRMWARE_HLE
#define GAM4980_ENABLE_AGGRESSIVE_REGION_HLE
#define GAM4980_FIRMWARE_HLE_MASK 0x01u
#include "../src/gam4980_core.c"

#define RANDOM_CASES 3000u

static uint32_t random_state = 0x5c5d9288u;

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
    uint32_t case_id, uint8_t width, uint8_t rows, uint8_t shift,
    uint8_t *initial_ram, uint8_t *reference_ram
)
{
    s6502_t initial_cpu;
    s6502_t reference_cpu;
    uint16_t source = (uint16_t)(0x3000u + (next_random() & 0x03ffu));
    uint16_t destination = (uint16_t)(0x0600u + (next_random() & 0x00ffu));
    uint16_t return_pc = (uint16_t)(0x4000u + (next_random() & 0x0fffu));
    uint32_t reference_cycles = 0u;
    uint32_t hle_cycles;
    uint32_t instruction_groups = 0u;
    uint32_t index;
    int reference_lcd_dirty;

    for (index = 0u; index < GAM4980_RAM_SIZE; ++index)
        sys.ram[index] = (uint8_t)next_random();
    sys.ram[_SYSCON] = 0u;
    sys.ram[0x2fu] = (uint8_t)source;
    sys.ram[0x30u] = (uint8_t)(source >> 8);
    sys.ram[0x31u] = (uint8_t)destination;
    sys.ram[0x32u] = (uint8_t)(destination >> 8);
    sys.ram[0x2081u] = (uint8_t)next_random();
    sys.ram[0x2083u] = (uint8_t)next_random();
    sys.ram[0x20cfu] = shift;
    sys.ram[0x20dau] = rows;
    sys.ram[0x20deu] = rows ? (uint8_t)(next_random() & 0x0fu) : 0u;
    sys.ram[0x20e3u] = (uint8_t)next_random();
    sys.ram[0x20e4u] = (uint8_t)next_random();
    sys.ram[0x20e8u] = width;
    initial_cpu.pc = 0x5c5du;
    initial_cpu.ac = (uint8_t)next_random();
    initial_cpu.ix = (uint8_t)next_random();
    initial_cpu.iy = (uint8_t)next_random();
    initial_cpu.sp = (uint8_t)next_random();
    initial_cpu.status = (uint8_t)(next_random() & ~0x08u);
    sys.ram[0x100u | (uint8_t)(initial_cpu.sp + 1u)] =
        (uint8_t)(return_pc - 1u);
    sys.ram[0x100u | (uint8_t)(initial_cpu.sp + 2u)] =
        (uint8_t)((return_pc - 1u) >> 8);
    memcpy(initial_ram, sys.ram, GAM4980_RAM_SIZE);

    reference_cpu = initial_cpu;
    lcd_dirty = 0;
    gam4980_set_firmware_hle_enabled(0);
    while (reference_cpu.pc != return_pc && instruction_groups < 1000000u) {
        reference_cycles += s6502_exec(&reference_cpu, 1u);
        ++instruction_groups;
    }
    reference_lcd_dirty = lcd_dirty;
    memcpy(reference_ram, sys.ram, GAM4980_RAM_SIZE);
    if (reference_cpu.pc != return_pc || !reference_cycles) {
        fprintf(stderr, "reference did not return case=%lu pc=%04x\n",
            (unsigned long)case_id, reference_cpu.pc);
        return 0;
    }

    memcpy(sys.ram, initial_ram, GAM4980_RAM_SIZE);
    lcd_dirty = 0;
    gam4980_set_firmware_hle_enabled(1);
    hle_cycles = s6502_exec(&initial_cpu, reference_cycles);
    if (hle_cycles != reference_cycles ||
        !cpu_equal(&initial_cpu, &reference_cpu) ||
        lcd_dirty != reference_lcd_dirty ||
        memcmp(sys.ram, reference_ram, GAM4980_RAM_SIZE) != 0) {
        uint32_t difference = 0u;

        while (difference < GAM4980_RAM_SIZE &&
               sys.ram[difference] == reference_ram[difference])
            ++difference;
        fprintf(
            stderr,
            "mismatch case=%lu w=%u rows=%u shift=%u cycles=%lu/%lu "
            "ram=%04lx\n"
            "ref pc=%04x a=%02x x=%02x y=%02x sp=%02x p=%02x dirty=%d\n"
            "hle pc=%04x a=%02x x=%02x y=%02x sp=%02x p=%02x dirty=%d\n",
            (unsigned long)case_id, width, rows, shift,
            (unsigned long)reference_cycles, (unsigned long)hle_cycles,
            (unsigned long)difference,
            reference_cpu.pc, reference_cpu.ac, reference_cpu.ix,
            reference_cpu.iy, reference_cpu.sp, reference_cpu.status,
            reference_lcd_dirty,
            initial_cpu.pc, initial_cpu.ac, initial_cpu.ix, initial_cpu.iy,
            initial_cpu.sp, initial_cpu.status, lcd_dirty
        );
        return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    static const uint8_t widths[] = {0u, 1u, 2u, 3u, 7u, 16u};
    static const uint8_t row_counts[] = {0u, 1u, 2u, 3u, 7u};
    gam4980_buffers_t buffers;
    uint8_t *initial_ram;
    uint8_t *reference_ram;
    uint32_t case_id = 0u;
    size_t width_index;
    size_t row_index;
    uint8_t shift;
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
        gam4980_init(&buffers) <= 0) {
        fprintf(stderr, "could not initialize exact ROM test environment\n");
        goto cleanup;
    }
    sys.bk_tab[2] = 0x0002u;
    sys.bk_tab[5] = 0x0eb8u;
    mem_bs(2u);
    mem_bs(5u);
    gam4980_set_performance_debug(1);
    if (!s6502_firmware_hle_bitmap_region_match()) {
        fprintf(stderr, "firmware bitmap-region signature did not match\n");
        result = 2;
        goto cleanup_core;
    }

    for (width_index = 0u;
         width_index < sizeof(widths) / sizeof(widths[0]); ++width_index) {
        for (row_index = 0u;
             row_index < sizeof(row_counts) / sizeof(row_counts[0]);
             ++row_index) {
            for (shift = 0u; shift < 8u; ++shift) {
                if (!run_case(
                        case_id++, widths[width_index],
                        row_counts[row_index], shift,
                        initial_ram, reference_ram)) {
                    result = 1;
                    goto cleanup_core;
                }
            }
        }
    }
    while (case_id < RANDOM_CASES) {
        if (!run_case(
                case_id++, (uint8_t)(next_random() & 0x1fu),
                (uint8_t)((next_random() & 7u) + 1u),
                (uint8_t)(next_random() & 7u),
                initial_ram, reference_ram)) {
            result = 1;
            goto cleanup_core;
        }
    }
    printf("firmware bitmap-region HLE: %lu exact-state cases passed\n",
        (unsigned long)case_id);
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
