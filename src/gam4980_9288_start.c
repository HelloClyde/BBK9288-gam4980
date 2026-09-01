#include "Dsys.h"
#include "gam4980_9288_iram.h"

/* 0x1700 is the SDK's linkable overlay size.  Until a larger continuous
 * range has passed on physical hardware, keep the player at the largest
 * extent previously exercised there (0x0800..0x1ec7). */
#define GAM4980_IRAM_LIMIT 0x16c8u
#define GAM4980_AMR_RESIDENT_FLAG_ADDRESS 0x00003fcau
#define GAM4980_PSR_IE_MASK 0x00000010u
#define GAM4980_IRAM_SESSION_RANGES 8u
#define GAM4980_IRAM_BACKUP_STORAGE \
    __attribute__((aligned(4), section(".scratch.iram_backup")))

typedef struct T_GAM4980_IramSessionRange {
    unsigned long address;
    unsigned long size;
} T_GAM4980_IramSessionRange;

T_GUI_RelocationTable *tpDL_GUITable;
T_CRTL_RelocationTable *tpDL_CRTLTable;
T_FS_RelocationTable *tpDL_FSTable;
T_ROS33_RelocationTable *tpDL_ROS33Table;
T_Audio_RelocationTable *tpDL_AudioTable;
T_Dict_RelocationTable *tpDL_DictTable;

extern unsigned char __bss_start;
extern unsigned char __bss_end;
extern unsigned char __iram_start;
extern unsigned char __iram_end;
extern unsigned char __iram_load_start;
extern T_WORD App_Main(void);

static int g_iram_status;
/* Keep the real linked overlay size observable in diagnostics.  With LTO the
 * compiler can otherwise shrink this internal value to a one-byte nonzero
 * flag because the install path recomputes the range independently. */
static volatile unsigned long g_iram_size;
#ifdef GAM4980_ENABLE_IRAM_HOT_CORE
static unsigned char g_iram_backup[GAM4980_IRAM_LIMIT]
    GAM4980_IRAM_BACKUP_STORAGE;
static volatile unsigned char *g_iram_active_address;
static unsigned long g_iram_active_size;
static unsigned long g_iram_saved_psr;
static unsigned long g_iram_range_depth;
static int g_iram_session_resident;
static T_GAM4980_IramSessionRange
    g_iram_session_ranges[GAM4980_IRAM_SESSION_RANGES];
static unsigned long g_iram_session_range_count;
static unsigned long g_iram_session_repair_count;
#endif
static unsigned long g_iram_calls;
static unsigned long g_iram_bytes_installed;

#ifdef GAM4980_ENABLE_IRAM_HOT_CORE
static unsigned long read_psr(void)
{
    unsigned long value;

    __asm__ volatile("ld.w %0,%%psr" : "=r"(value));
    return value;
}

static void write_psr(unsigned long value)
{
    __asm__ volatile("ld.w %%psr,%0" : : "r"(value) : "memory");
}

static int bytes_equal(
    const volatile unsigned char *left,
    const volatile unsigned char *right,
    unsigned long size
)
{
    unsigned long index;

    for (index = 0u; index < size; ++index) {
        if (left[index] != right[index])
            return 0;
    }
    return 1;
}

static void copy_bytes(
    volatile unsigned char *destination,
    const volatile unsigned char *source,
    unsigned long size
)
{
    unsigned long index;

    /* The independent true-device probe deliberately used byte accesses.
     * Keep that verified transfer width here: the 9288 IRAM path is not a
     * normal 32-bit external-RAM bus, and a word-copy looked valid through
     * data reads in the emulator while instruction fetch saw stale halfwords. */
    for (index = 0u; index < size; ++index)
        destination[index] = source[index];
    __asm__ volatile("" : : : "memory");
}
#endif

__attribute__((noinline)) int gam4980_9288_iram_enter(void)
{
#ifdef GAM4980_ENABLE_IRAM_HOT_CORE
    unsigned long size = (unsigned long)(&__iram_end - &__iram_start);

    g_iram_size = size;
    if (!size || size > GAM4980_IRAM_LIMIT || (size & 3u) != 0u) {
        g_iram_status = GAM4980_IRAM_STATUS_INVALID_SIZE;
        return 0;
    }
    g_iram_active_address = 0;
    g_iram_active_size = 0u;
    g_iram_calls = 0u;
    g_iram_bytes_installed = 0u;
    g_iram_range_depth = 0u;
    g_iram_session_resident = 0;
    g_iram_session_range_count = 0u;
    g_iram_session_repair_count = 0u;
    /* The GUI/firmware owns this range between calls.  Mark the transient
     * overlay ready here; each hot function is installed and restored inside
     * one interrupt-disabled window immediately around its invocation. */
    g_iram_status = GAM4980_IRAM_STATUS_RESTORED;
    return 1;
#else
    g_iram_status = GAM4980_IRAM_STATUS_DISABLED;
    g_iram_size = 0u;
    return 1;
#endif
}

__attribute__((noinline)) unsigned long gam4980_9288_iram_begin_range(
    unsigned long address, unsigned long size
)
{
#ifdef GAM4980_ENABLE_IRAM_HOT_CORE
    volatile unsigned char *amr_resident =
        (volatile unsigned char *)GAM4980_AMR_RESIDENT_FLAG_ADDRESS;
    unsigned long iram_start = (unsigned long)&__iram_start;
    unsigned long iram_end = (unsigned long)&__iram_end;
    unsigned long load_start = (unsigned long)&__iram_load_start;
    volatile unsigned char *iram;
    const volatile unsigned char *load;
    unsigned long psr;

    if (g_iram_session_resident) {
        unsigned long index;
        unsigned long offset;

        if (g_iram_status != GAM4980_IRAM_STATUS_ACTIVE || !size ||
            address < iram_start || address > iram_end ||
            size > iram_end - address) {
            g_iram_status = GAM4980_IRAM_STATUS_INVALID_SIZE;
            return 0u;
        }
        for (index = 0u; index < g_iram_session_range_count; ++index) {
            unsigned long installed_address =
                g_iram_session_ranges[index].address;
            unsigned long installed_size =
                g_iram_session_ranges[index].size;

            if (address >= installed_address &&
                address - installed_address <= installed_size &&
                size <= installed_size - (address - installed_address)) {
                ++g_iram_range_depth;
                ++g_iram_calls;
                return address;
            }
        }
        if (g_iram_session_range_count >= GAM4980_IRAM_SESSION_RANGES) {
            g_iram_status = GAM4980_IRAM_STATUS_INVALID_SIZE;
            return 0u;
        }
        offset = address - iram_start;
        iram = (volatile unsigned char *)address;
        load = (const volatile unsigned char *)(load_start + offset);
        copy_bytes(g_iram_backup + offset, iram, size);
        copy_bytes(iram, load, size);
        if (!bytes_equal(iram, load, size)) {
            copy_bytes(iram, g_iram_backup + offset, size);
            g_iram_status = bytes_equal(
                iram, g_iram_backup + offset, size
            ) ? GAM4980_IRAM_STATUS_COPY_FAILED
              : GAM4980_IRAM_STATUS_RESTORE_FAILED;
            return 0u;
        }
        g_iram_session_ranges[g_iram_session_range_count].address = address;
        g_iram_session_ranges[g_iram_session_range_count].size = size;
        ++g_iram_session_range_count;
        ++g_iram_range_depth;
        ++g_iram_calls;
        g_iram_bytes_installed += size;
        return address;
    }
    if (g_iram_status != GAM4980_IRAM_STATUS_RESTORED || !size ||
        address < iram_start || address > iram_end ||
        size > iram_end - address || size > GAM4980_IRAM_LIMIT) {
        g_iram_status = GAM4980_IRAM_STATUS_INVALID_SIZE;
        return 0u;
    }
    if (*amr_resident != 0u) {
        /* Key tones and other short audio may occupy AMR at any time.  Skip
         * only this invocation and keep the transient overlay ready so a
         * later HLE call can retry after audio releases IRAM. */
        return 0u;
    }
    iram = (volatile unsigned char *)address;
    load = (const volatile unsigned char *)(
        load_start + (address - iram_start)
    );
    psr = read_psr();
    write_psr(psr & ~GAM4980_PSR_IE_MASK);
    copy_bytes(g_iram_backup, iram, size);
    copy_bytes(iram, load, size);
    if (!bytes_equal(iram, load, size)) {
        copy_bytes(iram, g_iram_backup, size);
        write_psr(psr);
        g_iram_status = GAM4980_IRAM_STATUS_COPY_FAILED;
        return 0u;
    }
    g_iram_active_address = iram;
    g_iram_active_size = size;
    g_iram_saved_psr = psr;
    ++g_iram_calls;
    g_iram_bytes_installed += size;
    g_iram_status = GAM4980_IRAM_STATUS_ACTIVE;
    return address;
#else
    (void)address;
    (void)size;
    return 0u;
#endif
}

__attribute__((noinline)) void gam4980_9288_iram_leave(void)
{
#ifdef GAM4980_ENABLE_IRAM_HOT_CORE
    volatile unsigned char *iram = g_iram_active_address;
    unsigned long size = g_iram_active_size;
    unsigned long saved_psr = g_iram_saved_psr;

    if (g_iram_session_resident) {
        gam4980_9288_iram_session_restore();
        return;
    }
    if (g_iram_status != GAM4980_IRAM_STATUS_ACTIVE)
        return;
    copy_bytes(iram, g_iram_backup, size);
    if (!bytes_equal(iram, g_iram_backup, size)) {
        write_psr(saved_psr);
        g_iram_status = GAM4980_IRAM_STATUS_RESTORE_FAILED;
        return;
    }
    g_iram_active_address = 0;
    g_iram_active_size = 0u;
    write_psr(saved_psr);
    g_iram_status = GAM4980_IRAM_STATUS_RESTORED;
#endif
}

__attribute__((noinline)) void gam4980_9288_iram_end_range(void)
{
#ifdef GAM4980_ENABLE_IRAM_HOT_CORE
    if (g_iram_session_resident) {
        if (g_iram_range_depth)
            --g_iram_range_depth;
        return;
    }
#endif
    gam4980_9288_iram_leave();
}

__attribute__((noinline)) int gam4980_9288_iram_session_install(void)
{
#ifdef GAM4980_ENABLE_IRAM_HOT_CORE
    volatile unsigned char *amr_resident =
        (volatile unsigned char *)GAM4980_AMR_RESIDENT_FLAG_ADDRESS;
    unsigned long size = (unsigned long)(&__iram_end - &__iram_start);

    g_iram_size = size;
    if (g_iram_session_resident)
        return 1;
    if (g_iram_status != GAM4980_IRAM_STATUS_RESTORED || !size ||
        size > GAM4980_IRAM_LIMIT || (size & 3u) != 0u) {
        g_iram_status = GAM4980_IRAM_STATUS_INVALID_SIZE;
        return 0;
    }
    if (*amr_resident != 0u) {
        g_iram_status = GAM4980_IRAM_STATUS_AMR_BUSY;
        return 0;
    }
    /* Install only a small ownership token here.  Each hot function is
     * copied on its first call and then remains resident until session exit.
     * This avoids touching unused IRAM/system-variable holes and keeps the
     * exact 68-byte true-device experiment's conservative range semantics. */
    g_iram_active_address =
        (volatile unsigned char *)(unsigned long)&__iram_start;
    g_iram_active_size = size;
    g_iram_range_depth = 0u;
    g_iram_session_range_count = 0u;
    g_iram_session_resident = 1;
    ++g_iram_calls;
    g_iram_status = GAM4980_IRAM_STATUS_ACTIVE;
    return 1;
#else
    return 0;
#endif
}

__attribute__((noinline)) int gam4980_9288_iram_session_validate(void)
{
#ifdef GAM4980_ENABLE_IRAM_HOT_CORE
    unsigned long iram_start = (unsigned long)&__iram_start;
    unsigned long load_start = (unsigned long)&__iram_load_start;
    unsigned long index;

    if (!g_iram_session_resident ||
        g_iram_status != GAM4980_IRAM_STATUS_ACTIVE)
        return 0;
    /* The filesystem gateway itself runs from external RAM.  A direct SDK
     * read is therefore allowed while an IRAM caller is suspended on the
     * stack.  Verify every lazily installed range before returning through
     * that frame, and repair from the immutable KF2 load image if needed. */
    for (index = 0u; index < g_iram_session_range_count; ++index) {
        unsigned long address = g_iram_session_ranges[index].address;
        unsigned long size = g_iram_session_ranges[index].size;
        unsigned long offset = address - iram_start;
        volatile unsigned char *iram =
            (volatile unsigned char *)address;
        const volatile unsigned char *load =
            (const volatile unsigned char *)(load_start + offset);

        if (bytes_equal(iram, load, size))
            continue;
        copy_bytes(iram, load, size);
        if (!bytes_equal(iram, load, size)) {
            g_iram_status = GAM4980_IRAM_STATUS_COPY_FAILED;
            return 0;
        }
        ++g_iram_session_repair_count;
        g_iram_bytes_installed += size;
    }
    return 1;
#else
    return 0;
#endif
}

__attribute__((noinline)) void gam4980_9288_iram_session_restore(void)
{
#ifdef GAM4980_ENABLE_IRAM_HOT_CORE
    unsigned long iram_start = (unsigned long)&__iram_start;
    unsigned long index;
    int restored = 1;

    if (!g_iram_session_resident)
        return;
    if (g_iram_range_depth != 0u) {
        g_iram_status = GAM4980_IRAM_STATUS_RESTORE_FAILED;
        return;
    }
    for (index = g_iram_session_range_count; index > 0u; --index) {
        unsigned long address = g_iram_session_ranges[index - 1u].address;
        unsigned long size = g_iram_session_ranges[index - 1u].size;
        unsigned long offset = address - iram_start;
        volatile unsigned char *iram =
            (volatile unsigned char *)address;

        copy_bytes(iram, g_iram_backup + offset, size);
        if (!bytes_equal(iram, g_iram_backup + offset, size))
            restored = 0;
    }
    g_iram_active_address = 0;
    g_iram_active_size = 0u;
    g_iram_session_range_count = 0u;
    g_iram_session_resident = 0;
    g_iram_status = restored
        ? GAM4980_IRAM_STATUS_RESTORED
        : GAM4980_IRAM_STATUS_RESTORE_FAILED;
#endif
}

unsigned long gam4980_9288_iram_range_depth(void)
{
#ifdef GAM4980_ENABLE_IRAM_HOT_CORE
    return g_iram_range_depth;
#else
    return 0u;
#endif
}

unsigned long gam4980_9288_iram_session_repairs(void)
{
#ifdef GAM4980_ENABLE_IRAM_HOT_CORE
    return g_iram_session_repair_count;
#else
    return 0u;
#endif
}

int gam4980_9288_iram_status(void)
{
    return g_iram_status;
}

unsigned long gam4980_9288_iram_size(void)
{
    return g_iram_size;
}

unsigned long gam4980_9288_iram_calls(void)
{
    return g_iram_calls;
}

unsigned long gam4980_9288_iram_bytes_installed(void)
{
    return g_iram_bytes_installed;
}

static void init_relocation_tables(void)
{
    T_GeneralRelocationTable *table =
        (T_GeneralRelocationTable *)SYS_GENERAL_RELOCATION_TABLE_ADDRESS;

    tpDL_ROS33Table =
        (T_ROS33_RelocationTable *)table->ROS33_RelocationAddress;
    tpDL_GUITable = (T_GUI_RelocationTable *)table->GUI_RelocationAddress;
    tpDL_FSTable = (T_FS_RelocationTable *)table->FS_RelocationAddress;
    tpDL_CRTLTable = (T_CRTL_RelocationTable *)table->CRTL_RelocationAddress;
    tpDL_AudioTable =
        (T_Audio_RelocationTable *)table->Audio_RelocationAddress;
    tpDL_DictTable = (T_Dict_RelocationTable *)table->Dict_RelocationAddress;
}

__attribute__((section(".text.startup")))
long DL_AppMain(long argument)
{
    unsigned char *cursor = &__bss_start;
    unsigned char *end = &__bss_end;
    long result;

    (void)argument;
    while (cursor < end && ((unsigned long)cursor & 3u) != 0u)
        *cursor++ = 0;
    while (cursor + 32u <= end) {
        unsigned int *words = (unsigned int *)cursor;

        words[0] = 0;
        words[1] = 0;
        words[2] = 0;
        words[3] = 0;
        words[4] = 0;
        words[5] = 0;
        words[6] = 0;
        words[7] = 0;
        cursor += 32u;
    }
    while (cursor < end)
        *cursor++ = 0;
    init_relocation_tables();
    result = (long)App_Main();
    gam4980_9288_iram_leave();
    return result;
}
