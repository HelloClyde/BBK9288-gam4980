#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/gam4980_core.c"

static unsigned random_state = 0x7d349288u;

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

static void force_different(u8 *value, u8 other)
{
    if (*value == other)
        ++*value;
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
            "usage: game_hle_record_reverse_equivalence 8.BIN E.BIN game.gam\n"
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
    s6502_game_aot_requested = 0;
    match_count = s6502_game_hle_record_reverse_count;
    if (match_count != 1u) {
        fprintf(stderr, "reverse record matches=%u, expected 1\n", match_count);
        return 5;
    }

    for (case_index = 0u; case_index < 10000u; ++case_index) {
        const s6502_game_hle_record_reverse_t *match =
            &s6502_game_hle_record_reverses[0];
        s6502_hle_game_scan_result_t predicted;
        s6502_t initial_cpu;
        s6502_t reference_cpu;
        s6502_t hle_cpu;
        u16 object = (u16)(0x0400u + (next_random() & 0xffu));
        u16 data = (u16)(0x0800u + (next_random() & 0x7fu));
        u16 reference = (u16)(0x0c00u + (next_random() & 0xffu));
        u16 index = (u16)(5u + 3u * (next_random() & 7u));
        u16 previous = (u16)(index - 3u);
        u32 byte_index;
        u32 reference_cycles;
        u32 hle_cycles;
        u8 kind = (u8)(case_index % 6u);
        int predicted_status;

        memset(buffers.ram, 0xa5, GAM4980_RAM_SIZE);
        buffers.ram[_SYSCON] = 0u;
        write16(buffers.ram, match->object_pointer_zp, object);
        write16(
            buffers.ram, (u16)(object + match->index_offset), index
        );
        write16(
            buffers.ram, (u16)(object + match->data_pointer_offset), data
        );
        write16(
            buffers.ram,
            (u16)(object + match->reference_pointer_offset), reference
        );
        buffers.ram[object + match->found_offset] = kind == 5u ? 1u : 0u;
        for (byte_index = 0u; byte_index < 64u; ++byte_index)
            buffers.ram[data + byte_index] = (u8)next_random();
        for (byte_index = 0u; byte_index < 3u; ++byte_index)
            buffers.ram[reference + byte_index] = (u8)next_random();

        force_different(
            &buffers.ram[data + index - 2u], buffers.ram[reference]
        );
        force_different(
            &buffers.ram[data + index], buffers.ram[reference + 2u]
        );
        force_different(
            &buffers.ram[data + index - 1u], buffers.ram[reference + 1u]
        );
        if (kind >= 1u && kind <= 4u)
            buffers.ram[data + index - 2u] = buffers.ram[reference];
        if (kind >= 2u && kind <= 4u)
            buffers.ram[data + index] = buffers.ram[reference + 2u];
        if (kind == 3u)
            buffers.ram[data + index - 1u] = buffers.ram[reference + 1u];

        force_different(
            &buffers.ram[data + previous - 2u], buffers.ram[reference]
        );
        if (kind == 4u) {
            buffers.ram[data + previous - 2u] = buffers.ram[reference];
            buffers.ram[data + previous - 1u] = buffers.ram[reference + 1u];
            buffers.ram[data + previous] = buffers.ram[reference + 2u];
        }

        initial_cpu.pc = match->virtual_pc;
        initial_cpu.ac = (u8)next_random();
        initial_cpu.ix = (u8)next_random();
        initial_cpu.iy = (u8)next_random();
        initial_cpu.sp = (u8)next_random();
        initial_cpu.status = (u8)(0x20u | (next_random() & 0xc7u));
        sys.bk_tab[match->virtual_pc >> 12] =
            (u16)(match->physical_pc >> 12);
        mem_bs((u8)(match->virtual_pc >> 12));
        memcpy(initial_ram, buffers.ram, GAM4980_RAM_SIZE);

        predicted_status = s6502_game_hle_record_reverse_region(
            match, initial_cpu.status, 0x1000u, &predicted
        );
        if (predicted_status <= 0) {
            fprintf(stderr, "prediction rejected case=%u\n", case_index);
            return 6;
        }
        memcpy(buffers.ram, initial_ram, GAM4980_RAM_SIZE);

        reference_cpu = initial_cpu;
        s6502_game_hle_record_reverse_count = 0u;
        s6502_game_aot_enabled = 0;
        reference_cycles = 0u;
        do {
            reference_cycles += s6502_exec(&reference_cpu, 1u);
            if (reference_cycles > 0x1000u) {
                fprintf(
                    stderr, "reference did not reach exit case=%u pc=%04x\n",
                    case_index, reference_cpu.pc
                );
                return 8;
            }
        } while (reference_cpu.pc != predicted.pc);
        memcpy(reference_ram, buffers.ram, GAM4980_RAM_SIZE);

        memcpy(buffers.ram, initial_ram, GAM4980_RAM_SIZE);
        hle_cpu = initial_cpu;
        s6502_game_hle_record_reverse_count = match_count;
        s6502_game_aot_enabled = 1;
        s6502_game_aot_bank_mask |=
            (u16)(1u << (match->virtual_pc >> 12));
        hle_cycles = s6502_exec(&hle_cpu, predicted.cycles);
        if (reference_cycles != predicted.cycles ||
            reference_cycles != hle_cycles ||
            !cpu_equal(&reference_cpu, &hle_cpu) ||
            memcmp(reference_ram, buffers.ram, GAM4980_RAM_SIZE)) {
            u32 ram_index;

            fprintf(
                stderr,
                "FAIL case=%u kind=%u predicted=%u iterations=%u "
                "cycles=%u/%u\n",
                (unsigned)case_index, (unsigned)kind,
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
            s6502_hle_game_scan_result_t rejected;

            memcpy(buffers.ram, initial_ram, GAM4980_RAM_SIZE);
            if (s6502_game_hle_record_reverse_region(
                    match, initial_cpu.status, 1u, &rejected
                ) != 0 ||
                memcmp(initial_ram, buffers.ram, GAM4980_RAM_SIZE)) {
                fprintf(stderr, "budget mutation case=%u\n", case_index);
                return 7;
            }
        }
    }
    printf(
        "PASS reverse_record_matches=%u cases=10000\n",
        (unsigned)match_count
    );
    return 0;
}
