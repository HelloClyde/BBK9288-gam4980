#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DL_DOWN
#define _RLS_
#define GAM4980_ENABLE_AOT
#define GAM4980_ENABLE_NATIVE_TRACE_AOT
#define S6502_TEST_STOP_PC 0x7c4au
#include "../src/gam4980_core.c"

#define TEST_CASES 20000u

static uint32_t random_state = 0x7c309288u;

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
    uint32_t case_id, uint8_t *initial_ram, uint8_t *reference_ram
)
{
    static const uint32_t edge_budgets[] = {
        1u, 12u, 14u, 15u, 19u, 20u, 31u, 35u, 36u, 40u, 41u,
        79u, 127u, 255u, 511u, 1024u,
    };
    s6502_t initial_cpu;
    s6502_t reference_cpu;
    uint32_t budget;
    uint32_t reference_executed;
    uint32_t trace_executed;
    uint32_t trace_calls_before;
    uint32_t index;

    for (index = 0u; index < 0x2200u; ++index)
        sys.ram[index] = (uint8_t)next_random();
    sys.ram[_SYSCON] = 0u;
    sys.cpu.pc = 0x7c30u;
    sys.cpu.ac = (uint8_t)next_random();
    sys.cpu.ix = (uint8_t)next_random();
    sys.cpu.iy = (uint8_t)next_random();
    sys.cpu.sp = (uint8_t)next_random();
    sys.cpu.status = (uint8_t)next_random();
    if ((case_id % 17u) != 0u)
        sys.cpu.status &= (uint8_t)~0x08u;
    budget = case_id < sizeof(edge_budgets) / sizeof(edge_budgets[0])
        ? edge_budgets[case_id]
        : 1u + next_random() % 2048u;
    initial_cpu = sys.cpu;
    memcpy(initial_ram, sys.ram, GAM4980_RAM_SIZE);

    gam4980_set_native_trace_aot_enabled(0);
    reference_executed = s6502_exec(&sys.cpu, budget);
    reference_cpu = sys.cpu;
    memcpy(reference_ram, sys.ram, GAM4980_RAM_SIZE);

    memcpy(sys.ram, initial_ram, GAM4980_RAM_SIZE);
    sys.cpu = initial_cpu;
    gam4980_set_native_trace_aot_enabled(1);
    trace_calls_before = gam4980_native_trace_7c30_calls();
    trace_executed = s6502_exec(&sys.cpu, budget);
    if (reference_executed != trace_executed ||
        !cpu_equal(&reference_cpu, &sys.cpu) ||
        memcmp(reference_ram, sys.ram, GAM4980_RAM_SIZE) != 0 ||
        (!(initial_cpu.status & 0x08u) &&
         gam4980_native_trace_7c30_calls() != trace_calls_before + 1u) ||
        ((initial_cpu.status & 0x08u) &&
         gam4980_native_trace_7c30_calls() != trace_calls_before)) {
        fprintf(
            stderr,
            "native trace mismatch case=%lu budget=%lu decimal=%u\n"
            "reference cycles=%lu pc=%04x a=%02x x=%02x y=%02x "
            "sp=%02x p=%02x\n"
            "trace     cycles=%lu pc=%04x a=%02x x=%02x y=%02x "
            "sp=%02x p=%02x calls=%lu/%lu\n",
            (unsigned long)case_id, (unsigned long)budget,
            (initial_cpu.status & 0x08u) != 0u,
            (unsigned long)reference_executed, reference_cpu.pc,
            reference_cpu.ac, reference_cpu.ix, reference_cpu.iy,
            reference_cpu.sp, reference_cpu.status,
            (unsigned long)trace_executed, sys.cpu.pc, sys.cpu.ac,
            sys.cpu.ix, sys.cpu.iy, sys.cpu.sp, sys.cpu.status,
            (unsigned long)trace_calls_before,
            (unsigned long)gam4980_native_trace_7c30_calls()
        );
        return 0;
    }
    return 1;
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
        gam4980_init(&buffers) <= 0) {
        fprintf(stderr, "could not initialize exact ROM test environment\n");
        goto cleanup;
    }
    sys.bk_tab[7] = 0x0eb6u;
    mem_bs(7u);
    if (PA(0x7c30u) != 0xeb6c30u || sys.bk_tab[2] != 0x0002u) {
        fprintf(stderr, "unexpected 7C30 mapping: %06lx bank2=%03x\n",
                (unsigned long)PA(0x7c30u), sys.bk_tab[2]);
        goto cleanup_core;
    }

    result = 1;
    for (case_id = 0u; case_id < TEST_CASES; ++case_id) {
        if (!run_case(case_id, initial_ram, reference_ram))
            goto cleanup_core;
    }
    if (gam4980_native_trace_7c30_validation() != 1u ||
        !gam4980_native_trace_7c30_iterations() ||
        !gam4980_native_trace_7c30_guest_cycles()) {
        fprintf(stderr, "native trace comparison was vacuous\n");
        goto cleanup_core;
    }
    printf(
        "firmware native trace 7C30: %lu exact-state cases passed; "
        "calls=%lu iterations=%lu cycles=%lu\n",
        (unsigned long)case_id,
        (unsigned long)gam4980_native_trace_7c30_calls(),
        (unsigned long)gam4980_native_trace_7c30_iterations(),
        (unsigned long)gam4980_native_trace_7c30_guest_cycles()
    );
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
