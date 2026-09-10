/* Context-parametric cursor shared by EXE, NAT and flat glyphs.
 * No include guard: each memory adapter instantiates it once. */
static inline __attribute__((always_inline)) void FW_CURSOR_NAME(FW_CURSOR_CONTEXT c, uint8_t *ram,
    uint32_t status_value, uint32_t *out_value, uint32_t *out_status)
{
    uint32_t row, column, destination, carry, value = 0;
    row = (uint32_t)FW_CURSOR_READ(0x2082u) & 0xffu;
    if (row < 0x40u) {
        destination = (uint32_t)ram[0x3au] | ((uint32_t)ram[0x3bu] << 8);
        destination = (destination - 0x20u) & 0xffffu;
        ram[0x3au] = (uint8_t)destination;
        ram[0x3bu] = (uint8_t)(destination >> 8);

        destination = (uint32_t)ram[0x38u] | ((uint32_t)ram[0x39u] << 8);
        carry = destination >= 0x20u;
        status_value = (status_value & ~0x40u) |
            (((destination >> 8) == 0x80u && (destination & 0xffu) < 0x20u)
                ? 0x40u : 0u);
        destination = (destination - 0x20u) & 0xffffu;
        ram[0x38u] = (uint8_t)destination;
        ram[0x39u] = (uint8_t)(destination >> 8);
        value = destination >> 8;
        status_value = (status_value & ~1u) | carry;
    } else if (row == 0x40u) {
        destination = (uint32_t)ram[0x3au] | ((uint32_t)ram[0x3bu] << 8);
        carry = destination >= 0x20u;
        status_value = (status_value & ~0x40u) |
            (((destination >> 8) == 0x80u && (destination & 0xffu) < 0x20u)
                ? 0x40u : 0u);
        destination = (destination - 0x20u) & 0xffffu;
        ram[0x3au] = (uint8_t)destination;
        ram[0x3bu] = (uint8_t)(destination >> 8);
        ram[0x38u] = 0xf3u;
        ram[0x39u] = 0x0fu;
        value = 0x0fu;
        status_value = (status_value & ~1u) | carry;
    } else if (row == 0x41u) {
        ram[0x38u] = 0x33u;
        ram[0x39u] = 0x0cu;
        ram[0x3au] = 0x40u;
        ram[0x3bu] = 0x0cu;
        value = (uint32_t)FW_CURSOR_READ(0x2081u) & 0xffu;
        status_value &= ~1u;
        if (value >= 8u) {
            /* The final SBC #8 sees a remainder 0..7, so V is clear. */
            status_value &= ~0x40u;
            column = (value - 8u) & 0xffu;
            if (FW_CURSOR_CAN_FOLD) {
                destination = 0x0c40u + (column >> 3);
                column &= 7u;
                FW_CURSOR_WRITE(0x20b7u, (uint8_t)column);
                ram[0x3au] = (uint8_t)destination;
                ram[0x3bu] = (uint8_t)(destination >> 8);
            } else {
            FW_CURSOR_WRITE(0x20b7u, (uint8_t)column);
            while (column >= 8u) {
                column = (column - 8u) & 0xffu;
                FW_CURSOR_WRITE(0x20b7u, (uint8_t)column);
                destination = (uint32_t)ram[0x3au] |
                              ((uint32_t)ram[0x3bu] << 8);
                destination = (destination + 1u) & 0xffffu;
                ram[0x3au] = (uint8_t)destination;
                ram[0x3bu] = (uint8_t)(destination >> 8);
            }
            }
            value = (column - 8u) & 0xffu;
        }
    } else {
        destination = (uint32_t)ram[0x3au] | ((uint32_t)ram[0x3bu] << 8);
        destination = (destination + 0x20u) & 0xffffu;
        ram[0x3au] = (uint8_t)destination;
        ram[0x3bu] = (uint8_t)(destination >> 8);

        destination = (uint32_t)ram[0x38u] | ((uint32_t)ram[0x39u] << 8);
        carry = destination > 0xffdfu;
        status_value = (status_value & ~0x40u) |
            (((destination >> 8) == 0x7fu && (destination & 0xffu) >= 0xe0u)
                ? 0x40u : 0u);
        destination = (destination + 0x20u) & 0xffffu;
        ram[0x38u] = (uint8_t)destination;
        ram[0x39u] = (uint8_t)(destination >> 8);
        value = destination >> 8;
        status_value = (status_value & ~1u) | carry;
    }

    FW_CURSOR_WRITE(0x2082u, (uint8_t)((row + 1u) & 0xffu));
    *out_value = value;
    *out_status = status_value;
}
#undef FW_CURSOR_NAME
#undef FW_CURSOR_CONTEXT
#undef FW_CURSOR_READ
#undef FW_CURSOR_WRITE
#undef FW_CURSOR_CAN_FOLD
