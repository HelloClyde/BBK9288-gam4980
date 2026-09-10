#ifndef FIRMWARE_NATIVE_GRAPHICS_MEMORY_H
#define FIRMWARE_NATIVE_GRAPHICS_MEMORY_H

typedef struct fw_graphics_batch {
    uint32_t y, first, last, rows;
} fw_graphics_batch_t;

/* One local row sink per pageable function, not a copy at every store site.
 * Same-section local calls are PC-relative and move with the NAT payload. */
static __attribute__((noinline, section(FW_GRAPHICS_SECTION))) void fw_graphics_flush_row(
    uint8_t *ram, uint32_t guest_pc, firmware_native_graphics_services_t *s)
{
    fw_graphics_batch_t *b = (fw_graphics_batch_t *)(FW_MEMORY_POINTER)s->batch;
    uint8_t bytes[20];
    uint8_t *shadow = (uint8_t *)(FW_MEMORY_POINTER)s->synced_frame;
    uint32_t *valid = (uint32_t *)(FW_MEMORY_POINTER)s->synced_rows;
    uint32_t i, stores;
    if (!b || b->y >= 96u) return;
    for (i = b->first; i <= b->last; ++i)
        bytes[i - b->first] = ram[fw_gfx_lcd_address(i, b->y)];
    stores = fw_gfx_lcd_span(ram,
        (volatile uint32_t *)(FW_MEMORY_POINTER)s->framebuffer,
        (const uint32_t (*)[256])(FW_MEMORY_POINTER)s->expand_lut,
        b->first, b->last, b->y, bytes, 255u, 255u);
    if (s->metrics)
        ((uint32_t *)(FW_MEMORY_POINTER)s->metrics)[0] += stores;
    if (stores && shadow && valid) {
        /* The first word repairs half of the preceding pixel too. If that
         * pixel changed outside this span, a full row repair is still needed. */
        if (b->first && ((shadow[b->y * 20u + b->first - 1u] ^
                ram[fw_gfx_lcd_address(b->first - 1u, b->y)]) & 1u))
            valid[b->y >> 5] &= ~(1u << (b->y & 31u));
        for (i = b->first; i <= b->last; ++i)
            shadow[b->y * 20u + i] = bytes[i - b->first];
        if (!b->first && b->last == 19u)
            valid[b->y >> 5] |= 1u << (b->y & 31u);
    }
    b->y = 96u;
    if (!(++b->rows & 3u) && s->poll &&
        guest_pc != 0x650fu && guest_pc != 0x608au &&
        guest_pc != 0x650bu && guest_pc != 0x6086u)
        ((void (*)(void))(FW_MEMORY_POINTER)s->poll)();
}

/* Do not pass the whole CPU context to an out-of-line function: that makes
 * register state escape to the stack even though the sink only needs RAM/PC.
 * Callback-visible framebuffer/services are still read by the sink itself. */
static inline __attribute__((always_inline)) void fw_graphics_flush(
    s6502_iram_asm_context_t *c, firmware_native_graphics_services_t *s)
{
    fw_graphics_flush_row((uint8_t *)(FW_MEMORY_POINTER)c->ram, c->pc, s);
}

static inline __attribute__((always_inline)) void fw_graphics_stage(
    s6502_iram_asm_context_t *c, firmware_native_graphics_services_t *s,
    uint32_t physical)
{
    fw_graphics_batch_t *b = (fw_graphics_batch_t *)(FW_MEMORY_POINTER)s->batch;
    uint32_t column, y;
    if (!b || !fw_gfx_lcd_position(physical, &column, &y)) return;
    if (b->y != y) {
        fw_graphics_flush(c, s);
        b->y = y; b->first = column; b->last = column;
    } else {
        if (column < b->first) b->first = column;
        if (column > b->last) b->last = column;
    }
    if (s->metrics) ++((uint32_t *)(FW_MEMORY_POINTER)s->metrics)[1];
}

/* Reuse the LIVE read-page map. Page zero and page-3 I/O have no ordinary
 * RAM pointer; never infer writability from the guest address alone. */
static inline __attribute__((always_inline)) uint8_t fw_graphics_read(
    s6502_iram_asm_context_t *c, uint16_t address)
{
    uint8_t **pages = (uint8_t **)(FW_MEMORY_POINTER)c->pages;
    uint8_t *page = pages ? pages[address >> 8] : 0;
    if (page) return page[address & 255u];
    return ((uint8_t (*)(uint16_t))(FW_MEMORY_POINTER)c->read8)(address);
}

/* Return a changed canonical RAM offset, or ~0u when no LCD mirror is
 * needed. The exceptional PB/APO stores retain ram_write's postconditions.
 * ROM, flash, page-3 and I/O always use the original service. */
static inline __attribute__((always_inline)) uint32_t fw_graphics_write(
    s6502_iram_asm_context_t *c,
    firmware_native_graphics_services_t *s, uint16_t address, uint8_t value)
{
    uint8_t **pages = (uint8_t **)(FW_MEMORY_POINTER)c->pages;
    uint8_t *page = pages ? pages[address >> 8] : 0;
    FW_MEMORY_POINTER base = (FW_MEMORY_POINTER)page;
    FW_MEMORY_POINTER ram = (FW_MEMORY_POINTER)c->ram;
    if (page && base >= ram && base - ram <= 0x7f00u) {
        uint32_t physical = (uint32_t)(base - ram) + (address & 255u);
        uint8_t *out = page + (address & 255u);
        uint8_t old = *out;
        if (physical == 0x021bu) value = 0u;
        if (physical == 0x2028u) value = 0xffu;
        if (physical >= 0x0400u && physical <= 0x1000u) {
            if (c->lcd_write_calls)
                ++*(uint32_t *)(FW_MEMORY_POINTER)c->lcd_write_calls;
            if (old != value) {
                if (c->dirty) *(int *)(FW_MEMORY_POINTER)c->dirty = 1;
                if (c->lcd_changed_writes)
                    ++*(uint32_t *)(FW_MEMORY_POINTER)c->lcd_changed_writes;
            }
        }
        *out = value;
        return old != value ? physical : 0xffffffffu;
    }
    return ((uint32_t (*)(uint16_t, uint8_t))
        (FW_MEMORY_POINTER)s->write8_resolved)(address, value);
}
#endif
