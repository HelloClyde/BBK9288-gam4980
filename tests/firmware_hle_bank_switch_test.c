#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DL_DOWN
#define _RLS_
#define GAM4980_ENABLE_AOT
#define GAM4980_ENABLE_FIRMWARE_HLE
#define GAM4980_FIRMWARE_HLE_MASK 0x80u
#include "../src/gam4980_core.c"

typedef struct {
    uint32_t pc,ac,ix,iy,sp,status,cycle_budget,cycles;
    uintptr_t ram,read8,write8,graphics;
} host_fw_context;
#ifdef FW_NATIVE_BANK_TEST_SERVICES
typedef struct {uint32_t version;uintptr_t bank_map4,bank_descriptor_safe,bank_metrics;} host_bank_services;
static host_bank_services test_bank_services={5,(uintptr_t)native_bank_map4,(uintptr_t)native_bank_descriptor_safe,(uintptr_t)native_bank_metrics};
#define firmware_native_graphics_services_t host_bank_services
#endif
#define FIRMWARE_NATIVE_HOST_TEST
#define s6502_iram_asm_context_t host_fw_context
#include "../src/firmware_native_bank.c"
#ifdef FW_NATIVE_BANK_TEST_SERVICES
#undef firmware_native_graphics_services_t
#endif
#undef s6502_iram_asm_context_t
#undef FIRMWARE_NATIVE_HOST_TEST

#define TEST_CASES 10000u

static uint32_t random_state = 0xf52a9288u;

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

static void restore_banks(const uint16_t *banks, uint8_t selected)
{
    uint8_t bank;

    memcpy(sys.bk_tab, banks, sizeof(sys.bk_tab));
    for (bank = 1u; bank < 16u; ++bank)
        mem_bs(bank);
    sys.bk_sel = selected;
}

static int compare_hle(
    uint16_t entry_pc, uint16_t cycles, uint16_t return_pc,
    uint32_t case_id, uint8_t *initial_ram, uint8_t *reference_ram
)
{
    s6502_t initial_cpu = sys.cpu;
    s6502_t reference_cpu;
    uint16_t initial_banks[16];
    uint16_t reference_banks[16];
    uint8_t initial_selected = sys.bk_sel;
    uint8_t reference_selected;
    uint32_t reference_executed;
    uint32_t hle_executed;
    uint32_t hits_before;

    memcpy(initial_ram, sys.ram, GAM4980_RAM_SIZE);
    memcpy(initial_banks, sys.bk_tab, sizeof(initial_banks));
    gam4980_set_firmware_hle_enabled(0);
    reference_executed = s6502_exec(&sys.cpu, cycles);
    reference_cpu = sys.cpu;
    memcpy(reference_ram, sys.ram, GAM4980_RAM_SIZE);
    memcpy(reference_banks, sys.bk_tab, sizeof(reference_banks));
    reference_selected = sys.bk_sel;
    if (reference_executed != cycles || reference_cpu.pc != return_pc) {
        fprintf(stderr,
            "reference mismatch case=%lu entry=%04x cycles=%u "
            "executed=%lu pc=%04x expected=%04x\n",
            (unsigned long)case_id, entry_pc, cycles,
            (unsigned long)reference_executed, reference_cpu.pc, return_pc);
        return 0;
    }

    memcpy(sys.ram, initial_ram, GAM4980_RAM_SIZE);
    restore_banks(initial_banks, initial_selected);
    sys.cpu = initial_cpu;
    {
        host_fw_context c={0};
        c.pc=initial_cpu.pc;c.ac=initial_cpu.ac;c.ix=initial_cpu.ix;c.iy=initial_cpu.iy;
        c.sp=initial_cpu.sp;c.status=initial_cpu.status;c.cycle_budget=cycles+2u;
        c.ram=(uintptr_t)sys.ram;c.read8=(uintptr_t)mem_read;c.write8=(uintptr_t)mem_write;
        if(firmware_native_bank(&c)!=cycles || c.pc!=reference_cpu.pc ||
           c.ac!=reference_cpu.ac || c.ix!=reference_cpu.ix || c.iy!=reference_cpu.iy ||
           c.sp!=reference_cpu.sp || c.status!=reference_cpu.status ||
           memcmp(sys.ram,reference_ram,GAM4980_RAM_SIZE) ||
           memcmp(sys.bk_tab,reference_banks,sizeof(reference_banks)) ||
           sys.bk_sel!=reference_selected){
            fprintf(stderr,"authored bank mismatch pc=%04x\n",entry_pc);return 0;
        }
        memcpy(sys.ram,initial_ram,GAM4980_RAM_SIZE);restore_banks(initial_banks,initial_selected);
    }
    gam4980_set_firmware_hle_enabled(1);
    hits_before = s6502_firmware_hle_hits;
    hle_executed = s6502_exec(&sys.cpu, cycles);
    if (hle_executed != cycles ||
        s6502_firmware_hle_hits != hits_before + 1u ||
        !cpu_equal(&sys.cpu, &reference_cpu) ||
        memcmp(sys.ram, reference_ram, GAM4980_RAM_SIZE) != 0 ||
        memcmp(sys.bk_tab, reference_banks, sizeof(reference_banks)) != 0 ||
        sys.bk_sel != reference_selected) {
        uint32_t mismatch;

        fprintf(stderr,
            "HLE mismatch case=%lu entry=%04x cycles=%u executed=%lu "
            "hits=%lu/%lu bank=%u/%u\n"
            "reference pc=%04x a=%02x x=%02x y=%02x sp=%02x p=%02x\n"
            "HLE       pc=%04x a=%02x x=%02x y=%02x sp=%02x p=%02x\n",
            (unsigned long)case_id, entry_pc, cycles,
            (unsigned long)hle_executed, (unsigned long)hits_before,
            (unsigned long)s6502_firmware_hle_hits,
            reference_selected, sys.bk_sel,
            reference_cpu.pc, reference_cpu.ac, reference_cpu.ix,
            reference_cpu.iy, reference_cpu.sp, reference_cpu.status,
            sys.cpu.pc, sys.cpu.ac, sys.cpu.ix, sys.cpu.iy, sys.cpu.sp,
            sys.cpu.status);
        for (mismatch = 0u; mismatch < GAM4980_RAM_SIZE; ++mismatch) {
            if (sys.ram[mismatch] != reference_ram[mismatch]) {
                fprintf(stderr, "first RAM mismatch %04lx: %02x/%02x\n",
                    (unsigned long)mismatch, reference_ram[mismatch],
                    sys.ram[mismatch]);
                break;
            }
        }
        for (mismatch = 0u; mismatch < 16u; ++mismatch) {
            if (sys.bk_tab[mismatch] != reference_banks[mismatch]) {
                fprintf(stderr, "first bank mismatch %lu: %03x/%03x\n",
                    (unsigned long)mismatch, reference_banks[mismatch],
                    sys.bk_tab[mismatch]);
                break;
            }
        }
        return 0;
    }
    return 1;
}

static int run_case(
    uint32_t case_id, uint16_t entry_pc,
    uint8_t *initial_ram, uint8_t *reference_ram
)
{
    uint16_t return_pc = (uint16_t)next_random();
    uint8_t bank_number = (uint8_t)next_random();
    uint16_t cycles;
    uint32_t index;

    for (index = 0u; index < 0x2100u; ++index)
        sys.ram[index] = (uint8_t)next_random();
    sys.ram[_SYSCON] = 0u;
    sys.ram[0x03d6u] = (uint8_t)(0x0du + (next_random() & 0x1fu));
    sys.ram[0x03d5u] = 0x02u;
    sys.ram[0x2029u] = (uint8_t)(0x0du + (next_random() & 0x1fu));
    sys.ram[0x202au] = 0x02u;
    sys.cpu.pc = 0xf52au;
    sys.cpu.ac = bank_number;
    sys.cpu.ix = (uint8_t)next_random();
    sys.cpu.iy = (uint8_t)next_random();
    sys.cpu.sp = (uint8_t)(next_random() | 7u);
    sys.cpu.status = (uint8_t)(next_random() & ~0x08u);
    sys.ram[0x100u | (uint8_t)(sys.cpu.sp + 1u)] =
        (uint8_t)(return_pc - 1u);
    sys.ram[0x100u | (uint8_t)(sys.cpu.sp + 2u)] =
        (uint8_t)((return_pc - 1u) >> 8);

    if (entry_pc == 0xf549u) {
        if (bank_number < 0xe0u)
            sys.cpu.ac = bank_number = (uint8_t)(0xe0u | (bank_number & 0x1fu));
        gam4980_set_firmware_hle_enabled(0);
        if (s6502_exec(&sys.cpu, 28u) != 28u || sys.cpu.pc != 0xf549u)
            return 0;
        cycles = 185u;
    } else if (entry_pc == 0xf55bu) {
        if (bank_number >= 0xe0u)
            sys.cpu.ac = bank_number = (uint8_t)(bank_number & 0xdfu);
        gam4980_set_firmware_hle_enabled(0);
        if (s6502_exec(&sys.cpu, 46u) != 46u || sys.cpu.pc != 0xf55bu)
            return 0;
        cycles = 161u;
    } else {
        cycles = bank_number < 0xe0u ? 207u : 213u;
    }
    return compare_hle(
        entry_pc, cycles, return_pc, case_id, initial_ram, reference_ram);
}

int main(int argc, char **argv)
{
    static const uint16_t entries[] = {0xf52au, 0xf549u, 0xf55bu};
    gam4980_buffers_t buffers;
    uint8_t *initial_ram;
    uint8_t *reference_ram;
    uint32_t case_id;
    size_t entry;
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
    for (entry = 0u; entry < sizeof(entries) / sizeof(entries[0]); ++entry) {
        for (case_id = 0u; case_id < TEST_CASES; ++case_id) {
            if (!run_case(
                    case_id, entries[entry], initial_ram, reference_ram))
                goto cleanup_core;
        }
    }
    printf("firmware bank-switch HLE: %lu exact-state cases per entry passed\n",
           (unsigned long)TEST_CASES);
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
