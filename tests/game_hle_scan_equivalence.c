#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/gam4980_core.c"

static unsigned random_state = 0x67de9288u;

static unsigned next_random(void)
{
    random_state = random_state * 1664525u + 1013904223u;
    return random_state;
}

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

static int load_game(const char *path)
{
    u8 header[GAM4980_GAME_HEADER_SIZE];
    FILE *file = fopen(path, "rb");
    long size;

    if (!file)
        return 0;
    if (fseek(file, 0, SEEK_END) != 0 ||
        (size = ftell(file)) < (long)GAM4980_GAME_HEADER_SIZE ||
        size > (long)GAM4980_GAME_MAX_SIZE ||
        fseek(file, 0, SEEK_SET) != 0 ||
        fread(header, 1, sizeof(header), file) != sizeof(header) ||
        fseek(file, 0, SEEK_SET) != 0 ||
        fread(gam4980_game_storage(), 1, (size_t)size, file) != (size_t)size) {
        fclose(file);
        return 0;
    }
    fclose(file);
    return gam4980_load_game_header(header, (u32)size) > 0;
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
    u8 *initial_ram;
    u8 *reference_ram;
    u8 *rom_8;
    u8 *rom_e;
    u8 scan_count;
    u32 case_index;

    if (argc != 4) {
        fprintf(stderr, "usage: game_hle_scan_equivalence 8.BIN E.BIN game.gam\n");
        return 2;
    }
    memset(&buffers, 0, sizeof(buffers));
    buffers.ram = (u8 *)malloc(GAM4980_RAM_SIZE);
    buffers.flash = (u8 *)malloc(GAM4980_FLASH_SIZE);
    buffers.flash_size = GAM4980_FLASH_SIZE;
    rom_8 = (u8 *)malloc(GAM4980_ROM_SIZE);
    rom_e = (u8 *)malloc(GAM4980_ROM_SIZE);
    initial_ram = (u8 *)malloc(GAM4980_RAM_SIZE);
    reference_ram = (u8 *)malloc(GAM4980_RAM_SIZE);
    buffers.rom_8 = rom_8;
    buffers.rom_e = rom_e;
    if (!buffers.ram || !buffers.flash || !rom_8 || !rom_e ||
        !initial_ram || !reference_ram)
        return 3;
    gam4980_set_firmware_hle_enabled(1);
    gam4980_set_performance_debug(1);
    if (!load_exact(argv[1], rom_8, GAM4980_ROM_SIZE) ||
        !load_exact(argv[2], rom_e, GAM4980_ROM_SIZE) ||
        gam4980_init(&buffers) <= 0 || !load_game(argv[3]))
        return 4;
    s6502_game_aot_requested = 0;
    scan_count = s6502_game_hle_scan_count;
    if (scan_count != 1u)
        return 5;

    for (case_index = 0u; case_index < 5000u; ++case_index) {
        const s6502_game_hle_scan_t *match = &s6502_game_hle_scans[0];
        s6502_hle_game_scan_result_t predicted;
        s6502_t initial_cpu;
        s6502_t reference_cpu;
        s6502_t hle_cpu;
        u16 object = (u16)(0x0400u + (next_random() & 0x3fu));
        u16 array = (u16)(0x0600u + (next_random() & 0x3fu));
        u8 index = (u8)(next_random() & 7u);
        u8 limit = (u8)(index + (next_random() & 7u));
        u32 array_index;
        u32 reference_cycles;
        u32 hle_cycles;
        int predicted_status;

        memset(buffers.ram, 0xa5, GAM4980_RAM_SIZE);
        buffers.ram[_SYSCON] = 0u;
        buffers.ram[match->object_pointer_zp] = (u8)object;
        buffers.ram[(u8)(match->object_pointer_zp + 1u)] =
            (u8)(object >> 8);
        buffers.ram[object + match->index_offset] = index;
        buffers.ram[object + match->limit_offset] = limit;
        buffers.ram[object + match->array_pointer_offset] = (u8)array;
        buffers.ram[object + match->array_pointer_offset + 1u] =
            (u8)(array >> 8);
        for (array_index = 0u; array_index < 32u; ++array_index)
            buffers.ram[array + array_index] = (u8)next_random();
        if ((case_index % 3u) == 0u)
            buffers.ram[array + index] = 1u;
        else if ((case_index % 3u) == 1u)
            buffers.ram[array + index] = 0u;
        else
            buffers.ram[array + index] = (u8)(2u + (next_random() & 0x7fu));
        if (limit > index && (case_index & 1u))
            buffers.ram[array + index + 1u] = 1u;

        initial_cpu.pc = match->virtual_pc;
        initial_cpu.ac = (u8)next_random();
        initial_cpu.ix = (u8)next_random();
        initial_cpu.iy = (u8)next_random();
        initial_cpu.sp = (u8)next_random();
        initial_cpu.status = (u8)(0x20u | (next_random() & 0xd7u));
        sys.bk_tab[match->virtual_pc >> 12] =
            (u16)(match->physical_pc >> 12);
        mem_bs((u8)(match->virtual_pc >> 12));
        memcpy(initial_ram, buffers.ram, GAM4980_RAM_SIZE);

        predicted_status = s6502_game_hle_scan_region(
            match, initial_cpu.status, 0x800u, &predicted
        );
        if (predicted_status <= 0)
            return 6;
        memcpy(buffers.ram, initial_ram, GAM4980_RAM_SIZE);

        reference_cpu = initial_cpu;
        s6502_game_hle_scan_count = 0u;
        reference_cycles = s6502_exec(&reference_cpu, predicted.cycles);
        memcpy(reference_ram, buffers.ram, GAM4980_RAM_SIZE);

        memcpy(buffers.ram, initial_ram, GAM4980_RAM_SIZE);
        hle_cpu = initial_cpu;
        s6502_game_hle_scan_count = scan_count;
        hle_cycles = s6502_exec(&hle_cpu, predicted.cycles);
        if (reference_cycles != hle_cycles ||
            !cpu_equal(&reference_cpu, &hle_cpu) ||
            memcmp(reference_ram, buffers.ram, GAM4980_RAM_SIZE)) {
            fprintf(
                stderr,
                "FAIL case=%u value=%02x index=%u limit=%u predicted=%u "
                "iterations=%u cycles=%u/%u\n",
                (unsigned)case_index,
                (unsigned)initial_ram[array + index],
                (unsigned)index, (unsigned)limit,
                (unsigned)predicted.cycles,
                (unsigned)predicted.iterations,
                (unsigned)reference_cycles, (unsigned)hle_cycles
            );
            fprintf(
                stderr,
                "ref pc=%04x a=%02x x=%02x y=%02x sp=%02x p=%02x\n"
                "hle pc=%04x a=%02x x=%02x y=%02x sp=%02x p=%02x\n",
                reference_cpu.pc, reference_cpu.ac,
                reference_cpu.ix, reference_cpu.iy,
                reference_cpu.sp, reference_cpu.status,
                hle_cpu.pc, hle_cpu.ac, hle_cpu.ix, hle_cpu.iy,
                hle_cpu.sp, hle_cpu.status
            );
            return 1;
        }
    }
    printf("PASS scan_matches=%u cases=5000\n", (unsigned)scan_count);
    return 0;
}
