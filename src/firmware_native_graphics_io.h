#ifndef FIRMWARE_NATIVE_GRAPHICS_IO_H
#define FIRMWARE_NATIVE_GRAPHICS_IO_H

/* PIC drawing primitives compiled into each authored NAT function.  The ABI
 * header (or a host test's <stdint.h>) supplies the fixed-width integer types.
 * There are no globals, external symbols, SDK calls, guest-I/O callbacks or
 * assumptions about the current bank here.  Callers resolve bank/page-3
 * aliases through their memory service BEFORE passing canonical RAM offsets.
 *
 * RAM must contain at least bytes $0000..$1000.  A non-NULL framebuffer is the
 * aligned 320x240x2-bpp physical surface, with the 159x96 guest at (1,24).
 * A non-NULL LUT is the app's little-endian shifted [2][256] expansion table;
 * NULL uses local arithmetic, not an external helper.  NULL framebuffer means
 * guest-memory-only operation.  Callers retain responsibility for dirty flags,
 * cycle accounting and bounded event/input checkpoints between row spans.
 */
#define FW_GFX_INLINE static inline __attribute__((always_inline))
#define FW_GFX_LCD_COLUMNS 20u
#define FW_GFX_LCD_HEIGHT 96u
#define FW_GFX_FRAME_WORD_STRIDE 20u
#define FW_GFX_VIEW_Y 24u

/* The first byte column is folded differently from columns 1..19.  $400 is
 * not the visible byte at (1,65): that byte lives at $1000.  Raw stores to
 * $400 itself must NOT be silently redirected; firmware APIs that implement
 * that alias choose $1000 before entering these primitives. */
FW_GFX_INLINE uint16_t fw_gfx_lcd_address(uint32_t column, uint32_t y)
{
    uint32_t address;
    if (column >= FW_GFX_LCD_COLUMNS || y >= FW_GFX_LCD_HEIGHT)
        return 0u;
    if (!column) {
        if (y == 65u) return 0x0ff3u;
        address = 0x0413u + (y < 65u ? 64u - y : y - 1u) * 32u;
    } else {
        address = 0x0400u + (y <= 65u ? 65u - y : y) * 32u + column - 1u;
    }
    return (uint16_t)(address == 0x0400u ? 0x1000u : address);
}

FW_GFX_INLINE int fw_gfx_lcd_position(
    uint32_t address, uint32_t *column, uint32_t *y
)
{
    uint32_t row, x, screen_y;
    if (address <= 0x0400u || address > 0x1000u || !column || !y)
        return 0;
    if (address == 0x1000u) { x = 1u; screen_y = 65u; }
    else if (address == 0x0ff3u) { x = 0u; screen_y = 65u; }
    else {
        row = (address - 0x0400u) >> 5;
        x = (address - 0x0400u) & 31u;
        if (x < 19u) {
            ++x;
            screen_y = row <= 65u ? 65u - row : row;
        } else if (x == 19u) {
            x = 0u;
            screen_y = row < 65u ? 64u - row : row + 1u;
        } else return 0;
        if (screen_y >= FW_GFX_LCD_HEIGHT) return 0;
    }
    *column = x;
    *y = screen_y;
    return 1;
}

FW_GFX_INLINE uint32_t fw_gfx_expand_word(
    uint8_t bits, uint32_t incoming_white, const uint32_t (*lut)[256]
)
{
    uint32_t word = 0u, index, carry = incoming_white ? 0xc0u : 0u;
    if (lut) return lut[incoming_white != 0u][bits];
    for (index = 0u; index < 4u; ++index) {
        uint32_t raw = (bits & (0x80u >> (index * 2u)) ? 0u : 0xf0u) |
            (bits & (0x40u >> (index * 2u)) ? 0u : 0x0fu);
        word |= (carry | (raw >> 2)) << (index * 8u);
        carry = (raw & 3u) << 6;
    }
    return word;
}

/* Mirror an already committed canonical RAM byte.  The alignment shift makes
 * one guest byte affect one host word plus two bits of the next word.  Only
 * those neighbour bits are repaired.  Hidden guest pixel x=159 stays in RAM
 * but is forced white at the physical right edge.  Return actual LCD stores
 * in bytes (8, or 10 with the two neighbour-byte repairs). */
FW_GFX_INLINE uint32_t fw_gfx_lcd_mirror(
    const uint8_t *ram, volatile uint32_t *frame,
    const uint32_t (*lut)[256], uint32_t address
)
{
    uint32_t column, y, incoming_white, word;
    uint8_t bits;
    volatile uint32_t *out;
    if (!ram || !frame || !fw_gfx_lcd_position(address, &column, &y))
        return 0u;
    bits = ram[address];
    if (column == FW_GFX_LCD_COLUMNS - 1u) bits &= 0xfeu;
    incoming_white = !column || !(ram[fw_gfx_lcd_address(column - 1u, y)] & 1u);
    word = fw_gfx_expand_word(bits, incoming_white, lut);
    out = frame + (FW_GFX_VIEW_Y + y * 2u) * FW_GFX_FRAME_WORD_STRIDE + column;
    out[0] = word;
    out[FW_GFX_FRAME_WORD_STRIDE] = word;
    if (column < FW_GFX_LCD_COLUMNS - 1u) {
        uint8_t carry = (bits & 1u) ? 0u : 0xc0u;
        volatile uint8_t *edge = (volatile uint8_t *)(out + 1u);
        edge[0] = (uint8_t)((edge[0] & 0x3fu) | carry);
        edge[FW_GFX_FRAME_WORD_STRIDE * 4u] =
            (uint8_t)((edge[FW_GFX_FRAME_WORD_STRIDE * 4u] & 0x3fu) | carry);
        return 10u;
    }
    return 8u;
}

/* Raw physical LCD-range store, including folded-row padding and $400.  Such
 * invisible bytes retain exact guest RAM effects but cause no LCD writes.
 * The return value counts host stores, NOT whether guest RAM was updated. */
FW_GFX_INLINE uint32_t fw_gfx_lcd_store(
    uint8_t *ram, volatile uint32_t *frame, const uint32_t (*lut)[256],
    uint32_t address, uint8_t value, uint8_t mask
)
{
    if (!ram || address < 0x0400u || address > 0x1000u) return 0u;
    ram[address] = (uint8_t)((ram[address] & (uint8_t)~mask) | (value & mask));
    return fw_gfx_lcd_mirror(ram, frame, lut, address);
}

/* Masked, already-composed packed row, with inclusive byte-column endpoints.
 * Snapshot <=20 bytes before storing, so source may overlap guest destination
 * RAM.  Both masks apply when first==last.  All RAM updates (including hidden
 * x=159) commit before expansion; the loop performs no per-pixel callbacks and
 * repairs the right neighbour once for the entire span. */
FW_GFX_INLINE uint32_t fw_gfx_lcd_span(
    uint8_t *ram, volatile uint32_t *frame, const uint32_t (*lut)[256],
    uint32_t first, uint32_t last, uint32_t y, const uint8_t *source,
    uint8_t left_mask, uint8_t right_mask
)
{
    uint8_t row[FW_GFX_LCD_COLUMNS];
    uint32_t count, index, column, incoming_white;
    volatile uint32_t *out;
    if (!ram || !source || first > last || last >= FW_GFX_LCD_COLUMNS ||
        y >= FW_GFX_LCD_HEIGHT) return 0u;
    count = last - first + 1u;
    /* Keep the bounded scratch extent explicit for freestanding compilers
     * that lose the first<=last range fact across inlined pointer loops. */
    if (!count || count > FW_GFX_LCD_COLUMNS) return 0u;
    for (index = 0u; index < count; ++index) row[index] = source[index];
    if (count == 1u) {
        uint8_t mask = (uint8_t)(left_mask & right_mask);
        uint8_t old = ram[fw_gfx_lcd_address(first, y)];
        row[0] = (uint8_t)((old & (uint8_t)~mask) | (row[0] & mask));
    } else {
        if (left_mask != 0xffu) {
            uint8_t old = ram[fw_gfx_lcd_address(first, y)];
            row[0] = (uint8_t)((old & (uint8_t)~left_mask) | (row[0] & left_mask));
        }
        if (right_mask != 0xffu) {
            uint8_t old = ram[fw_gfx_lcd_address(last, y)];
            row[count - 1u] = (uint8_t)((old & (uint8_t)~right_mask) |
                                      (row[count - 1u] & right_mask));
        }
    }
    /* Fold once per span.  Column zero and (1,65) are the only disconnected
     * bytes; everything after them is a contiguous RAM run. */
    index = 0u;
    column = first;
    if (!column) {
        ram[fw_gfx_lcd_address(0u, y)] = row[index++];
        ++column;
    }
    if (index < count && y == 65u && column == 1u) {
        ram[0x1000u] = row[index++];
        ++column;
    }
    if (index < count) {
        uint8_t *destination = ram + 0x0400u +
            (y <= 65u ? 65u - y : y) * 32u + column - 1u;
        while (index < count) *destination++ = row[index++];
    }
    if (!frame) return 0u;
    if (last == FW_GFX_LCD_COLUMNS - 1u) row[count - 1u] &= 0xfeu;
    incoming_white = !first || !(ram[fw_gfx_lcd_address(first - 1u, y)] & 1u);
    out = frame + (FW_GFX_VIEW_Y + y * 2u) * FW_GFX_FRAME_WORD_STRIDE + first;
    for (index = 0u; index < count; ++index) {
        uint32_t word = fw_gfx_expand_word(row[index], incoming_white, lut);
        out[index] = word;
        out[index + FW_GFX_FRAME_WORD_STRIDE] = word;
        incoming_white = !(row[index] & 1u);
    }
    if (last < FW_GFX_LCD_COLUMNS - 1u) {
        uint8_t carry = incoming_white ? 0xc0u : 0u;
        volatile uint8_t *edge = (volatile uint8_t *)(out + count);
        edge[0] = (uint8_t)((edge[0] & 0x3fu) | carry);
        edge[FW_GFX_FRAME_WORD_STRIDE * 4u] =
            (uint8_t)((edge[FW_GFX_FRAME_WORD_STRIDE * 4u] & 0x3fu) | carry);
        return count * 8u + 2u;
    }
    return count * 8u;
}

#undef FW_GFX_INLINE
#endif
