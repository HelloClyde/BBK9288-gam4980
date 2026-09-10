#ifndef FIRMWARE_NATIVE_ARITHMETIC_H
#define FIRMWARE_NATIVE_ARITHMETIC_H
static inline uint8_t fw_add8_status(uint8_t left, uint8_t right, uint32_t p)
{
    uint32_t sum = (uint32_t)left + right;
    uint8_t value = (uint8_t)sum;
    return (uint8_t)((p & ~0xc3u) | (sum > 255u) |
        (((left ^ sum) & (right ^ sum) & 128u) ? 64u : 0u) |
        (value & 128u) | (value ? 0u : 2u));
}
/* Final high-byte NZ and full-width CV of a binary 16-bit addition. */
static inline uint8_t fw_add16_status(uint32_t left, uint32_t right, uint32_t p)
{
    uint32_t sum = left + right;
    uint32_t high = (sum >> 8) & 255u;
    return (uint8_t)((p & ~0xc3u) | (sum > 65535u) |
        (((left ^ sum) & (right ^ sum) & 0x8000u) ? 64u : 0u) |
        (high & 128u) | (high ? 0u : 2u));
}
static inline uint8_t fw_sub8_status(uint8_t left, uint8_t right, uint32_t p)
{
    uint8_t difference = (uint8_t)(left - right);
    return (uint8_t)((p & ~0xc3u) | (left >= right) |
        (difference ? 0u : 2u) | (difference & 128u) |
        (((left ^ difference) & (left ^ right) & 128u) ? 64u : 0u));
}
static inline uint8_t fw_sub16_status(uint32_t left, uint32_t right, uint32_t p)
{
    uint32_t difference = (left - right) & 65535u;
    uint32_t high = difference >> 8;
    return (uint8_t)((p & ~0xc3u) | (left >= right) |
        (high ? 0u : 2u) | (high & 128u) |
        (((left ^ difference) & (left ^ right) & 0x8000u) ? 64u : 0u));
}
#endif
