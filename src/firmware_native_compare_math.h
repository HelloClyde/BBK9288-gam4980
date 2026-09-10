/* Shared arithmetic semantics, independent of guest memory and call ABI. */
#ifndef FIRMWARE_NATIVE_COMPARE_MATH_H
#define FIRMWARE_NATIVE_COMPARE_MATH_H
static inline unsigned fw_compare_count(uint32_t difference, unsigned bytes)
{
    unsigned count = !!(difference & 255u) + !!(difference & 65280u);
    if (bytes == 4u)
        count += !!(difference & 0xff0000u) + !!(difference & 0xff000000u);
    return count;
}
static inline uint8_t fw_compare_status(uint32_t left, uint32_t right,
                                       unsigned bytes, uint32_t status)
{
    uint32_t difference = left - right;
    uint32_t sign = bytes == 2u ? 0x8000u : 0x80000000u;
    uint32_t mask = bytes == 2u ? 0xffffu : 0xffffffffu;
    return (uint8_t)((status & ~0xc3u) | 0x30u | (left >= right) |
        ((difference & mask) ? 0u : 2u) |
        ((difference & sign) ? 128u : 0u) |
        (((left ^ right) & (left ^ difference) & sign) ? 64u : 0u));
}
#endif
