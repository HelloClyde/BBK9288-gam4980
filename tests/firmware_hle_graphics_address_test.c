#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DL_DOWN
#define _RLS_
#define GAM4980_ENABLE_AOT
#define GAM4980_ENABLE_FIRMWARE_HLE
#define GAM4980_ENABLE_AGGRESSIVE_REGION_HLE
#define GAM4980_FIRMWARE_HLE_MASK 0x200u
#define S6502_TEST_STOP_PC 0x4000u
#include "../src/gam4980_core.c"

#define RANDOM_CASES 5000u

static uint32_t random_state = 0x876b9288u;

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
    sys.bk_tab[7] = 0x0eb6u;
    sys.bk_tab[8] = 0x0eb7u;
    mem_bs(7u);
    mem_bs(8u);
    if (!s6502_firmware_hle_graphics_address_match())
        goto cleanup_core;
    memset(s6502_aot_validation, 2, sizeof(s6502_aot_validation));

    for (case_id = 0u; case_id < RANDOM_CASES; ++case_id) {
        s6502_t initial_cpu;
        s6502_t reference_cpu;
        s6502_t actual_cpu;
        uint8_t x = (uint8_t)(next_random() % 160u);
        uint8_t y = (uint8_t)(next_random() % 96u);
        uint32_t reference_cycles;
        uint32_t actual_cycles;
        uint32_t difference = 0u;

        memset(sys.ram, (int)(next_random() & 0xffu), GAM4980_RAM_SIZE);
        sys.ram[_SYSCON] = 0u;
        sys.ram[0x2081u] = x;
        sys.ram[0x2082u] = y;
        initial_cpu.pc = 0x876bu;
        initial_cpu.ac = (uint8_t)next_random();
        initial_cpu.ix = (uint8_t)next_random();
        initial_cpu.iy = (uint8_t)next_random();
        initial_cpu.sp = (uint8_t)(0x20u + (next_random() & 0x7fu));
        initial_cpu.status = (uint8_t)(0x20u | (next_random() & 0xd7u));
        sys.ram[0x100u | (uint8_t)(initial_cpu.sp + 1u)] = 0xffu;
        sys.ram[0x100u | (uint8_t)(initial_cpu.sp + 2u)] = 0x3fu;
        memcpy(initial_ram, sys.ram, GAM4980_RAM_SIZE);

        if (s6502_firmware_hle_graphics_address(
                initial_cpu.ix, initial_cpu.iy, initial_cpu.sp,
                initial_cpu.status, 0xffffffffu,
                &s6502_hle_direct_result) != 1)
            goto cleanup_core;
        actual_cycles = s6502_hle_direct_result.cycles;
        actual_cpu.pc = s6502_hle_direct_result.pc;
        actual_cpu.ac = s6502_hle_direct_result.ac;
        actual_cpu.ix = s6502_hle_direct_result.ix;
        actual_cpu.iy = s6502_hle_direct_result.iy;
        actual_cpu.sp = s6502_hle_direct_result.dt;
        actual_cpu.status = s6502_hle_direct_result.status;

        memcpy(sys.ram, initial_ram, GAM4980_RAM_SIZE);
        reference_cpu = initial_cpu;
        gam4980_set_firmware_hle_enabled(0);
        reference_cycles = s6502_exec(&reference_cpu, 0xffffffffu);
        memcpy(reference_ram, sys.ram, GAM4980_RAM_SIZE);

        memcpy(sys.ram, initial_ram, GAM4980_RAM_SIZE);
        if (s6502_firmware_hle_graphics_address(
                initial_cpu.ix, initial_cpu.iy, initial_cpu.sp,
                initial_cpu.status, 0xffffffffu,
                &s6502_hle_direct_result) != 1)
            goto cleanup_core;
        while (difference < GAM4980_RAM_SIZE &&
               reference_ram[difference] == sys.ram[difference])
            ++difference;
        if (reference_cycles != actual_cycles ||
            !cpu_equal(&reference_cpu, &actual_cpu) ||
            difference != GAM4980_RAM_SIZE) {
            fprintf(stderr,
                "graphics-address mismatch case=%lu xy=%u,%u "
                "cycles=%lu/%lu ram=%04lx\n"
                "ref pc=%04x a=%02x x=%02x y=%02x sp=%02x p=%02x\n"
                "got pc=%04x a=%02x x=%02x y=%02x sp=%02x p=%02x\n",
                (unsigned long)case_id, x, y,
                (unsigned long)reference_cycles, (unsigned long)actual_cycles,
                (unsigned long)difference,
                reference_cpu.pc, reference_cpu.ac, reference_cpu.ix,
                reference_cpu.iy, reference_cpu.sp, reference_cpu.status,
                actual_cpu.pc, actual_cpu.ac, actual_cpu.ix, actual_cpu.iy,
                actual_cpu.sp, actual_cpu.status);
            result = 1;
            goto cleanup_core;
        }
    }
    printf("firmware graphics-address HLE: %lu exact-state cases passed\n",
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
