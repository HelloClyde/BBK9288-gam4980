#include "firmware_native_long_math.h"
#if !defined(FIRMWARE_NATIVE_HOST_TEST) || defined(FW_RUNTIME_DIRECT_TEST)
static inline __attribute__((always_inline)) int fw_runtime_long_direct(
    s6502_iram_asm_context_t *c, unsigned operation, unsigned left,
    unsigned right, unsigned target, uint8_t *last)
{
    uint8_t **pages = (uint8_t **)(FR_POINTER)c->pages;
    uint8_t *kind = (uint8_t *)(FR_POINTER)c->page_kind;
    uint8_t *l, *r = 0, *out;
    FR_POINTER base = (FR_POINTER)c->ram, physical;
    unsigned i;
    uint32_t a = 0, b = 0, value;
    if (!pages || !kind || (left & 255u) > 252u || (target & 255u) > 252u ||
        !(kind[left >> 8] & 1u) || !(kind[target >> 8] & 2u) ||
        !pages[left >> 8] || !pages[target >> 8]) return 0;
    l = pages[left >> 8] + (left & 255u);
    out = pages[target >> 8] + (target & 255u);
    if ((FR_POINTER)out < base) return 0;
    physical = (FR_POINTER)out - base;
    /* Ordinary RAM only. LCD, control registers and zero-page use the
     * memory adapter, which owns dirty tracking and write side effects. */
    if (physical <= 0x1000u || physical > 0x7ffcu ||
        (physical < 0x2100u && physical + 4u > 0x2000u)) return 0;
    if (operation < 6u) {
        if ((right & 255u) > 252u || !(kind[right >> 8] & 1u) || !pages[right >> 8]) return 0;
        r = pages[right >> 8] + (right & 255u);
    }
    /* Sequential guest reads/writes are not a memmove when spans overlap. */
    if (((FR_POINTER)l < (FR_POINTER)out + 4u && (FR_POINTER)out < (FR_POINTER)l + 4u) ||
        (r && (FR_POINTER)r < (FR_POINTER)out + 4u && (FR_POINTER)out < (FR_POINTER)r + 4u)) return 0;
    for (i = 0; i < 4u; ++i) { a |= (uint32_t)l[i] << (8u*i); if (r) b |= (uint32_t)r[i] << (8u*i); }
    value = fw_long_value(operation, a, b);
    for (i = 0; i < 4u; ++i) out[i] = (uint8_t)(value >> (8u*i));
    *last = (uint8_t)(value >> 24);
    return 1;
}
#else
#define fw_runtime_long_direct(c,operation,left,right,target,last) 0
#endif
