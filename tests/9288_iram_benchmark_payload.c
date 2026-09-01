#include "gam4980_types.h"

/*
 * Compiled separately and copied to both external scratch RAM and IRAM.
 * Keep this leaf function free of global references and helper calls so the
 * raw section remains position independent without a runtime linker.
 */
__attribute__((noinline, used, section(".iram_benchmark_payload")))
u32 iram_benchmark_payload(u32 seed, u32 iterations)
{
    u32 left = seed ^ 0xa5a55a5au;
    u32 right = seed + 0x13579bdfu;

    while (iterations--) {
        left += right;
        right ^= left >> 3;
        left ^= right << 7;
        right += left >> 11;
        left += 0x9e3779b9u;
    }
    return left ^ right;
}
