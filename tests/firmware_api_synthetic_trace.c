#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DL_DOWN
#define _RLS_
#define GAM4980_ENABLE_PROFILING
#include "../src/gam4980_core.c"

typedef struct api_case {
    uint16_t vector;
    const char *name;
    uint8_t first_argument;
    uint8_t arguments[8];
} api_case_t;

static const api_case_t api_cases[] = {
    {0xe78eu, "SysPicture", 8u, {8u, 31u, 23u, 0u, 0x30u, 0u}},
    {0xe8c5u, "SysPartPicture", 8u, {8u, 0u, 0u, 16u, 16u, 0u, 0x30u}},
    {0xe7beu, "SysChinese", 8u, {8u, 0xb0u, 0xa1u}},
    {0xe7bbu, "SysAscii", 8u, {8u, 'A'}},
    {0xe794u, "SysLine", 8u, {8u, 31u, 23u}},
    {0xe797u, "SysRect", 8u, {8u, 31u, 23u}},
    {0xe87du, "SysFillRect", 8u, {8u, 31u, 23u}},
};

static const char *active_name;
static uint32_t trace_count;
static uint32_t last_physical = 0xffffffffu;
static uint16_t last_virtual = 0xffffu;

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

static void trace_instruction(
    void *context, uint16_t virtual_pc, uint32_t physical_pc, uint8_t opcode
)
{
    uint32_t physical_page = physical_pc >> 12;
    uint32_t last_page = last_physical >> 12;

    (void)context;
    if (trace_count < 240u &&
        (trace_count < 24u || physical_page != last_page ||
         virtual_pc == 0xd572u || virtual_pc == 0xd596u ||
         (physical_pc >= 0xeb4000u && physical_pc < 0xec2000u &&
          !(last_physical >= 0xeb4000u && last_physical < 0xec2000u)))) {
        printf(
            "%s %06lx:%04x op=%02x bank=%x/%03x,%03x,%03x,%03x\n",
            active_name, (unsigned long)physical_pc, virtual_pc, opcode,
            sys.bk_sel, sys.bk_tab[4], sys.bk_tab[5], sys.bk_tab[6],
            sys.bk_tab[7]
        );
    }
    ++trace_count;
    last_physical = physical_pc;
    last_virtual = virtual_pc;
}

int main(int argc, char **argv)
{
    gam4980_buffers_t buffers;
    uint32_t index;
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
    if (!buffers.ram || !buffers.flash || !buffers.rom_8 || !buffers.rom_e ||
        !load_exact(argv[1], buffers.rom_8, GAM4980_ROM_SIZE) ||
        !load_exact(argv[2], buffers.rom_e, GAM4980_ROM_SIZE) ||
        gam4980_init(&buffers) <= 0)
        goto cleanup;

    for (index = 0u; index < sizeof(api_cases) / sizeof(api_cases[0]); ++index) {
        s6502_t cpu;
        uint32_t argument;

        memset(sys.ram, 0, GAM4980_RAM_SIZE);
        sys.ram[_SYSCON] = 0u;
        sys.ram[0x26u] = (uint8_t)api_cases[index].vector;
        sys.ram[0x27u] = (uint8_t)(api_cases[index].vector >> 8);
        sys.ram[0x28u] = 0x00u;
        sys.ram[0x29u] = 0x20u;
        for (argument = 0u; argument < sizeof(api_cases[index].arguments);
             ++argument)
            sys.ram[0x2000u + argument] = api_cases[index].arguments[argument];
        for (argument = 0u; argument < 64u; ++argument)
            sys.ram[0x3000u + argument] = (uint8_t)(argument * 37u + 11u);
        sys.ram[0x01feu] = 0xffu;
        sys.ram[0x01ffu] = 0x3fu;
        sys.ram[0x4000u] = 0x4cu;
        sys.ram[0x4001u] = 0x00u;
        sys.ram[0x4002u] = 0x40u;
        cpu.pc = 0xd2f6u;
        cpu.ac = api_cases[index].first_argument;
        cpu.ix = 0u;
        cpu.iy = 0u;
        cpu.sp = 0xfdu;
        cpu.status = 0x20u;
        active_name = api_cases[index].name;
        trace_count = 0u;
        last_physical = 0xffffffffu;
        last_virtual = 0xffffu;
        printf("BEGIN %s vector=%04x\n", active_name, api_cases[index].vector);
        gam4980_set_instruction_profile(trace_instruction, 0);
        (void)s6502_exec(&cpu, 4000u);
        gam4980_set_instruction_profile(0, 0);
        printf(
            "END %s instructions=%lu pc=%04x sp=%02x\n",
            active_name, (unsigned long)trace_count, cpu.pc, cpu.sp
        );
    }
    result = 0;
    gam4980_deinit();

cleanup:
    free(buffers.rom_e);
    free(buffers.rom_8);
    free(buffers.flash);
    free(buffers.ram);
    return result;
}
