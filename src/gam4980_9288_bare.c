#include "Dsys.h"
#include "gam4980_9288_bare.h"
#include "gam4980_9288_iram.h"

#define IO_BASE 0x00040000u
#define CTM_RUN_CTRL_ADDRESS (IO_BASE + 0x0151u)
#define CTM_DIVIDER_ADDRESS (IO_BASE + 0x0153u)
#define CTM_SECOND_ADDRESS (IO_BASE + 0x0154u)
#define CTM_MINUTE_ADDRESS (IO_BASE + 0x0155u)
#define CTM_HOUR_ADDRESS (IO_BASE + 0x0156u)
#define CTM_DAY_LOW_ADDRESS (IO_BASE + 0x0157u)
#define CTM_DAY_HIGH_ADDRESS (IO_BASE + 0x0158u)

#define IRQ_ENABLE_BASE (IO_BASE + 0x0270u)
#define IRQ_FACTOR_BASE (IO_BASE + 0x0280u)
#define IRQ_GROUP_COUNT 10u
#define IRQ_EXTRA_ENABLE_ADDRESS (IO_BASE + 0x02a6u)
#define IRQ_EXTRA_FACTOR_ADDRESS (IO_BASE + 0x02a9u)

#define K5_FUNCTION_ADDRESS (IO_BASE + 0x02c0u)
#define K5_DATA_ADDRESS (IO_BASE + 0x02c1u)
#define PORT0_DATA_ADDRESS (IO_BASE + 0x02d1u)
#define PORT0_IOCTRL_ADDRESS (IO_BASE + 0x02d2u)
#define KEY_ROW_SELECT_ADDRESS 0x00300f46u

#define HSDMA_BASE (IO_BASE + 0x8220u)
#define HSDMA_CHANNEL_STRIDE 0x10u
#define HSDMA_ENABLE_OFFSET 0x0cu
#define HSDMA_CHANNELS 4u
#define AMR_RESIDENT_FLAG_ADDRESS 0x00003fcau
#define TTBR_REGISTER_ADDRESS (IO_BASE + 0x8134u)
#define PSR_IE_MASK 0x00000010u
#define VIDEO_BASE 0x003c0000u
#define QUIESCE_TIMEOUT_TICKS 512u

typedef struct T_BareIrqState {
    u8 enable[IRQ_GROUP_COUNT];
    u8 factor[IRQ_GROUP_COUNT];
    u8 extra_enable;
    u8 extra_factor;
} T_BareIrqState;

typedef struct T_BareKeyState {
    u8 row_select;
    u8 k5_function;
    u8 port0_ioctrl;
} T_BareKeyState;

typedef struct T_BareClockSample {
    u16 day;
    u8 hour;
    u8 minute;
    u8 second;
    u8 divider;
} T_BareClockSample;

static T_BareIrqState g_irq_state;
static T_BareKeyState g_key_state;
static u32 g_saved_psr;
static u32 g_saved_ttbr;
static int g_status;
static int g_active;
static int g_sdk_suspended;
static u32 g_entries;
static u32 g_restores;
static u32 g_rom_suspends;
static u32 g_new_irq_factors;
static u32 g_direct_sdk_calls;
static u32 g_direct_sdk_failures;

static volatile u8 *byte_register(u32 address)
{
    return (volatile u8 *)(unsigned long)address;
}

static u32 read_psr(void)
{
    u32 value;

    __asm__ volatile("ld.w %0,%%psr" : "=r"(value));
    return value;
}

static void write_psr(u32 value)
{
    __asm__ volatile("ld.w %%psr,%0" : : "r"(value) : "memory");
}

static void read_clock_sample(T_BareClockSample *sample)
{
    u8 second_before, second_after;
    u8 minute_before, minute_after;
    u8 hour_before, hour_after;
    u8 day_low_before, day_low_after;
    u8 day_high_before, day_high_after;

    do {
        day_high_before = *byte_register(CTM_DAY_HIGH_ADDRESS);
        day_low_before = *byte_register(CTM_DAY_LOW_ADDRESS);
        hour_before = *byte_register(CTM_HOUR_ADDRESS);
        minute_before = *byte_register(CTM_MINUTE_ADDRESS);
        second_before = *byte_register(CTM_SECOND_ADDRESS);
        sample->divider = *byte_register(CTM_DIVIDER_ADDRESS);
        second_after = *byte_register(CTM_SECOND_ADDRESS);
        minute_after = *byte_register(CTM_MINUTE_ADDRESS);
        hour_after = *byte_register(CTM_HOUR_ADDRESS);
        day_low_after = *byte_register(CTM_DAY_LOW_ADDRESS);
        day_high_after = *byte_register(CTM_DAY_HIGH_ADDRESS);
    } while (second_before != second_after ||
             minute_before != minute_after || hour_before != hour_after ||
             day_low_before != day_low_after ||
             day_high_before != day_high_after);

    sample->day = (u16)(((u16)day_high_before << 8) | day_low_before);
    sample->hour = hour_before;
    sample->minute = minute_before;
    sample->second = second_before;
}

static u8 hsdma_active_mask(void)
{
    u8 mask = 0u;
    u32 channel;

    for (channel = 0u; channel < HSDMA_CHANNELS; ++channel) {
        u32 address = HSDMA_BASE + channel * HSDMA_CHANNEL_STRIDE +
            HSDMA_ENABLE_OFFSET;

        if (*byte_register(address) & 1u)
            mask |= (u8)(1u << channel);
    }
    return mask;
}

static int system_is_quiescent(void)
{
    return *byte_register(AMR_RESIDENT_FLAG_ADDRESS) == 0u &&
        hsdma_active_mask() == 0u;
}

static void save_irq_state(void)
{
    u32 index;

    for (index = 0u; index < IRQ_GROUP_COUNT; ++index) {
        g_irq_state.enable[index] =
            *byte_register(IRQ_ENABLE_BASE + index);
        g_irq_state.factor[index] =
            *byte_register(IRQ_FACTOR_BASE + index);
    }
    g_irq_state.extra_enable = *byte_register(IRQ_EXTRA_ENABLE_ADDRESS);
    g_irq_state.extra_factor = *byte_register(IRQ_EXTRA_FACTOR_ADDRESS);
}

static void mask_irq_sources(void)
{
    u32 index;

    for (index = 0u; index < IRQ_GROUP_COUNT; ++index)
        *byte_register(IRQ_ENABLE_BASE + index) = 0u;
    *byte_register(IRQ_EXTRA_ENABLE_ADDRESS) = 0u;
}

static int irq_sources_masked(void)
{
    u32 index;

    for (index = 0u; index < IRQ_GROUP_COUNT; ++index) {
        if (*byte_register(IRQ_ENABLE_BASE + index) != 0u)
            return 0;
    }
    return *byte_register(IRQ_EXTRA_ENABLE_ADDRESS) == 0u;
}

static int restore_irq_state(void)
{
    u8 seen[IRQ_GROUP_COUNT] = {0};
    u8 extra_seen = 0u;
    u32 attempt;
    u32 index;
    int restored = 1;

    /* Factor registers are W1C and periodic hardware can set them again
     * between our clear and readback.  Clear factors created during the bare
     * interval on a best-effort basis, but never make exact factor equality a
     * fatal restore condition.  Only the writable enable state is stable
     * enough to verify. */
    for (attempt = 0u; attempt < 8u; ++attempt) {
        int new_factor_seen = 0;

        for (index = 0u; index < IRQ_GROUP_COUNT; ++index) {
            volatile u8 *factor = byte_register(IRQ_FACTOR_BASE + index);
            u8 new_bits = (u8)(*factor & (u8)~g_irq_state.factor[index]);

            seen[index] |= new_bits;
            if (new_bits) {
                new_factor_seen = 1;
                *factor = new_bits;
            }
        }
        {
            volatile u8 *factor = byte_register(IRQ_EXTRA_FACTOR_ADDRESS);
            u8 new_bits = (u8)(*factor &
                               (u8)~g_irq_state.extra_factor);

            extra_seen |= new_bits;
            if (new_bits) {
                new_factor_seen = 1;
                *factor = new_bits;
            }
        }
        if (!new_factor_seen)
            break;
    }
    for (index = 0u; index < IRQ_GROUP_COUNT; ++index) {
        u8 bits = seen[index];

        while (bits) {
            g_new_irq_factors += bits & 1u;
            bits >>= 1;
        }
        *byte_register(IRQ_ENABLE_BASE + index) =
            g_irq_state.enable[index];
    }
    while (extra_seen) {
        g_new_irq_factors += extra_seen & 1u;
        extra_seen >>= 1;
    }
    *byte_register(IRQ_EXTRA_ENABLE_ADDRESS) = g_irq_state.extra_enable;
    for (index = 0u; index < IRQ_GROUP_COUNT; ++index) {
        if (*byte_register(IRQ_ENABLE_BASE + index) !=
            g_irq_state.enable[index])
            restored = 0;
    }
    if (*byte_register(IRQ_EXTRA_ENABLE_ADDRESS) !=
        g_irq_state.extra_enable)
        restored = 0;
    return restored;
}

static void save_key_state(void)
{
    g_key_state.row_select = *byte_register(KEY_ROW_SELECT_ADDRESS);
    g_key_state.k5_function = *byte_register(K5_FUNCTION_ADDRESS);
    g_key_state.port0_ioctrl = *byte_register(PORT0_IOCTRL_ADDRESS);
}

static void prepare_key_scan(void)
{
    *byte_register(K5_FUNCTION_ADDRESS) &= (u8)~0x0fu;
    *byte_register(PORT0_IOCTRL_ADDRESS) &= (u8)~0x70u;
    *byte_register(KEY_ROW_SELECT_ADDRESS) = 0xffu;
}

static int restore_key_state(void)
{
    *byte_register(KEY_ROW_SELECT_ADDRESS) = g_key_state.row_select;
    *byte_register(K5_FUNCTION_ADDRESS) = g_key_state.k5_function;
    *byte_register(PORT0_IOCTRL_ADDRESS) = g_key_state.port0_ioctrl;
    return *byte_register(KEY_ROW_SELECT_ADDRESS) ==
            g_key_state.row_select &&
        *byte_register(K5_FUNCTION_ADDRESS) == g_key_state.k5_function &&
        *byte_register(PORT0_IOCTRL_ADDRESS) == g_key_state.port0_ioctrl;
}

int gam4980_9288_bare_prepare(void)
{
#ifdef GAM4980_ENABLE_BARE_SESSION
    u8 divider;
    u16 start;
    u32 quiet_start;

    if ((*byte_register(CTM_RUN_CTRL_ADDRESS) & 1u) == 0u) {
        g_status = GAM4980_BARE_STATUS_CLOCK_STOPPED;
        return 0;
    }
    divider = *byte_register(CTM_DIVIDER_ADDRESS);
    start = (u16)fnGUI_GetTickCount();
    while (*byte_register(CTM_DIVIDER_ADDRESS) == divider) {
        if ((u16)((u16)fnGUI_GetTickCount() - start) >= 400u) {
            g_status = GAM4980_BARE_STATUS_CLOCK_STOPPED;
            return 0;
        }
    }
    quiet_start = gam4980_9288_bare_clock();
    while (!system_is_quiescent() &&
           (u32)(gam4980_9288_bare_clock() - quiet_start) <
               QUIESCE_TIMEOUT_TICKS) {
        /* System IRQs are still enabled here.  A key tone or filesystem DMA
         * can finish naturally before the session takes ownership. */
    }
    if (!system_is_quiescent()) {
        g_status = GAM4980_BARE_STATUS_BACKGROUND_BUSY;
        return 0;
    }
    g_status = GAM4980_BARE_STATUS_READY;
    return 1;
#else
    g_status = GAM4980_BARE_STATUS_DISABLED;
    return 0;
#endif
}

int gam4980_9288_bare_enter(void)
{
#ifdef GAM4980_ENABLE_BARE_SESSION
    if (g_active)
        return 1;
    if (!system_is_quiescent()) {
        g_status = GAM4980_BARE_STATUS_BACKGROUND_BUSY;
        return 0;
    }
    g_saved_psr = read_psr();
    g_saved_ttbr = *(volatile u32 *)(unsigned long)TTBR_REGISTER_ADDRESS;
    /* Match the true-device probe: freeze firmware ISRs before snapshotting
     * IRQ factors/enables and keyboard configuration.  Taking those snapshots
     * with IE still set allowed an ISR to change them between read and mask. */
    write_psr(g_saved_psr & ~PSR_IE_MASK);
    if (!system_is_quiescent()) {
        write_psr(g_saved_psr);
        g_status = GAM4980_BARE_STATUS_BACKGROUND_BUSY;
        return 0;
    }
    save_irq_state();
    save_key_state();
    mask_irq_sources();
    prepare_key_scan();
    /* The first quiescence check runs with system IRQs enabled.  A pending
     * audio/key task can therefore claim AMR in the few instructions before
     * PSR is masked.  Recheck after suppression so IRAM ownership cannot be
     * lost to that race. */
    if (!system_is_quiescent()) {
        (void)restore_key_state();
        (void)restore_irq_state();
        write_psr(g_saved_psr);
        g_status = GAM4980_BARE_STATUS_BACKGROUND_BUSY;
        return 0;
    }
    if (!gam4980_9288_iram_session_install()) {
        (void)restore_key_state();
        (void)restore_irq_state();
        write_psr(g_saved_psr);
        g_status = GAM4980_BARE_STATUS_IRAM_FAILED;
        return 0;
    }
    g_active = 1;
    g_sdk_suspended = 0;
    ++g_entries;
    g_status = GAM4980_BARE_STATUS_ACTIVE;
    return 1;
#else
    return 0;
#endif
}

int gam4980_9288_bare_leave(void)
{
#ifdef GAM4980_ENABLE_BARE_SESSION
    int restored = 1;

    if (!g_active)
        return g_status >= 0;
    gam4980_9288_iram_session_restore();
    if (gam4980_9288_iram_status() != GAM4980_IRAM_STATUS_RESTORED)
        restored = 0;
    if (!restore_key_state())
        restored = 0;
    if (!restore_irq_state())
        restored = 0;
    if (*(volatile u32 *)(unsigned long)TTBR_REGISTER_ADDRESS != g_saved_ttbr)
        restored = 0;
    /* Publish the inactive state before interrupts can run again. */
    g_active = 0;
    ++g_restores;
    g_status = restored
        ? GAM4980_BARE_STATUS_READY
        : GAM4980_BARE_STATUS_RESTORE_FAILED;
    write_psr(g_saved_psr);
    if ((read_psr() & PSR_IE_MASK) != (g_saved_psr & PSR_IE_MASK)) {
        restored = 0;
        g_status = GAM4980_BARE_STATUS_RESTORE_FAILED;
    }
    return restored;
#else
    return 1;
#endif
}

int gam4980_9288_bare_active(void)
{
    return g_active;
}

u32 gam4980_9288_bare_clock(void)
{
    T_BareClockSample sample;
    u32 seconds;

    read_clock_sample(&sample);
    seconds = (u32)sample.day * 86400u +
        (u32)sample.hour * 3600u + (u32)sample.minute * 60u +
        sample.second;
    return seconds * 256u + sample.divider;
}

void gam4980_9288_bare_scan_keys(T_GAM4980_9288_BareKeys *keys)
{
    u32 row;

    if (!keys)
        return;
    for (row = 0u; row < 8u; ++row) {
        u8 k5;
        u8 p0;

        *byte_register(KEY_ROW_SELECT_ADDRESS) = (u8)~(1u << row);
        k5 = *byte_register(K5_DATA_ADDRESS);
        p0 = *byte_register(PORT0_DATA_ADDRESS);
        keys->row[row] = (u8)(((u8)~k5 & 0x0fu) |
                              ((u8)~p0 & 0x70u));
    }
    *byte_register(KEY_ROW_SELECT_ADDRESS) = 0xffu;
}

void gam4980_9288_bare_submit(const u8 *frame, u32 size)
{
    volatile u32 *video = (volatile u32 *)(unsigned long)VIDEO_BASE;
    const u32 *source = (const u32 *)(const void *)frame;
    u32 words;
    u32 index;

    if (!frame || (size & 3u) != 0u)
        return;
    words = size >> 2;
    for (index = 0u; index < words; ++index)
        video[index] = source[index];
}

int gam4980_9288_bare_wait_video_idle(u32 max_polls)
{
    while (max_polls-- != 0u) {
        if (hsdma_active_mask() == 0u)
            return 1;
    }
    return hsdma_active_mask() == 0u;
}

int gam4980_9288_bare_direct_sdk_begin(void)
{
#ifdef GAM4980_ENABLE_BARE_SESSION
    int valid = g_active &&
        g_status == GAM4980_BARE_STATUS_ACTIVE &&
        (read_psr() & PSR_IE_MASK) == 0u &&
        *(volatile u32 *)(unsigned long)TTBR_REGISTER_ADDRESS ==
            g_saved_ttbr &&
        irq_sources_masked() && system_is_quiescent() &&
        gam4980_9288_iram_status() == GAM4980_IRAM_STATUS_ACTIVE;

    if (!valid) {
        ++g_direct_sdk_failures;
        g_status = GAM4980_BARE_STATUS_DIRECT_SDK_FAILED;
    }
    return valid;
#else
    return 0;
#endif
}

int gam4980_9288_bare_direct_sdk_end(void)
{
#ifdef GAM4980_ENABLE_BARE_SESSION
    u32 psr_after;
    int valid;

    if (!g_active) {
        ++g_direct_sdk_failures;
        g_status = GAM4980_BARE_STATUS_DIRECT_SDK_FAILED;
        return 0;
    }
    ++g_direct_sdk_calls;
    psr_after = read_psr();
    valid = (psr_after & PSR_IE_MASK) == 0u &&
        *(volatile u32 *)(unsigned long)TTBR_REGISTER_ADDRESS ==
            g_saved_ttbr &&
        irq_sources_masked() && system_is_quiescent();

    /* Reassert the bare-session ownership before inspecting resident code.
     * The validated FS implementation leaves these controls untouched; the
     * writes make a non-fatal flag change deterministic while the recorded
     * validity still exposes any unexpected SDK behaviour. */
    write_psr(psr_after & ~PSR_IE_MASK);
    mask_irq_sources();
    prepare_key_scan();
    if (!gam4980_9288_iram_session_validate())
        valid = 0;
    if (!system_is_quiescent())
        valid = 0;
    if (!valid) {
        ++g_direct_sdk_failures;
        g_status = GAM4980_BARE_STATUS_DIRECT_SDK_FAILED;
        return 0;
    }
    g_status = GAM4980_BARE_STATUS_ACTIVE;
    return 1;
#else
    return 0;
#endif
}

int gam4980_9288_bare_suspend_for_sdk(void)
{
    if (!g_active || gam4980_9288_iram_range_depth() != 0u) {
        if (g_active)
            gam4980_9288_bare_note_nested_rom_read();
        return 0;
    }
    if (!gam4980_9288_bare_leave())
        return 0;
    g_sdk_suspended = 1;
    ++g_rom_suspends;
    return 1;
}

int gam4980_9288_bare_resume_after_sdk(void)
{
    if (!g_sdk_suspended)
        return 0;
    /* Filesystem service re-enables the firmware long enough for deferred
     * audio/DMA work to start.  Use the same bounded quiescence gate as the
     * first entry instead of racing straight back into IRAM ownership. */
    if (!gam4980_9288_bare_prepare())
        return 0;
    if (!gam4980_9288_bare_enter())
        return 0;
    g_sdk_suspended = 0;
    return 1;
}

void gam4980_9288_bare_note_nested_rom_read(void)
{
    g_status = GAM4980_BARE_STATUS_NESTED_ROM_READ;
}

int gam4980_9288_bare_status(void) { return g_status; }
u32 gam4980_9288_bare_entries(void) { return g_entries; }
u32 gam4980_9288_bare_restores(void) { return g_restores; }
u32 gam4980_9288_bare_rom_suspends(void) { return g_rom_suspends; }
u32 gam4980_9288_bare_new_irq_factors(void) { return g_new_irq_factors; }
u32 gam4980_9288_bare_direct_sdk_calls(void)
{
    return g_direct_sdk_calls;
}
u32 gam4980_9288_bare_direct_sdk_failures(void)
{
    return g_direct_sdk_failures;
}
