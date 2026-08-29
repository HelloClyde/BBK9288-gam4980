#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DL_DOWN
#define _RLS_
#define GAM4980_ENABLE_AOT
#define GAM4980_ENABLE_FIRMWARE_HLE
#define GAM4980_FIRMWARE_HLE_MASK 0x100u
#include "../src/gam4980_core.c"

#define TEST_CASES 10000u

static uint32_t random_state = 0x4980d2cau;

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

static int compare_hle(
    uint16_t entry_pc, uint16_t cycles, uint16_t return_pc,
    uint32_t case_id, uint8_t *initial_ram, uint8_t *reference_ram
)
{
    s6502_t initial_cpu = sys.cpu;
    s6502_t reference_cpu;
    uint32_t reference_executed;
    uint32_t hle_executed;
    uint32_t hits_before;

    memcpy(initial_ram, sys.ram, GAM4980_RAM_SIZE);
    gam4980_set_firmware_hle_enabled(0);
    reference_executed = s6502_exec(&sys.cpu, cycles);
    reference_cpu = sys.cpu;
    memcpy(reference_ram, sys.ram, GAM4980_RAM_SIZE);
    if (reference_executed != cycles || reference_cpu.pc != return_pc) {
        fprintf(stderr,
            "reference mismatch case=%lu entry=%04x cycles=%u "
            "executed=%lu pc=%04x expected=%04x\n",
            (unsigned long)case_id, entry_pc, cycles,
            (unsigned long)reference_executed, reference_cpu.pc, return_pc);
        return 0;
    }

    memcpy(sys.ram, initial_ram, GAM4980_RAM_SIZE);
    sys.cpu = initial_cpu;
    gam4980_set_firmware_hle_enabled(1);
    hits_before = s6502_firmware_hle_hits;
    hle_executed = s6502_exec(&sys.cpu, cycles);
    if (hle_executed != cycles ||
        s6502_firmware_hle_hits != hits_before + 1u ||
        !cpu_equal(&sys.cpu, &reference_cpu) ||
        memcmp(sys.ram, reference_ram, GAM4980_RAM_SIZE) != 0) {
        fprintf(stderr,
            "HLE mismatch case=%lu entry=%04x cycles=%u executed=%lu "
            "hits=%lu/%lu\n"
            "reference pc=%04x a=%02x x=%02x y=%02x sp=%02x p=%02x\n"
            "HLE       pc=%04x a=%02x x=%02x y=%02x sp=%02x p=%02x\n",
            (unsigned long)case_id, entry_pc, cycles,
            (unsigned long)hle_executed, (unsigned long)hits_before,
            (unsigned long)s6502_firmware_hle_hits,
            reference_cpu.pc, reference_cpu.ac, reference_cpu.ix,
            reference_cpu.iy, reference_cpu.sp, reference_cpu.status,
            sys.cpu.pc, sys.cpu.ac, sys.cpu.ix, sys.cpu.iy, sys.cpu.sp,
            sys.cpu.status);
        return 0;
    }
    return 1;
}

static int run_load_case(
    uint32_t case_id, uint8_t *initial_ram, uint8_t *reference_ram
)
{
    uint16_t return_pc = (uint16_t)next_random();
    uint16_t temp = (uint16_t)(0x0300u + next_random() % 0x7cf0u);
    uint32_t index;

    for (index = 0u; index < 0x0300u; ++index)
        sys.ram[index] = (uint8_t)next_random();
    sys.ram[_SYSCON] = 0u;
    sys.ram[0x2au] = (uint8_t)temp;
    sys.ram[0x2bu] = (uint8_t)(temp >> 8);
    sys.cpu.pc = 0xd596u;
    sys.cpu.ac = (uint8_t)next_random();
    sys.cpu.ix = (uint8_t)next_random();
    sys.cpu.iy = (uint8_t)next_random();
    sys.cpu.sp = (uint8_t)(next_random() | 3u);
    sys.cpu.status = (uint8_t)(next_random() & ~0x08u);
    sys.ram[0x100u | (uint8_t)(sys.cpu.sp + 1u)] =
        (uint8_t)(return_pc - 1u);
    sys.ram[0x100u | (uint8_t)(sys.cpu.sp + 2u)] =
        (uint8_t)((return_pc - 1u) >> 8);
    return compare_hle(
        0xd596u, 31u, return_pc, case_id, initial_ram, reference_ram);
}

static int run_and_case(
    uint32_t case_id, uint8_t *initial_ram, uint8_t *reference_ram
)
{
    uint16_t return_pc = (uint16_t)next_random();
    uint16_t left = (uint16_t)(0x0300u + next_random() % 0x7cfcu);
    uint16_t right = (uint16_t)(0x0300u + next_random() % 0x7cfcu);
    uint16_t output = (uint16_t)(0x0300u + next_random() % 0x7cf4u);
    uint16_t cycles = 123u;
    uint32_t index;

    for (index = 0u; index < 0x8000u; ++index)
        sys.ram[index] = (uint8_t)next_random();
    sys.ram[_SYSCON] = 0u;
    sys.ram[0x20u] = (uint8_t)left;
    sys.ram[0x21u] = (uint8_t)(left >> 8);
    sys.ram[0x23u] = (uint8_t)right;
    sys.ram[0x24u] = (uint8_t)(right >> 8);
    sys.ram[0x2au] = (uint8_t)output;
    sys.ram[0x2bu] = (uint8_t)(output >> 8);
    for (index = 1u; index < 4u; ++index) {
        cycles = (uint16_t)(cycles +
            (((left & 0xffu) + index) > 0xffu) +
            (((right & 0xffu) + index) > 0xffu));
    }
    sys.cpu.pc = 0xd2cau;
    sys.cpu.ac = (uint8_t)next_random();
    sys.cpu.ix = (uint8_t)next_random();
    sys.cpu.iy = (uint8_t)next_random();
    sys.cpu.sp = (uint8_t)(next_random() | 7u);
    sys.cpu.status = (uint8_t)(next_random() & ~0x08u);
    sys.ram[0x100u | (uint8_t)(sys.cpu.sp + 1u)] =
        (uint8_t)(return_pc - 1u);
    sys.ram[0x100u | (uint8_t)(sys.cpu.sp + 2u)] =
        (uint8_t)((return_pc - 1u) >> 8);
    return compare_hle(
        0xd2cau, cycles, return_pc, case_id, initial_ram, reference_ram);
}

static int run_compare_long_case(
    uint32_t case_id, uint8_t *initial_ram, uint8_t *reference_ram
)
{
    uint16_t return_pc = (uint16_t)next_random();
    uint16_t left = (uint16_t)(0x0400u + next_random() % 0x0bfcu);
    uint16_t right = (uint16_t)(0x0400u + next_random() % 0x0bfcu);
    uint16_t cycles = 0u;
    uint16_t nonzero = 0u;
    uint16_t difference;
    uint8_t carry = 1u;
    uint32_t index;

    for (index = 0u; index < 0x5000u; ++index)
        sys.ram[index] = (uint8_t)next_random();
    sys.ram[_SYSCON] = 0u;
    sys.ram[0x20u] = (uint8_t)left;
    sys.ram[0x21u] = (uint8_t)(left >> 8);
    sys.ram[0x23u] = (uint8_t)right;
    sys.ram[0x24u] = (uint8_t)(right >> 8);
    for (index = 0u; index < 4u; ++index) {
        difference = (uint16_t)(sys.ram[left + index] +
            (uint8_t)~sys.ram[right + index] + carry);
        carry = difference > 0xffu;
        nonzero += (uint8_t)difference != 0u;
        if (index) {
            cycles = (uint16_t)(cycles +
                (((left & 0xffu) + index) > 0xffu) +
                (((right & 0xffu) + index) > 0xffu));
        }
    }
    cycles = (uint16_t)(cycles + (nonzero ? 92u : 91u) + 8u * nonzero);
    sys.cpu.pc = 0xd362u;
    sys.cpu.ac = (uint8_t)next_random();
    sys.cpu.ix = (uint8_t)next_random();
    sys.cpu.iy = (uint8_t)next_random();
    sys.cpu.sp = (uint8_t)(next_random() | 3u);
    sys.cpu.status = (uint8_t)(next_random() & ~0x08u);
    sys.ram[0x100u | (uint8_t)(sys.cpu.sp + 1u)] =
        (uint8_t)(return_pc - 1u);
    sys.ram[0x100u | (uint8_t)(sys.cpu.sp + 2u)] =
        (uint8_t)((return_pc - 1u) >> 8);
    if (!compare_hle(
            0xd362u, cycles, return_pc, case_id,
            initial_ram, reference_ram)) {
        fprintf(stderr,
            "compare-long operands left=%04x right=%04x "
            "L=%02x%02x%02x%02x R=%02x%02x%02x%02x "
            "nonzero=%u cycles=%u\n",
            left, right,
            initial_ram[left], initial_ram[left + 1u],
            initial_ram[left + 2u], initial_ram[left + 3u],
            initial_ram[right], initial_ram[right + 1u],
            initial_ram[right + 2u], initial_ram[right + 3u],
            nonzero, cycles);
        return 0;
    }
    return 1;
}

static int run_indirect_call_case(
    uint32_t case_id, uint8_t *initial_ram, uint8_t *reference_ram
)
{
    uint16_t target_pc = (uint16_t)next_random();
    uint16_t caller_pc = (uint16_t)next_random();
    uint32_t index;

    for (index = 0u; index < 0x0300u; ++index)
        sys.ram[index] = (uint8_t)next_random();
    sys.ram[_SYSCON] = 0u;
    sys.ram[0x26u] = (uint8_t)target_pc;
    sys.ram[0x27u] = (uint8_t)(target_pc >> 8);
    sys.cpu.pc = 0xd572u;
    sys.cpu.ac = (uint8_t)next_random();
    sys.cpu.ix = (uint8_t)next_random();
    sys.cpu.iy = (uint8_t)next_random();
    sys.cpu.sp = (uint8_t)(next_random() | 3u);
    sys.cpu.status = (uint8_t)(next_random() & ~0x08u);
    sys.ram[0x100u | (uint8_t)(sys.cpu.sp + 1u)] =
        (uint8_t)(caller_pc - 1u);
    sys.ram[0x100u | (uint8_t)(sys.cpu.sp + 2u)] =
        (uint8_t)((caller_pc - 1u) >> 8);
    return compare_hle(
        0xd572u, 37u, target_pc, case_id, initial_ram, reference_ram);
}

int main(int argc, char **argv)
{
    gam4980_buffers_t buffers;
    uint8_t *initial_ram;
    uint8_t *reference_ram;
    uint32_t case_id;
    int result = 1;

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
        result = 2;
        goto cleanup;
    }
    gam4980_set_performance_debug(1);
    /* Keep the reference side in the instruction interpreter.  The guarded
     * HLE hooks run before AOT signature lookup, so they remain testable while
     * avoiding an AOT superblock's intentional end-of-slice overshoot. */
    memset(s6502_aot_validation, 2, sizeof(s6502_aot_validation));
    for (case_id = 0u; case_id < TEST_CASES; ++case_id) {
        if (!run_load_case(case_id, initial_ram, reference_ram) ||
            !run_and_case(case_id, initial_ram, reference_ram) ||
            !run_compare_long_case(case_id, initial_ram, reference_ram) ||
            !run_indirect_call_case(case_id, initial_ram, reference_ram))
            goto cleanup_core;
    }
    printf("firmware C runtime HLE: %lu load + %lu and + %lu compare-long + "
           "%lu indirect-call exact-state cases passed\n",
           (unsigned long)TEST_CASES, (unsigned long)TEST_CASES,
           (unsigned long)TEST_CASES, (unsigned long)TEST_CASES);
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
