#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DL_DOWN
#define _RLS_
#define GAM4980_ENABLE_AOT
#define GAM4980_ENABLE_FIRMWARE_HLE
#define GAM4980_FIRMWARE_HLE_MASK 0x20u
#include "../src/gam4980_core.c"

#define TEST_CASES 20000u

static uint32_t random_state = 0x49809288u;

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
    uint16_t multiplicand, uint16_t multiplier, uint32_t case_id,
    uint8_t *initial_ram, uint8_t *reference_ram
)
{
    s6502_t initial_cpu;
    s6502_t reference_cpu;
    uint16_t return_pc = (uint16_t)next_random();
    uint16_t cycles;
    uint32_t reference_executed;
    uint32_t hle_executed;
    uint32_t hits_before;
    uint32_t index;

    for (index = 0; index < 0x300u; ++index)
        sys.ram[index] = (uint8_t)next_random();
    sys.ram[_SYSCON] = 0u;
    sys.ram[0x20u] = (uint8_t)multiplicand;
    sys.ram[0x21u] = (uint8_t)(multiplicand >> 8);
    sys.ram[0x23u] = (uint8_t)multiplier;
    sys.ram[0x24u] = (uint8_t)(multiplier >> 8);
    sys.cpu.pc = 0xd1a2u;
    sys.cpu.ac = (uint8_t)next_random();
    sys.cpu.ix = (uint8_t)next_random();
    sys.cpu.iy = (uint8_t)next_random();
    sys.cpu.sp = (uint8_t)next_random();
    sys.cpu.status = (uint8_t)(next_random() & ~0x08u);
    sys.ram[0x100u | (uint8_t)(sys.cpu.sp + 1u)] =
        (uint8_t)(return_pc - 1u);
    sys.ram[0x100u | (uint8_t)(sys.cpu.sp + 2u)] =
        (uint8_t)((return_pc - 1u) >> 8);
    initial_cpu = sys.cpu;
    memcpy(initial_ram, sys.ram, GAM4980_RAM_SIZE);
    cycles = s6502_firmware_hle_multiply_cycles();

    gam4980_set_firmware_hle_enabled(0);
    reference_executed = s6502_exec(&sys.cpu, cycles);
    reference_cpu = sys.cpu;
    memcpy(reference_ram, sys.ram, GAM4980_RAM_SIZE);
    if (reference_executed != cycles || reference_cpu.pc != return_pc) {
        fprintf(
            stderr,
            "reference mismatch case=%lu a=%04x b=%04x cycles=%u "
            "executed=%lu pc=%04x expected_pc=%04x\n",
            (unsigned long)case_id, multiplicand, multiplier, cycles,
            (unsigned long)reference_executed, reference_cpu.pc, return_pc
        );
        return 0;
    }

    memcpy(sys.ram, initial_ram, GAM4980_RAM_SIZE);
    sys.cpu = initial_cpu;
    gam4980_set_firmware_hle_enabled(1);
    hits_before = s6502_firmware_hle_hits;
    hle_executed = s6502_exec(&sys.cpu, cycles);
    if (hle_executed != cycles || s6502_firmware_hle_hits != hits_before + 1u ||
        !cpu_equal(&sys.cpu, &reference_cpu) ||
        memcmp(sys.ram, reference_ram, GAM4980_RAM_SIZE) != 0) {
        fprintf(
            stderr,
            "HLE mismatch case=%lu a=%04x b=%04x cycles=%u "
            "executed=%lu hits=%lu/%lu\n"
            "reference pc=%04x a=%02x x=%02x y=%02x sp=%02x p=%02x\n"
            "HLE       pc=%04x a=%02x x=%02x y=%02x sp=%02x p=%02x\n",
            (unsigned long)case_id, multiplicand, multiplier, cycles,
            (unsigned long)hle_executed, (unsigned long)hits_before,
            (unsigned long)s6502_firmware_hle_hits,
            reference_cpu.pc, reference_cpu.ac, reference_cpu.ix,
            reference_cpu.iy, reference_cpu.sp, reference_cpu.status,
            sys.cpu.pc, sys.cpu.ac, sys.cpu.ix, sys.cpu.iy, sys.cpu.sp,
            sys.cpu.status
        );
        return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    static const uint16_t edge_values[] = {
        0x0000u, 0x0001u, 0x0002u, 0x007fu, 0x0080u, 0x00ffu,
        0x0100u, 0x7fffu, 0x8000u, 0xff00u, 0xffffu,
    };
    gam4980_buffers_t buffers;
    uint8_t *initial_ram;
    uint8_t *reference_ram;
    uint32_t case_id = 0u;
    size_t left;
    size_t right;
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
    if (sys.bk_tab[0x0du] != 0x0ea8u) {
        fprintf(stderr, "unexpected firmware bank D=%03x\n", sys.bk_tab[0x0du]);
        result = 2;
        goto cleanup_core;
    }
    gam4980_set_performance_debug(1);

    for (left = 0; left < sizeof(edge_values) / sizeof(edge_values[0]); ++left) {
        for (right = 0; right < sizeof(edge_values) / sizeof(edge_values[0]);
             ++right) {
            if (!run_case(
                    edge_values[left], edge_values[right], case_id++,
                    initial_ram, reference_ram)) {
                result = 1;
                goto cleanup_core;
            }
        }
    }
    while (case_id < TEST_CASES) {
        if (!run_case(
                (uint16_t)next_random(), (uint16_t)next_random(), case_id++,
                initial_ram, reference_ram)) {
            result = 1;
            goto cleanup_core;
        }
    }
    printf("firmware multiply HLE: %lu exact-state cases passed\n",
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
