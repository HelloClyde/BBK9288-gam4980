#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned long gam4980_9288_iram_begin_range(
    const void *source, uint32_t size
);
int gam4980_9288_iram_end_range(void);

#define DL_DOWN
#define _RLS_
#define GAM4980_ENABLE_AOT
#define GAM4980_ENABLE_FIRMWARE_HLE
#define GAM4980_FIRMWARE_HLE_MASK 1023
#define GAM4980_ENABLE_AGGRESSIVE_REGION_HLE
#define GAM4980_ENABLE_GAME_LOAD_AOT
#define GAM4980_ENABLE_IRAM_EXEC_ENGINE
#define GAM4980_IRAM_EXEC_ASM
#define GAM4980_IRAM_EXEC_NATIVE_TEST
#define GAM4980_IRAM_SUPER_HOST_TEST
#include "../src/gam4980_core.c"

unsigned long gam4980_9288_iram_begin_range(
    const void *source, uint32_t size
)
{
    (void)source;
    (void)size;
    return 1u;
}

int gam4980_9288_iram_end_range(void)
{
    return 1;
}

uint32_t s6502_iram_exec_burst_asm(s6502_iram_asm_context_t *context)
{
    (void)context;
    return 0u;
}

static int load_exact(const char *path, uint8_t *data, uint32_t size)
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
    uint8_t header[GAM4980_GAME_HEADER_SIZE];
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
        fread(gam4980_game_storage(), 1, (size_t)size, file) !=
            (size_t)size) {
        fclose(file);
        return 0;
    }
    fclose(file);
    return gam4980_load_game_header(header, (uint32_t)size) > 0;
}

static int resident_semantic(uint8_t semantic)
{
    return semantic == C6502_TEMPLATE_LOAD_OPER1_IMM16 ||
        semantic == C6502_TEMPLATE_LOAD_OPER2_IMM16 ||
        semantic == C6502_TEMPLATE_STACK_ADD16 ||
        semantic == C6502_TEMPLATE_STACK_SUB16 ||
        semantic == C6502_TEMPLATE_ADD16_OPER1_OPER2;
}

static uint32_t bank_marker_count(uint16_t physical_bank)
{
    uint32_t count = 0u;
    uint16_t link = s6502_iram_super_bank_head[
        physical_bank - S6502_IRAM_SUPER_BANK_FIRST
    ];

    while (link) {
        ++count;
        link = s6502_iram_super_bank_next[link - 1u];
    }
    return count;
}

static int shadow_is_disabled_and_original(void)
{
    uint32_t index;

    if (s6502_game_aot_enabled)
        return 0;
    for (index = 0u; index < GAM4980_IRAM_SUPER_COUNT; ++index) {
        if (s6502_iram_super_match_builds[index])
            return 0;
    }
    for (index = S6502_IRAM_SHADOW_FIRST_PAGE;
         index < S6502_IRAM_SHADOW_FIRST_PAGE +
            S6502_IRAM_SHADOW_PAGE_COUNT; ++index) {
        if (s6502_iram_code_pages[index] != sys.mem_r[index])
            return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    gam4980_buffers_t buffers;
    uint8_t *rom8;
    uint8_t *rome;
    uint32_t index;
    uint32_t matches = 0u;
    uint32_t checked_entries = 0u;
    uint32_t shadow_pages = 0u;
    uint16_t first_bank = 0xffffu;
    uint16_t second_bank = 0xffffu;

    if (argc != 4) {
        fprintf(stderr, "usage: iram_super_shadow_runtime 8.BIN E.BIN game.gam\n");
        return 2;
    }
    memset(&buffers, 0, sizeof(buffers));
    buffers.ram = (uint8_t *)malloc(GAM4980_RAM_SIZE);
    buffers.flash = (uint8_t *)malloc(GAM4980_FLASH_SIZE);
    buffers.flash_size = GAM4980_FLASH_SIZE;
    rom8 = (uint8_t *)malloc(GAM4980_ROM_SIZE);
    rome = (uint8_t *)malloc(GAM4980_ROM_SIZE);
    buffers.rom_8 = rom8;
    buffers.rom_e = rome;
    if (!buffers.ram || !buffers.flash || !rom8 || !rome)
        return 3;
    if (!load_exact(argv[1], rom8, GAM4980_ROM_SIZE) ||
        !load_exact(argv[2], rome, GAM4980_ROM_SIZE) ||
        gam4980_init(&buffers) <= 0) {
        return 4;
    }
    gam4980_set_iram_exec_enabled(1);
    if (!load_game(argv[3]))
        return 5;

    for (index = 0u; index < S6502_IRAM_SUPER_BANK_COUNT; ++index) {
        if (!s6502_iram_super_bank_head[index])
            continue;
        if (first_bank == 0xffffu)
            first_bank = (uint16_t)(S6502_IRAM_SUPER_BANK_FIRST + index);
        else {
            second_bank = (uint16_t)(S6502_IRAM_SUPER_BANK_FIRST + index);
            break;
        }
    }
    if (first_bank == 0xffffu)
        return 6;
    sys.bk_tab[5] = first_bank;
    mem_bs(5u);
    {
        uint32_t rebuilds = s6502_iram_shadow_rebuilds;
        uint32_t visits = s6502_iram_shadow_marker_visits;
        uint32_t built = 0u;

        for (index = 0u; index < GAM4980_IRAM_SUPER_COUNT; ++index)
            built += s6502_iram_super_match_builds[index];
        mem_bs(5u);
        if (s6502_iram_shadow_rebuilds != rebuilds ||
            s6502_iram_shadow_marker_visits != visits) {
            fprintf(stderr, "same-bank mem_bs rebuilt shadow\n");
            return 7;
        }
        matches = 0u;
        for (index = 0u; index < GAM4980_IRAM_SUPER_COUNT; ++index)
            matches += s6502_iram_super_match_builds[index];
        if (matches != built) {
            fprintf(stderr, "same-bank mem_bs rebuilt markers\n");
            return 8;
        }
    }
    if (second_bank != 0xffffu) {
        uint32_t visits = s6502_iram_shadow_marker_visits;
        uint32_t expected = bank_marker_count(second_bank);

        sys.bk_tab[5] = second_bank;
        mem_bs(5u);
        if (s6502_iram_shadow_marker_visits - visits != expected) {
            fprintf(stderr, "bank switch marker visits mismatch\n");
            return 9;
        }
    }

    matches = 0u;
    for (index = 0u; index < GAM4980_IRAM_SUPER_COUNT; ++index)
        matches += s6502_iram_super_match_builds[index];
    for (index = 0u; index < 0x100u; ++index) {
        if (s6502_iram_code_pages[index] &&
            s6502_iram_code_pages[index] != sys.mem_r[index])
            ++shadow_pages;
    }
    if (!matches || !shadow_pages) {
        fprintf(stderr, "vacuous initial shadow matches=%u pages=%u\n",
            matches, shadow_pages);
        return 11;
    }

    /* Debug-off keeps the assembly hit pointer null.  Repeatedly alternate
     * two real indexed banks and verify the bounded no-metrics churn policy
     * permanently selects the raw live page table for this game session. */
    if (second_bank != 0xffffu) {
        uint32_t rebuilds;
        uint32_t visits;

        for (index = 0u;
             index < S6502_IRAM_SHADOW_NO_METRICS_LIMIT + 4u; ++index) {
            sys.bk_tab[5] = (index & 1u) ? first_bank : second_bank;
            mem_bs(5u);
            if (!s6502_iram_shadow_enabled)
                break;
        }
        if (s6502_iram_shadow_enabled ||
            s6502_iram_shadow_disable_reason !=
                S6502_IRAM_SHADOW_DISABLE_NO_METRICS_CHURN ||
            s6502_iram_shadow_adaptive_disables != 1u ||
            s6502_iram_shadow_rebuilds >
                S6502_IRAM_SHADOW_NO_METRICS_LIMIT) {
            fprintf(stderr, "adaptive churn guard did not disable shadow\n");
            return 12;
        }
        for (index = 0u; index < 0x100u; ++index) {
            if (s6502_iram_code_pages[index] != sys.mem_r[index]) {
                fprintf(stderr, "adaptive disable did not restore raw pages\n");
                return 13;
            }
        }
        rebuilds = s6502_iram_shadow_rebuilds;
        visits = s6502_iram_shadow_marker_visits;
        sys.bk_tab[5] = first_bank;
        mem_bs(5u);
        if (s6502_iram_shadow_rebuilds != rebuilds ||
            s6502_iram_shadow_marker_visits != visits) {
            fprintf(stderr, "disabled shadow refresh still did work\n");
            return 14;
        }
    }

    for (index = 0u; index < s6502_game_aot_entry_count; ++index) {
        const s6502_game_aot_entry_t *entry =
            &s6502_game_aot_entries[index];
        uint32_t bank = 5u;
        uint16_t virtual_pc;

        if (!resident_semantic(entry->semantic))
            continue;
        virtual_pc = (uint16_t)(
            (bank << 12) | (entry->physical_pc & 0x0fffu)
        );
        sys.bk_tab[bank] = (uint16_t)(entry->physical_pc >> 12);
        mem_bs((uint8_t)bank);
        if (s6502_iram_dispatch_bits[virtual_pc]) {
            fprintf(stderr, "resident semantic dispatch collision pc=%04x\n",
                virtual_pc);
            return 15;
        }
        ++checked_entries;
    }
    if (!checked_entries) {
        fprintf(stderr, "vacuous shadow test matches=%u pages=%u entries=%u\n",
            matches, shadow_pages, checked_entries);
        return 16;
    }

    if (second_bank != 0xffffu) {
        if (!load_game(argv[3]))
            return 17;
        gam4980_set_performance_debug(1);
        for (index = 0u;
             index < S6502_IRAM_SHADOW_CHECK_REBUILDS + 4u; ++index) {
            sys.bk_tab[5] = (index & 1u) ? first_bank : second_bank;
            mem_bs(5u);
            if (!s6502_iram_shadow_enabled)
                break;
        }
        if (s6502_iram_shadow_enabled ||
            s6502_iram_shadow_disable_reason !=
                S6502_IRAM_SHADOW_DISABLE_LOW_YIELD ||
            !s6502_iram_shadow_adaptive_checks) {
            fprintf(stderr, "debug low-yield guard did not disable shadow\n");
            return 18;
        }
        gam4980_set_performance_debug(0);
    }

    /* Byte programming game code invalidates AOT before changing the source
     * byte; prepare must immediately restore original fetch pages. */
    sys.flash_cycles = 0u;
    sys.flash_cmd = 0u;
    flash_write(0x5555u, 0xaau);
    flash_write(0x2aaau, 0x55u);
    flash_write(0x5555u, 0xa0u);
    flash_write(0xd000u, (uint8_t)(sys.flash[0x15000u] ^ 1u));
    if (!shadow_is_disabled_and_original()) {
        fprintf(stderr, "byte program left stale shadow markers\n");
        return 19;
    }
    if (!load_game(argv[3]))
        return 20;
    flash_erase_range(0x15000u, 0x1000u);
    if (!shadow_is_disabled_and_original()) {
        fprintf(stderr, "sector erase left stale shadow markers\n");
        return 21;
    }
    printf("shadow matches=%u pages=%u resident_entries=%u\n",
        matches, shadow_pages, checked_entries);
    return 0;
}
