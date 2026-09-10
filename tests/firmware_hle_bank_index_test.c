/* Differential check of indexed bank publication against the original full
 * table scanner. No SDK calls, games or external firmware files are needed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define DL_DOWN
#define _RLS_
#define GAM4980_ENABLE_AOT
#define GAM4980_ENABLE_FIRMWARE_HLE
#define GAM4980_ENABLE_GAME_LOAD_AOT
#define GAM4980_ENABLE_IRAM_EXEC_ENGINE
#define GAM4980_IRAM_EXEC_ASM
#define GAM4980_IRAM_SUPER_HOST_TEST
#define GAM4980_IRAM_EXEC_NATIVE_TEST
#include "../src/gam4980_core.c"

uint32_t s6502_iram_exec_burst_asm(s6502_iram_asm_context_t *context)
{ (void)context; return 0; }

static uint8_t reference_bits[65536];
static uint16_t reference_banks[16];
static uint32_t reference_count, checks, rng = 0x92884980u;
static uint32_t random_value(void)
{ rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static void old_scanner(uint32_t virtual_bank, uint16_t physical_bank, int set_bits)
{
    uint32_t index;
    if (set_bits && !s6502_firmware_hle_enabled)
        return;
    for (index = 0; index < S6502_IRAM_FIRMWARE_HLE_ENTRY_COUNT; ++index) {
        const s6502_iram_firmware_hle_entry_t *entry =
            &s6502_iram_firmware_hle_entries[index];
        if ((entry->virtual_pc >> 12) != virtual_bank ||
            (entry->physical_pc >> 12) != physical_bank)
            continue;
        if (set_bits) {
            reference_bits[entry->virtual_pc] = 1u;
            ++reference_count;
        } else {
            reference_bits[entry->virtual_pc] = 0u;
            if (reference_count)
                --reference_count;
        }
    }
}

static int equal(void)
{
    ++checks;
    if (reference_count != s6502_iram_dispatch_firmware_hle_entries ||
        memcmp(reference_bits, s6502_iram_dispatch_bits, sizeof(reference_bits))) {
        fprintf(stderr, "bank index check %u: count=%u/%u\n", checks,
            reference_count, s6502_iram_dispatch_firmware_hle_entries);
        return 0;
    }
    return 1;
}

static void reference_prepare(void)
{
    unsigned slot;
    memset(reference_bits, 0, sizeof(reference_bits));
    reference_count = 0;
    for (slot = 0; slot < 16u; ++slot) {
        reference_banks[slot] = sys.bk_tab[slot];
        old_scanner(slot, reference_banks[slot], 1);
    }
}

static int verify_index(void)
{
    unsigned bucket, index, total = 0;
    uint8_t seen[S6502_IRAM_FIRMWARE_HLE_ENTRY_COUNT];
    memset(seen, 0, sizeof(seen));
    for (bucket = 0; bucket <= S6502_IRAM_HLE_BANK_COUNT; ++bucket) {
        uint16_t link = s6502_iram_firmware_hle_head[bucket];
        unsigned chain = 0;
        while (link) {
            unsigned expected_bucket;
            index = link - 1u;
            if (++chain > S6502_IRAM_FIRMWARE_HLE_ENTRY_COUNT ||
                index >= S6502_IRAM_FIRMWARE_HLE_ENTRY_COUNT || seen[index]++)
                return 0;
            expected_bucket =
                (s6502_iram_firmware_hle_entries[index].physical_pc >> 12) -
                S6502_IRAM_HLE_BANK_FIRST;
            if (expected_bucket >= S6502_IRAM_HLE_BANK_COUNT)
                expected_bucket = S6502_IRAM_HLE_BANK_COUNT;
            if (expected_bucket != bucket)
                return 0;
            ++total;
            link = s6502_iram_firmware_hle_next[index];
        }
    }
    return total == S6502_IRAM_FIRMWARE_HLE_ENTRY_COUNT;
}

int main(void)
{
    unsigned i, slot, set, t;
    s6502_firmware_hle_enabled = 1;
    s6502_iram_prepare_dispatch_bits();
    if (!verify_index()) return 2;

    /* Every table entry, every correct/incorrect virtual window and both
     * operations; untouched bytes start nonzero to catch broad clearing. */
    for (i = 0; i < S6502_IRAM_FIRMWARE_HLE_ENTRY_COUNT; ++i)
        for (slot = 0; slot < 32u; ++slot)
            for (set = 0; set < 2u; ++set) {
                uint16_t bank = (uint16_t)(s6502_iram_firmware_hle_entries[i].physical_pc >> 12);
                memset(reference_bits, 0xa5, sizeof(reference_bits));
                memcpy(s6502_iram_dispatch_bits, reference_bits, sizeof(reference_bits));
                reference_count = s6502_iram_dispatch_firmware_hle_entries = random_value();
                old_scanner(slot, bank, (int)set);
                s6502_iram_apply_firmware_hle_bank(slot, bank, (int)set);
                if (!equal()) return 1;
            }

    /* Repeated set/clear calls deliberately retain the old counter semantics
     * even when a caller repeats a publication or starts near uint32 wrap. */
    for (t = 0; t < 100000u; ++t) {
        uint16_t bank;
        i = random_value() % S6502_IRAM_FIRMWARE_HLE_ENTRY_COUNT;
        bank = (t & 1u) ? (uint16_t)(s6502_iram_firmware_hle_entries[i].physical_pc >> 12)
                         : (uint16_t)random_value();
        slot = (t & 2u) ? s6502_iram_firmware_hle_entries[i].virtual_pc >> 12
                         : random_value() % 32u;
        set = random_value() & 1u;
        s6502_firmware_hle_enabled = (random_value() & 3u) != 0u;
        if (!(t % 257u))
            reference_count = s6502_iram_dispatch_firmware_hle_entries = (t & 1u) ? 0u : 0xfffffffeu;
        old_scanner(slot, bank, (int)set);
        s6502_iram_apply_firmware_hle_bank(slot, bank, (int)set);
        if (!equal()) return 1;
    }

    /* Use the real remap caller, including no-op remaps and HLE-toggle rebuilds.
     * Game/AOT lists are empty here so their independently indexed publication
     * cannot mask a discrepancy in firmware bits or entry counts. */
    for (slot = 0; slot < 16u; ++slot) sys.bk_tab[slot] = 0u;
    s6502_iram_prepare_dispatch_bits();
    reference_prepare();
    if (!equal()) return 1;
    for (t = 0; t < 50000u; ++t) {
        uint16_t bank;
        i = random_value() % S6502_IRAM_FIRMWARE_HLE_ENTRY_COUNT;
        slot = random_value() % 20u;
        bank = t % 3u ? (uint16_t)(s6502_iram_firmware_hle_entries[i].physical_pc >> 12)
                       : (uint16_t)random_value();
        if (slot < 16u) {
            if (t % 7u == 0u) bank = sys.bk_tab[slot];
            sys.bk_tab[slot] = bank;
            if (reference_banks[slot] != bank) {
                old_scanner(slot, reference_banks[slot], 0);
                old_scanner(slot, bank, 1);
                reference_banks[slot] = bank;
            }
        }
        s6502_iram_refresh_dispatch_bank(slot);
        if (!equal()) return 1;
        if (!(t % 257u)) {
            s6502_firmware_hle_enabled = !s6502_firmware_hle_enabled;
            s6502_iram_prepare_dispatch_bits();
            reference_prepare();
            if (!verify_index() || !equal()) return 1;
        }
    }
    printf("firmware HLE bank index: %u entries, %u exact bitmap/counter checks passed; index=%u bytes\n",
        (unsigned)S6502_IRAM_FIRMWARE_HLE_ENTRY_COUNT, checks,
        (unsigned)(sizeof(s6502_iram_firmware_hle_head) + sizeof(s6502_iram_firmware_hle_next)));
    return 0;
}
