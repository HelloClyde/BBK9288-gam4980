#ifndef FIRMWARE_NATIVE_LONG_MATH_H
#define FIRMWARE_NATIVE_LONG_MATH_H
static inline uint32_t fw_long_value(unsigned operation, uint32_t left, uint32_t right)
{
    switch (operation) {
    case 1: return left & right;
    case 2: return left | right;
    case 3: return left ^ right;
    case 4: return left + right;
    case 5: return left - right;
    case 6: return left ^ 0x80000000u;
    case 7: return 0u - left;
    default: return ~left;
    }
}
#endif
