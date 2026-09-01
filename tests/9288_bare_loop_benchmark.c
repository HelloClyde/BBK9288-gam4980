#include "Dsys.h"
#include "gam4980_types.h"
#include "9288_iram_benchmark_payload.h"

#ifdef GAM4980_BARE_FS_PROBE
#define APP_TITLE "BARE FS PROBE"
#define REPORT_PATH "a:\\BAREFS.LOG"
#define FS_PROBE_PATH "a:\\BAREFS.DAT"
#define FS_PROBE_SIZE 4096u
#else
#define APP_TITLE "BARE LOOP BENCH"
#define REPORT_PATH "a:\\BARELOOP.LOG"
#endif

#define IO_BASE 0x00040000u
#define CTM_RUN_CTRL_ADDRESS (IO_BASE + 0x0151u)
#define CTM_DIVIDER_ADDRESS (IO_BASE + 0x0153u)
#define CTM_SECOND_ADDRESS (IO_BASE + 0x0154u)
#define CTM_MINUTE_ADDRESS (IO_BASE + 0x0155u)
#define CTM_HOUR_ADDRESS (IO_BASE + 0x0156u)
#define CTM_DAY_LOW_ADDRESS (IO_BASE + 0x0157u)
#define CTM_DAY_HIGH_ADDRESS (IO_BASE + 0x0158u)
#define CTM_TICKS_PER_SECOND 256u
#define FRAME_TICKS (CTM_TICKS_PER_SECOND / 32u)

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
#define EXIT_KEY_ROW 5u
#define EXIT_KEY_COLUMN 6u

#define HSDMA_BASE (IO_BASE + 0x8220u)
#define HSDMA_CHANNEL_STRIDE 0x10u
#define HSDMA_ENABLE_OFFSET 0x0cu
#define HSDMA_CHANNELS 4u

#define IRAM_TARGET_ADDRESS 0x00000800u
#define IRAM_BACKUP_SIZE 128u
#define AMR_RESIDENT_FLAG_ADDRESS 0x00003fcau
#define TTBR_REGISTER_ADDRESS (IO_BASE + 0x8134u)
#define PSR_IE_MASK 0x00000010u

#define VIDEO_BASE 0x003c0000u
#define BARE_SCREEN_WIDTH 320u
#define BARE_SCREEN_HEIGHT 240u
#define BARE_SCREEN_PITCH 80u
#define BARE_SCREEN_BYTES (BARE_SCREEN_PITCH * BARE_SCREEN_HEIGHT)
#ifdef GAM4980_BARE_FS_PROBE
#define TEST_TICKS (2u * CTM_TICKS_PER_SECOND)
#else
#define TEST_TICKS (10u * CTM_TICKS_PER_SECOND)
#endif
#define TICK_STALL_ITERATION_LIMIT 20000000u
#define STORAGE __attribute__((aligned(16), section(".scratch")))

typedef u32 (*T_IramPayload)(u32 seed, u32 iterations);

typedef struct T_IrqState {
    u8 enable[IRQ_GROUP_COUNT];
    u8 factor[IRQ_GROUP_COUNT];
    u8 extra_enable;
    u8 extra_factor;
} T_IrqState;

typedef struct T_KeyState {
    u8 row_select;
    u8 k5_function;
    u8 port0_ioctrl;
} T_KeyState;

typedef struct T_KeyScan {
    u8 row[8];
    u8 any_down;
    u8 exit_down;
} T_KeyScan;

typedef struct T_ClockSample {
    u16 day;
    u8 hour;
    u8 minute;
    u8 second;
    u8 divider;
} T_ClockSample;

typedef struct T_RunResult {
    u32 start_clock_day;
    u32 end_clock_day;
    u32 start_clock_second_of_day;
    u32 end_clock_second_of_day;
    u32 elapsed_ticks;
    u32 clock_valid_samples;
    u32 clock_invalid_samples;
    u32 divider_changes;
    u32 divider_wraps;
    u32 divider_max_step;
    u32 loop_iterations;
    u32 kernel_calls;
    u32 kernel_result;
    u32 frame_updates;
    u32 key_scans;
    u32 key_transitions;
    u32 iram_hash_before;
    u32 iram_hash_active;
    u32 iram_hash_after;
    u32 psr_before;
    u32 psr_after;
    u32 ttbr_before;
    u32 ttbr_after;
    u32 gui_tick_before;
    u32 gui_tick_after;
    u32 new_irq_factors;
    u8 amr_before;
    u8 amr_after;
    u8 dma_active_before;
    u8 dma_active_after;
    u8 start_divider;
    u8 end_divider;
    u8 exit_reason;
    u8 exit_key_seen;
    u8 hardware_tick_progress;
    u8 iram_installed;
    u8 iram_stable;
    u8 iram_restored;
    u8 video_restored;
    u8 irq_restored;
    u8 keyboard_restored;
    u8 ttbr_restored;
    u8 psr_ie_restored;
#ifdef GAM4980_BARE_FS_PROBE
    u32 fs_bytes_read;
    u32 fs_expected_hash;
    u32 fs_actual_hash;
    u32 fs_elapsed_ticks;
    u32 fs_psr_before;
    u32 fs_psr_after;
    u32 fs_ttbr_before;
    u32 fs_ttbr_after;
    u32 fs_iram_hash_before;
    u32 fs_iram_hash_after;
    u8 fs_source_created;
    u8 fs_opened;
    u8 fs_closed;
    u8 fs_data_match;
    u8 fs_psr_ie_preserved;
    u8 fs_ttbr_preserved;
    u8 fs_irq_mask_preserved;
    u8 fs_iram_preserved;
    u8 fs_iram_reinstalled;
    u8 fs_amr_idle;
    u8 fs_dma_idle;
#endif
} T_RunResult;

enum {
    EXIT_REASON_TIMEOUT = 1,
    EXIT_REASON_KEY = 2,
    EXIT_REASON_TICK_STALLED = 3,
    EXIT_REASON_INSTALL_FAILED = 4,
    EXIT_REASON_BACKGROUND_ACTIVE = 5,
    EXIT_REASON_FS_API_FAILED = 6
};

static u8 g_iram_backup[IRAM_BACKUP_SIZE] STORAGE;
static u8 g_video_backup[BARE_SCREEN_BYTES] STORAGE;
static u8 g_frame[BARE_SCREEN_BYTES] STORAGE;
static char g_report[4096];
static char g_summary[768];
#ifdef GAM4980_BARE_FS_PROBE
static u8 g_fs_expected[FS_PROBE_SIZE] STORAGE;
static u8 g_fs_actual[FS_PROBE_SIZE] STORAGE;
static int clock_elapsed_ticks(
    const T_ClockSample *start, const T_ClockSample *now, u32 *elapsed
);
static void read_clock_sample(T_ClockSample *sample);
static u8 hsdma_active_mask(void);
static void mask_irq_sources(void);
static void prepare_direct_key_scan(void);
#endif

void *memset(void *destination, int value, unsigned int size)
{
    u8 *output = (u8 *)destination;

    while (size--)
        *output++ = (u8)value;
    return destination;
}

static volatile u8 *byte_register(u32 address)
{
    return (volatile u8 *)(unsigned long)address;
}

static char *append_text(char *out, const char *text)
{
    while (*text)
        *out++ = *text++;
    return out;
}

static char *append_u32(char *out, u32 value)
{
    char digits[10];
    u32 count = 0u;

    do {
        digits[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value && count < 10u);
    while (count)
        *out++ = digits[--count];
    return out;
}

static char *append_hex32(char *out, u32 value)
{
    static const char digits[] = "0123456789ABCDEF";
    int shift = 28;

    while (shift >= 0) {
        *out++ = digits[(value >> (u32)shift) & 0x0fu];
        shift -= 4;
    }
    return out;
}

static u32 text_length(const char *text)
{
    u32 length = 0u;

    while (text[length])
        ++length;
    return length;
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

static u32 read_sp(void)
{
    u32 value;

    __asm__ volatile("ld.w %0,%%sp" : "=r"(value));
    return value;
}

static u32 read_ttbr(void)
{
    return *(volatile u32 *)(unsigned long)TTBR_REGISTER_ADDRESS;
}

static void copy_from_volatile(
    u8 *destination, const volatile u8 *source, u32 size
)
{
    u32 index;

    for (index = 0u; index < size; ++index)
        destination[index] = source[index];
}

static void copy_to_volatile(
    volatile u8 *destination, const u8 *source, u32 size
)
{
    u32 index;

    for (index = 0u; index < size; ++index)
        destination[index] = source[index];
    __asm__ volatile("" : : : "memory");
}

static int volatile_equals(
    const volatile u8 *left, const u8 *right, u32 size
)
{
    u32 index;

    for (index = 0u; index < size; ++index) {
        if (left[index] != right[index])
            return 0;
    }
    return 1;
}

static u32 hash_volatile(const volatile u8 *bytes, u32 size)
{
    u32 hash = 2166136261u;
    u32 index;

    for (index = 0u; index < size; ++index) {
        hash ^= bytes[index];
        hash *= 16777619u;
    }
    return hash;
}

#ifdef GAM4980_BARE_FS_PROBE
static u32 hash_bytes(const u8 *bytes, u32 size)
{
    u32 hash = 2166136261u;
    u32 index;

    for (index = 0u; index < size; ++index) {
        hash ^= bytes[index];
        hash *= 16777619u;
    }
    return hash;
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

static int prepare_fs_probe_source(void)
{
    FS_FILE *file;
    u32 index;
    size_t written;

    for (index = 0u; index < FS_PROBE_SIZE; ++index)
        g_fs_expected[index] = (u8)(index * 37u + (index >> 3) + 0x5au);
    (void)fs_remove(FS_PROBE_PATH);
    file = fs_fopen(FS_PROBE_PATH, FS_O_WRONLY);
    if (!file)
        return 0;
    written = fs_fwrite(g_fs_expected, 1, FS_PROBE_SIZE, file);
    (void)fs_update(file);
    fs_fclose(file);
    return written == FS_PROBE_SIZE;
}

static void run_direct_fs_probe(
    T_RunResult *result, volatile u8 *iram
)
{
    volatile u8 *amr = byte_register(AMR_RESIDENT_FLAG_ADDRESS);
    T_ClockSample start;
    T_ClockSample end;
    FS_FILE *file;
    u32 elapsed = 0u;

    result->fs_expected_hash = hash_bytes(g_fs_expected, FS_PROBE_SIZE);
    result->fs_psr_before = read_psr();
    result->fs_ttbr_before = read_ttbr();
    result->fs_iram_hash_before = hash_volatile(
        iram, IRAM_BENCHMARK_PAYLOAD_SIZE
    );
    read_clock_sample(&start);
    file = fs_fopen(FS_PROBE_PATH, FS_O_RDONLY);
    result->fs_opened = file != 0;
    if (file) {
        result->fs_bytes_read = (u32)fs_fread(
            g_fs_actual, 1, FS_PROBE_SIZE, file
        );
        fs_fclose(file);
        result->fs_closed = 1u;
    }
    read_clock_sample(&end);
    if (clock_elapsed_ticks(&start, &end, &elapsed))
        result->fs_elapsed_ticks = elapsed;
    result->fs_actual_hash = hash_bytes(
        g_fs_actual, result->fs_bytes_read
    );
    result->fs_data_match =
        result->fs_bytes_read == FS_PROBE_SIZE &&
        result->fs_actual_hash == result->fs_expected_hash;
    result->fs_psr_after = read_psr();
    result->fs_ttbr_after = read_ttbr();
    result->fs_psr_ie_preserved =
        (result->fs_psr_after & PSR_IE_MASK) ==
        (result->fs_psr_before & PSR_IE_MASK);
    result->fs_ttbr_preserved =
        result->fs_ttbr_after == result->fs_ttbr_before;
    result->fs_irq_mask_preserved = irq_sources_masked();
    result->fs_iram_hash_after = hash_volatile(
        iram, IRAM_BENCHMARK_PAYLOAD_SIZE
    );
    result->fs_iram_preserved =
        result->fs_iram_hash_after == result->fs_iram_hash_before &&
        volatile_equals(
            iram, g_iram_benchmark_payload,
            IRAM_BENCHMARK_PAYLOAD_SIZE
        );
    if (!result->fs_iram_preserved) {
        copy_to_volatile(
            iram, g_iram_benchmark_payload,
            IRAM_BENCHMARK_PAYLOAD_SIZE
        );
    }
    result->fs_iram_reinstalled = volatile_equals(
        iram, g_iram_benchmark_payload, IRAM_BENCHMARK_PAYLOAD_SIZE
    );
    /* Continue the probe with the same suppression invariants even if the
     * SDK touched them; the report retains the uncorrected observations. */
    write_psr(result->fs_psr_before & ~PSR_IE_MASK);
    mask_irq_sources();
    prepare_direct_key_scan();
    result->fs_amr_idle = *amr == 0u;
    result->fs_dma_idle = hsdma_active_mask() == 0u;
}
#endif

static void read_clock_sample(T_ClockSample *sample)
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

static u32 clock_second_of_day(const T_ClockSample *sample)
{
    return (u32)sample->hour * 3600u +
        (u32)sample->minute * 60u + sample->second;
}

static int clock_elapsed_ticks(
    const T_ClockSample *start, const T_ClockSample *now, u32 *elapsed
)
{
    u32 start_second;
    u32 now_second;
    u32 day_delta;
    u32 second_delta;
    s32 tick_delta;

    if (start->hour >= 24u || now->hour >= 24u ||
        start->minute >= 60u || now->minute >= 60u ||
        start->second >= 60u || now->second >= 60u)
        return 0;
    start_second = clock_second_of_day(start);
    now_second = clock_second_of_day(now);
    day_delta = (u16)(now->day - start->day);
    if (day_delta == 0u) {
        if (now_second < start_second)
            return 0;
        second_delta = now_second - start_second;
    } else if (day_delta == 1u) {
        second_delta = 86400u - start_second + now_second;
    } else {
        return 0;
    }
    if (second_delta > 60u)
        return 0;
    tick_delta = (s32)(second_delta * CTM_TICKS_PER_SECOND) +
        (s32)now->divider - (s32)start->divider;
    if (tick_delta < 0 || tick_delta > (s32)(60u * CTM_TICKS_PER_SECOND))
        return 0;
    *elapsed = (u32)tick_delta;
    return 1;
}

static int clock_timer_preflight(void)
{
    u8 divider = *byte_register(CTM_DIVIDER_ADDRESS);
    u16 start = (u16)fnGUI_GetTickCount();

    if ((*byte_register(CTM_RUN_CTRL_ADDRESS) & 1u) == 0u)
        return 0;
    while (*byte_register(CTM_DIVIDER_ADDRESS) == divider) {
        u16 now = (u16)fnGUI_GetTickCount();

        if ((u16)(now - start) >= 400u)
            return 0;
    }
    return 1;
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

static int wait_for_quiescent_system(void)
{
    volatile u8 *amr = byte_register(AMR_RESIDENT_FLAG_ADDRESS);
    T_ClockSample start;
    T_ClockSample now;
    u32 elapsed = 0u;

    read_clock_sample(&start);

    do {
        if (*amr == 0u && hsdma_active_mask() == 0u)
            return 1;
        read_clock_sample(&now);
    } while (!clock_elapsed_ticks(&start, &now, &elapsed) ||
             elapsed < 2u * CTM_TICKS_PER_SECOND);
    return 0;
}

static void save_irq_state(T_IrqState *state)
{
    u32 index;

    for (index = 0u; index < IRQ_GROUP_COUNT; ++index) {
        state->enable[index] = *byte_register(IRQ_ENABLE_BASE + index);
        state->factor[index] = *byte_register(IRQ_FACTOR_BASE + index);
    }
    state->extra_enable = *byte_register(IRQ_EXTRA_ENABLE_ADDRESS);
    state->extra_factor = *byte_register(IRQ_EXTRA_FACTOR_ADDRESS);
}

static void mask_irq_sources(void)
{
    u32 index;

    for (index = 0u; index < IRQ_GROUP_COUNT; ++index)
        *byte_register(IRQ_ENABLE_BASE + index) = 0u;
    *byte_register(IRQ_EXTRA_ENABLE_ADDRESS) = 0u;
}

static int restore_irq_state(
    const T_IrqState *state, u32 *new_factor_count
)
{
    u8 seen[IRQ_GROUP_COUNT] = {0};
    u8 extra_seen = 0u;
    u32 attempt;
    u32 index;
    u32 count = 0u;
    int restored = 0;

    /* Factors are W1C and hardware continues setting them even while CPU
     * interrupts and EIRs are masked.  Clear only factors created during the
     * experiment, retrying until a readback lands between hardware edges.
     * Bits that were already pending on entry are never written, so they are
     * still delivered after PSR.IE is restored. */
    for (attempt = 0u; attempt < 8u; ++attempt) {
        for (index = 0u; index < IRQ_GROUP_COUNT; ++index) {
            volatile u8 *factor = byte_register(IRQ_FACTOR_BASE + index);
            u8 new_bits = (u8)(*factor & (u8)~state->factor[index]);

            seen[index] |= new_bits;
            if (new_bits)
                *factor = new_bits;
        }
        {
            volatile u8 *factor = byte_register(IRQ_EXTRA_FACTOR_ADDRESS);
            u8 new_bits = (u8)(*factor & (u8)~state->extra_factor);

            extra_seen |= new_bits;
            if (new_bits)
                *factor = new_bits;
        }
        restored = 1;
        for (index = 0u; index < IRQ_GROUP_COUNT; ++index) {
            if (*byte_register(IRQ_FACTOR_BASE + index) !=
                state->factor[index])
                restored = 0;
        }
        if (*byte_register(IRQ_EXTRA_FACTOR_ADDRESS) != state->extra_factor)
            restored = 0;
        if (restored)
            break;
    }
    for (index = 0u; index < IRQ_GROUP_COUNT; ++index) {
        u8 bits = seen[index];

        while (bits) {
            count += bits & 1u;
            bits >>= 1;
        }
        *byte_register(IRQ_ENABLE_BASE + index) = state->enable[index];
    }
    while (extra_seen) {
        count += extra_seen & 1u;
        extra_seen >>= 1;
    }
    *byte_register(IRQ_EXTRA_ENABLE_ADDRESS) = state->extra_enable;
    for (index = 0u; index < IRQ_GROUP_COUNT; ++index) {
        if (*byte_register(IRQ_ENABLE_BASE + index) != state->enable[index])
            restored = 0;
    }
    if (*byte_register(IRQ_EXTRA_ENABLE_ADDRESS) != state->extra_enable)
        restored = 0;
    *new_factor_count = count;
    return restored;
}

static void save_key_state(T_KeyState *state)
{
    state->row_select = *byte_register(KEY_ROW_SELECT_ADDRESS);
    state->k5_function = *byte_register(K5_FUNCTION_ADDRESS);
    state->port0_ioctrl = *byte_register(PORT0_IOCTRL_ADDRESS);
}

static void prepare_direct_key_scan(void)
{
    *byte_register(K5_FUNCTION_ADDRESS) &= (u8)~0x0fu;
    *byte_register(PORT0_IOCTRL_ADDRESS) &= (u8)~0x70u;
    *byte_register(KEY_ROW_SELECT_ADDRESS) = 0xffu;
}

static int restore_key_state(const T_KeyState *state)
{
    *byte_register(KEY_ROW_SELECT_ADDRESS) = state->row_select;
    *byte_register(K5_FUNCTION_ADDRESS) = state->k5_function;
    *byte_register(PORT0_IOCTRL_ADDRESS) = state->port0_ioctrl;
    return *byte_register(KEY_ROW_SELECT_ADDRESS) == state->row_select &&
        *byte_register(K5_FUNCTION_ADDRESS) == state->k5_function &&
        *byte_register(PORT0_IOCTRL_ADDRESS) == state->port0_ioctrl;
}

static void scan_keyboard(T_KeyScan *scan)
{
    u32 row;

    scan->any_down = 0u;
    scan->exit_down = 0u;
    for (row = 0u; row < 8u; ++row) {
        u8 k5;
        u8 p0;
        u8 pressed;

        *byte_register(KEY_ROW_SELECT_ADDRESS) = (u8)~(1u << row);
        k5 = *byte_register(K5_DATA_ADDRESS);
        p0 = *byte_register(PORT0_DATA_ADDRESS);
        pressed = (u8)(((u8)~k5 & 0x0fu) | ((u8)~p0 & 0x70u));
        scan->row[row] = pressed;
        scan->any_down |= pressed;
    }
    *byte_register(KEY_ROW_SELECT_ADDRESS) = 0xffu;
    scan->exit_down =
        (scan->row[EXIT_KEY_ROW] & (1u << EXIT_KEY_COLUMN)) != 0u;
}

static void clear_frame(u8 *frame)
{
    u32 index;

    for (index = 0u; index < BARE_SCREEN_BYTES; ++index)
        frame[index] = 0xffu;
}

static void black_pixel(u8 *frame, u32 x, u32 y)
{
    u8 *pixel;
    u32 shift;

    if (x >= BARE_SCREEN_WIDTH || y >= BARE_SCREEN_HEIGHT)
        return;
    pixel = &frame[y * BARE_SCREEN_PITCH + (x >> 2)];
    shift = 6u - ((x & 3u) << 1);
    *pixel = (u8)(*pixel & (u8)~(3u << shift));
}

static void black_rect(
    u8 *frame, u32 left, u32 top, u32 right, u32 bottom
)
{
    u32 y;

    for (y = top; y < bottom; ++y) {
        u32 x;

        for (x = left; x < right; ++x)
            black_pixel(frame, x, y);
    }
}

static const u8 *glyph(char character)
{
    static const u8 blank[7] = {0,0,0,0,0,0,0};
    static const u8 a[7] = {0x0e,0x11,0x11,0x1f,0x11,0x11,0x11};
    static const u8 b[7] = {0x1e,0x11,0x11,0x1e,0x11,0x11,0x1e};
    static const u8 e[7] = {0x1f,0x10,0x10,0x1e,0x10,0x10,0x1f};
    static const u8 l[7] = {0x10,0x10,0x10,0x10,0x10,0x10,0x1f};
    static const u8 o[7] = {0x0e,0x11,0x11,0x11,0x11,0x11,0x0e};
    static const u8 p[7] = {0x1e,0x11,0x11,0x1e,0x10,0x10,0x10};
    static const u8 r[7] = {0x1e,0x11,0x11,0x1e,0x14,0x12,0x11};
    static const u8 u[7] = {0x11,0x11,0x11,0x11,0x11,0x11,0x0e};
    static const u8 n[7] = {0x11,0x19,0x15,0x13,0x11,0x11,0x11};

    switch (character) {
    case 'A': return a;
    case 'B': return b;
    case 'E': return e;
    case 'L': return l;
    case 'O': return o;
    case 'P': return p;
    case 'R': return r;
    case 'U': return u;
    case 'N': return n;
    default: return blank;
    }
}

static void draw_text(u8 *frame, u32 x, u32 y, const char *text)
{
    while (*text) {
        const u8 *rows = glyph(*text++);
        u32 row;

        for (row = 0u; row < 7u; ++row) {
            u32 column;

            for (column = 0u; column < 5u; ++column) {
                if (rows[row] & (1u << (4u - column))) {
                    u32 px = x + column * 3u;
                    u32 py = y + row * 3u;

                    black_rect(frame, px, py, px + 2u, py + 2u);
                }
            }
        }
        x += 18u;
    }
}

static void make_frame(u32 elapsed_ticks, const T_KeyScan *scan)
{
    u32 position = 12u + (elapsed_ticks * 7u) % 272u;
    u32 bit;

    clear_frame(g_frame);
    black_rect(g_frame, 0u, 0u, BARE_SCREEN_WIDTH, 4u);
    black_rect(g_frame, 0u, BARE_SCREEN_HEIGHT - 4u,
               BARE_SCREEN_WIDTH, BARE_SCREEN_HEIGHT);
    black_rect(g_frame, 0u, 0u, 4u, BARE_SCREEN_HEIGHT);
    black_rect(g_frame, BARE_SCREEN_WIDTH - 4u, 0u,
               BARE_SCREEN_WIDTH, BARE_SCREEN_HEIGHT);
    draw_text(g_frame, 76u, 24u, "BARE LOOP");
    draw_text(g_frame, 108u, 58u, "IRAM RUN");
    black_rect(g_frame, position, 96u, position + 24u, 144u);
    for (bit = 0u; bit < 10u; ++bit) {
        if (elapsed_ticks & (1u << bit))
            black_rect(g_frame, 24u + bit * 27u, 172u,
                       42u + bit * 27u, 190u);
    }
    if (scan->any_down)
        black_rect(g_frame, 24u, 208u, 144u, 224u);
    if (scan->exit_down)
        black_rect(g_frame, 176u, 208u, 296u, 224u);
}

static void submit_direct_frame(const u8 *frame)
{
    volatile u32 *video = (volatile u32 *)(unsigned long)VIDEO_BASE;
    const u32 *source = (const u32 *)(const void *)frame;
    u32 word;

    for (word = 0u; word < BARE_SCREEN_BYTES / 4u; ++word)
        video[word] = source[word];
}

static const char *exit_reason_text(u8 reason)
{
    switch (reason) {
    case EXIT_REASON_TIMEOUT: return "timeout";
    case EXIT_REASON_KEY: return "exit_key";
    case EXIT_REASON_TICK_STALLED: return "tick_stalled";
    case EXIT_REASON_INSTALL_FAILED: return "install_failed";
    case EXIT_REASON_BACKGROUND_ACTIVE: return "background_active";
    case EXIT_REASON_FS_API_FAILED: return "fs_api_failed";
    default: return "unknown";
    }
}

static void save_report(const char *text)
{
    FS_FILE *file = fs_fopen(REPORT_PATH, FS_O_WRONLY);

    if (!file)
        return;
    (void)fs_fwrite(text, 1, (size_t)text_length(text), file);
    (void)fs_update(file);
    fs_fclose(file);
}

static int result_passed(const T_RunResult *result)
{
    int passed = result->hardware_tick_progress && result->iram_installed &&
        result->iram_stable && result->iram_restored &&
        result->video_restored && result->irq_restored &&
        result->keyboard_restored && result->ttbr_restored &&
        result->psr_ie_restored && result->amr_before == 0u &&
        result->amr_after == 0u && result->dma_active_before == 0u &&
        result->dma_active_after == 0u &&
        ((result->exit_reason == EXIT_REASON_TIMEOUT &&
          result->elapsed_ticks >= TEST_TICKS) ||
         (result->exit_reason == EXIT_REASON_KEY && result->exit_key_seen));

#ifdef GAM4980_BARE_FS_PROBE
    passed = passed && result->fs_source_created && result->fs_opened &&
        result->fs_closed && result->fs_data_match &&
        result->fs_psr_ie_preserved && result->fs_ttbr_preserved &&
        result->fs_irq_mask_preserved && result->fs_iram_reinstalled &&
        result->fs_amr_idle && result->fs_dma_idle;
#endif
    return passed;
}

static void build_report(const T_RunResult *result)
{
    char *out = g_report;

#ifdef GAM4980_BARE_FS_PROBE
    out = append_text(out, "[9288 BARE FS PROBE 1]\n");
#else
    out = append_text(out, "[9288 BARE LOOP BENCH 2]\n");
#endif
    out = append_text(out, "target_ticks=");
    out = append_u32(out, TEST_TICKS);
    out = append_text(out, "\nhardware_tick_hz=256");
    out = append_text(out, "\nframe_update_hz=32");
    out = append_text(out, "\nstart_clock_day=");
    out = append_u32(out, result->start_clock_day);
    out = append_text(out, "\nend_clock_day=");
    out = append_u32(out, result->end_clock_day);
    out = append_text(out, "\nstart_clock_second_of_day=");
    out = append_u32(out, result->start_clock_second_of_day);
    out = append_text(out, "\nend_clock_second_of_day=");
    out = append_u32(out, result->end_clock_second_of_day);
    out = append_text(out, "\nstart_divider=");
    out = append_u32(out, result->start_divider);
    out = append_text(out, "\nend_divider=");
    out = append_u32(out, result->end_divider);
    out = append_text(out, "\nelapsed_ticks=");
    out = append_u32(out, result->elapsed_ticks);
    out = append_text(out, "\nelapsed_ms_approx=");
    out = append_u32(out,
        result->elapsed_ticks * 1000u / CTM_TICKS_PER_SECOND);
    out = append_text(out, "\nhardware_tick_progress=");
    *out++ = result->hardware_tick_progress ? '1' : '0';
    out = append_text(out, "\nclock_valid_samples=");
    out = append_u32(out, result->clock_valid_samples);
    out = append_text(out, "\nclock_invalid_samples=");
    out = append_u32(out, result->clock_invalid_samples);
    out = append_text(out, "\ndivider_changes=");
    out = append_u32(out, result->divider_changes);
    out = append_text(out, "\ndivider_wraps=");
    out = append_u32(out, result->divider_wraps);
    out = append_text(out, "\ndivider_max_step=");
    out = append_u32(out, result->divider_max_step);
    out = append_text(out, "\nloop_iterations=");
    out = append_u32(out, result->loop_iterations);
    out = append_text(out, "\nkernel_calls=");
    out = append_u32(out, result->kernel_calls);
    out = append_text(out, "\nkernel_result=");
    out = append_hex32(out, result->kernel_result);
    out = append_text(out, "\nframe_updates=");
    out = append_u32(out, result->frame_updates);
    out = append_text(out, "\nkey_scans=");
    out = append_u32(out, result->key_scans);
    out = append_text(out, "\nkey_transitions=");
    out = append_u32(out, result->key_transitions);
    out = append_text(out, "\nexit_reason=");
    out = append_text(out, exit_reason_text(result->exit_reason));
    out = append_text(out, "\nexit_key_seen=");
    *out++ = result->exit_key_seen ? '1' : '0';
    out = append_text(out, "\niram_address=");
    out = append_hex32(out, IRAM_TARGET_ADDRESS);
    out = append_text(out, "\niram_payload_size=");
    out = append_u32(out, IRAM_BENCHMARK_PAYLOAD_SIZE);
    out = append_text(out, "\niram_hash_before=");
    out = append_hex32(out, result->iram_hash_before);
    out = append_text(out, "\niram_hash_active=");
    out = append_hex32(out, result->iram_hash_active);
    out = append_text(out, "\niram_hash_after=");
    out = append_hex32(out, result->iram_hash_after);
    out = append_text(out, "\niram_installed=");
    *out++ = result->iram_installed ? '1' : '0';
    out = append_text(out, "\niram_stable=");
    *out++ = result->iram_stable ? '1' : '0';
    out = append_text(out, "\niram_restored=");
    *out++ = result->iram_restored ? '1' : '0';
    out = append_text(out, "\nvideo_restored=");
    *out++ = result->video_restored ? '1' : '0';
    out = append_text(out, "\nirq_restored=");
    *out++ = result->irq_restored ? '1' : '0';
    out = append_text(out, "\nnew_irq_factors=");
    out = append_u32(out, result->new_irq_factors);
    out = append_text(out, "\nkeyboard_restored=");
    *out++ = result->keyboard_restored ? '1' : '0';
    out = append_text(out, "\npsr_before=");
    out = append_hex32(out, result->psr_before);
    out = append_text(out, "\npsr_after=");
    out = append_hex32(out, result->psr_after);
    out = append_text(out, "\npsr_ie_restored=");
    *out++ = result->psr_ie_restored ? '1' : '0';
    out = append_text(out, "\nttbr_before=");
    out = append_hex32(out, result->ttbr_before);
    out = append_text(out, "\nttbr_after=");
    out = append_hex32(out, result->ttbr_after);
    out = append_text(out, "\nttbr_restored=");
    *out++ = result->ttbr_restored ? '1' : '0';
    out = append_text(out, "\ngui_tick_before=");
    out = append_u32(out, result->gui_tick_before);
    out = append_text(out, "\ngui_tick_after=");
    out = append_u32(out, result->gui_tick_after);
    out = append_text(out, "\namr_before=");
    out = append_u32(out, result->amr_before);
    out = append_text(out, "\namr_after=");
    out = append_u32(out, result->amr_after);
    out = append_text(out, "\nhsdma_active_before=");
    out = append_u32(out, result->dma_active_before);
    out = append_text(out, "\nhsdma_active_after=");
    out = append_u32(out, result->dma_active_after);
    out = append_text(out, "\nwatchdog_control_known=0");
    out = append_text(out, "\nsystem_irq_handlers_suppressed=1");
#ifdef GAM4980_BARE_FS_PROBE
    out = append_text(out, "\nfs_source_created=");
    *out++ = result->fs_source_created ? '1' : '0';
    out = append_text(out, "\nfs_opened=");
    *out++ = result->fs_opened ? '1' : '0';
    out = append_text(out, "\nfs_closed=");
    *out++ = result->fs_closed ? '1' : '0';
    out = append_text(out, "\nfs_bytes_read=");
    out = append_u32(out, result->fs_bytes_read);
    out = append_text(out, "\nfs_expected_hash=");
    out = append_hex32(out, result->fs_expected_hash);
    out = append_text(out, "\nfs_actual_hash=");
    out = append_hex32(out, result->fs_actual_hash);
    out = append_text(out, "\nfs_data_match=");
    *out++ = result->fs_data_match ? '1' : '0';
    out = append_text(out, "\nfs_elapsed_ticks=");
    out = append_u32(out, result->fs_elapsed_ticks);
    out = append_text(out, "\nfs_psr_before=");
    out = append_hex32(out, result->fs_psr_before);
    out = append_text(out, "\nfs_psr_after=");
    out = append_hex32(out, result->fs_psr_after);
    out = append_text(out, "\nfs_psr_ie_preserved=");
    *out++ = result->fs_psr_ie_preserved ? '1' : '0';
    out = append_text(out, "\nfs_ttbr_before=");
    out = append_hex32(out, result->fs_ttbr_before);
    out = append_text(out, "\nfs_ttbr_after=");
    out = append_hex32(out, result->fs_ttbr_after);
    out = append_text(out, "\nfs_ttbr_preserved=");
    *out++ = result->fs_ttbr_preserved ? '1' : '0';
    out = append_text(out, "\nfs_irq_mask_preserved=");
    *out++ = result->fs_irq_mask_preserved ? '1' : '0';
    out = append_text(out, "\nfs_iram_hash_before=");
    out = append_hex32(out, result->fs_iram_hash_before);
    out = append_text(out, "\nfs_iram_hash_after=");
    out = append_hex32(out, result->fs_iram_hash_after);
    out = append_text(out, "\nfs_iram_preserved=");
    *out++ = result->fs_iram_preserved ? '1' : '0';
    out = append_text(out, "\nfs_iram_reinstalled=");
    *out++ = result->fs_iram_reinstalled ? '1' : '0';
    out = append_text(out, "\nfs_amr_idle=");
    *out++ = result->fs_amr_idle ? '1' : '0';
    out = append_text(out, "\nfs_dma_idle=");
    *out++ = result->fs_dma_idle ? '1' : '0';
#endif
    out = append_text(out, "\nresult=");
    out = append_text(out, result_passed(result) ? "PASS" : "FAIL");
    out = append_text(out, "\n[END]\n");
    *out = 0;
}

static void show_abort(const char *reason)
{
    char *out = g_report;

#ifdef GAM4980_BARE_FS_PROBE
    out = append_text(out, "[9288 BARE FS PROBE 1]\nresult=ABORT\nreason=");
#else
    out = append_text(out, "[9288 BARE LOOP BENCH 2]\nresult=ABORT\nreason=");
#endif
    out = append_text(out, reason);
    out = append_text(out, "\n[END]\n");
    *out = 0;
    save_report(g_report);
    (void)fnGUI_MessageBox(
        HWND_DESKTOP, (const T_BYTE *)reason,
        (const T_BYTE *)APP_TITLE, MB_OK
    );
}

static void run_bare_loop(T_RunResult *result)
{
    volatile u8 *iram = byte_register(IRAM_TARGET_ADDRESS);
    volatile u8 *video = byte_register(VIDEO_BASE);
    volatile u8 *amr = byte_register(AMR_RESIDENT_FLAG_ADDRESS);
    T_IramPayload kernel = (T_IramPayload)(void *)iram;
    T_IrqState irq_state;
    T_KeyState key_state;
    T_KeyScan scan = {{0}, 0u, 0u};
    T_ClockSample start_clock;
    T_ClockSample now_clock;
    u8 last_any_down = 0u;
    u8 last_divider;
    u32 last_elapsed = 0u;
    u32 last_frame_slot = 0u;
    u32 stagnant_iterations = 0u;
    u32 saved_psr;

    result->psr_before = read_psr();
    result->ttbr_before = read_ttbr();
    result->gui_tick_before = (u16)fnGUI_GetTickCount();
    saved_psr = result->psr_before;
    write_psr(saved_psr & ~PSR_IE_MASK);
    result->amr_before = *amr;
    result->dma_active_before = hsdma_active_mask();
    if (result->amr_before != 0u || result->dma_active_before != 0u) {
        result->exit_reason = EXIT_REASON_BACKGROUND_ACTIVE;
        result->amr_after = result->amr_before;
        result->dma_active_after = result->dma_active_before;
        result->ttbr_after = read_ttbr();
        result->ttbr_restored = result->ttbr_after == result->ttbr_before;
        write_psr(saved_psr);
        result->psr_after = read_psr();
        result->psr_ie_restored =
            (result->psr_after & PSR_IE_MASK) ==
            (result->psr_before & PSR_IE_MASK);
        result->gui_tick_after = (u16)fnGUI_GetTickCount();
        return;
    }

    save_irq_state(&irq_state);
    mask_irq_sources();
    save_key_state(&key_state);
    prepare_direct_key_scan();
    copy_from_volatile(g_video_backup, video, BARE_SCREEN_BYTES);
    copy_from_volatile(g_iram_backup, iram, IRAM_BENCHMARK_PAYLOAD_SIZE);
    result->iram_hash_before = hash_volatile(
        iram, IRAM_BENCHMARK_PAYLOAD_SIZE
    );
    copy_to_volatile(
        iram, g_iram_benchmark_payload, IRAM_BENCHMARK_PAYLOAD_SIZE
    );
    result->iram_installed = volatile_equals(
        iram, g_iram_benchmark_payload, IRAM_BENCHMARK_PAYLOAD_SIZE
    );
    result->iram_stable = result->iram_installed;
    if (!result->iram_installed) {
        result->exit_reason = EXIT_REASON_INSTALL_FAILED;
        goto restore_state;
    }

#ifdef GAM4980_BARE_FS_PROBE
    run_direct_fs_probe(result, iram);
    result->iram_stable = result->fs_iram_reinstalled;
    if (!result->fs_opened || !result->fs_closed ||
        !result->fs_data_match || !result->fs_psr_ie_preserved ||
        !result->fs_ttbr_preserved || !result->fs_irq_mask_preserved ||
        !result->fs_iram_reinstalled || !result->fs_amr_idle ||
        !result->fs_dma_idle) {
        result->exit_reason = EXIT_REASON_FS_API_FAILED;
        goto restore_state;
    }
#endif

    read_clock_sample(&start_clock);
    now_clock = start_clock;
    result->start_clock_day = start_clock.day;
    result->end_clock_day = start_clock.day;
    result->start_clock_second_of_day = clock_second_of_day(&start_clock);
    result->end_clock_second_of_day = result->start_clock_second_of_day;
    result->start_divider = start_clock.divider;
    result->end_divider = start_clock.divider;
    last_divider = start_clock.divider;
    scan_keyboard(&scan);
    ++result->key_scans;
    make_frame(0u, &scan);
    submit_direct_frame(g_frame);
    ++result->frame_updates;
    result->kernel_result = 0x02700880u;

    for (;;) {
        u32 elapsed;
        u32 frame_slot;
        u32 divider_step;

        result->kernel_result = kernel(result->kernel_result, 32u);
        ++result->kernel_calls;
        ++result->loop_iterations;
        read_clock_sample(&now_clock);
        if (!clock_elapsed_ticks(&start_clock, &now_clock, &elapsed)) {
            ++result->clock_invalid_samples;
            if (++stagnant_iterations >= TICK_STALL_ITERATION_LIMIT) {
                result->exit_reason = EXIT_REASON_TICK_STALLED;
                break;
            }
            continue;
        }
        ++result->clock_valid_samples;
        result->end_clock_day = now_clock.day;
        result->end_clock_second_of_day = clock_second_of_day(&now_clock);
        result->end_divider = now_clock.divider;
        result->elapsed_ticks = elapsed;
        result->hardware_tick_progress = result->elapsed_ticks != 0u;
        if (now_clock.divider != last_divider) {
            divider_step = (u8)(now_clock.divider - last_divider);
            ++result->divider_changes;
            if (now_clock.divider < last_divider)
                ++result->divider_wraps;
            if (divider_step > result->divider_max_step)
                result->divider_max_step = divider_step;
            last_divider = now_clock.divider;
        }
        if (elapsed == last_elapsed) {
            if (++stagnant_iterations >= TICK_STALL_ITERATION_LIMIT) {
                result->exit_reason = EXIT_REASON_TICK_STALLED;
                break;
            }
            continue;
        }
        stagnant_iterations = 0u;
        last_elapsed = elapsed;
        frame_slot = elapsed / FRAME_TICKS;
        if (frame_slot == last_frame_slot && elapsed < TEST_TICKS)
            continue;
        last_frame_slot = frame_slot;

        scan_keyboard(&scan);
        ++result->key_scans;
        if (scan.any_down != last_any_down) {
            ++result->key_transitions;
            last_any_down = scan.any_down;
        }
        if (scan.exit_down) {
            result->exit_key_seen = 1u;
            result->exit_reason = EXIT_REASON_KEY;
        }
        if (!volatile_equals(
                iram, g_iram_benchmark_payload,
                IRAM_BENCHMARK_PAYLOAD_SIZE)) {
            result->iram_stable = 0u;
            result->exit_reason = EXIT_REASON_INSTALL_FAILED;
            break;
        }
        make_frame(result->elapsed_ticks / FRAME_TICKS, &scan);
        submit_direct_frame(g_frame);
        ++result->frame_updates;
        if (result->exit_reason == EXIT_REASON_KEY) {
            T_ClockSample release_start;
            u32 release_elapsed = 0u;

            read_clock_sample(&release_start);
            while (scan.exit_down && release_elapsed <
                   2u * CTM_TICKS_PER_SECOND) {
                scan_keyboard(&scan);
                read_clock_sample(&now_clock);
                if (!clock_elapsed_ticks(
                        &release_start, &now_clock, &release_elapsed))
                    release_elapsed = 0u;
            }
            break;
        }
        if (result->elapsed_ticks >= TEST_TICKS) {
            result->exit_reason = EXIT_REASON_TIMEOUT;
            break;
        }
    }
    result->iram_hash_active = hash_volatile(
        iram, IRAM_BENCHMARK_PAYLOAD_SIZE
    );

restore_state:
    copy_to_volatile(iram, g_iram_backup, IRAM_BENCHMARK_PAYLOAD_SIZE);
    result->iram_restored = volatile_equals(
        iram, g_iram_backup, IRAM_BENCHMARK_PAYLOAD_SIZE
    );
    result->iram_hash_after = hash_volatile(
        iram, IRAM_BENCHMARK_PAYLOAD_SIZE
    );
    copy_to_volatile(video, g_video_backup, BARE_SCREEN_BYTES);
    result->video_restored = volatile_equals(
        video, g_video_backup, BARE_SCREEN_BYTES
    );
    result->keyboard_restored = restore_key_state(&key_state);
    result->irq_restored = restore_irq_state(
        &irq_state, &result->new_irq_factors
    );
    result->dma_active_after = hsdma_active_mask();
    result->amr_after = *amr;
    result->ttbr_after = read_ttbr();
    result->ttbr_restored = result->ttbr_after == result->ttbr_before;
    write_psr(saved_psr);
    result->psr_after = read_psr();
    result->psr_ie_restored =
        (result->psr_after & PSR_IE_MASK) ==
        (result->psr_before & PSR_IE_MASK);
    result->gui_tick_after = (u16)fnGUI_GetTickCount();
}

T_WORD App_Main(void)
{
    volatile u8 *amr = byte_register(AMR_RESIDENT_FLAG_ADDRESS);
    T_RunResult result = {0};
    u32 sp = read_sp();
    char *out;

    if (IRAM_BENCHMARK_PAYLOAD_SIZE == 0u ||
        IRAM_BENCHMARK_PAYLOAD_SIZE > IRAM_BACKUP_SIZE) {
        show_abort("Generated IRAM payload size is invalid.");
        return -1;
    }
    if (!clock_timer_preflight()) {
        show_abort("The hardware 256 Hz clock timer is not advancing.");
        return -5;
    }
    if (!wait_for_quiescent_system()) {
        show_abort("AMR/HSDMA stayed active for 2 seconds. Stop audio and retry.");
        return -2;
    }
    if (*amr != 0u || hsdma_active_mask() != 0u) {
        show_abort("AMR/HSDMA became active. Bare-loop test was not started.");
        return -3;
    }
    if (sp < 0x00004000u) {
        show_abort("Stack is inside IRAM. Bare-loop test was not started.");
        return -4;
    }
    out = g_summary;
#ifdef GAM4980_BARE_FS_PROBE
    out = append_text(out,
        "Risky bare-mode filesystem experiment.\n\n"
        "A 4 KiB source file is created before entry.\n"
        "CPU interrupts and system IRQ sources are then masked.\n"
        "The SDK fopen/fread/fclose API is called directly\n"
        "without restoring firmware scheduling.\n\n"
        "The probe checks data, PSR, TTBR, IRQ, DMA, AMR\n"
        "and whether the SDK overwrites resident IRAM.\n"
        "Press physical EXIT to finish the stability loop.\n\n"
        "SP="
    );
#else
    out = append_text(out,
        "Risky 10-second bare-loop experiment.\n\n"
        "CPU interrupts and system IRQ sources will be masked.\n"
        "A resident IRAM leaf kernel will run continuously.\n"
        "Time: hardware 256 Hz clock counter.\n"
        "Keys: direct 9288 matrix scan.\n"
        "Display: direct 0x3C0000 framebuffer.\n\n"
        "The test restores IRAM, display, keyboard and IRQ state.\n"
        "Press physical EXIT to finish early.\n"
        "Watchdog control is still unknown.\n\n"
        "SP="
    );
#endif
    out = append_hex32(out, sp);
    out = append_text(out, " TTBR=");
    out = append_hex32(out, read_ttbr());
    out = append_text(out, "\n\nPress OK to start or EXIT to cancel.");
    *out = 0;
    if (fnGUI_MessageBox(
            HWND_DESKTOP, (const T_BYTE *)g_summary,
            (const T_BYTE *)APP_TITLE, MB_OKCANCEL) != IDOK) {
        show_abort("Cancelled before entering bare-loop mode.");
        return 0;
    }
    if (!wait_for_quiescent_system()) {
        show_abort("AMR/HSDMA stayed active after confirmation.");
        return -6;
    }
#ifdef GAM4980_BARE_FS_PROBE
    if (!prepare_fs_probe_source()) {
        show_abort("Could not create A:\\BAREFS.DAT before bare entry.");
        return -7;
    }
    result.fs_source_created = 1u;
    if (!wait_for_quiescent_system()) {
        show_abort("Filesystem setup left AMR/HSDMA active.");
        return -8;
    }
#endif

    run_bare_loop(&result);
    build_report(&result);
    save_report(g_report);
#ifdef GAM4980_BARE_FS_PROBE
    (void)fs_remove(FS_PROBE_PATH);
#endif
    out = g_summary;
#ifdef GAM4980_BARE_FS_PROBE
    out = append_text(out, result_passed(&result) ?
        "BARE FS RESULT: PASS\n\n" : "BARE FS RESULT: FAIL\n\n");
    out = append_text(out, "read_bytes=");
    out = append_u32(out, result.fs_bytes_read);
    out = append_text(out, " data_match=");
    *out++ = result.fs_data_match ? '1' : '0';
    out = append_text(out, "\nIRAM preserved=");
    *out++ = result.fs_iram_preserved ? '1' : '0';
    out = append_text(out, " reinstalled=");
    *out++ = result.fs_iram_reinstalled ? '1' : '0';
    out = append_text(out, "\nPSR/TTBR/IRQ preserved=");
    *out++ = result.fs_psr_ie_preserved && result.fs_ttbr_preserved &&
        result.fs_irq_mask_preserved ? '1' : '0';
    out = append_text(out, "\n");
#else
    out = append_text(out, result_passed(&result) ?
        "BARE LOOP RESULT: PASS\n\n" : "BARE LOOP RESULT: FAIL\n\n");
#endif
    out = append_text(out, "elapsed_ticks=");
    out = append_u32(out, result.elapsed_ticks);
    out = append_text(out, " kernel_calls=");
    out = append_u32(out, result.kernel_calls);
    out = append_text(out, "\nframes=");
    out = append_u32(out, result.frame_updates);
    out = append_text(out, " new_irq_factors=");
    out = append_u32(out, result.new_irq_factors);
    out = append_text(out, "\nIRAM restored=");
    *out++ = result.iram_restored ? '1' : '0';
    out = append_text(out, " desktop state restored=");
    *out++ = result.video_restored && result.irq_restored &&
        result.keyboard_restored ? '1' : '0';
#ifdef GAM4980_BARE_FS_PROBE
    out = append_text(out, "\n\nLog: A:\\BAREFS.LOG");
#else
    out = append_text(out, "\n\nLog: A:\\BARELOOP.LOG");
#endif
    *out = 0;
    (void)fnGUI_MessageBox(
        HWND_DESKTOP, (const T_BYTE *)g_summary,
        (const T_BYTE *)APP_TITLE, MB_OK
    );
    return result_passed(&result) ? 0 : -6;
}
