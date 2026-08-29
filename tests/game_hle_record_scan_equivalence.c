#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/gam4980_core.c"

static unsigned random_state = 0x7b9e9288u;

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
    u32 staged_case_count = 0u;

    if (argc != 4) {
        fprintf(
            stderr,
            "usage: game_hle_record_scan_equivalence 8.BIN E.BIN game.gam\n"
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
    match_count = s6502_game_hle_record_scan_count;
    if (match_count != 1u) {
        fprintf(stderr, "record scan matches=%u, expected 1\n", match_count);
        return 5;
    }

    for (case_index = 0u; case_index < 10000u; ++case_index) {
        const s6502_game_hle_record_scan_t *match =
            &s6502_game_hle_record_scans[0];
        s6502_hle_game_scan_result_t predicted;
        s6502_t initial_cpu;
        s6502_t reference_cpu;
        s6502_t hle_cpu;
        u16 object = (u16)(0x0400u + (next_random() & 0xffu));
        u16 data = (u16)(0x0800u + (next_random() & 0x7fu));
        u16 reference = (u16)(0x0c00u + (next_random() & 0xffu));
        u16 index = (u16)(2u + 3u * (next_random() & 7u));
        u16 next_index = (u16)(index + 3u);
        u32 byte_index;
        u32 reference_cycles;
        u32 hle_cycles;
        u32 stage_step;
        u8 kind = (u8)(case_index & 3u);
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
        buffers.ram[object + match->found_offset] = 0u;
        for (byte_index = 0u; byte_index < 64u; ++byte_index)
            buffers.ram[data + byte_index] = (u8)next_random();
        for (byte_index = 0u; byte_index < 3u; ++byte_index)
            buffers.ram[reference + byte_index] = (u8)next_random();

        if (kind >= 1u)
            buffers.ram[data + index - 2u] = buffers.ram[reference];
        else if (buffers.ram[data + index - 2u] == buffers.ram[reference])
            ++buffers.ram[data + index - 2u];
        if (kind >= 2u)
            buffers.ram[data + index] = buffers.ram[reference + 2u];
        else if (kind == 1u &&
                 buffers.ram[data + index] == buffers.ram[reference + 2u])
            ++buffers.ram[data + index];
        if (kind == 3u)
            buffers.ram[data + index - 1u] = buffers.ram[reference + 1u];
        else if (kind == 2u &&
                 buffers.ram[data + index - 1u] ==
                    buffers.ram[reference + 1u])
            ++buffers.ram[data + index - 1u];

        /* Mismatch paths advance by three.  Make the following iteration
         * terminate at its first comparison so the batch path is covered. */
        if (kind == 1u || kind == 2u) {
            buffers.ram[data + next_index - 2u] =
                (u8)(buffers.ram[reference] + 1u);
            if (buffers.ram[data + next_index - 2u] ==
                buffers.ram[reference])
                ++buffers.ram[data + next_index - 2u];
        }

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

        predicted_status = s6502_game_hle_record_scan_region(
            match, initial_cpu.status, 0x1000u, &predicted
        );
        if (predicted_status <= 0)
            return 6;
        memcpy(buffers.ram, initial_ram, GAM4980_RAM_SIZE);

        reference_cpu = initial_cpu;
        s6502_game_hle_record_scan_count = 0u;
        reference_cycles = s6502_exec(&reference_cpu, predicted.cycles);
        memcpy(reference_ram, buffers.ram, GAM4980_RAM_SIZE);

        memcpy(buffers.ram, initial_ram, GAM4980_RAM_SIZE);
        hle_cpu = initial_cpu;
        s6502_game_hle_record_scan_count = match_count;
        hle_cycles = s6502_exec(&hle_cpu, predicted.cycles);
        if (reference_cycles != hle_cycles ||
            !cpu_equal(&reference_cpu, &hle_cpu) ||
            memcmp(reference_ram, buffers.ram, GAM4980_RAM_SIZE)) {
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
            return 1;
        }

        /* Force the entry helper to commit only the first comparison, then
         * enter every reachable resume PC directly with a tight budget. */
        memcpy(buffers.ram, initial_ram, GAM4980_RAM_SIZE);
        predicted_status = s6502_game_hle_record_scan_region(
            match, initial_cpu.status, 200u, &predicted
        );
        if (predicted_status <= 0)
            return 7;
        memcpy(buffers.ram, initial_ram, GAM4980_RAM_SIZE);
        reference_cpu = initial_cpu;
        s6502_game_hle_record_scan_count = 0u;
        reference_cycles = s6502_exec(&reference_cpu, predicted.cycles);
        memcpy(reference_ram, buffers.ram, GAM4980_RAM_SIZE);
        memcpy(buffers.ram, initial_ram, GAM4980_RAM_SIZE);
        hle_cpu = initial_cpu;
        s6502_game_hle_record_scan_count = match_count;
        hle_cycles = s6502_exec(&hle_cpu, predicted.cycles);
        ++staged_case_count;
        if (reference_cycles != hle_cycles ||
            !cpu_equal(&reference_cpu, &hle_cpu) ||
            memcmp(reference_ram, buffers.ram, GAM4980_RAM_SIZE)) {
            fprintf(
                stderr,
                "FAIL first-stage case=%u kind=%u predicted=%u "
                "cycles=%u/%u\n",
                (unsigned)case_index, (unsigned)kind,
                (unsigned)predicted.cycles,
                (unsigned)reference_cycles, (unsigned)hle_cycles
            );
            return 1;
        }

        memcpy(initial_ram, reference_ram, GAM4980_RAM_SIZE);
        initial_cpu = reference_cpu;
        for (stage_step = 0u; stage_step < 3u; ++stage_step) {
            u32 stage_budget;

            if (initial_cpu.pc ==
                (u16)(match->virtual_pc + 0x60u))
                stage_budget = 70u;
            else if (initial_cpu.pc ==
                (u16)(match->virtual_pc + 0xa5u))
                stage_budget = 6u;
            else if (initial_cpu.pc ==
                (u16)(match->virtual_pc + 0xa8u))
                stage_budget = 180u;
            else if (initial_cpu.pc ==
                (u16)(match->virtual_pc - 0x26u))
                stage_budget = 80u;
            else
                break;

            memcpy(buffers.ram, initial_ram, GAM4980_RAM_SIZE);
            predicted_status = s6502_game_hle_record_scan_resume_region(
                match, initial_cpu.pc, initial_cpu.ac, initial_cpu.iy,
                initial_cpu.status,
                stage_budget, &predicted
            );
            if (predicted_status <= 0)
                return 8;
            memcpy(buffers.ram, initial_ram, GAM4980_RAM_SIZE);
            reference_cpu = initial_cpu;
            s6502_game_hle_record_scan_count = 0u;
            reference_cycles = s6502_exec(
                &reference_cpu, stage_budget
            );
            memcpy(reference_ram, buffers.ram, GAM4980_RAM_SIZE);
            memcpy(buffers.ram, initial_ram, GAM4980_RAM_SIZE);
            hle_cpu = initial_cpu;
            s6502_game_hle_record_scan_count = match_count;
            hle_cycles = s6502_exec(&hle_cpu, stage_budget);
            ++staged_case_count;
            if (reference_cycles != hle_cycles ||
                !cpu_equal(&reference_cpu, &hle_cpu) ||
                memcmp(reference_ram, buffers.ram, GAM4980_RAM_SIZE)) {
                fprintf(
                    stderr,
                    "FAIL resume case=%u kind=%u step=%u pc=%04x "
                    "predicted=%u cycles=%u/%u\n",
                    (unsigned)case_index, (unsigned)kind,
                    (unsigned)stage_step, initial_cpu.pc,
                    (unsigned)predicted.cycles,
                    (unsigned)reference_cycles, (unsigned)hle_cycles
                );
                fprintf(
                    stderr,
                    "ref pc=%04x a=%02x y=%02x p=%02x "
                    "hle pc=%04x a=%02x y=%02x p=%02x\n",
                    reference_cpu.pc, reference_cpu.ac,
                    reference_cpu.iy, reference_cpu.status,
                    hle_cpu.pc, hle_cpu.ac, hle_cpu.iy, hle_cpu.status
                );
                return 1;
            }
            memcpy(initial_ram, reference_ram, GAM4980_RAM_SIZE);
            initial_cpu = reference_cpu;
        }
    }
    printf(
        "PASS record_scan_matches=%u cases=10000 staged_cases=%u\n",
        (unsigned)match_count, (unsigned)staged_case_count
    );
    return 0;
}
