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
#include "../src/gam4980_core.c"

#define RANDOM_CASES 3000u

static uint32_t random_state = 0x69889288u;

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

static uint32_t run_until_return(
    s6502_t *cpu, uint16_t return_pc, uint32_t slice_cycles,
    uint32_t maximum_calls
)
{
    uint32_t cycles = 0u;
    uint32_t calls = 0u;

    while (cpu->pc != return_pc && calls < maximum_calls) {
        cycles += s6502_exec(cpu, slice_cycles);
        ++calls;
    }
    return cpu->pc == return_pc ? cycles : 0u;
}

static int compare_result(
    uint32_t case_id, const char *kind, const s6502_t *actual_cpu,
    uint32_t actual_cycles, int actual_lcd_dirty,
    const s6502_t *reference_cpu, uint32_t reference_cycles,
    int reference_lcd_dirty, const uint8_t *actual_ram,
    const uint8_t *reference_ram, int check_cycles
)
{
    uint32_t difference = 0u;

    if ((!check_cycles || actual_cycles == reference_cycles) &&
        cpu_equal(actual_cpu, reference_cpu) &&
        actual_lcd_dirty == reference_lcd_dirty &&
        memcmp(actual_ram, reference_ram, GAM4980_RAM_SIZE) == 0)
        return 1;
    while (difference < GAM4980_RAM_SIZE &&
           actual_ram[difference] == reference_ram[difference])
        ++difference;
    fprintf(
        stderr,
        "%s mismatch case=%lu cycles=%lu/%lu ram=%04lx\n"
        "ref pc=%04x a=%02x x=%02x y=%02x sp=%02x p=%02x dirty=%d\n"
        "got pc=%04x a=%02x x=%02x y=%02x sp=%02x p=%02x dirty=%d\n",
        kind, (unsigned long)case_id, (unsigned long)reference_cycles,
        (unsigned long)actual_cycles, (unsigned long)difference,
        reference_cpu->pc, reference_cpu->ac, reference_cpu->ix,
        reference_cpu->iy, reference_cpu->sp, reference_cpu->status,
        reference_lcd_dirty,
        actual_cpu->pc, actual_cpu->ac, actual_cpu->ix, actual_cpu->iy,
        actual_cpu->sp, actual_cpu->status, actual_lcd_dirty
    );
    return 0;
}

static int run_case(
    uint32_t case_id, uint8_t width, uint8_t rows, uint8_t shift,
    uint8_t row, uint8_t column, uint8_t *initial_ram,
    uint8_t *reference_ram, uint8_t *actual_ram
)
{
    s6502_t initial_cpu;
    s6502_t reference_cpu;
    s6502_t whole_reference_cpu;
    s6502_t actual_cpu;
    uint16_t source = (uint16_t)(0x3000u + (next_random() & 0x01ffu));
    uint16_t destination = (uint16_t)(0x0800u + (next_random() & 0x007fu));
    uint16_t first_destination =
        (uint16_t)(0x0e00u + (next_random() & 0x007fu));
    uint16_t return_pc = 0x4000u;
    uint32_t reference_cycles;
    uint32_t sliced_reference_cycles;
    uint32_t actual_cycles;
    uint32_t index;
    int reference_lcd_dirty;
    int whole_reference_lcd_dirty;
    int actual_lcd_dirty;

    for (index = 0u; index < GAM4980_RAM_SIZE; ++index)
        sys.ram[index] = (uint8_t)next_random();
    sys.ram[_SYSCON] = 0u;
    sys.ram[0x2fu] = (uint8_t)source;
    sys.ram[0x30u] = (uint8_t)(source >> 8);
    sys.ram[0x38u] = (uint8_t)first_destination;
    sys.ram[0x39u] = (uint8_t)(first_destination >> 8);
    sys.ram[0x3au] = (uint8_t)destination;
    sys.ram[0x3bu] = (uint8_t)(destination >> 8);
    sys.ram[0x20e9u] = (uint8_t)destination;
    sys.ram[0x20eau] = (uint8_t)(destination >> 8);
    sys.ram[0x2081u] = column;
    sys.ram[0x2082u] = row;
    sys.ram[0x2083u] = (uint8_t)next_random();
    sys.ram[0x20cfu] = shift;
    sys.ram[0x20dau] = rows;
    sys.ram[0x20e3u] = (uint8_t)next_random();
    sys.ram[0x20e4u] = (uint8_t)next_random();
    sys.ram[0x20e8u] = width;
    sys.ram[return_pc] = 0x4cu;
    sys.ram[return_pc + 1u] = (uint8_t)return_pc;
    sys.ram[return_pc + 2u] = (uint8_t)(return_pc >> 8);
    initial_cpu.pc = 0x6988u;
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
    reference_cycles = run_until_return(
        &reference_cpu, return_pc, 1u, 1000000u
    );
    reference_lcd_dirty = lcd_dirty;
    whole_reference_lcd_dirty = reference_lcd_dirty;
    whole_reference_cpu = reference_cpu;
    memcpy(reference_ram, sys.ram, GAM4980_RAM_SIZE);
    if (!reference_cycles) {
        fprintf(stderr, "reference did not return case=%lu pc=%04x\n",
            (unsigned long)case_id, reference_cpu.pc);
        return 0;
    }

    memcpy(sys.ram, initial_ram, GAM4980_RAM_SIZE);
    reference_cpu = initial_cpu;
    lcd_dirty = 0;
    gam4980_set_firmware_hle_enabled(0);
    sliced_reference_cycles = s6502_exec(&reference_cpu, 0x800u);
    reference_lcd_dirty = lcd_dirty;
    memcpy(actual_ram, sys.ram, GAM4980_RAM_SIZE);

    memcpy(sys.ram, initial_ram, GAM4980_RAM_SIZE);
    actual_cpu = initial_cpu;
    lcd_dirty = 0;
    gam4980_set_firmware_hle_enabled(1);
    actual_cycles = s6502_exec(&actual_cpu, 0x800u);
    actual_lcd_dirty = lcd_dirty;
    if (!compare_result(
            case_id, "sliced", &actual_cpu, actual_cycles, actual_lcd_dirty,
            &reference_cpu, sliced_reference_cycles, reference_lcd_dirty,
            sys.ram, actual_ram, 0)) {
        fprintf(stderr,
            "input width=%u rows=%u shift=%u row=%u column=%u sp=%02x "
            "return=%04x\n",
            width, rows, shift, row, column, initial_cpu.sp, return_pc);
        return 0;
    }

    memcpy(sys.ram, initial_ram, GAM4980_RAM_SIZE);
    actual_cpu = initial_cpu;
    lcd_dirty = 0;
    gam4980_set_firmware_hle_enabled(1);
    actual_cycles = run_until_return(
        &actual_cpu, return_pc, reference_cycles, 2u
    );
    actual_lcd_dirty = lcd_dirty;
    memcpy(actual_ram, sys.ram, GAM4980_RAM_SIZE);
    return compare_result(
        case_id, "whole", &actual_cpu, actual_cycles, actual_lcd_dirty,
        &whole_reference_cpu, reference_cycles, whole_reference_lcd_dirty,
        actual_ram, reference_ram, 1
    );
}

int main(int argc, char **argv)
{
    static const uint8_t widths[] = {1u, 2u, 3u, 7u, 16u};
    static const uint8_t row_starts[] = {
        0u, 0x3fu, 0x40u, 0x41u, 0x42u, 0x7fu,
    };
    static const uint8_t columns[] = {0u, 7u, 8u, 15u, 16u, 0x90u};
    gam4980_buffers_t buffers;
    uint8_t *initial_ram;
    uint8_t *reference_ram;
    uint8_t *actual_ram;
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
    actual_ram = (uint8_t *)malloc(GAM4980_RAM_SIZE);
    if (!buffers.ram || !buffers.flash || !buffers.rom_8 || !buffers.rom_e ||
        !initial_ram || !reference_ram || !actual_ram ||
        !load_exact(argv[1], buffers.rom_8, GAM4980_ROM_SIZE) ||
        !load_exact(argv[2], buffers.rom_e, GAM4980_ROM_SIZE) ||
        gam4980_init(&buffers) <= 0) {
        fprintf(stderr, "could not initialize exact ROM test environment\n");
        goto cleanup;
    }
    sys.bk_tab[3] = 0x0003u;
    sys.bk_tab[4] = 0x0004u;
    sys.bk_tab[6] = 0x0eb5u;
    mem_bs(3u);
    mem_bs(4u);
    mem_bs(6u);
    gam4980_set_performance_debug(1);
    if (!s6502_firmware_hle_shift_region_match()) {
        fprintf(stderr, "firmware shift-region signature did not match\n");
        goto cleanup_core;
    }

    for (width_index = 0u;
         width_index < sizeof(widths) / sizeof(widths[0]); ++width_index) {
        for (row_index = 0u;
             row_index < sizeof(row_starts) / sizeof(row_starts[0]);
             ++row_index) {
            for (shift = 0u; shift < 8u; ++shift) {
                if (!run_case(
                        case_id++, widths[width_index],
                        (uint8_t)((next_random() & 3u) + 1u), shift,
                        row_starts[row_index],
                        columns[next_random() %
                            (sizeof(columns) / sizeof(columns[0]))],
                        initial_ram, reference_ram, actual_ram)) {
                    result = 1;
                    goto cleanup_core;
                }
            }
        }
    }
    while (case_id < RANDOM_CASES) {
        if (!run_case(
                case_id++, (uint8_t)((next_random() & 0x0fu) + 1u),
                (uint8_t)((next_random() & 3u) + 1u),
                (uint8_t)(next_random() & 7u),
                row_starts[next_random() %
                    (sizeof(row_starts) / sizeof(row_starts[0]))],
                (uint8_t)next_random(),
                initial_ram, reference_ram, actual_ram)) {
            result = 1;
            goto cleanup_core;
        }
    }
    printf("firmware shift-region HLE: %lu exact-state cases passed\n",
        (unsigned long)case_id);
    result = 0;

cleanup_core:
    gam4980_deinit();
cleanup:
    free(actual_ram);
    free(reference_ram);
    free(initial_ram);
    free(buffers.rom_e);
    free(buffers.rom_8);
    free(buffers.flash);
    free(buffers.ram);
    return result;
}
