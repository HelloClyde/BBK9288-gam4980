#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DL_DOWN
#define _RLS_
#define GAM4980_ENABLE_PROFILING
#include "../src/gam4980_core.c"

typedef struct api_target {
    uint16_t vector;
    const char *name;
    uint8_t seen;
} api_target_t;

static api_target_t api_targets[] = {
    {0xe78eu, "SysPicture", 0u},
    {0xe8c5u, "SysPartPicture", 0u},
    {0xe7beu, "SysChinese", 0u},
    {0xe7bbu, "SysAscii", 0u},
    {0xe794u, "SysLine", 0u},
    {0xe797u, "SysRect", 0u},
    {0xe87du, "SysFillRect", 0u},
};

static uint32_t trace_remaining;
static uint32_t trace_index;
static uint16_t observed_vectors[128];
static uint32_t observed_counts[128];
static uint8_t observed_traced[128];
static uint32_t observed_count;

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

static int load_game(const char *path)
{
    FILE *file = fopen(path, "rb");
    long size;
    uint8_t header[GAM4980_GAME_HEADER_SIZE];

    if (!file || fseek(file, 0, SEEK_END) != 0 ||
        (size = ftell(file)) < (long)sizeof(header) ||
        size > (long)GAM4980_GAME_MAX_SIZE || fseek(file, 0, SEEK_SET) != 0 ||
        fread(header, 1u, sizeof(header), file) != sizeof(header) ||
        fseek(file, 0, SEEK_SET) != 0 ||
        fread(gam4980_game_storage(), 1u, (size_t)size, file) != (size_t)size) {
        if (file)
            fclose(file);
        return 0;
    }
    fclose(file);
    return gam4980_load_game_header(header, (uint32_t)size) > 0;
}

static void trace_instruction(
    void *context, uint16_t virtual_pc, uint32_t physical_pc, uint8_t opcode
)
{
    uint16_t vector;
    uint32_t index;

    (void)context;
    if (virtual_pc == 0xd2f6u) {
        vector = (uint16_t)(sys.ram[0x26u] |
            ((uint16_t)sys.ram[0x27u] << 8));
        for (index = 0u; index < observed_count; ++index) {
            if (observed_vectors[index] == vector)
                break;
        }
        if (index == observed_count && observed_count < 128u) {
            observed_vectors[index] = vector;
            ++observed_count;
        }
        if (index < observed_count)
            ++observed_counts[index];
        for (index = 0u; index < sizeof(api_targets) / sizeof(api_targets[0]);
             ++index) {
            if (api_targets[index].vector == vector &&
                !api_targets[index].seen) {
                api_targets[index].seen = 1u;
                trace_index = index;
                trace_remaining = 320u;
                printf(
                    "BEGIN %s vector=%04x a=%02x x=%02x y=%02x sp=%02x "
                    "arg=%02x%02x\n",
                    api_targets[index].name, vector, sys.cpu.ac, sys.cpu.ix,
                    sys.cpu.iy, sys.cpu.sp, sys.ram[0x29u], sys.ram[0x28u]
                );
                break;
            }
        }
        if (!trace_remaining) {
            for (index = 0u; index < observed_count; ++index) {
                if (observed_vectors[index] == vector)
                    break;
            }
            if (index < observed_count && !observed_traced[index] &&
                index < 16u) {
                observed_traced[index] = 1u;
                trace_index = (uint32_t)(
                    sizeof(api_targets) / sizeof(api_targets[0])
                );
                trace_remaining = 120u;
                printf("BEGIN vector=%04x\n", vector);
            }
        }
    }
    if (trace_remaining) {
        if (trace_index < sizeof(api_targets) / sizeof(api_targets[0]))
            printf(
                "%s %06lx:%04x op=%02x bank=%x/%03x,%03x,%03x,%03x\n",
                api_targets[trace_index].name, (unsigned long)physical_pc,
                virtual_pc, opcode, sys.bk_sel, sys.bk_tab[4], sys.bk_tab[5],
                sys.bk_tab[6], sys.bk_tab[7]
            );
        else
            printf(
                "TRACE %06lx:%04x op=%02x bank=%x/%03x,%03x,%03x,%03x\n",
                (unsigned long)physical_pc, virtual_pc, opcode, sys.bk_sel,
                sys.bk_tab[4], sys.bk_tab[5], sys.bk_tab[6], sys.bk_tab[7]
            );
        --trace_remaining;
        if (!trace_remaining) {
            if (trace_index < sizeof(api_targets) / sizeof(api_targets[0]))
                printf("END %s\n", api_targets[trace_index].name);
            else
                printf("END TRACE\n");
        }
    }
}

int main(int argc, char **argv)
{
    gam4980_buffers_t buffers;
    uint32_t frame;
    int result = 2;

    if (argc != 4) {
        fprintf(stderr, "usage: %s 8.BIN E.BIN game.gam\n", argv[0]);
        return 2;
    }
    memset(&buffers, 0, sizeof(buffers));
    buffers.ram = (uint8_t *)malloc(GAM4980_RAM_SIZE);
    buffers.flash = (uint8_t *)malloc(GAM4980_FLASH_SIZE);
    buffers.rom_8 = (uint8_t *)malloc(GAM4980_ROM_SIZE);
    buffers.rom_e = (uint8_t *)malloc(GAM4980_ROM_SIZE);
    buffers.flash_size = GAM4980_FLASH_SIZE;
    if (!buffers.ram || !buffers.flash || !buffers.rom_8 || !buffers.rom_e ||
        !load_exact(argv[1], buffers.rom_8, GAM4980_ROM_SIZE) ||
        !load_exact(argv[2], buffers.rom_e, GAM4980_ROM_SIZE) ||
        gam4980_init(&buffers) <= 0 || !load_game(argv[3]))
        goto cleanup;
    gam4980_set_instruction_profile(trace_instruction, 0);
    for (frame = 0u; frame < 1200u; ++frame)
        gam4980_step_frame();
    gam4980_set_instruction_profile(0, 0);
    for (frame = 0u; frame < observed_count; ++frame)
        printf(
            "VECTOR %04x count=%lu\n", observed_vectors[frame],
            (unsigned long)observed_counts[frame]
        );
    result = 0;
    gam4980_deinit();

cleanup:
    free(buffers.rom_e);
    free(buffers.rom_8);
    free(buffers.flash);
    free(buffers.ram);
    return result;
}
