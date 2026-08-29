#include "gam4980_types.h"

/*
 * These leaf functions are compiled separately, checked for relocations, and
 * converted into byte arrays by build_9288_exec_ram_probe.py.  Keep them free
 * of global references and helper calls so the copied code is position
 * independent without a runtime linker.
 */
__attribute__((noinline, used, section(".probe_payload_a")))
u32 exec_ram_payload_a(u32 left, u32 right)
{
    return left + right + 7u;
}

__attribute__((noinline, used, section(".probe_payload_b")))
u32 exec_ram_payload_b(u32 left, u32 right)
{
    return (left ^ right) + 19u;
}
