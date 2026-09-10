#ifndef FIRMWARE_NATIVE_MULTIPLY_MATH_H
#define FIRMWARE_NATIVE_MULTIPLY_MATH_H
/* Recover only the final CV of the firmware's multiplication algorithm.
 * The product itself is a native multiply, never an 8/16-round guest loop. */
static inline uint8_t fw_mul16_status(uint32_t a, uint32_t b, uint32_t p)
{
    uint32_t last=b, before, sum;
    if (!a || !b) return (uint8_t)p;
    if (!(last & 255u)) last >>= 8;
    if (!(last & 15u)) last >>= 4;
    if (!(last & 3u)) last >>= 2;
    if (!(last & 1u)) last >>= 1;
    before = (uint16_t)((uint16_t)(a * (last >> 1)) << 1);
    sum = (a >> 8) + (before >> 8) + (((a & 255u) + (before & 255u)) >> 8);
    return (uint8_t)((p & ~0x41u) |
        ((((a >> 8) ^ sum) & ((before >> 8) ^ sum) & 128u) ? 64u : 0u) |
        ((b & 1u) && sum > 255u));
}
#endif
