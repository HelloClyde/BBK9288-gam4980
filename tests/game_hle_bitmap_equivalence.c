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
    u8 bitmap_count;
    u32 match_index;
    u32 case_count = 0u;
    u32 partial_case_count = 0u;
    u32 outer_case_count = 0u;
    u32 outer_batch_hit_count = 0u;
    u32 row_case_count = 0u;

    if (argc != 4) {
        fprintf(stderr, "usage: game_hle_bitmap_equivalence 8.BIN E.BIN game.gam\n");
        return 2;
    }
    memset(&buffers, 0, sizeof(buffers));
    buffers.ram = (u8 *)malloc(GAM4980_RAM_SIZE);
    buffers.flash = (u8 *)malloc(GAM4980_FLASH_SIZE);
    buffers.framebuffer = 0;
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
    bitmap_count = s6502_game_hle_bitmap_count;
    if (!bitmap_count)
        return 5;
    for (match_index = 0u; match_index < bitmap_count; ++match_index) {
        const s6502_game_hle_bitmap_t *match =
            &s6502_game_hle_bitmaps[match_index];
        printf("bitmap tables and=%x or=%x\n",match->and_table,match->or_table);
        u32 source;
        u32 subpixel;
        u32 x;

        sys.bk_tab[match->virtual_pc >> 12] =
            (uint16_t)(match->physical_pc >> 12);
        mem_bs((u8)(match->virtual_pc >> 12));
        for (source = 0u; source < 256u; ++source) {
            for (subpixel = 0u; subpixel < 4u; ++subpixel) {
                for (x = 0u; x < 8u; ++x) {
                    s6502_t initial_cpu;
                    s6502_t reference_cpu;
                    s6502_t hle_cpu;
                    u16 block_cycles;
                    u16 iteration_cycles;
                    u32 reference_cycles;
                    u32 hle_cycles;

                    buffers.ram[_SYSCON] = 0u;
                    buffers.ram[match->source_zp] = (u8)source;
                    buffers.ram[match->accumulator_zp] = 0x5au;
                    buffers.ram[match->destination_index_zp] = 0x20u;
                    buffers.ram[match->destination_pointer_zp] = 0x00u;
                    buffers.ram[(u8)(match->destination_pointer_zp + 1u)] =
                        0x04u;
                    buffers.ram[match->subpixel_zp] = (u8)subpixel;
                    buffers.ram[0x0420u] = 0xa5u;
                    buffers.ram[0x0421u] = 0x3cu;
                    initial_cpu.pc = match->virtual_pc;
                    initial_cpu.ac = 0x69u;
                    initial_cpu.ix = (u8)x;
                    initial_cpu.iy = 0x96u;
                    initial_cpu.sp = 0xe0u;
                    initial_cpu.status = 0x35u;
                    block_cycles = s6502_game_hle_bitmap_cycles(match, x);
                    iteration_cycles =
                        s6502_game_hle_bitmap_iteration_cycles(match, x);
                    if (!block_cycles || !iteration_cycles)
                        return 6;
                    memcpy(initial_ram, buffers.ram, GAM4980_RAM_SIZE);

                    reference_cpu = initial_cpu;
                    s6502_game_hle_bitmap_count = 0u;
                    reference_cycles = s6502_exec(
                        &reference_cpu, block_cycles
                    );
                    memcpy(reference_ram, buffers.ram, GAM4980_RAM_SIZE);

                    memcpy(buffers.ram, initial_ram, GAM4980_RAM_SIZE);
                    hle_cpu = initial_cpu;
                    s6502_game_hle_bitmap_count = bitmap_count;
                    hle_cycles = s6502_exec(&hle_cpu, block_cycles);
                    ++case_count;
                    if (reference_cycles != hle_cycles ||
                        !cpu_equal(&reference_cpu, &hle_cpu) ||
                        memcmp(reference_ram, buffers.ram, GAM4980_RAM_SIZE)) {
                        fprintf(
                            stderr,
                            "FAIL match=%u source=%02x subpixel=%u x=%u "
                            "budget=%u cycles=%u/%u\n",
                            (unsigned)match_index,
                            (unsigned)source,
                            (unsigned)subpixel, (unsigned)x,
                            (unsigned)block_cycles,
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

                    memcpy(buffers.ram, initial_ram, GAM4980_RAM_SIZE);
                    reference_cpu = initial_cpu;
                    s6502_game_hle_bitmap_count = 0u;
                    reference_cycles = s6502_exec(
                        &reference_cpu, iteration_cycles
                    );
                    memcpy(reference_ram, buffers.ram, GAM4980_RAM_SIZE);

                    memcpy(buffers.ram, initial_ram, GAM4980_RAM_SIZE);
                    hle_cpu = initial_cpu;
                    s6502_game_hle_bitmap_count = bitmap_count;
                    hle_cycles = s6502_exec(&hle_cpu, iteration_cycles);
                    ++partial_case_count;
                    if (reference_cycles != hle_cycles ||
                        !cpu_equal(&reference_cpu, &hle_cpu) ||
                        memcmp(reference_ram, buffers.ram, GAM4980_RAM_SIZE)) {
                        fprintf(
                            stderr,
                            "FAIL partial match=%u source=%02x subpixel=%u "
                            "x=%u budget=%u cycles=%u/%u\n",
                            (unsigned)match_index,
                            (unsigned)source,
                            (unsigned)subpixel, (unsigned)x,
                            (unsigned)iteration_cycles,
                            (unsigned)reference_cycles, (unsigned)hle_cycles
                        );
                        return 1;
                    }
                }
            }
        }
        if (match->outer_physical_pc) {
            u32 outer_case;

            sys.bk_tab[match->outer_virtual_pc >> 12] =
                (uint16_t)(match->outer_physical_pc >> 12);
            mem_bs((u8)(match->outer_virtual_pc >> 12));
            for (outer_case = 0u; outer_case < 4096u; ++outer_case) {
                s6502_t initial_cpu;
                s6502_t reference_cpu;
                s6502_t hle_cpu;
                u16 block_cycles;
                u32 reference_cycles;
                u32 hle_cycles;
                u8 source_index = (u8)(outer_case & 0x0fu);
                u8 compared = (u8)((u8)(source_index + 1u) << 2);
                u32 byte_index;

                buffers.ram[_SYSCON] = 0u;
                buffers.ram[match->source_pointer_zp] = 0x00u;
                buffers.ram[(u8)(match->source_pointer_zp + 1u)] = 0x03u;
                buffers.ram[match->source_index_zp] = source_index;
                buffers.ram[match->source_zp] = (u8)(outer_case >> 3);
                buffers.ram[match->accumulator_zp] =
                    (u8)(0x5au ^ outer_case);
                buffers.ram[match->destination_index_zp] =
                    (u8)((outer_case >> 4) & 0x1fu);
                buffers.ram[match->destination_pointer_zp] = 0x00u;
                buffers.ram[(u8)(match->destination_pointer_zp + 1u)] =
                    (outer_case & 1u) ? 0x34u : 0x04u;
                buffers.ram[match->subpixel_zp] = 0u;
                buffers.ram[match->width_zp] = (outer_case & 0x40u)
                    ? compared : 0xfcu;
                for (byte_index = 0u; byte_index < 32u; ++byte_index) {
                    buffers.ram[0x0300u + byte_index] = (u8)(
                        outer_case * 37u + byte_index * 73u
                    );
                    buffers.ram[0x0400u + byte_index] = (u8)(
                        outer_case * 19u + byte_index * 29u
                    );
                }
                initial_cpu.pc = match->outer_virtual_pc;
                initial_cpu.ac = (u8)(outer_case * 11u);
                initial_cpu.ix = (u8)((outer_case >> 7) & 7u);
                initial_cpu.iy = (u8)(outer_case * 7u);
                initial_cpu.sp = 0xe0u;
                initial_cpu.status = (u8)(0x24u | (outer_case & 0xc1u));
                block_cycles = s6502_game_hle_bitmap_outer_cycles(
                    match, initial_cpu.ix
                );
                if (!block_cycles)
                    return 8;
                memcpy(initial_ram, buffers.ram, GAM4980_RAM_SIZE);

                reference_cpu = initial_cpu;
                s6502_game_hle_bitmap_count = 0u;
                reference_cycles = s6502_exec(
                    &reference_cpu, block_cycles
                );
                memcpy(reference_ram, buffers.ram, GAM4980_RAM_SIZE);

                memcpy(buffers.ram, initial_ram, GAM4980_RAM_SIZE);
                hle_cpu = initial_cpu;
                s6502_game_hle_bitmap_count = bitmap_count;
                hle_cycles = s6502_exec(&hle_cpu, block_cycles);
                ++outer_case_count;
                if (reference_cycles != hle_cycles ||
                    !cpu_equal(&reference_cpu, &hle_cpu) ||
                    memcmp(reference_ram, buffers.ram, GAM4980_RAM_SIZE)) {
                    fprintf(
                        stderr,
                        "FAIL outer match=%u case=%u budget=%u "
                        "cycles=%u/%u\n",
                        (unsigned)match_index, (unsigned)outer_case,
                        (unsigned)block_cycles,
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
                if (!(outer_case & 0x40u) &&
                    (outer_case & 0x0fu) == 0u) {
                    u32 hits_before;

                    memcpy(buffers.ram, initial_ram, GAM4980_RAM_SIZE);
                    reference_cpu = initial_cpu;
                    s6502_game_hle_bitmap_count = 0u;
                    reference_cycles = s6502_exec(&reference_cpu, 0x800u);
                    memcpy(reference_ram, buffers.ram, GAM4980_RAM_SIZE);

                    memcpy(buffers.ram, initial_ram, GAM4980_RAM_SIZE);
                    hle_cpu = initial_cpu;
                    s6502_game_hle_bitmap_count = bitmap_count;
                    hits_before = s6502_firmware_hle_path_hits[
                        S6502_HLE_ID_GAME_BITMAP
                    ];
                    hle_cycles = s6502_exec(&hle_cpu, 0x800u);
                    outer_batch_hit_count +=
                        s6502_firmware_hle_path_hits[
                            S6502_HLE_ID_GAME_BITMAP
                        ] - hits_before;
                    if (reference_cycles != hle_cycles ||
                        !cpu_equal(&reference_cpu, &hle_cpu) ||
                        memcmp(
                            reference_ram, buffers.ram, GAM4980_RAM_SIZE
                        )) {
                        fprintf(
                            stderr,
                            "FAIL outer batch match=%u case=%u "
                            "cycles=%u/%u\n",
                            (unsigned)match_index, (unsigned)outer_case,
                            (unsigned)reference_cycles,
                            (unsigned)hle_cycles
                        );
                        fprintf(
                            stderr,
                            "ref pc=%04x a=%02x x=%02x y=%02x p=%02x\n"
                            "hle pc=%04x a=%02x x=%02x y=%02x p=%02x\n",
                            reference_cpu.pc, reference_cpu.ac,
                            reference_cpu.ix, reference_cpu.iy,
                            reference_cpu.status,
                            hle_cpu.pc, hle_cpu.ac,
                            hle_cpu.ix, hle_cpu.iy, hle_cpu.status
                        );
                        fprintf(
                            stderr,
                            "hle zp src_i=%02x dst_i=%02x sub=%02x "
                            "src=%02x acc=%02x width=%02x hits=%u\n",
                            buffers.ram[match->source_index_zp],
                            buffers.ram[match->destination_index_zp],
                            buffers.ram[match->subpixel_zp],
                            buffers.ram[match->source_zp],
                            buffers.ram[match->accumulator_zp],
                            buffers.ram[match->width_zp],
                            (unsigned)outer_batch_hit_count
                        );
                        return 1;
                    }
                }
            }
        }
        if (match->row_physical_pc) {
            u32 row_case;

            sys.bk_tab[match->outer_exit_pc >> 12] =
                (uint16_t)(match->row_physical_pc >> 12);
            mem_bs((u8)(match->outer_exit_pc >> 12));
            for (row_case = 0u; row_case < 10000u; ++row_case) {
                s6502_t initial_cpu;
                s6502_t reference_cpu;
                s6502_t hle_cpu;
                u16 row_cycles;
                u32 reference_cycles;
                u32 hle_cycles;
                u8 next_row = (u8)(1u + row_case % 63u);
                u8 last_row = (u8)((row_case >> 3) & 1u);
                u32 byte_index;

                buffers.ram[_SYSCON] = 0u;
                buffers.ram[match->accumulator_zp] =
                    (u8)(0x5au ^ row_case);
                buffers.ram[match->destination_index_zp] =
                    (u8)((row_case >> 2) & 0x1fu);
                buffers.ram[match->destination_pointer_zp] = 0x00u;
                buffers.ram[(u8)(match->destination_pointer_zp + 1u)] =
                    0x04u;
                buffers.ram[match->source_index_zp] =
                    (u8)(row_case & 0x0fu);
                buffers.ram[match->source_pointer_zp] = 0x00u;
                buffers.ram[(u8)(match->source_pointer_zp + 1u)] = 0x06u;
                buffers.ram[match->width_zp] =
                    (u8)(1u + (row_case * 13u) % 127u);
                buffers.ram[match->initial_x_zp] =
                    (u8)((row_case >> 4) & 7u);
                buffers.ram[match->row_count_zp] = (u8)(next_row - 1u);
                buffers.ram[match->height_zp] = last_row
                    ? next_row : (u8)(next_row + 1u);
                buffers.ram[match->vertical_zp] =
                    (u8)((row_case * 17u) % 95u);
                buffers.ram[match->subpixel_zp] = 0u;
                for (byte_index = 0u; byte_index < 64u; ++byte_index) {
                    buffers.ram[0x0400u + byte_index] =
                        (u8)(row_case * 29u + byte_index * 31u);
                    buffers.ram[0x0600u + byte_index] =
                        (u8)(row_case * 37u + byte_index * 41u);
                }
                initial_cpu.pc = match->outer_exit_pc;
                initial_cpu.ac = (u8)(row_case * 7u);
                initial_cpu.ix = (row_case & 3u)
                    ? (u8)(1u + ((row_case >> 5) & 7u)) : 0u;
                initial_cpu.iy = (u8)(row_case * 11u);
                initial_cpu.sp = 0xe0u;
                initial_cpu.status = (u8)(0x24u | (row_case & 0xc1u));
                row_cycles = s6502_game_hle_bitmap_row_cycles(
                    match, initial_cpu.ix
                );
                if (!row_cycles)
                    return 9;
                memcpy(initial_ram, buffers.ram, GAM4980_RAM_SIZE);

                reference_cpu = initial_cpu;
                s6502_game_hle_bitmap_count = 0u;
                reference_cycles = s6502_exec(&reference_cpu, row_cycles);
                memcpy(reference_ram, buffers.ram, GAM4980_RAM_SIZE);

                memcpy(buffers.ram, initial_ram, GAM4980_RAM_SIZE);
                hle_cpu = initial_cpu;
                s6502_game_hle_bitmap_count = bitmap_count;
                hle_cycles = s6502_exec(&hle_cpu, row_cycles);
                ++row_case_count;
                if (reference_cycles != hle_cycles ||
                    !cpu_equal(&reference_cpu, &hle_cpu) ||
                    memcmp(reference_ram, buffers.ram, GAM4980_RAM_SIZE)) {
                    fprintf(
                        stderr,
                        "FAIL row match=%u case=%u last=%u budget=%u "
                        "cycles=%u/%u\n",
                        (unsigned)match_index, (unsigned)row_case,
                        (unsigned)last_row, (unsigned)row_cycles,
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
        }
    }
    if (s6502_firmware_hle_path_hits[S6502_HLE_ID_GAME_BITMAP] !=
        case_count + partial_case_count + outer_case_count +
            outer_batch_hit_count + row_case_count)
        return 7;
    printf(
        "PASS bitmap_matches=%u cases=%u partial_cases=%u outer_cases=%u "
        "outer_batch_hits=%u row_cases=%u hle_hits=%u\n",
        bitmap_count, case_count, partial_case_count, outer_case_count,
        outer_batch_hit_count, row_case_count,
        (unsigned)s6502_firmware_hle_path_hits[S6502_HLE_ID_GAME_BITMAP]
    );
    if (!hle_bitmap_packed_groups) return 8;
    printf("packed pixel groups=%u\n",hle_bitmap_packed_groups);
    return 0;
}
