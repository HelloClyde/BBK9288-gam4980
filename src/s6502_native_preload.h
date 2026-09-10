#ifndef S6502_NATIVE_PRELOAD_H
#define S6502_NATIVE_PRELOAD_H

/* A preload hint set, not a claim of whole-program reachability.  Only
 * decoded reachable calls enter this set. Unknown indirect targets remain
 * eligible for first-use loading. Overflow never changes guest execution. */
#define S6502_NATIVE_PRELOAD_CAPACITY 256u
static uint32_t native_function_preload_targets[S6502_NATIVE_PRELOAD_CAPACITY];
static uint16_t native_function_preload_count;
static uint8_t native_function_preload_overflow;

static void native_function_preload_reset(void)
{
    native_function_preload_count = 0u;
    native_function_preload_overflow = 0u;
}

static int native_function_preload_candidate(uint32_t physical_pc)
{
    uint16_t i;
    for (i = 0u; i < native_function_preload_count; ++i)
        if (native_function_preload_targets[i] == physical_pc)
            return 1;
    return 0;
}

static void native_function_preload_note(uint32_t physical_pc)
{
    if (physical_pc < 0xe00000u || physical_pc >= 0x1000000u ||
        native_function_preload_candidate(physical_pc))
        return;
    if (native_function_preload_count == S6502_NATIVE_PRELOAD_CAPACITY) {
        native_function_preload_overflow = 1u;
        return;
    }
    native_function_preload_targets[native_function_preload_count++] = physical_pc;
}

static void native_function_preload_prepare(void)
{
    /* Collected during load_game's existing CFG walk, before its scratch
     * bitmap is reused by IRAM. Opening NAT must not rescan or clear it.
     * Public API heads remain guest code; preload their computational NAT
     * dependencies by physical identity, not every function in the package.
     * NAT's ROM fingerprints still decide whether these hints are valid. */
    uint16_t cursor;
    for (cursor = 0u; cursor < native_function_preload_count; ++cursor) {
        uint32_t pc = native_function_preload_targets[cursor];
        switch (pc) {
        case 0xeb582du: /* SysPicture */
        case 0xeb56fdu: /* SysPictureFill */
        case 0xeb8b18u: /* SysPictureDummy */
            native_function_preload_note(0xeb582du);
            native_function_preload_note(0xeb590fu);
            native_function_preload_note(0xeb5988u);
            native_function_preload_note(0xeb8c5du);
            native_function_preload_note(0xeb5b1au);
            native_function_preload_note(0xeb5ba4u);
            native_function_preload_note(0xeb776bu);
            break;
        case 0xeb8000u: /* SysPartPicture */
            native_function_preload_note(0xeb8351u);
            native_function_preload_note(0xeb8801u);
            native_function_preload_note(0xeb776bu);
            break;
        case 0xeb53d7u: /* SysAscii */
            native_function_preload_note(0xeb550fu);
            break;
        case 0xeb4c57u: /* SysChinese */
            native_function_preload_note(0xeb508au);
            break;
        case 0xeb6029u: /* LcdPartClear */
        case 0xeb638du: /* LcdReverse */
        case 0xeb7360u: /* Line */
        case 0xeb7358u: /* LineClear */
        case 0xeb7662u: /* Rect */
        case 0xeb765au: /* RectClear */
        case 0xeb7700u: /* FillRect */
            native_function_preload_note(0xeb7039u);
            native_function_preload_note(0xeb776bu);
            native_function_preload_note(0xeb759eu);
            break;
        case 0xeb7581u: /* PutPixel */
            native_function_preload_note(0xeb759eu);
            native_function_preload_note(0xeb776bu);
            break;
        default: break;
        }
    }
}
#endif
