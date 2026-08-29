#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t test_stop_pc;

#define DL_DOWN
#define _RLS_
#define GAM4980_ENABLE_AOT
#define GAM4980_ENABLE_FIRMWARE_HLE
#define GAM4980_ENABLE_AGGRESSIVE_REGION_HLE
#define GAM4980_FIRMWARE_HLE_MASK 0x200u
#define S6502_TEST_STOP_PREDICATE(value) \
    ((uint16_t)(value) == test_stop_pc)
#include "../src/gam4980_core.c"

#define RANDOM_CASES 3000u

static uint32_t random_state = 0x53515801u;

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

static int run_direction(int shift_left, uint8_t *initial_ram,
                         uint8_t *reference_ram)
{
    uint32_t case_id;

    test_stop_pc = shift_left ? 0x588du : 0x53ddu;
    for (case_id = 0u; case_id < RANDOM_CASES; ++case_id) {
        s6502_t initial_cpu;
        s6502_t reference_cpu;
        s6502_t actual_cpu;
        uint8_t remaining = (uint8_t)(1u + next_random() % 18u);
        uint16_t source = (uint16_t)(0x0400u +
            next_random() % (0x1800u - remaining - 1u));
        uint16_t destination = (uint16_t)(0x1c00u +
            next_random() % (0x03e0u - remaining));
        uint8_t shift = (uint8_t)(next_random() & 7u);
        uint32_t reference_cycles;
        uint32_t actual_cycles;
        uint32_t difference = 0u;
        int reference_dirty;
        int actual_dirty;

        memset(sys.ram, (int)(next_random() & 0xffu), GAM4980_RAM_SIZE);
        sys.ram[_SYSCON] = 0u;
        sys.ram[0x2fu] = (uint8_t)source;
        sys.ram[0x30u] = (uint8_t)(source >> 8);
        sys.ram[0x3au] = (uint8_t)destination;
        sys.ram[0x3bu] = (uint8_t)(destination >> 8);
        sys.ram[0x20d8u] = remaining;
        sys.ram[0x20cfu] = shift;
        sys.ram[0x20e5u] = (uint8_t)next_random();
        sys.ram[0x20e6u] = (uint8_t)next_random();
        s6502_page3[0xe5u] = (uint8_t)(next_random() & 1u);
        if ((next_random() & 3u) == 0u) {
            s6502_page3[0xe6u] = (uint8_t)destination;
            s6502_page3[0xe7u] = (uint8_t)(destination >> 8);
        } else {
            s6502_page3[0xe6u] = (uint8_t)next_random();
            s6502_page3[0xe7u] = (uint8_t)next_random();
        }
        s6502_page3[0xe8u] = 0x80u;
        s6502_page3[0xe9u] = 0x1fu;
        initial_cpu.pc = shift_left ? 0x5801u : 0x5351u;
        initial_cpu.ac = (uint8_t)next_random();
        initial_cpu.ix = (uint8_t)next_random();
        initial_cpu.iy = (uint8_t)next_random();
        initial_cpu.sp = (uint8_t)next_random();
        initial_cpu.status = (uint8_t)(0x20u | (next_random() & 0xd7u));
        memcpy(initial_ram, sys.ram, GAM4980_RAM_SIZE);

        lcd_dirty = 0;
        if (s6502_firmware_hle_part_picture_row(
                shift_left, initial_cpu.ac, initial_cpu.iy,
                initial_cpu.sp, initial_cpu.status, 0xffffffffu,
                &s6502_hle_direct_result) != 1)
            return 2;
        actual_cycles = s6502_hle_direct_result.cycles;
        actual_cpu.pc = s6502_hle_direct_result.pc;
        actual_cpu.ac = s6502_hle_direct_result.ac;
        actual_cpu.ix = s6502_hle_direct_result.ix;
        actual_cpu.iy = s6502_hle_direct_result.iy;
        actual_cpu.sp = s6502_hle_direct_result.dt;
        actual_cpu.status = s6502_hle_direct_result.status;
        actual_dirty = lcd_dirty;

        memcpy(sys.ram, initial_ram, GAM4980_RAM_SIZE);
        lcd_dirty = 0;
        reference_cpu = initial_cpu;
        gam4980_set_firmware_hle_enabled(0);
        reference_cycles = s6502_exec(&reference_cpu, 0xffffffffu);
        reference_dirty = lcd_dirty;
        memcpy(reference_ram, sys.ram, GAM4980_RAM_SIZE);

        memcpy(sys.ram, initial_ram, GAM4980_RAM_SIZE);
        lcd_dirty = 0;
        if (s6502_firmware_hle_part_picture_row(
                shift_left, initial_cpu.ac, initial_cpu.iy,
                initial_cpu.sp, initial_cpu.status, 0xffffffffu,
                &s6502_hle_direct_result) != 1)
            return 2;
        while (difference < GAM4980_RAM_SIZE &&
               reference_ram[difference] == sys.ram[difference])
            ++difference;
        if (reference_cycles != actual_cycles ||
            !cpu_equal(&reference_cpu, &actual_cpu) ||
            reference_dirty != actual_dirty ||
            difference != GAM4980_RAM_SIZE) {
            fprintf(stderr,
                "part-picture mismatch dir=%s case=%lu src=%04x dst=%04x "
                "n=%u shift=%u cycles=%lu/%lu ram=%04lx dirty=%d/%d\n"
                "ref pc=%04x a=%02x x=%02x y=%02x sp=%02x p=%02x\n"
                "got pc=%04x a=%02x x=%02x y=%02x sp=%02x p=%02x\n",
                shift_left ? "left" : "right", (unsigned long)case_id,
                source, destination, remaining, shift,
                (unsigned long)reference_cycles, (unsigned long)actual_cycles,
                (unsigned long)difference, reference_dirty, actual_dirty,
                reference_cpu.pc, reference_cpu.ac, reference_cpu.ix,
                reference_cpu.iy, reference_cpu.sp, reference_cpu.status,
                actual_cpu.pc, actual_cpu.ac, actual_cpu.ix, actual_cpu.iy,
                actual_cpu.sp, actual_cpu.status);
            return 1;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    gam4980_buffers_t buffers;
    uint8_t *initial_ram;
    uint8_t *reference_ram;
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
    sys.bk_tab[5] = 0x0eb8u;
    mem_bs(5u);
    if (!s6502_firmware_hle_part_picture_match())
        goto cleanup_core;
    memset(s6502_aot_validation, 2, sizeof(s6502_aot_validation));

    result = run_direction(0, initial_ram, reference_ram);
    if (!result)
        result = run_direction(1, initial_ram, reference_ram);
    if (!result)
        printf("firmware part-picture row HLE: %lu exact-state cases "
               "per direction passed\n", (unsigned long)RANDOM_CASES);

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
