#include "Dsys.h"
#include "gam4980_9288_iram.h"
#include "gam4980_types.h"
#include "s6502_iram_exec_abi.h"

#define APP_TITLE "IRAM SUPER EQ"
#define TEST_PAGE 0x40u
#define TEST_OFFSET 0x40u
#define TEST_PC ((TEST_PAGE << 8) | TEST_OFFSET)
#define TEST_CASES 8u
#define SUPER_KINDS 5u
#define FLAG_N 0x80u
#define FLAG_V 0x40u
#define FLAG_U 0x20u
#define FLAG_B 0x10u
#define FLAG_D 0x08u
#define FLAG_I 0x04u
#define FLAG_Z 0x02u
#define FLAG_C 0x01u
#define REPORT_MAGIC 0x53555045u /* "SUPE" */
#define AMR_RESIDENT_FLAG_ADDRESS 0x00003fcau
#define PSR_IE_MASK 0x00000010u
#define TEST_STORAGE __attribute__((aligned(16), section(".scratch")))

enum T_SuperKind {
    SUPER_LOAD_OPER1_IMM16 = 0,
    SUPER_LOAD_OPER2_IMM16 = 1,
    SUPER_STACK_ADD16 = 2,
    SUPER_STACK_SUB16 = 3,
    SUPER_ADD16_OPER1_OPER2 = 4
};

typedef struct T_CaseObservation {
    u32 pc;
    u32 cycles;
    u32 instructions;
    u32 exit_reason;
    u32 ac;
    u32 status;
    u32 result;
} T_CaseObservation;

typedef struct T_SuperEquivReport {
    u32 magic;
    u32 completed;
    u32 passed;
    u32 fail_mask;
    u32 cases_run;
    u32 cases_passed;
    u32 iram_status;
    u32 iram_size;
    u32 super_hits[SUPER_KINDS];
    u32 decimal_fallbacks;
    T_CaseObservation cases[TEST_CASES];
} T_SuperEquivReport;

typedef struct T_NativeSharedMetrics {
    u32 calls;
    u32 blocks;
    u32 guest_cycles;
    u32 entries_7c30;
    u32 misses;
} T_NativeSharedMetrics;

volatile T_SuperEquivReport g_super_equiv_report;

static u8 g_ram[0x10000u] TEST_STORAGE;
static u8 g_shadow_page[0x100u] TEST_STORAGE;
static u8 *g_pages[0x100u] TEST_STORAGE;
static u8 *g_code_pages[0x100u] TEST_STORAGE;
static u8 g_page_kind[0x100u] TEST_STORAGE;
static u8 g_dispatch[0x10000u] TEST_STORAGE;
static u32 g_native_page_entries[256] TEST_STORAGE;
static u32 g_super_hits[SUPER_KINDS] TEST_STORAGE;
static T_NativeSharedMetrics g_native_metrics TEST_STORAGE;
static u32 g_dirty TEST_STORAGE;

extern unsigned char __iram_exec_engine_start;
extern unsigned char __iram_exec_engine_end;
typedef u32 (*T_IramExec)(s6502_iram_asm_context_t *context);

static u32 test_native_dispatch(s6502_iram_asm_context_t *context);

static void clear_bytes(volatile u8 *data, u32 size)
{
    u32 index;

    for (index = 0u; index < size; ++index)
        data[index] = 0u;
}

static void copy_bytes(u8 *destination, const u8 *source, u32 size)
{
    u32 index;

    for (index = 0u; index < size; ++index)
        destination[index] = source[index];
}

static void prepare_memory(const u8 *program, u32 size)
{
    u32 page;

    clear_bytes(g_ram, sizeof(g_ram));
    clear_bytes(g_shadow_page, sizeof(g_shadow_page));
    clear_bytes(g_page_kind, sizeof(g_page_kind));
    clear_bytes(g_dispatch, sizeof(g_dispatch));
    clear_bytes(
        (volatile u8 *)g_native_page_entries,
        sizeof(g_native_page_entries)
    );
    clear_bytes((volatile u8 *)g_super_hits, sizeof(g_super_hits));
    clear_bytes(
        (volatile u8 *)&g_native_metrics, sizeof(g_native_metrics)
    );
    g_dirty = 0u;
    for (page = 0u; page < 0x100u; ++page) {
        g_pages[page] = g_ram + (page << 8);
        g_code_pages[page] = g_ram + (page << 8);
        g_page_kind[page] = S6502_IRAM_PAGE_READ_DIRECT |
            S6502_IRAM_PAGE_WRITE_DIRECT | S6502_IRAM_PAGE_FETCH_DIRECT;
    }
    g_page_kind[0x02u] |= S6502_IRAM_PAGE_FORCE_PB_ZERO;
    g_page_kind[0x20u] |= S6502_IRAM_PAGE_FORCE_APO_FF;
    copy_bytes(g_ram + TEST_PC, program, size);
    g_shadow_page[TEST_OFFSET] = 0x02u;
    g_code_pages[TEST_PAGE] = g_shadow_page;
}

static void prepare_context(
    s6502_iram_asm_context_t *context, u8 ac, u8 sp, u8 status
)
{
    clear_bytes((volatile u8 *)context, sizeof(*context));
    context->pc = TEST_PC;
    context->ac = ac;
    context->ix = 0x66u;
    context->iy = 0x77u;
    context->sp = sp;
    context->status = status;
    context->cycle_budget = 1000u;
    context->pages = (u32)(unsigned long)g_pages;
    context->page_kind = (u32)(unsigned long)g_page_kind;
    context->ram = (u32)(unsigned long)g_ram;
    context->dirty = (u32)(unsigned long)&g_dirty;
    context->dispatch_bits = 0u;
    context->code_pages = (u32)(unsigned long)g_code_pages;
    context->super_hits = (u32)(unsigned long)g_super_hits;
    context->native_shared_entry = 0u;
    context->native_shared_metrics = 0u;
}

static int hits_are_exact(u32 expected_kind)
{
    u32 kind;

    for (kind = 0u; kind < SUPER_KINDS; ++kind) {
        u32 expected = kind == expected_kind ? 1u : 0u;

        if (g_super_hits[kind] != expected)
            return 0;
    }
    return 1;
}

static int hits_are_zero(void)
{
    u32 kind;

    for (kind = 0u; kind < SUPER_KINDS; ++kind) {
        if (g_super_hits[kind] != 0u)
            return 0;
    }
    return 1;
}

static void record_case(
    u32 index, const s6502_iram_asm_context_t *context, u32 result, int passed
)
{
    volatile T_CaseObservation *observation =
        &g_super_equiv_report.cases[index];
    u32 kind;

    observation->pc = context->pc;
    observation->cycles = context->cycles;
    observation->instructions = context->instructions;
    observation->exit_reason = context->exit_reason;
    observation->ac = context->ac;
    observation->status = context->status;
    observation->result = result;
    for (kind = 0u; kind < SUPER_KINDS; ++kind)
        g_super_equiv_report.super_hits[kind] += g_super_hits[kind];
    ++g_super_equiv_report.cases_run;
    if (passed)
        ++g_super_equiv_report.cases_passed;
    else
        g_super_equiv_report.fail_mask |= 1u << index;
}

static int check_common(
    const s6502_iram_asm_context_t *context, u32 result,
    u32 pc, u32 cycles, u32 instructions, u32 ac, u32 status
)
{
    return result == cycles && context->pc == pc &&
        context->cycles == cycles && context->instructions == instructions &&
        context->control_transitions == 0u &&
        context->exit_reason == S6502_IRAM_EXIT_SLOW &&
        context->ac == ac && context->ix == 0x66u &&
        context->iy == 0x77u && context->status == status &&
        g_dirty == 0u;
}

static T_IramExec install_engine(void);

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

static int execute_context(
    s6502_iram_asm_context_t *context, u32 *result
)
{
    volatile u8 *amr =
        (volatile u8 *)(unsigned long)AMR_RESIDENT_FLAG_ADDRESS;
    u32 saved_psr = read_psr();
    u8 saved_amr;
    T_IramExec execute;
    int restored;

    /* The pristine emulator's OK-key tone leaves the AMR ownership flag set
     * even after playback has stopped.  Run the exact same transient copy
     * window as production, but mask interrupts before clearing that stale
     * flag, restore it before executing, and keep interrupts masked until the
     * original IRAM bytes are back.  No AMR/system code can observe the
     * temporary value or execute from the overlaid range. */
    write_psr(saved_psr & ~PSR_IE_MASK);
    saved_amr = *amr;
    *amr = 0u;
    execute = install_engine();
    *amr = saved_amr;

    if (!execute) {
        write_psr(saved_psr);
        *result = 0xffffffffu;
        return 0;
    }
    *result = execute(context);
    gam4980_9288_iram_end_range();
    restored = gam4980_9288_iram_status() ==
        GAM4980_IRAM_STATUS_RESTORED;
    write_psr(saved_psr);
    return restored;
}

static int run_load_oper1(u32 case_index)
{
    static const u8 program[] = {
        0xa9u, 0x34u, 0x85u, 0x20u,
        0xa9u, 0x80u, 0x85u, 0x21u
    };
    s6502_iram_asm_context_t context;
    u32 result;
    int passed;

    prepare_memory(program, sizeof(program));
    prepare_context(&context, 0x5au, 0xf0u, FLAG_U | FLAG_I | FLAG_C);
    passed = execute_context(&context, &result);
    passed = passed && check_common(
        &context, result, TEST_PC + sizeof(program), 10u, 4u,
        0x80u, FLAG_N | FLAG_U | FLAG_I | FLAG_C
    ) && context.sp == 0xf0u && g_ram[0x20u] == 0x34u &&
        g_ram[0x21u] == 0x80u && hits_are_exact(SUPER_LOAD_OPER1_IMM16);
    record_case(case_index, &context, result, passed);
    return passed;
}

static int run_load_oper2(u32 case_index)
{
    static const u8 program[] = {
        0xa9u, 0x78u, 0x85u, 0x23u,
        0xa9u, 0x00u, 0x85u, 0x24u
    };
    s6502_iram_asm_context_t context;
    u32 result;
    int passed;

    prepare_memory(program, sizeof(program));
    prepare_context(&context, 0x5au, 0xefu, FLAG_U | FLAG_I | FLAG_C);
    passed = execute_context(&context, &result);
    passed = passed && check_common(
        &context, result, TEST_PC + sizeof(program), 10u, 4u,
        0x00u, FLAG_U | FLAG_I | FLAG_Z | FLAG_C
    ) && context.sp == 0xefu && g_ram[0x23u] == 0x78u &&
        g_ram[0x24u] == 0x00u && hits_are_exact(SUPER_LOAD_OPER2_IMM16);
    record_case(case_index, &context, result, passed);
    return passed;
}

static int run_stack_add(u32 case_index)
{
    static const u8 program[] = {
        0x08u, 0x78u, 0x18u, 0xa5u, 0x28u, 0x69u, 0x03u, 0x85u,
        0x28u, 0xa5u, 0x29u, 0x69u, 0x01u, 0x85u, 0x29u, 0x28u
    };
    s6502_iram_asm_context_t context;
    u32 result;
    int passed;

    prepare_memory(program, sizeof(program));
    g_ram[0x28u] = 0xfeu;
    g_ram[0x29u] = 0x12u;
    prepare_context(&context, 0x5au, 0xf0u, FLAG_I | FLAG_C);
    passed = execute_context(&context, &result);
    passed = passed && check_common(
        &context, result, TEST_PC + sizeof(program), 27u, 10u,
        0x14u, FLAG_B | FLAG_U | FLAG_I | FLAG_C
    ) && context.sp == 0xf0u && g_ram[0x28u] == 0x01u &&
        g_ram[0x29u] == 0x14u && g_ram[0x1f0u] ==
            (FLAG_B | FLAG_U | FLAG_I | FLAG_C) &&
        hits_are_exact(SUPER_STACK_ADD16);
    record_case(case_index, &context, result, passed);
    return passed;
}

static int run_stack_sub(u32 case_index)
{
    static const u8 program[] = {
        0x08u, 0x78u, 0x38u, 0xa5u, 0x28u, 0xe9u, 0x01u, 0x85u,
        0x28u, 0xa5u, 0x29u, 0xe9u, 0x00u, 0x85u, 0x29u, 0x28u
    };
    s6502_iram_asm_context_t context;
    u32 result;
    int passed;

    prepare_memory(program, sizeof(program));
    g_ram[0x28u] = 0x00u;
    g_ram[0x29u] = 0x10u;
    prepare_context(&context, 0x5au, 0xeeu, FLAG_I);
    passed = execute_context(&context, &result);
    passed = passed && check_common(
        &context, result, TEST_PC + sizeof(program), 27u, 10u,
        0x0fu, FLAG_B | FLAG_U | FLAG_I
    ) && context.sp == 0xeeu && g_ram[0x28u] == 0xffu &&
        g_ram[0x29u] == 0x0fu && g_ram[0x1eeu] ==
            (FLAG_B | FLAG_U | FLAG_I) &&
        hits_are_exact(SUPER_STACK_SUB16);
    record_case(case_index, &context, result, passed);
    return passed;
}

static int run_add16(u32 case_index)
{
    static const u8 program[] = {
        0x18u, 0xa5u, 0x20u, 0x65u, 0x23u, 0x85u, 0x20u,
        0xa5u, 0x21u, 0x65u, 0x24u, 0x85u, 0x21u
    };
    s6502_iram_asm_context_t context;
    u32 result;
    int passed;

    prepare_memory(program, sizeof(program));
    g_ram[0x20u] = 0xffu;
    g_ram[0x21u] = 0x7fu;
    g_ram[0x23u] = 0x01u;
    g_ram[0x24u] = 0x00u;
    prepare_context(&context, 0x5au, 0xedu, FLAG_U | FLAG_I);
    passed = execute_context(&context, &result);
    passed = passed && check_common(
        &context, result, TEST_PC + sizeof(program), 20u, 7u,
        0x80u, FLAG_N | FLAG_V | FLAG_U | FLAG_I
    ) && context.sp == 0xedu && g_ram[0x20u] == 0x00u &&
        g_ram[0x21u] == 0x80u && g_ram[0x23u] == 0x01u &&
        g_ram[0x24u] == 0x00u && hits_are_exact(SUPER_ADD16_OPER1_OPER2);
    record_case(case_index, &context, result, passed);
    return passed;
}

static int run_decimal_fallback(u32 case_index)
{
    static const u8 program[] = {
        0x18u, 0xa5u, 0x20u, 0x65u, 0x23u, 0x85u, 0x20u,
        0xa5u, 0x21u, 0x65u, 0x24u, 0x85u, 0x21u
    };
    s6502_iram_asm_context_t context;
    u32 result;
    int passed;

    prepare_memory(program, sizeof(program));
    g_ram[0x20u] = 0x34u;
    g_ram[0x21u] = 0x12u;
    g_ram[0x23u] = 0x78u;
    g_ram[0x24u] = 0x56u;
    prepare_context(
        &context, 0x5au, 0xecu, FLAG_U | FLAG_D | FLAG_I | FLAG_C
    );
    passed = execute_context(&context, &result);
    passed = passed && result == 0u && context.pc == TEST_PC &&
        context.cycles == 0u && context.instructions == 0u &&
        context.control_transitions == 0u &&
        context.exit_reason == S6502_IRAM_EXIT_SLOW &&
        context.ac == 0x5au && context.ix == 0x66u &&
        context.iy == 0x77u && context.sp == 0xecu &&
        context.status == (FLAG_U | FLAG_D | FLAG_I | FLAG_C) &&
        g_ram[0x20u] == 0x34u && g_ram[0x21u] == 0x12u &&
        g_ram[0x23u] == 0x78u && g_ram[0x24u] == 0x56u &&
        g_dirty == 0u && hits_are_zero();
    if (passed)
        ++g_super_equiv_report.decimal_fallbacks;
    record_case(case_index, &context, result, passed);
    return passed;
}

static int run_cross_page_lda(u32 case_index)
{
    static const u8 program[] = {0xadu, 0x34u, 0x12u, 0x00u};
    s6502_iram_asm_context_t context;
    u32 result;
    int passed;

    prepare_memory(program, 0u);
    copy_bytes(g_ram + 0x40feu, program, sizeof(program));
    g_code_pages[0x40u] = g_ram + 0x4000u;
    g_code_pages[0x41u] = g_ram + 0x4100u;
    g_ram[0x1234u] = 0x80u;
    prepare_context(&context, 0x5au, 0xebu, FLAG_U | FLAG_I | FLAG_C);
    context.pc = 0x40feu;
    passed = execute_context(&context, &result);
    passed = passed && check_common(
        &context, result, 0x4101u, 4u, 1u, 0x80u,
        FLAG_N | FLAG_U | FLAG_I | FLAG_C
    ) && context.sp == 0xebu && hits_are_zero();
    record_case(case_index, &context, result, passed);
    return passed;
}

static int run_adc_abs_x(u32 case_index)
{
    static const u8 program[] = {0x7du, 0x00u, 0x20u, 0x00u};
    s6502_iram_asm_context_t context;
    u32 result;
    int passed;

    prepare_memory(program, sizeof(program));
    g_code_pages[TEST_PAGE] = g_ram + (TEST_PAGE << 8);
    g_ram[0x2066u] = 0x50u;
    prepare_context(&context, 0x40u, 0xe7u, FLAG_U | FLAG_I | FLAG_C);
    passed = execute_context(&context, &result);
    passed = passed && check_common(
        &context, result, TEST_PC + 3u, 4u, 1u, 0x91u,
        FLAG_N | FLAG_V | FLAG_U | FLAG_I
    ) && context.sp == 0xe7u && hits_are_zero();
    record_case(case_index, &context, result, passed);
    return passed;
}

static int run_sbc_abs_x_cross(u32 case_index)
{
    static const u8 program[] = {0xfdu, 0xf0u, 0x20u, 0x00u};
    s6502_iram_asm_context_t context;
    u32 result;
    int passed;

    prepare_memory(program, sizeof(program));
    g_code_pages[TEST_PAGE] = g_ram + (TEST_PAGE << 8);
    g_ram[0x2156u] = 0x01u;
    prepare_context(&context, 0x10u, 0xe6u, FLAG_U | FLAG_I | FLAG_C);
    passed = execute_context(&context, &result);
    passed = passed && check_common(
        &context, result, TEST_PC + 3u, 5u, 1u, 0x0fu,
        FLAG_U | FLAG_I | FLAG_C
    ) && context.sp == 0xe6u && hits_are_zero();
    record_case(case_index, &context, result, passed);
    return passed;
}

static int run_sta_abs_x(u32 case_index)
{
    static const u8 program[] = {0x9du, 0x00u, 0x20u, 0x00u};
    s6502_iram_asm_context_t context;
    u32 result;
    int passed;

    prepare_memory(program, sizeof(program));
    g_code_pages[TEST_PAGE] = g_ram + (TEST_PAGE << 8);
    prepare_context(&context, 0xa5u, 0xe5u, FLAG_U | FLAG_I | FLAG_C);
    passed = execute_context(&context, &result);
    passed = passed && check_common(
        &context, result, TEST_PC + 3u, 5u, 1u, 0xa5u,
        FLAG_U | FLAG_I | FLAG_C
    ) && context.sp == 0xe5u && g_ram[0x2066u] == 0xa5u &&
        hits_are_zero();
    record_case(case_index, &context, result, passed);
    return passed;
}

static int run_cross_page_jmp(u32 case_index)
{
    static const u8 program[] = {0x4cu, 0x34u, 0x12u};
    s6502_iram_asm_context_t context;
    u32 result;
    int passed;

    prepare_memory(program, 0u);
    copy_bytes(g_ram + 0x40feu, program, sizeof(program));
    g_code_pages[0x40u] = g_ram + 0x4000u;
    g_code_pages[0x41u] = g_ram + 0x4100u;
    prepare_context(&context, 0x5au, 0xe4u, FLAG_U | FLAG_I | FLAG_C);
    context.pc = 0x40feu;
    passed = execute_context(&context, &result);
    passed = passed && result == 3u && context.pc == 0x1234u &&
        context.cycles == 3u && context.instructions == 1u &&
        context.control_transitions == 1u &&
        context.exit_reason == S6502_IRAM_EXIT_SLOW &&
        context.ac == 0x5au && context.ix == 0x66u &&
        context.iy == 0x77u && context.sp == 0xe4u &&
        context.status == (FLAG_U | FLAG_I | FLAG_C) &&
        g_dirty == 0u && hits_are_zero();
    record_case(case_index, &context, result, passed);
    return passed;
}

static int run_pb_direct_write(u32 case_index)
{
    static const u8 program[] = {
        0xa9u, 0x7fu, 0x8du, 0x1bu, 0x02u, 0x00u
    };
    s6502_iram_asm_context_t context;
    u32 result;
    int passed;

    prepare_memory(program, sizeof(program));
    g_code_pages[TEST_PAGE] = g_ram + (TEST_PAGE << 8);
    prepare_context(&context, 0x5au, 0xeau, FLAG_U | FLAG_I | FLAG_C);
    passed = execute_context(&context, &result);
    passed = passed && check_common(
        &context, result, TEST_PC + 5u, 6u, 2u, 0x7fu,
        FLAG_U | FLAG_I | FLAG_C
    ) && context.sp == 0xeau && g_ram[0x021bu] == 0u && hits_are_zero();
    record_case(case_index, &context, result, passed);
    return passed;
}

static int run_apo_direct_write(u32 case_index)
{
    static const u8 program[] = {
        0xa9u, 0x00u, 0x8du, 0x28u, 0x20u, 0x00u
    };
    s6502_iram_asm_context_t context;
    u32 result;
    int passed;

    prepare_memory(program, sizeof(program));
    g_code_pages[TEST_PAGE] = g_ram + (TEST_PAGE << 8);
    g_ram[0x2028u] = 0xffu;
    prepare_context(&context, 0x5au, 0xe9u, FLAG_U | FLAG_I | FLAG_C);
    passed = execute_context(&context, &result);
    passed = passed && check_common(
        &context, result, TEST_PC + 5u, 6u, 2u, 0x00u,
        FLAG_U | FLAG_I | FLAG_Z | FLAG_C
    ) && context.sp == 0xe9u && g_ram[0x2028u] == 0xffu &&
        hits_are_zero();
    record_case(case_index, &context, result, passed);
    return passed;
}

static int run_native_shared_7c30(u32 case_index)
{
    static const u8 program[] = {
        0x0eu, 0x89u, 0x20u, 0x2eu, 0x85u, 0x20u, 0x90u, 0x0fu,
        0x18u, 0xadu, 0x87u, 0x20u, 0x6du, 0x89u, 0x20u, 0x8du,
        0x89u, 0x20u, 0x90u, 0x03u, 0xeeu, 0x85u, 0x20u, 0x88u,
        0xd0u, 0xe6u, 0x02u
    };
    s6502_iram_asm_context_t context;
    u32 result;
    int passed;

    prepare_memory(program, 0u);
    copy_bytes(g_ram + 0x7c30u, program, sizeof(program));
    g_code_pages[0x7cu] = g_ram + 0x7c00u;
    g_dispatch[0x7c30u] = 1u;
    g_dispatch[0x7c38u] = 1u;
    g_dispatch[0x7c44u] = 1u;
    g_dispatch[0x7c47u] = 1u;
    g_ram[0x2085u] = 0x80u;
    g_ram[0x2087u] = 0x00u;
    g_ram[0x2089u] = 0x80u;
    prepare_context(&context, 0x5au, 0xe8u, FLAG_U | FLAG_I);
    context.pc = 0x7c30u;
    context.iy = 1u;
    context.dispatch_bits = (u32)(unsigned long)g_dispatch;
    g_native_page_entries[0x7cu] =
        (u32)(unsigned long)(void *)test_native_dispatch;
    context.native_shared_entry =
        (u32)(unsigned long)g_native_page_entries;
    context.native_shared_metrics =
        (u32)(unsigned long)&g_native_metrics;

    passed = execute_context(&context, &result);
    passed = passed && result == 35u && context.pc == 0x7c4au &&
        context.cycles == 35u && context.instructions == 10u &&
        context.control_transitions == 3u &&
        context.exit_reason == S6502_IRAM_EXIT_SLOW &&
        context.ac == 0u && context.ix == 0x66u && context.iy == 0u &&
        context.sp == 0xe8u && context.status == (FLAG_U | FLAG_I | FLAG_Z) &&
        g_ram[0x2085u] == 1u && g_ram[0x2087u] == 0u &&
        g_ram[0x2089u] == 0u && g_dirty == 0u && hits_are_zero() &&
        g_native_metrics.calls == 3u &&
        g_native_metrics.blocks == 3u &&
        g_native_metrics.guest_cycles == 35u &&
        g_native_metrics.entries_7c30 == 1u &&
        g_native_metrics.misses == 0u;
    record_case(case_index, &context, result, passed);
    return passed;
}

static void test_native_set_nz(s6502_iram_asm_context_t *context, u8 value)
{
    context->status &= ~(FLAG_N | FLAG_Z);
    context->status |= value & FLAG_N;
    if (!value)
        context->status |= FLAG_Z;
}

/* Model one generated disk-module entry with the normal S1C33 C ABI.  The
 * resident IRAM loop publishes state before the call, reloads it afterwards,
 * and accounts for the terminating branch in its common control tail. */
static u32 test_native_dispatch(s6502_iram_asm_context_t *context)
{
    T_NativeSharedMetrics *metrics =
        (T_NativeSharedMetrics *)(unsigned long)
            context->native_shared_metrics;
    u32 start_cycles = context->cycles;
    u32 start_pc = context->pc;
    u16 sum;
    u8 left;
    u8 right;
    u8 carry;

    if (metrics)
        ++metrics->calls;
    if ((context->status & FLAG_D) || (context->pc >> 8) != 0x7cu)
        goto miss;

    switch ((u8)context->pc) {
    case 0x30u:
        left = g_ram[0x2089u];
        carry = (u8)(left >> 7);
        g_ram[0x2089u] = (u8)(left << 1);
        left = g_ram[0x2085u];
        right = (u8)(left >> 7);
        g_ram[0x2085u] = (u8)((left << 1) | carry);
        context->status &= ~(FLAG_N | FLAG_Z | FLAG_C);
        context->status |= right ? FLAG_C : 0u;
        test_native_set_nz(context, g_ram[0x2085u]);
        context->pc = right ? 0x7c38u : 0x7c47u;
        context->cycles += right ? 14u : 15u;
        context->instructions += 2u;
        break;
    case 0x38u:
        left = g_ram[0x2087u];
        right = g_ram[0x2089u];
        sum = (u16)left + (u16)right;
        context->ac = (u8)sum;
        g_ram[0x2089u] = (u8)sum;
        context->status &= ~(FLAG_N | FLAG_V | FLAG_Z | FLAG_C);
        if (sum > 0xffu)
            context->status |= FLAG_C;
        if (((left ^ (u8)sum) & (right ^ (u8)sum) & 0x80u) != 0u)
            context->status |= FLAG_V;
        test_native_set_nz(context, (u8)sum);
        context->pc = sum > 0xffu ? 0x7c44u : 0x7c47u;
        context->cycles += sum > 0xffu ? 16u : 17u;
        context->instructions += 4u;
        break;
    case 0x44u:
        ++g_ram[0x2085u];
        context->iy = (u8)(context->iy - 1u);
        test_native_set_nz(context, (u8)context->iy);
        context->pc = context->iy ? 0x7c30u : 0x7c4au;
        context->cycles += context->iy ? 11u : 10u;
        context->instructions += 2u;
        break;
    case 0x47u:
        context->iy = (u8)(context->iy - 1u);
        test_native_set_nz(context, (u8)context->iy);
        context->pc = context->iy ? 0x7c30u : 0x7c4au;
        context->cycles += context->iy ? 5u : 4u;
        context->instructions += 1u;
        break;
    default:
        goto miss;
    }

    if (metrics) {
        ++metrics->blocks;
        metrics->guest_cycles += context->cycles - start_cycles;
        if (start_pc == 0x7c30u)
            ++metrics->entries_7c30;
    }
    return 1u;

miss:
    if (metrics)
        ++metrics->misses;
    return 0u;
}

static T_IramExec install_engine(void)
{
    unsigned long start =
        (unsigned long)(void *)&__iram_exec_engine_start;
    unsigned long end =
        (unsigned long)(void *)&__iram_exec_engine_end;
    unsigned long entry;

    if (end <= start)
        return (T_IramExec)0;
    entry = gam4980_9288_iram_begin_range(start, end - start);
    if (!entry)
        return (T_IramExec)0;
    return (T_IramExec)(void *)entry;
}

T_WORD App_Main(void)
{
    int passed = 1;

    clear_bytes(
        (volatile u8 *)&g_super_equiv_report,
        sizeof(g_super_equiv_report)
    );
    g_super_equiv_report.magic = REPORT_MAGIC;
    (void)fnGUI_MessageBox(
        HWND_DESKTOP,
        (const T_BYTE *)
            "True S1C33 IRAM v2 equivalence probe.\n"
            "Press OK, then wait for PASS/FAIL.",
        (const T_BYTE *)APP_TITLE, MB_OK
    );
    if (!gam4980_9288_iram_enter()) {
        g_super_equiv_report.fail_mask = 0x80000000u;
        g_super_equiv_report.iram_status =
            (u32)gam4980_9288_iram_status();
        g_super_equiv_report.completed = 1u;
        (void)fnGUI_MessageBox(
            HWND_DESKTOP, (const T_BYTE *)"IRAM setup failed.",
            (const T_BYTE *)APP_TITLE, MB_OK
        );
        return -1;
    }
    g_super_equiv_report.iram_size = gam4980_9288_iram_size();
    passed &= run_decimal_fallback(0u);
    passed &= run_cross_page_lda(1u);
    passed &= run_pb_direct_write(2u);
    passed &= run_apo_direct_write(3u);
    passed &= run_adc_abs_x(4u);
    passed &= run_sbc_abs_x_cross(5u);
    passed &= run_sta_abs_x(6u);
    passed &= run_cross_page_jmp(7u);
    g_super_equiv_report.iram_status =
        (u32)gam4980_9288_iram_status();
    if (g_super_equiv_report.iram_status != GAM4980_IRAM_STATUS_RESTORED) {
        g_super_equiv_report.fail_mask |= 0x20000000u;
        passed = 0;
    }
    if (g_super_equiv_report.cases_run != TEST_CASES ||
        g_super_equiv_report.cases_passed != TEST_CASES ||
        g_super_equiv_report.decimal_fallbacks != 1u)
        passed = 0;
    g_super_equiv_report.passed = passed ? 1u : 0u;
    g_super_equiv_report.completed = 1u;

    (void)fnGUI_MessageBox(
        HWND_DESKTOP,
        (const T_BYTE *)(passed ?
            "IRAM v2 opcode equivalence: PASS" :
            "IRAM v2 opcode equivalence: FAIL"),
        (const T_BYTE *)APP_TITLE, MB_OK
    );
    return passed ? 0 : -3;
}
