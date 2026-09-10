/* Native ASCII/Chinese row composition, including the shared $6646 row
 * cursor update. These are exact internal ROM entry points, not claims that
 * the complete public SysAscii/SysChinese entry prologues were replaced.
 *
 * All byte composition executes in this PIC NAT module. The EXE callbacks
 * provide guest memory semantics only; the physical LCD mirror is inline.
 */
#ifndef FIRMWARE_NATIVE_HOST_TEST
#include "s6502_iram_exec_abi.h"
#include "firmware_native_graphics_abi.h"
#define FNT_POINTER unsigned long
#else
#define FNT_POINTER uintptr_t
#endif
#include "firmware_native_metrics.h"
#include "firmware_native_graphics_io.h"
#define FW_MEMORY_POINTER FNT_POINTER
#define FW_GRAPHICS_SECTION ".text.firmware_native_text"
#include "firmware_native_graphics_memory.h"

#define FNT_INLINE static inline __attribute__((always_inline))
typedef struct firmware_native_text_result {
    uint8_t ac, ix, iy, status;
} firmware_native_text_result_t;

FNT_INLINE uint8_t fnt_read(s6502_iram_asm_context_t *c, uint16_t address)
{
    return fw_graphics_read(c, address);
}

FNT_INLINE void fnt_write(s6502_iram_asm_context_t *c,
                         uint16_t address, uint8_t value)
{
    firmware_native_graphics_services_t *graphics =
        (firmware_native_graphics_services_t *)(FNT_POINTER)c->graphics;
    uint32_t physical = fw_graphics_write(c, graphics, address, value);
    /* A callback may have yielded to SDK/ROM I/O. Resolve current display
     * ownership only after the guest write returns, never retain a stale LCD
     * target across that gateway. */
    if (physical == 0xffffffffu) return;
    if (graphics->framebuffer) fw_graphics_stage(c, graphics, physical);
}

#define FW_CURSOR_NAME fnt_cursor
#define FW_CURSOR_CONTEXT s6502_iram_asm_context_t *
#define FW_CURSOR_READ(a) fnt_read(c, (a))
#define FW_CURSOR_WRITE(a,v) fnt_write(c, (a), (v))
#define FW_CURSOR_CAN_FOLD (c->pages && ((uint8_t **)(FNT_POINTER)c->pages)[0x20] == ram + 0x2000u)
#include "firmware_native_text_cursor.h"

#define FNT_HELPER_6646_CYCLES(row, column) ( \
    (row) < 0x40u ? 59u : (row) == 0x40u ? 51u : \
    (row) == 0x41u ? ((column) < 8u ? 53u : \
        (uint16_t)(76u + 40u * (((column) - 8u) >> 3))) : 67u \
)

FNT_INLINE uint32_t
fnt_glyph_bits(uint32_t value, uint32_t shift)
{
    uint8_t high;
    uint8_t low;
    uint8_t left_mask;
    uint8_t right_mask;

    /*
     * S1C33 keeps narrow C values in full-width registers.  Canonicalize the
     * input explicitly before a right shift so stale upper bits cannot enter
     * the low byte. Keep every shift count constant as well. This helper is
     * inlined into the separately loaded NAT module, outside the resident
     * CPU dispatcher, and introduces no external call or relocation.
     */
    value &= 0xffu;
    shift &= 7u;
    switch (shift) {
    default:
    case 0u:
        high = value;
        low = 0u;
        left_mask = 0u;
        right_mask = 0xffu;
        break;
    case 1u:
        high = (uint8_t)(value >> 1);
        low = (uint8_t)(value << 7);
        left_mask = 0x80u;
        right_mask = 0x7fu;
        break;
    case 2u:
        high = (uint8_t)(value >> 2);
        low = (uint8_t)(value << 6);
        left_mask = 0xc0u;
        right_mask = 0x3fu;
        break;
    case 3u:
        high = (uint8_t)(value >> 3);
        low = (uint8_t)(value << 5);
        left_mask = 0xe0u;
        right_mask = 0x1fu;
        break;
    case 4u:
        high = (uint8_t)(value >> 4);
        low = (uint8_t)(value << 4);
        left_mask = 0xf0u;
        right_mask = 0x0fu;
        break;
    case 5u:
        high = (uint8_t)(value >> 5);
        low = (uint8_t)(value << 3);
        left_mask = 0xf8u;
        right_mask = 0x07u;
        break;
    case 6u:
        high = (uint8_t)(value >> 6);
        low = (uint8_t)(value << 2);
        left_mask = 0xfcu;
        right_mask = 0x03u;
        break;
    case 7u:
        high = (uint8_t)(value >> 7);
        low = (uint8_t)(value << 1);
        left_mask = 0xfeu;
        right_mask = 0x01u;
        break;
    }

    return (uint32_t)high | ((uint32_t)low << 8) |
           ((uint32_t)left_mask << 16) | ((uint32_t)right_mask << 24);
}

FNT_INLINE uint16_t
fnt_row_cycles(s6502_iram_asm_context_t *c, uint32_t source_index, int wide)
{
    uint8_t *ram = ((uint8_t *)(FNT_POINTER)c->ram);
    uint16_t source = (uint16_t)(ram[0x2fu] | (ram[0x30u] << 8));
    uint16_t destination = (uint16_t)(ram[0x3au] | (ram[0x3bu] << 8));
    uint8_t shift = fnt_read(c, 0x208bu);
    uint8_t column = fnt_read(c, 0x2081u);
    uint8_t lcd_alias_enabled = fnt_read(c, 0x03e5u);
    uint8_t lcd_alias_low = fnt_read(c, 0x03e6u);
    uint8_t lcd_alias_high = fnt_read(c, 0x03e7u);
    uint16_t value;

    source_index &= 0xffu;
    if (wide) {
        value = (uint16_t)(51u + 43u * shift +
            !!(0xff00u & (source ^ (uint16_t)(source + source_index))) +
            !!(0xff00u & (source ^
                (uint16_t)(source + source_index + 1u))));
        if (column == 0x90u) {
            value = (uint16_t)(value + 67u +
                !!(0xff00u & (destination ^
                    (uint16_t)(destination + 1u))));
        } else if (column < 8u) {
            value = (uint16_t)(value +
                (lcd_alias_enabled != 1u ? 88u :
                 ram[0x3bu] != lcd_alias_high ? 98u :
                 ram[0x3au] != lcd_alias_low ? 108u : 142u) +
                !!(0xff00u & (destination ^
                    (uint16_t)(destination + 1u))));
        } else {
            value = (uint16_t)(value +
                (lcd_alias_enabled != 1u ? 87u :
                 ram[0x3bu] != lcd_alias_high ? 97u :
                 ram[0x3au] != lcd_alias_low ? 107u : 140u) +
                !!(0xff00u & (destination ^
                    (uint16_t)(destination + 2u))));
        }
        return (uint16_t)(value + 13u +
            FNT_HELPER_6646_CYCLES(fnt_read(c, 0x2082u), column));
    }

    value = (uint16_t)(40u + 37u * shift +
        !!(0xff00u & (source ^ (uint16_t)(source + source_index))));
    if (column == 0x98u) {
        value = (uint16_t)(value + 57u);
    } else if (lcd_alias_enabled == 1u &&
               destination == (uint16_t)(lcd_alias_low |
                                          (lcd_alias_high << 8))) {
        value = (uint16_t)(value + (column < 8u ? 137u :
            (uint16_t)(136u + ((uint8_t)destination == 0xffu))));
    } else {
        value = (uint16_t)(value + (column < 8u ?
            (uint16_t)(78u +
                (lcd_alias_enabled != 1u ? 9u :
                 ram[0x3bu] != lcd_alias_high ? 19u : 29u)) :
            (uint16_t)(76u +
                    (lcd_alias_enabled != 1u ? 9u :
                     ram[0x3bu] != lcd_alias_high ? 19u : 29u) +
                ((uint8_t)destination == 0xffu))));
    }
    return (uint16_t)(value +
        FNT_HELPER_6646_CYCLES(fnt_read(c, 0x2082u), column));
}

FNT_INLINE void
fnt_ascii_row(s6502_iram_asm_context_t *c,
    uint32_t ix, uint32_t sp, uint32_t status,
    firmware_native_text_result_t *result)
{
    const firmware_native_graphics_services_t *g =
        (const firmware_native_graphics_services_t *)(FNT_POINTER)c->graphics;
    const uint8_t *page3 = (const uint8_t *)(FNT_POINTER)g->page3;
    uint8_t *ram = ((uint8_t *)(FNT_POINTER)c->ram);
    uint32_t source_index = ix & 0xffu;
    uint32_t stack_pointer = sp & 0xffu;
    uint32_t status_value = status & 0xffu;
    uint32_t source;
    uint32_t destination;
    uint32_t alternate;
    uint32_t value;
    uint32_t bits;
    uint32_t column;
    uint32_t row;
    uint32_t return_address;
    uint32_t carry;

    /*
     * Keep the complete HLE row outside s6502_exec().  The S1C33 backend can
     * otherwise reuse dirty upper bits of its byte-sized CPU-register locals
     * across the many indirect memory calls in this block.  Full-width values
     * with explicit masks make each 6502 wrap point unambiguous.
     */
    source = (uint32_t)ram[0x2fu] | ((uint32_t)ram[0x30u] << 8);
    value = (uint32_t)fnt_read(c, (uint16_t)((source + source_index) & 0xffffu));
    bits = fnt_glyph_bits(
        value & 0xffu, (uint32_t)fnt_read(c, 0x208bu) & 7u
    );
    fnt_write(c, 0x20b3u, (uint8_t)bits);
    fnt_write(c, 0x20b4u, (uint8_t)(bits >> 8));
    fnt_write(c, 0x20e5u, (uint8_t)(bits >> 16));
    fnt_write(c, 0x20e6u, (uint8_t)(bits >> 24));

    column = (uint32_t)fnt_read(c, 0x2081u) & 0xffu;
    destination = (uint32_t)ram[0x3au] | ((uint32_t)ram[0x3bu] << 8);
    if (column == 0x98u) {
        result->iy = 0u;
        value = (uint32_t)fnt_read(c, 0x20b3u) & 0xfeu;
        fnt_write(c, 0x20e6u, (uint8_t)value);
        value = ((uint32_t)fnt_read(c, (uint16_t)destination) & 0x01u) | value;
        fnt_write(c, (uint16_t)destination, (uint8_t)value);
        return_address = 0x663du;
    } else if (column >= 8u) {
        result->iy = 1u;
        alternate = (uint32_t)page3[0xe6u] |
                    ((uint32_t)page3[0xe7u] << 8);
        if (page3[0xe5u] == 1u && destination == alternate) {
            alternate = (uint32_t)page3[0xe8u] |
                        ((uint32_t)page3[0xe9u] << 8);
            value = ((uint32_t)fnt_read(c, (uint16_t)alternate) &
                     (uint32_t)fnt_read(c, 0x20e5u)) |
                    (uint32_t)fnt_read(c, 0x20b3u);
            fnt_write(c, (uint16_t)alternate, (uint8_t)value);
            return_address = 0x6598u;
        } else {
            value = ((uint32_t)fnt_read(c, (uint16_t)destination) &
                     (uint32_t)fnt_read(c, 0x20e5u)) |
                    (uint32_t)fnt_read(c, 0x20b3u);
            fnt_write(c, (uint16_t)destination, (uint8_t)value);
            return_address = 0x65b9u;
        }
        value = ((uint32_t)fnt_read(c,
                     (uint16_t)((destination + 1u) & 0xffffu)) &
                 (uint32_t)fnt_read(c, 0x20e6u)) |
                (uint32_t)fnt_read(c, 0x20b4u);
        fnt_write(c,
            (uint16_t)((destination + 1u) & 0xffffu), (uint8_t)value
        );
    } else {
        result->iy = 0u;
        alternate = (uint32_t)ram[0x38u] | ((uint32_t)ram[0x39u] << 8);
        value = ((uint32_t)fnt_read(c, (uint16_t)alternate) &
                 (uint32_t)fnt_read(c, 0x20e5u)) |
                (uint32_t)fnt_read(c, 0x20b3u);
        fnt_write(c, (uint16_t)alternate, (uint8_t)value);

        alternate = (uint32_t)page3[0xe6u] |
                    ((uint32_t)page3[0xe7u] << 8);
        if (page3[0xe5u] == 1u && destination == alternate) {
            alternate = (uint32_t)page3[0xe8u] |
                        ((uint32_t)page3[0xe9u] << 8);
            value = ((uint32_t)fnt_read(c, (uint16_t)alternate) &
                     (uint32_t)fnt_read(c, 0x20e6u)) |
                    (uint32_t)fnt_read(c, 0x20b4u);
            fnt_write(c, (uint16_t)alternate, (uint8_t)value);
            return_address = 0x660au;
        } else {
            value = ((uint32_t)fnt_read(c, (uint16_t)destination) &
                     (uint32_t)fnt_read(c, 0x20e6u)) |
                    (uint32_t)fnt_read(c, 0x20b4u);
            fnt_write(c, (uint16_t)destination, (uint8_t)value);
            return_address = 0x6620u;
        }
    }

    /* Preserve the two stack bytes written by the inlined JSR. */
    ram[0x100u | stack_pointer] = (uint8_t)(return_address >> 8);
    ram[0x100u | ((stack_pointer - 1u) & 0xffu)] =
        (uint8_t)return_address;

    fnt_cursor(c, ram, status_value, &value, &status_value);
    source_index = (source_index + 1u) & 0xffu;
    status_value = (status_value & ~0x82u) | (source_index & 0x80u) |
                   (source_index ? 0u : 0x02u);

    result->ac = (uint8_t)value;
    result->ix = (uint8_t)source_index;
    result->status = (uint8_t)status_value;
}

FNT_INLINE void
fnt_chinese_row(s6502_iram_asm_context_t *c,
    uint32_t ix, uint32_t sp, uint32_t status,
    firmware_native_text_result_t *result)
{
    const firmware_native_graphics_services_t *g =
        (const firmware_native_graphics_services_t *)(FNT_POINTER)c->graphics;
    const uint8_t *page3 = (const uint8_t *)(FNT_POINTER)g->page3;
    uint8_t *ram = ((uint8_t *)(FNT_POINTER)c->ram);
    uint32_t source_index = ix & 0xffu;
    uint32_t stack_pointer = sp & 0xffu;
    uint32_t status_value = status & 0xffu;
    uint32_t source;
    uint32_t destination;
    uint32_t alternate;
    uint32_t raw;
    uint32_t bits;
    uint32_t value;
    uint32_t b3;
    uint32_t b4;
    uint32_t b5;
    uint32_t left_mask;
    uint32_t right_mask;
    uint32_t shift;
    uint32_t column;
    uint32_t row;
    uint32_t return_address;
    uint32_t carry;

    source = (uint32_t)ram[0x2fu] | ((uint32_t)ram[0x30u] << 8);
    b3 = (uint32_t)fnt_read(c,
        (uint16_t)((source + source_index) & 0xffffu)
    );
    b4 = (uint32_t)fnt_read(c,
        (uint16_t)((source + source_index + 1u) & 0xffffu)
    );
    raw = ((b3 & 0xffu) << 16) | ((b4 & 0xffu) << 8);
    shift = (uint32_t)fnt_read(c, 0x208bu) & 7u;
    switch (shift) {
    default:
    case 0u:
        bits = raw;
        left_mask = 0u;
        right_mask = 0xffu;
        break;
    case 1u:
        bits = raw >> 1;
        left_mask = 0x80u;
        right_mask = 0x7fu;
        break;
    case 2u:
        bits = raw >> 2;
        left_mask = 0xc0u;
        right_mask = 0x3fu;
        break;
    case 3u:
        bits = raw >> 3;
        left_mask = 0xe0u;
        right_mask = 0x1fu;
        break;
    case 4u:
        bits = raw >> 4;
        left_mask = 0xf0u;
        right_mask = 0x0fu;
        break;
    case 5u:
        bits = raw >> 5;
        left_mask = 0xf8u;
        right_mask = 0x07u;
        break;
    case 6u:
        bits = raw >> 6;
        left_mask = 0xfcu;
        right_mask = 0x03u;
        break;
    case 7u:
        bits = raw >> 7;
        left_mask = 0xfeu;
        right_mask = 0x01u;
        break;
    }
    b3 = (bits >> 16) & 0xffu;
    b4 = (bits >> 8) & 0xffu;
    b5 = bits & 0xffu;
    fnt_write(c, 0x20b3u, (uint8_t)b3);
    fnt_write(c, 0x20b4u, (uint8_t)b4);
    fnt_write(c, 0x20b5u, (uint8_t)b5);
    fnt_write(c, 0x20e5u, (uint8_t)left_mask);
    fnt_write(c, 0x20e6u, (uint8_t)right_mask);

    column = (uint32_t)fnt_read(c, 0x2081u) & 0xffu;
    destination = (uint32_t)ram[0x3au] | ((uint32_t)ram[0x3bu] << 8);
    if (column == 0x90u) {
        result->iy = 0u;
        value = ((uint32_t)fnt_read(c, (uint16_t)destination) & left_mask) | b3;
        fnt_write(c, (uint16_t)destination, (uint8_t)value);
        result->iy = 1u;
        right_mask = b4 & 0xfeu;
        fnt_write(c, 0x20e6u, (uint8_t)right_mask);
        value = ((uint32_t)fnt_read(c,
                     (uint16_t)((destination + 1u) & 0xffffu)) & 0x01u) |
                right_mask;
        fnt_write(c,
            (uint16_t)((destination + 1u) & 0xffffu), (uint8_t)value
        );
        return_address = 0x61bbu;
    } else if (column >= 8u) {
        result->iy = 0u;
        alternate = (uint32_t)page3[0xe6u] |
                    ((uint32_t)page3[0xe7u] << 8);
        if (page3[0xe5u] == 1u && destination == alternate) {
            alternate = (uint32_t)page3[0xe8u] |
                        ((uint32_t)page3[0xe9u] << 8);
            value = ((uint32_t)fnt_read(c, (uint16_t)alternate) & left_mask) |
                    b3;
            fnt_write(c, (uint16_t)alternate, (uint8_t)value);
        } else {
            value = ((uint32_t)fnt_read(c, (uint16_t)destination) & left_mask) |
                    b3;
            fnt_write(c, (uint16_t)destination, (uint8_t)value);
        }
        fnt_write(c,
            (uint16_t)((destination + 1u) & 0xffffu), (uint8_t)b4
        );
        value = ((uint32_t)fnt_read(c,
                     (uint16_t)((destination + 2u) & 0xffffu)) & right_mask) |
                b5;
        fnt_write(c,
            (uint16_t)((destination + 2u) & 0xffffu), (uint8_t)value
        );
        result->iy = 2u;
        return_address = 0x6130u;
    } else {
        result->iy = 0u;
        alternate = (uint32_t)ram[0x38u] | ((uint32_t)ram[0x39u] << 8);
        value = ((uint32_t)fnt_read(c, (uint16_t)alternate) & left_mask) | b3;
        fnt_write(c, (uint16_t)alternate, (uint8_t)value);

        alternate = (uint32_t)page3[0xe6u] |
                    ((uint32_t)page3[0xe7u] << 8);
        if (page3[0xe5u] == 1u && destination == alternate) {
            alternate = (uint32_t)page3[0xe8u] |
                        ((uint32_t)page3[0xe9u] << 8);
            fnt_write(c, (uint16_t)alternate, (uint8_t)b4);
        } else {
            fnt_write(c, (uint16_t)destination, (uint8_t)b4);
        }
        value = ((uint32_t)fnt_read(c,
                     (uint16_t)((destination + 1u) & 0xffffu)) & right_mask) |
                b5;
        fnt_write(c,
            (uint16_t)((destination + 1u) & 0xffffu), (uint8_t)value
        );
        result->iy = 1u;
        return_address = 0x6192u;
    }

    ram[0x100u | stack_pointer] = (uint8_t)(return_address >> 8);
    ram[0x100u | ((stack_pointer - 1u) & 0xffu)] =
        (uint8_t)return_address;

    /* Inline the shared $6646 address-update helper exactly as the regular
     * glyph-row HLE does. */
    fnt_cursor(c, ram, status_value, &value, &status_value);
    source_index = (source_index + 2u) & 0xffu;
    status_value = (status_value & ~0x82u) | (source_index & 0x80u) |
                   (source_index ? 0u : 0x02u);

    result->ac = (uint8_t)value;
    result->ix = (uint8_t)source_index;
    result->status = (uint8_t)status_value;
}


#include "firmware_native_text_flat.h"

__attribute__((used,noinline,section(".text.firmware_native_text")))
uint32_t firmware_native_text(s6502_iram_asm_context_t *c)
{
    firmware_native_graphics_services_t *graphics;
    firmware_native_text_result_t result;
    fw_graphics_batch_t batch;
    uint32_t total = 0u, rows = 0u, cost, wide, limit, p;
    uint32_t pc = c->pc;
    fnt_flat_env flat;
    int use_flat;
    if ((pc != 0x650fu && pc != 0x608au) || (c->status & 8u) ||
        !c->graphics || !c->ram || !c->read8 ||
        c->cycles > c->cycle_budget)
        return 0u;
    graphics = (firmware_native_graphics_services_t *)(FNT_POINTER)c->graphics;
    if (graphics->version != FW_GRAPHICS_SERVICE_VERSION ||
        !graphics->page3 || !graphics->write8_resolved)
        return 0u;
    wide = pc == 0x608au;
    limit = wide ? 32u : 16u;
    if (c->ix >= limit || fnt_read(c, 0x208bu) > 7u)
        return 0u;
    cost = fnt_row_cycles(c, c->ix, (int)wide);
    if (cost > c->cycle_budget - c->cycles) return 0u;
    use_flat=ft_begin(&flat,c,wide);
    batch.y = 96u; batch.rows = 0u;
    graphics->batch = (FNT_POINTER)&batch;

    for (;;) {
        if(use_flat && wide)ft_chinese_row(&flat,c->ix,c->sp,c->status,&result);
        else if(use_flat)ft_ascii_row(&flat,c->ix,c->sp,c->status,&result);
        else if (wide) fnt_chinese_row(c, c->ix, c->sp, c->status, &result);
        else fnt_ascii_row(c, c->ix, c->sp, c->status, &result);
        c->ac = result.ac; c->ix = result.ix; c->iy = result.iy;
        c->status = result.status;
        c->cycles += cost; total += cost; ++rows;
        if (graphics->metrics) {
            uint32_t *metrics = (uint32_t *)(FNT_POINTER)graphics->metrics;
            ++metrics[2];
            if(use_flat)++metrics[3];
        }
        /* Bound native-only work without re-entering the guest interpreter.
         * Four rows are well below one full glyph; input callbacks themselves
         * never execute guest code. */
        if (!(rows & 3u) && graphics->poll) {
            void (*poll)(void) = (void (*)(void))(FNT_POINTER)graphics->poll;
            poll();
        }
        if (c->ix >= limit || c->cycles >= c->cycle_budget ||
            (((const uint8_t *)(FNT_POINTER)c->ram)[0x200u] & 8u))
            break;
        cost = fnt_row_cycles(c, c->ix, (int)wide);
        if (cost + 4u > c->cycle_budget - c->cycles) break;
        /* CPX #$10/$20; BEQ not taken between complete rows. */
        p = c->status;
        c->status = (p & ~0x83u) | ((c->ix - limit) & 0x80u);
        c->cycles += 4u; total += 4u;
    }
    c->pc = wide ? 0x6086u : 0x650bu;
    fw_graphics_flush(c, graphics);
    graphics->batch = 0u;
    FW_RECORD(c, total);
    return total;
}

#undef FNT_HELPER_6646_CYCLES
#undef FNT_INLINE
#undef FNT_POINTER
