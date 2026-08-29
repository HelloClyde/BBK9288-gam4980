#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/gam4980_core.c"

static int load_exact(const char *path, u8 *data, u32 size)
{
    FILE *file = fopen(path, "rb");
    int ok;

    if (!file)
        return 0;
    ok = fread(data, 1, size, file) == size && fgetc(file) == EOF;
    fclose(file);
    return ok;
}

static int cpu_equal(const s6502_t *left, const s6502_t *right)
{
    return left->pc == right->pc && left->ac == right->ac &&
        left->ix == right->ix && left->iy == right->iy &&
        left->sp == right->sp && left->status == right->status;
}

static u32 find_block(u32 physical_pc, u16 virtual_pc)
{
    u32 index;

    for (index = 0; index < S6502_AOT_BLOCK_COUNT; ++index) {
        if (s6502_aot_blocks[index].physical_pc == physical_pc &&
            s6502_aot_blocks[index].virtual_pc == virtual_pc)
            return index;
    }
    return S6502_AOT_BLOCK_COUNT;
}

static int verify_routine(
    u32 block_id, u16 virtual_pc, u8 *ram, u8 *initial_ram
)
{
    u32 value;

    for (value = 0; value <= 0xffffu; ++value) {
        s6502_t initial_cpu;
        s6502_t reference_cpu;
        s6502_t optimized_cpu;
        u8 reference_words[4];
        u32 reference_cycles;
        u32 optimized_cycles;
        u16 second = (u16)(value ^ 0xa55au);

        memset(ram, 0, GAM4980_RAM_SIZE);
        ram[0x38] = (u8)second;
        ram[0x39] = (u8)(second >> 8);
        ram[0x3a] = (u8)value;
        ram[0x3b] = (u8)(value >> 8);
        ram[0x1fe] = 0x33u;
        ram[0x1ff] = 0x12u;
        memcpy(initial_ram, ram, GAM4980_RAM_SIZE);

        initial_cpu.pc = virtual_pc;
        initial_cpu.ac = 0x69u;
        initial_cpu.ix = 0x5au;
        initial_cpu.iy = 0xa5u;
        initial_cpu.sp = 0xfdu;
        initial_cpu.status = 0x20u;

        reference_cpu = initial_cpu;
        s6502_aot_validation[block_id] = 2u;
        reference_cycles = s6502_exec(&reference_cpu, 50u);
        memcpy(reference_words, ram + 0x38, sizeof(reference_words));

        memcpy(ram, initial_ram, GAM4980_RAM_SIZE);
        optimized_cpu = initial_cpu;
        s6502_aot_validation[block_id] = 1u;
        optimized_cycles = s6502_exec(&optimized_cpu, 50u);

        if (reference_cycles != optimized_cycles ||
            !cpu_equal(&reference_cpu, &optimized_cpu) ||
            memcmp(reference_words, ram + 0x38, sizeof(reference_words))) {
            fprintf(stderr, "mismatch pc=%04x value=%04x\n", virtual_pc,
                (unsigned)value);
            return 0;
        }
    }
    return 1;
}

int main(int argc, char **argv)
{
    gam4980_buffers_t buffers;
    u8 *rom_8;
    u8 *rom_e;
    u8 *initial_ram;
    u32 add_block;
    u32 sub_block;
    int ok;

    if (argc != 3) {
        fprintf(stderr, "usage: aot_addsub16_equivalence 8.BIN E.BIN\n");
        return 2;
    }
    memset(&buffers, 0, sizeof(buffers));
    buffers.ram = (u8 *)malloc(GAM4980_RAM_SIZE);
    buffers.flash = (u8 *)malloc(GAM4980_FLASH_SIZE);
    buffers.flash_size = GAM4980_FLASH_SIZE;
    rom_8 = (u8 *)malloc(GAM4980_ROM_SIZE);
    rom_e = (u8 *)malloc(GAM4980_ROM_SIZE);
    initial_ram = (u8 *)malloc(GAM4980_RAM_SIZE);
    buffers.rom_8 = rom_8;
    buffers.rom_e = rom_e;
    if (!buffers.ram || !buffers.flash || !rom_8 || !rom_e || !initial_ram)
        return 3;
    if (!load_exact(argv[1], rom_8, GAM4980_ROM_SIZE) ||
        !load_exact(argv[2], rom_e, GAM4980_ROM_SIZE) ||
        gam4980_init(&buffers) <= 0)
        return 4;
    gam4980_set_firmware_hle_enabled(0);
    sys.bk_tab[6] = 0x0eb5u;
    mem_bs(6u);
    add_block = find_block(0xeb5655u, 0x6655u);
    sub_block = find_block(0xeb5678u, 0x6678u);
    if (add_block == S6502_AOT_BLOCK_COUNT ||
        sub_block == S6502_AOT_BLOCK_COUNT)
        return 5;

    ok = verify_routine(add_block, 0x6655u, buffers.ram, initial_ram) &&
        verify_routine(sub_block, 0x6678u, buffers.ram, initial_ram);
    printf("%s addsub16_cases=%u\n", ok ? "PASS" : "FAIL", 2u * 65536u);
    return ok ? 0 : 1;
}
