#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/gam4980_core.c"

static unsigned random_state = 0x61ca69a4u;

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

static void write16(u8 *ram, u16 address, u16 value)
{
    ram[address] = (u8)value;
    ram[(u16)(address + 1u)] = (u8)(value >> 8);
}

int main(int argc, char **argv)
{
    gam4980_buffers_t buffers;
    u8 *initial_ram;
    u8 *reference_ram;
    u8 *rom_8;
    u8 *rom_e;
    u8 match_count;
    u32 case_index;

    if (argc != 4) {
        fprintf(
            stderr,
            "usage: game_hle_callback_scan_equivalence 8.BIN E.BIN game.gam\n"
        );
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
    match_count = s6502_game_hle_callback_scan_count;
    if (match_count != 3u) {
        u8 index;

        fprintf(stderr, "callback scan matches=%u, expected 3\n", match_count);
        for (index = 0u; index < match_count; ++index)
            fprintf(
                stderr, "  physical=%06x virtual=%04x table=%04x\n",
                (unsigned)s6502_game_hle_callback_scans[index].physical_pc,
                s6502_game_hle_callback_scans[index].virtual_pc,
                s6502_game_hle_callback_scans[index].table_base
            );
        return 5;
    }

    for (case_index = 0u; case_index < 20000u; ++case_index) {
        const s6502_game_hle_callback_scan_t *match =
            &s6502_game_hle_callback_scans[case_index % match_count];
        s6502_hle_game_table_result_t predicted;
        s6502_t initial_cpu;
        s6502_t reference_cpu;
        s6502_t hle_cpu;
        u16 counter_pointer = (u16)(0x1200u + (next_random() & 0x3ffu));
        u8 counter = (u8)(next_random() & 0x7fu);
        u16 table_address = (u16)(
            match->table_base + ((u16)counter << 1)
        );
        u16 table_entry = (u16)next_random();
        u8 virtual_bank = (u8)(match->virtual_pc >> 12);
        u32 reference_cycles;
        u32 hle_cycles;
        int predicted_status;

        if ((case_index & 7u) == 0u)
            table_entry = 0u;
        else if ((case_index & 7u) == 1u)
            table_entry &= 0x00ffu;
        else if ((case_index & 7u) == 2u)
            table_entry &= 0xff00u;
        memset(buffers.ram, 0xa5, GAM4980_RAM_SIZE);
        buffers.ram[_SYSCON] = 0u;
        write16(
            buffers.ram, match->counter_pointer_zp, counter_pointer
        );
        buffers.ram[counter_pointer] = counter;
        write16(buffers.ram, table_address, table_entry);
        buffers.ram[0x20u] = (u8)next_random();
        buffers.ram[0x21u] = (u8)next_random();
        buffers.ram[0x23u] = (u8)next_random();
        buffers.ram[0x24u] = (u8)next_random();

        initial_cpu.pc = (case_index & 1u)
            ? match->increment_pc : match->virtual_pc;
        initial_cpu.ac = (u8)next_random();
        initial_cpu.ix = (u8)next_random();
        initial_cpu.iy = (u8)next_random();
        initial_cpu.sp = (u8)(0x40u + (next_random() & 0x7fu));
        initial_cpu.status = (u8)(0x20u | (next_random() & 0xc5u));
        sys.bk_tab[virtual_bank] = (u16)(match->physical_pc >> 12);
        mem_bs(virtual_bank);
        s6502_game_aot_enabled = 1;
        s6502_game_aot_bank_mask |= (u16)(1u << virtual_bank);
        memcpy(initial_ram, buffers.ram, GAM4980_RAM_SIZE);

        predicted_status = s6502_game_hle_callback_scan_region(
            match, initial_cpu.pc, initial_cpu.ac, initial_cpu.ix,
            initial_cpu.iy, initial_cpu.sp, initial_cpu.status,
            0x1000u, &predicted
        );
        if (predicted_status <= 0) {
            fprintf(stderr, "prediction rejected case=%u\n", case_index);
            return 6;
        }
        memcpy(buffers.ram, initial_ram, GAM4980_RAM_SIZE);

        reference_cpu = initial_cpu;
        s6502_game_hle_callback_scan_count = 0u;
        reference_cycles = s6502_exec(&reference_cpu, predicted.cycles);
        memcpy(reference_ram, buffers.ram, GAM4980_RAM_SIZE);

        memcpy(buffers.ram, initial_ram, GAM4980_RAM_SIZE);
        hle_cpu = initial_cpu;
        s6502_game_hle_callback_scan_count = match_count;
        hle_cycles = s6502_exec(&hle_cpu, predicted.cycles);
        if (reference_cycles != predicted.cycles ||
            reference_cycles != hle_cycles ||
            !cpu_equal(&reference_cpu, &hle_cpu) ||
            memcmp(reference_ram, buffers.ram, GAM4980_RAM_SIZE)) {
            u32 ram_index;

            fprintf(
                stderr,
                "FAIL case=%u match=%u entry=%04x predicted=%u "
                "cycles=%u/%u\n",
                (unsigned)case_index,
                (unsigned)(case_index % match_count), table_entry,
                (unsigned)predicted.cycles,
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
            for (ram_index = 0u; ram_index < GAM4980_RAM_SIZE; ++ram_index) {
                if (reference_ram[ram_index] != buffers.ram[ram_index]) {
                    fprintf(
                        stderr, "ram[%04x]=%02x/%02x\n",
                        (unsigned)ram_index, reference_ram[ram_index],
                        buffers.ram[ram_index]
                    );
                    break;
                }
            }
            return 1;
        }

        if ((case_index & 15u) == 0u) {
            s6502_hle_game_table_result_t rejected;

            memcpy(buffers.ram, initial_ram, GAM4980_RAM_SIZE);
            if (s6502_game_hle_callback_scan_region(
                    match, initial_cpu.pc, initial_cpu.ac, initial_cpu.ix,
                    initial_cpu.iy, initial_cpu.sp, initial_cpu.status,
                    48u, &rejected
                ) != 0 ||
                memcmp(initial_ram, buffers.ram, GAM4980_RAM_SIZE)) {
                fprintf(stderr, "budget mutation case=%u\n", case_index);
                return 7;
            }
        }
    }
    printf(
        "PASS callback_scan_matches=%u cases=20000\n",
        (unsigned)match_count
    );
    return 0;
}
