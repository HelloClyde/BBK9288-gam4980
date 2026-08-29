#include "gam4980_core.h"

#ifdef GAM4980_ENABLE_GAME_LOAD_AOT
#include "s6502_c6502_spec_generated.h"
#endif

typedef gam4980_bool_t bool;
#define false GAM4980_FALSE
#define true GAM4980_TRUE

static gam4980_load_progress_fn core_load_progress_callback;
static void *core_load_progress_context;

static void *gam4980_memset(void *destination, int value, u32 size)
{
    u8 *out = (u8 *)destination;

    while (size--)
        *out++ = (u8)value;
    return destination;
}

static void *gam4980_memcpy(void *destination, const void *source, u32 size)
{
    u8 *out = (u8 *)destination;
    const u8 *in = (const u8 *)source;

    while (size--)
        *out++ = *in++;
    return destination;
}

static void gam4980_report_load_progress(
    u32 stage, u32 current, u32 total
)
{
    if (core_load_progress_callback)
        core_load_progress_callback(
            core_load_progress_context, stage, current, total
        );
}

#define _DATA1          0x00
#define _DATA2          0x01
#define _DATA3          0x02
#define _DATA4          0x03
#define _ISR            0x04
#define _TISR           0x05
#define _BK_SEL         0x0c
#define _BK_ADRL        0x0d
#define _BK_ADRH        0x0e
#define _IRCNT          0x1b
#define __oper1         0x20
#define __oper2         0x23
#define __addr_reg      0x26
#define _SYSCON         0x200
#define _INCR           0x207
#define _ADDR1L         0x208
#define _ADDR1M         0x209
#define _ADDR1H         0x20a
#define _ADDR2L         0x20b
#define _ADDR2M         0x20c
#define _ADDR2H         0x20d
#define _ADDR3L         0x20e
#define _ADDR3M         0x20f
#define _ADDR3H         0x210
#define _ADDR4L         0x211
#define _ADDR4M         0x212
#define _ADDR4H         0x213
#define _PB             0x21b
#define _STCON          0x226
#define _ST1LD          0x227
#define _ST2LD          0x228
#define _ST3LD          0x229
#define _ST4LD          0x22a
#define _MTCT           0x22b
#define _STCTCON        0x22e
#define _CTLD           0x22f
#define _ALMMIN         0x230
#define _ALMHR          0x231
#define _ALMDAYL        0x232
#define _ALMDAYH        0x233
#define _RTCSEC         0x234
#define _RTCMIN         0x235
#define _RTCHR          0x236
#define _RTCDAYL        0x237
#define _RTCDAYH        0x238
#define _IER            0x23a
#define _TIER           0x23b
#define _AUDCON         0x23f
#define _KEYCODE        0x24e
#define _MACCTL         0x260
#define _KeyBuffTop     0x2003
#define _KeyBuffBottom  0x2004
#define _KeyBuffer      0x2008

#define LCD_WIDTH GAM4980_LCD_WIDTH
#define LCD_HEIGHT GAM4980_LCD_HEIGHT
#define LCD_STRIDE GAM4980_LCD_STRIDE
#define LCD_PACKED_STRIDE GAM4980_LCD_PACKED_STRIDE

static uint16_t *fb;
static int shutdown_requested;
static uint16_t shutdown_pc;
static uint32_t step_cycles;
static uint32_t step_ticked;
static uint32_t step_cycle_fraction;
static uint32_t rtc_frames;
static uint32_t timer_ticks[5];
static uint32_t lcd_nibble_lut[16][2];
static uint8_t lcd_frame[GAM4980_LCD_PACKED_SIZE];
static int lcd_frame_valid;
static int lcd_dirty;
static uint32_t lcd_changed_rows[3];
static int save_dirty;
#ifdef GAM4980_RUNTIME_PERFORMANCE_LOG
static uint32_t performance_lcd_write_calls;
static uint32_t performance_lcd_changed_writes;
#endif


static void sys_isr(void);
static bool sys_halt_p(void);
static void mem_bs(uint8_t sel);
static uint8_t mem_read(uint16_t addr);
static uint8_t mem_readx(uint16_t addr);
static uint16_t mem_read16(uint16_t addr);
static uint16_t mem_readx16(uint16_t addr);
static uint16_t mem_read16_wrapped(uint16_t addr);
static void mem_write(uint16_t addr, uint8_t val);
#ifndef GAM4980_CACHE_STORAGE
#if defined(GAM4980_TARGET_9288)
/* gam4980_core.c is compiled as a separate object on 9288, so the frontend's
 * local definition is not visible here.  These buffers are completely
 * initialized before use and belong in the loader-reserved scratch range,
 * not in the byte-at-a-time startup .bss clear. */
#define GAM4980_CACHE_STORAGE \
    __attribute__((aligned(4), section(".scratch")))
#else
#define GAM4980_CACHE_STORAGE
#endif
#endif
#if (defined(GAM4980_ENABLE_AOT) && defined(GAM4980_AOT_DIAGNOSTICS)) || \
    defined(GAM4980_RUNTIME_PERFORMANCE_LOG) || \
    defined(GAM4980_ENABLE_FIRMWARE_HLE)
#ifdef GAM4980_LIGHTWEIGHT_PERFORMANCE_LOG
/* Compile every hot-path diagnostic branch away.  The 9288 frontend keeps
 * only coarse RTC/frame counters for a release-speed benchmark. */
#define s6502_performance_debug 0
#else
static int s6502_performance_debug;
#endif
#endif
#ifdef GAM4980_ENABLE_GAME_LOAD_AOT
#ifndef GAM4980_ENABLE_AOT
#error GAM4980_ENABLE_GAME_LOAD_AOT requires GAM4980_ENABLE_AOT
#endif
#define S6502_GAME_AOT_MAX_ENTRIES 4096u
#define S6502_GAME_AOT_HASH_SIZE 8192u
#define S6502_GAME_AOT_CFG_LIMIT 0x80000u
#define S6502_GAME_AOT_CFG_BYTES (S6502_GAME_AOT_CFG_LIMIT / 8u)
#define S6502_GAME_AOT_CFG_QUEUE_SIZE 4096u
typedef struct s6502_game_aot_entry {
    uint32_t physical_pc;
    uint32_t linked_physical_pc;
    uint16_t linked_table_address;
    uint16_t linked_virtual_pc;
    uint8_t pattern;
    uint8_t size;
    uint8_t semantic;
    uint8_t memory_class;
    uint8_t cycle_cost;
    uint8_t linked_bank_number;
} s6502_game_aot_entry_t;
typedef struct s6502_game_aot_cfg_item {
    uint32_t offset;
    uint16_t virtual_pc;
} s6502_game_aot_cfg_item_t;
static s6502_game_aot_entry_t
    s6502_game_aot_entries[S6502_GAME_AOT_MAX_ENTRIES]
    GAM4980_CACHE_STORAGE;
static uint16_t s6502_game_aot_hash[S6502_GAME_AOT_HASH_SIZE]
    GAM4980_CACHE_STORAGE;
static uint8_t s6502_game_aot_cfg_bits[S6502_GAME_AOT_CFG_BYTES]
    GAM4980_CACHE_STORAGE;
static s6502_game_aot_cfg_item_t
    s6502_game_aot_cfg_queue[S6502_GAME_AOT_CFG_QUEUE_SIZE]
    GAM4980_CACHE_STORAGE;
static uint16_t s6502_game_aot_entry_count;
static uint16_t s6502_game_aot_bank_mask;
static uint16_t *s6502_game_aot_banks;
static uint32_t s6502_game_aot_physical_end;
static uint32_t s6502_game_aot_storage_end;
static uint16_t s6502_game_aot_semantic_count;
static uint16_t s6502_game_aot_linked_call_count;
static uint32_t s6502_game_aot_direct_link_hits;
#ifdef GAM4980_AOT_DIAGNOSTICS
static uint32_t s6502_game_aot_direct_link_stage_hits[5];
#define S6502_GAME_AOT_DIRECT_LINK_STAGE(stage) \
    (++s6502_game_aot_direct_link_stage_hits[(stage)])
#else
#define S6502_GAME_AOT_DIRECT_LINK_STAGE(stage) ((void)0)
#endif
static uint16_t s6502_game_aot_reachable_count;
static uint32_t s6502_game_aot_code_size;
static const uint8_t *s6502_game_aot_code_base;
static int s6502_game_aot_enabled;
static int s6502_game_aot_requested = 1;
static int s6502_game_aot_direct_links_enabled = 1;
static uint8_t s6502_game_aot_direct_link_validation;
static uint32_t s6502_game_aot_semantic_mask = 0xffffffffu;
static int s6502_game_aot_metrics_enabled;
static uint32_t
    s6502_game_aot_semantic_hits[C6502_TEMPLATE_SPEC_COUNT];
static uint16_t s6502_game_aot_entry_limit = S6502_GAME_AOT_MAX_ENTRIES;
static void s6502_game_aot_prepare(const uint8_t *game, uint32_t size);
static void s6502_game_aot_invalidate(uint32_t offset, uint32_t size);
static int s6502_game_aot_direct_link_match(void);
#ifdef GAM4980_ENABLE_FIRMWARE_HLE
#define S6502_GAME_HLE_MAX_MATCHES 8u
typedef struct s6502_game_hle_counter {
    uint32_t physical_pc;
    uint16_t virtual_pc;
    uint16_t exit_pc;
    uint8_t pointer_zp;
    uint8_t index;
    uint8_t limit_low;
    uint8_t limit_high;
    uint8_t value_high;
    uint8_t increment;
} s6502_game_hle_counter_t;
typedef struct s6502_game_hle_bitmap {
    uint32_t physical_pc;
    uint32_t outer_physical_pc;
    uint32_t row_physical_pc;
    uint16_t virtual_pc;
    uint16_t outer_virtual_pc;
    uint16_t outer_exit_pc;
    uint16_t function_exit_pc;
    uint16_t and_table;
    uint16_t or_table;
    uint8_t source_zp;
    uint8_t accumulator_zp;
    uint8_t destination_index_zp;
    uint8_t destination_pointer_zp;
    uint8_t subpixel_zp;
    uint8_t source_index_zp;
    uint8_t source_pointer_zp;
    uint8_t width_zp;
    uint8_t initial_x_zp;
    uint8_t row_count_zp;
    uint8_t height_zp;
    uint8_t vertical_zp;
    uint8_t row_stride;
} s6502_game_hle_bitmap_t;
typedef struct s6502_game_hle_scan {
    uint32_t physical_pc;
    uint16_t virtual_pc;
    uint16_t special_pc;
    uint16_t exit_pc;
    uint8_t object_pointer_zp;
    uint8_t temporary_pointer_zp;
    uint8_t index_offset;
    uint8_t limit_offset;
    uint8_t array_pointer_offset;
} s6502_game_hle_scan_t;
typedef struct s6502_game_hle_record_scan {
    uint32_t physical_pc;
    uint16_t virtual_pc;
    uint16_t exit_pc;
    uint8_t object_pointer_zp;
    uint8_t index_offset;
    uint8_t data_pointer_offset;
    uint8_t candidate_offset;
    uint8_t reference_pointer_offset;
    uint8_t found_offset;
} s6502_game_hle_record_scan_t;
typedef struct s6502_game_hle_record_reverse {
    uint32_t physical_pc;
    uint16_t virtual_pc;
    uint16_t success_pc;
    uint16_t failure_pc;
    uint16_t error_address;
    uint8_t object_pointer_zp;
    uint8_t index_offset;
    uint8_t data_pointer_offset;
    uint8_t candidate_offset;
    uint8_t reference_pointer_offset;
    uint8_t found_offset;
    uint8_t error_value;
} s6502_game_hle_record_reverse_t;
typedef struct s6502_game_hle_table_chain {
    uint32_t physical_pc;
    uint16_t firmware_pc;
    uint8_t field_offset;
    uint8_t output_offset;
} s6502_game_hle_table_chain_t;
typedef struct s6502_game_hle_object_flow {
    uint32_t physical_pc;
    uint16_t firmware_pc;
    uint16_t exit_firmware_pc;
    uint8_t object_pointer_zp;
    uint8_t first_pointer_offset;
    uint8_t first_value_offset;
    uint8_t first_output_offset;
    uint8_t second_pointer_offset;
    uint8_t second_value_offset;
    uint8_t second_output_offset;
    uint8_t index_left_offset;
    uint8_t index_right_offset;
    uint8_t table_pointer_offset;
    uint8_t table_bias;
    uint8_t output_offset;
} s6502_game_hle_object_flow_t;
typedef struct s6502_hle_game_scan_result {
    uint32_t cycles;
    uint32_t iterations;
    uint16_t pc;
    uint8_t ac;
    uint8_t iy;
    uint8_t status;
} s6502_hle_game_scan_result_t;
typedef struct s6502_hle_game_table_result {
    uint32_t cycles;
    uint16_t pc;
    uint8_t ac;
    uint8_t ix;
    uint8_t iy;
    uint8_t sp;
    uint8_t status;
} s6502_hle_game_table_result_t;
static s6502_game_hle_counter_t
    s6502_game_hle_counters[S6502_GAME_HLE_MAX_MATCHES];
static s6502_game_hle_bitmap_t
    s6502_game_hle_bitmaps[S6502_GAME_HLE_MAX_MATCHES];
static s6502_game_hle_scan_t
    s6502_game_hle_scans[S6502_GAME_HLE_MAX_MATCHES];
static s6502_game_hle_record_scan_t
    s6502_game_hle_record_scans[S6502_GAME_HLE_MAX_MATCHES];
static s6502_game_hle_record_reverse_t
    s6502_game_hle_record_reverses[S6502_GAME_HLE_MAX_MATCHES];
static s6502_game_hle_table_chain_t
    s6502_game_hle_table_chains[S6502_GAME_HLE_MAX_MATCHES];
static s6502_game_hle_object_flow_t
    s6502_game_hle_object_flows[S6502_GAME_HLE_MAX_MATCHES];
static uint8_t s6502_game_hle_counter_count;
static uint8_t s6502_game_hle_bitmap_count;
static uint8_t s6502_game_hle_scan_count;
static uint8_t s6502_game_hle_record_scan_count;
static uint8_t s6502_game_hle_record_reverse_count;
static uint8_t s6502_game_hle_table_chain_count;
static uint8_t s6502_game_hle_object_flow_count;
static s6502_hle_game_scan_result_t s6502_game_hle_scan_result;
static s6502_hle_game_table_result_t s6502_game_hle_table_result;
static void s6502_game_hle_prepare(const uint8_t *game, uint32_t size);
static const s6502_game_hle_counter_t *
s6502_game_hle_find_counter(uint32_t physical_pc);
static const s6502_game_hle_bitmap_t *
s6502_game_hle_find_bitmap(uint32_t physical_pc);
static const s6502_game_hle_scan_t *
s6502_game_hle_find_scan(uint32_t physical_pc);
static const s6502_game_hle_record_scan_t *
s6502_game_hle_find_record_scan(uint32_t physical_pc);
static const s6502_game_hle_record_reverse_t *
s6502_game_hle_find_record_reverse(uint32_t physical_pc);
static const s6502_game_hle_table_chain_t *
s6502_game_hle_find_table_chain(uint32_t physical_pc);
static const s6502_game_hle_object_flow_t *
s6502_game_hle_find_object_flow(uint32_t physical_pc);
#endif
#ifdef GAM4980_AOT_DIAGNOSTICS
static uint64_t s6502_game_aot_instruction_hits;
static uint64_t s6502_game_aot_entry_hits[S6502_GAME_AOT_MAX_ENTRIES]
    GAM4980_CACHE_STORAGE;
static __attribute__((noinline)) void s6502_game_aot_hit(
    uint32_t entry_id, uint32_t instructions
);
#define S6502_GAME_AOT_HIT(instructions) do {                              \
    if (s6502_performance_debug)                                            \
        s6502_game_aot_hit(game_aot_entry_id - 1u, (instructions));         \
} while (0)
#else
#define S6502_GAME_AOT_HIT(instructions) ((void)0)
#endif
#define S6502_GAME_AOT_SEMANTIC_HIT(kind) do {                            \
    if (s6502_game_aot_metrics_enabled) {                                  \
        ++s6502_game_aot_semantic_hits[(kind) - 1u];                       \
    }                                                                      \
} while (0)
#define S6502_GAME_AOT_DISPATCH() do {                                      \
    if (s6502_game_aot_enabled &&                                           \
        (s6502_game_aot_bank_mask & (uint16_t)(1u << (pc >> 12)))) {       \
        game_aot_physical_pc =                                              \
            ((uint32_t)s6502_game_aot_banks[pc >> 12] << 12) |             \
            (pc & 0x0fffu);                                                 \
        S6502_GAME_HLE_DISPATCH();                                          \
        if (!s6502_game_aot_requested)                                     \
            break;                                                         \
        game_aot_hash_slot = (uint16_t)(                                   \
            (game_aot_physical_pc ^ (game_aot_physical_pc >> 9) ^          \
                (game_aot_physical_pc >> 18)) &                            \
            (S6502_GAME_AOT_HASH_SIZE - 1u)                                \
        );                                                                  \
        game_aot_entry_id = s6502_game_aot_hash[game_aot_hash_slot];       \
        while (game_aot_entry_id &&                                        \
            s6502_game_aot_entries[game_aot_entry_id - 1u].physical_pc !=  \
                game_aot_physical_pc) {                                     \
            game_aot_hash_slot = (uint16_t)(                               \
                (game_aot_hash_slot + 1u) &                                \
                (S6502_GAME_AOT_HASH_SIZE - 1u)                            \
            );                                                              \
            game_aot_entry_id = s6502_game_aot_hash[game_aot_hash_slot];   \
        }                                                                   \
        if (game_aot_entry_id) {                                            \
            if (game_aot_entry_id > s6502_game_aot_entry_limit)            \
                break;                                                     \
            game_aot_entry =                                               \
                &s6502_game_aot_entries[game_aot_entry_id - 1u];           \
            if (game_aot_entry->semantic &&                                \
                !(s6502_game_aot_semantic_mask &                           \
                    (1u << (game_aot_entry->semantic - 1u))))              \
                break;                                                     \
            game_aot_code = s6502_game_aot_code_base +                    \
                (game_aot_entry->physical_pc - 0x20d000u);                 \
            goto _game_aot_dispatch;                                       \
        }                                                                   \
    }                                                                       \
} while (0)
#define S6502_GAME_AOT_EMIT_BLOCKS
#endif
#ifdef GAM4980_ENABLE_AOT
static __attribute__((noinline)) int s6502_aot_match(uint32_t block_id);
static __attribute__((noinline)) int s6502_aot_validate(uint32_t block_id);
#ifdef GAM4980_AOT_DIAGNOSTICS
static __attribute__((noinline)) void s6502_aot_hit(
    uint32_t block_id, uint32_t instructions
);
#define S6502_AOT_HIT(id, instructions) do {                               \
    if (s6502_performance_debug)                                            \
        s6502_aot_hit((id), (instructions));                                \
} while (0)
#else
#define S6502_AOT_HIT(id, instructions) ((void)0)
#endif
#define S6502_AOT_DEFINE_DATA
#include "s6502_aot_ebin_generated.h"
#undef S6502_AOT_DEFINE_DATA
static uint8_t s6502_aot_validation[S6502_AOT_BLOCK_COUNT];
#define S6502_AOT_DEFINE_DISPATCH
#include "s6502_aot_ebin_generated.h"
#undef S6502_AOT_DEFINE_DISPATCH
#endif
#ifdef GAM4980_ENABLE_FIRMWARE_HLE
#ifndef GAM4980_ENABLE_AOT
#error GAM4980_ENABLE_FIRMWARE_HLE requires GAM4980_ENABLE_AOT
#endif
typedef struct s6502_resource_span_cache {
    uint16_t address;
    uint32_t size;
    uint8_t *pointer;
    uint8_t *first_page;
    uint8_t *last_page;
    uint8_t valid;
} s6502_resource_span_cache_t;
#define S6502_RESOURCE_SPAN_CACHE_SIZE 32u
static s6502_resource_span_cache_t
    s6502_resource_span_cache[S6502_RESOURCE_SPAN_CACHE_SIZE]
    GAM4980_CACHE_STORAGE;
static uint32_t s6502_resource_span_cache_hits;
static uint32_t s6502_resource_span_cache_misses;
#ifndef GAM4980_FIRMWARE_HLE_MASK
#define GAM4980_FIRMWARE_HLE_MASK 0x3ffu
#endif
#define S6502_HLE_BITMAP_COPY 0x01u
#define S6502_HLE_GLYPH_ROW   0x02u
#define S6502_HLE_SHIFT_BLIT  0x04u
#define S6502_HLE_BYTE_FILL   0x08u
#define S6502_HLE_WIDE_GLYPH  0x10u
#define S6502_HLE_MULTIPLY16   0x20u
#define S6502_HLE_COMPARE16    0x40u
#define S6502_HLE_BANK_SWITCH  0x80u
#define S6502_HLE_C_RUNTIME    0x100u
#define S6502_HLE_GRAPHICS_ADDRESS 0x200u
#define S6502_HLE_DIAGNOSTIC_COUNT 28u
#define S6502_HLE_ID_BITMAP_COPY   0u
#define S6502_HLE_ID_WIDE_GLYPH    1u
#define S6502_HLE_ID_GLYPH_ROW     2u
#define S6502_HLE_ID_SHIFT_BLIT    3u
#define S6502_HLE_ID_BYTE_FILL     4u
#define S6502_HLE_ID_MULTIPLY16    5u
#define S6502_HLE_ID_COMPARE16     6u
#define S6502_HLE_ID_GAME_COUNTER  7u
#define S6502_HLE_ID_GAME_BITMAP   8u
#define S6502_HLE_ID_BITMAP_REGION 9u
#define S6502_HLE_ID_SHIFT_REGION  10u
#define S6502_HLE_ID_GAME_SCAN     11u
#define S6502_HLE_ID_GAME_RECORD_SCAN 12u
#define S6502_HLE_ID_GAME_TABLE_CHAIN 13u
#define S6502_HLE_ID_GAME_OBJECT_FLOW 14u
#define S6502_HLE_ID_GAME_RECORD_REVERSE 15u
#define S6502_HLE_ID_BANK_SWITCH 16u
#define S6502_HLE_ID_AND_LONG 17u
#define S6502_HLE_ID_LOAD_OPER1_TEMP 18u
#define S6502_HLE_ID_COMPARE_LONG 19u
#define S6502_HLE_ID_INDIRECT_CALL 20u
#define S6502_HLE_ID_PICTURE_TAIL 21u
#define S6502_HLE_ID_PICTURE_HEAD 22u
#define S6502_HLE_ID_PICTURE_RESUME 23u
#define S6502_HLE_ID_GRAPHICS_ADDRESS 24u
#define S6502_HLE_ID_HLINE_MIDDLE 25u
#define S6502_HLE_ID_PART_PICTURE_ROW 26u
#define S6502_HLE_ID_PIXEL_TAIL 27u
static int s6502_firmware_hle_enabled;
static uint8_t s6502_firmware_hle_validation;
static uint8_t s6502_firmware_hle_glyph_validation;
static uint8_t s6502_firmware_hle_bitmap_validation;
static uint8_t s6502_firmware_hle_bitmap_region_validation;
static uint8_t s6502_firmware_hle_shift_region_validation;
static uint8_t s6502_firmware_hle_picture_head_validation;
static uint8_t s6502_firmware_hle_graphics_address_validation;
static uint8_t s6502_firmware_hle_hline_validation;
static uint8_t s6502_firmware_hle_part_picture_validation;
static uint8_t s6502_firmware_hle_pixel_tail_validation;
static uint8_t s6502_firmware_hle_fill_validation;
static uint8_t s6502_firmware_hle_wide_glyph_validation;
static uint8_t s6502_firmware_hle_multiply_validation;
static uint8_t s6502_firmware_hle_compare_validation;
static uint8_t s6502_firmware_hle_bank_switch_validation;
static uint8_t s6502_firmware_hle_c_runtime_validation;
static uint8_t s6502_firmware_hle_compare_long_validation;
static uint8_t s6502_firmware_hle_indirect_call_validation;
static uint8_t s6502_game_hle_object_flow_firmware_validation;
#ifdef GAM4980_PICTURE_TAIL_TEST_CONTROL
static int s6502_firmware_hle_picture_tail_test_enabled = 1;
static int s6502_firmware_hle_picture_tail_trace_mode;
static uint32_t s6502_firmware_hle_picture_tail_trace_count[2];
static uint16_t s6502_firmware_hle_picture_tail_trace_ea[2];
static uint16_t s6502_firmware_hle_picture_tail_trace_et[2];
static uint16_t s6502_firmware_hle_picture_tail_trace_executed[2];
static uint8_t s6502_firmware_hle_picture_tail_trace_dt[2];
#define S6502_PICTURE_TAIL_TEST_ENABLED \
    s6502_firmware_hle_picture_tail_test_enabled
#define S6502_PICTURE_TAIL_TEST_TRACE(ea_value, et_value, dt_value, cycles_value) \
    do {                                                                    \
        int trace_index_ = s6502_firmware_hle_picture_tail_trace_mode - 1;  \
        if (trace_index_ >= 0 && trace_index_ < 2 &&                        \
            !s6502_firmware_hle_picture_tail_trace_count[trace_index_]) {   \
            s6502_firmware_hle_picture_tail_trace_count[trace_index_] = 1u; \
            s6502_firmware_hle_picture_tail_trace_ea[trace_index_] =        \
                (uint16_t)(ea_value);                                       \
            s6502_firmware_hle_picture_tail_trace_et[trace_index_] =        \
                (uint16_t)(et_value);                                       \
            s6502_firmware_hle_picture_tail_trace_dt[trace_index_] =        \
                (uint8_t)(dt_value);                                        \
            s6502_firmware_hle_picture_tail_trace_executed[trace_index_] =  \
                (uint16_t)(cycles_value);                                   \
        }                                                                   \
    } while (0)
#else
#define S6502_PICTURE_TAIL_TEST_ENABLED 1
#define S6502_PICTURE_TAIL_TEST_TRACE(ea_value, et_value, dt_value, cycles_value) \
    ((void)0)
#endif
static uint32_t s6502_firmware_hle_hits;
static uint64_t s6502_firmware_hle_guest_cycles;
static uint32_t s6502_firmware_hle_attempts[S6502_HLE_DIAGNOSTIC_COUNT];
static uint32_t
    s6502_firmware_hle_path_hits[S6502_HLE_DIAGNOSTIC_COUNT];
static uint32_t
    s6502_firmware_hle_condition_rejects[S6502_HLE_DIAGNOSTIC_COUNT];
static uint32_t
    s6502_firmware_hle_budget_rejects[S6502_HLE_DIAGNOSTIC_COUNT];
static uint32_t
    s6502_firmware_hle_batch_groups[S6502_HLE_DIAGNOSTIC_COUNT];
static uint32_t
    s6502_firmware_hle_batch_iterations[S6502_HLE_DIAGNOSTIC_COUNT];
static uint32_t
    s6502_firmware_hle_batch_max[S6502_HLE_DIAGNOSTIC_COUNT];
static uint32_t
    s6502_firmware_hle_direct_groups[S6502_HLE_DIAGNOSTIC_COUNT];
static uint32_t
    s6502_firmware_hle_direct_iterations[S6502_HLE_DIAGNOSTIC_COUNT];
static uint64_t
    s6502_firmware_hle_path_guest_cycles[S6502_HLE_DIAGNOSTIC_COUNT];
static uint16_t *s6502_firmware_hle_banks;
static uint8_t **s6502_firmware_hle_mem_pages;
typedef struct s6502_hle_glyph_result {
    uint8_t ac;
    uint8_t ix;
    uint8_t iy;
    uint8_t status;
} s6502_hle_glyph_result_t;
typedef struct s6502_hle_compare_result {
    uint16_t pc;
    uint8_t ac;
    uint8_t ix;
    uint8_t sp;
    uint8_t status;
} s6502_hle_compare_result_t;
typedef struct s6502_hle_direct_result {
    uint32_t cycles;
    uint32_t hits;
    uint16_t pc;
    uint16_t ea;
    uint8_t ac;
    uint8_t ix;
    uint8_t iy;
    uint8_t dt;
    uint8_t status;
} s6502_hle_direct_result_t;
typedef struct s6502_hle_region_result {
    uint32_t cycles;
    uint32_t rows;
    uint16_t pc;
    uint8_t ac;
    uint8_t ix;
    uint8_t iy;
    uint8_t sp;
    uint8_t status;
} s6502_hle_region_result_t;
#ifdef GAM4980_ENABLE_AGGRESSIVE_REGION_HLE
typedef struct s6502_hle_shift_cache {
    uint8_t *source;
    uint8_t *destination;
    uint8_t *destination_1000;
    uint16_t source_address;
    uint16_t destination_address;
    uint16_t source_size;
    uint16_t destination_size;
    uint8_t valid;
} s6502_hle_shift_cache_t;
static s6502_hle_shift_cache_t s6502_hle_shift_cache;
#endif
/* The emulator core and its state are single-threaded; use one result slot so
 * the large dispatcher does not need another aggregate local. */
static s6502_hle_direct_result_t s6502_hle_direct_result;
#if defined(__clang__) && defined(__s1c33__)
#define S6502_HLE_GLYPH_ROW_ATTRIBUTE __attribute__((noinline, optnone))
#else
#define S6502_HLE_GLYPH_ROW_ATTRIBUTE __attribute__((noinline))
#endif
static __attribute__((noinline)) int s6502_firmware_hle_match(void);
static __attribute__((noinline)) int s6502_firmware_hle_glyph_match(void);
static __attribute__((noinline)) int s6502_firmware_hle_bitmap_match(void);
static __attribute__((noinline)) int
s6502_firmware_hle_bitmap_region_match(void);
static __attribute__((noinline)) int
s6502_firmware_hle_shift_region_match(void);
static __attribute__((noinline)) int
s6502_firmware_hle_picture_head_match(void);
static __attribute__((noinline)) int
s6502_firmware_hle_graphics_address_match(void);
static __attribute__((noinline)) int
s6502_firmware_hle_hline_match(void);
static __attribute__((noinline)) int
s6502_firmware_hle_part_picture_match(void);
static __attribute__((noinline)) int
s6502_firmware_hle_pixel_tail_match(void);
static __attribute__((noinline)) int s6502_firmware_hle_fill_match(void);
static __attribute__((noinline)) int s6502_firmware_hle_wide_glyph_match(void);
static __attribute__((noinline)) int s6502_firmware_hle_multiply_match(void);
static __attribute__((noinline)) int s6502_firmware_hle_compare_match(void);
static __attribute__((noinline)) int
s6502_firmware_hle_bank_switch_match(void);
static __attribute__((noinline)) int s6502_firmware_hle_c_runtime_match(void);
static __attribute__((noinline)) int s6502_firmware_hle_compare_long_match(void);
static __attribute__((noinline)) int s6502_firmware_hle_indirect_call_match(void);
static __attribute__((noinline)) uint16_t
s6502_firmware_hle_multiply_cycles(void);
static __attribute__((noinline)) uint16_t
s6502_firmware_hle_compare_cycles(void);
static __attribute__((noinline)) void s6502_firmware_hle_compare16(
    uint32_t sp, uint32_t status, s6502_hle_compare_result_t *result
);
#ifdef GAM4980_ENABLE_AGGRESSIVE_REGION_HLE
static __attribute__((noinline)) int s6502_firmware_hle_bitmap_region(
    uint32_t sp, uint32_t status, uint32_t cycle_budget,
    s6502_hle_region_result_t *result
);
static __attribute__((noinline)) int s6502_firmware_hle_shift_region(
    uint32_t sp, uint32_t status, uint32_t cycle_budget,
    s6502_hle_region_result_t *result
);
static uint8_t *s6502_stack_ram;
static inline __attribute__((always_inline)) uint16_t
s6502_firmware_hle_zp16(const uint8_t *ram, uint32_t low);
static inline __attribute__((always_inline)) uint32_t
s6502_firmware_hle_indirect_y_cycles(uint8_t pointer_low, uint8_t y)
{
    return 5u + ((uint32_t)pointer_low + y > 0xffu);
}

static inline __attribute__((always_inline)) uint8_t
s6502_firmware_hle_nz(uint8_t status, uint8_t value)
{
    return (uint8_t)((status & ~0x82u) | (value & 0x80u) |
        (value ? 0u : 0x02u));
}

static inline __attribute__((always_inline)) uint8_t
s6502_firmware_hle_adc8(
    uint8_t status, uint8_t left, uint8_t right, uint8_t *value)
{
    uint16_t sum = (uint16_t)left + right + (status & 1u);
    uint8_t result = (uint8_t)sum;

    status = s6502_firmware_hle_nz(status, result);
    status = (uint8_t)((status & ~0x41u) | (sum > 0xffu) |
        ((~(left ^ right) & (left ^ result) & 0x80u) ? 0x40u : 0u));
    *value = result;
    return status;
}

static inline __attribute__((always_inline)) uint8_t
s6502_firmware_hle_sbc8(
    uint8_t status, uint8_t left, uint8_t right, uint8_t *value)
{
    uint16_t difference = (uint16_t)left - right -
        ((status & 1u) ? 0u : 1u);
    uint8_t result = (uint8_t)difference;

    status = s6502_firmware_hle_nz(status, result);
    status = (uint8_t)((status & ~0x41u) |
        (difference < 0x100u ? 1u : 0u) |
        (((left ^ result) & (left ^ right) & 0x80u) ? 0x40u : 0u));
    *value = result;
    return status;
}

static __attribute__((noinline)) int s6502_firmware_hle_graphics_address(
    uint32_t ix, uint32_t iy, uint32_t sp, uint32_t status,
    uint32_t cycle_budget, s6502_hle_direct_result_t *result)
{
    uint8_t *ram = s6502_stack_ram;
    uint8_t x = ram[0x2081u];
    uint8_t row = ram[0x2082u];
    uint8_t stack_pointer = (uint8_t)sp;
    uint8_t p = (uint8_t)status;
    uint8_t a;
    uint8_t y = (uint8_t)iy;
    uint8_t product_low = 0u;
    uint8_t product_high = 0u;
    uint8_t multiplier = 0u;
    uint8_t destination_low = 0u;
    uint8_t destination_high = 0u;
    uint8_t left_low = 0u;
    uint8_t left_high = 0u;
    uint8_t column_remainder = ram[0x20b7u];
    uint16_t return_address = (uint16_t)(
        ram[0x100u | (uint8_t)(stack_pointer + 1u)] |
        (ram[0x100u | (uint8_t)(stack_pointer + 2u)] << 8));
    uint16_t internal_return = 0u;
    uint32_t cycles = 0u;

    a = row;
    p = s6502_firmware_hle_nz(p, a);
    cycles += 4u;
    p = (uint8_t)((p & ~0x83u) | (a >= 0x41u) |
        ((uint8_t)(a - 0x41u) & 0x80u) |
        (a == 0x41u ? 0x02u : 0u));
    cycles += 2u;
    if (row == 0x41u) {
        cycles += 2u + 3u;
        a = 0xf3u;
        p = s6502_firmware_hle_nz(p, a);
        left_low = a;
        a = 0x0fu;
        p = s6502_firmware_hle_nz(p, a);
        left_high = a;
        a = 0u;
        p = s6502_firmware_hle_nz(p, a);
        destination_low = a;
        a = 4u;
        p = s6502_firmware_hle_nz(p, a);
        destination_high = a;
        cycles += 24u;
    } else {
        cycles += 3u;
        if (row > 0x41u) {
            cycles += 2u;
            p |= 1u;
            cycles += 2u;
            a = row;
            p = s6502_firmware_hle_nz(p, a);
            cycles += 4u;
            p = s6502_firmware_hle_sbc8(p, a, 0x42u, &a);
            cycles += 2u;
            multiplier = a;
            cycles += 4u;
            a = 0x20u;
            p = s6502_firmware_hle_nz(p, a);
            product_low = a;
            cycles += 12u;
            internal_return = 0x8787u;
        } else {
            cycles += 3u;
            a = row;
            p = s6502_firmware_hle_nz(p, a);
            multiplier = a;
            a = 0x20u;
            p = s6502_firmware_hle_nz(p, a);
            product_low = a;
            cycles += 4u + 4u + 2u + 4u + 6u;
            internal_return = 0x87ecu;
        }

        /* Exact $7C29 8-bit multiply, including its flag and cycle path. */
        a = 0u;
        p = s6502_firmware_hle_nz(p, a);
        product_high = a;
        y = 8u;
        p = s6502_firmware_hle_nz(p, y);
        cycles += 8u;
        do {
            uint8_t carry = (uint8_t)(product_high >> 7);

            product_high = (uint8_t)(product_high << 1);
            p = s6502_firmware_hle_nz(p, product_high);
            p = (uint8_t)((p & ~1u) | carry);
            cycles += 6u;
            carry = (uint8_t)(multiplier >> 7);
            multiplier = (uint8_t)((multiplier << 1) | (p & 1u));
            p = s6502_firmware_hle_nz(p, multiplier);
            p = (uint8_t)((p & ~1u) | carry);
            cycles += 6u;
            if (!(p & 1u)) {
                cycles += 3u;
            } else {
                cycles += 2u;
                p &= (uint8_t)~1u;
                cycles += 2u;
                a = product_low;
                p = s6502_firmware_hle_nz(p, a);
                cycles += 4u;
                p = s6502_firmware_hle_adc8(
                    p, a, product_high, &a);
                cycles += 4u;
                product_high = a;
                cycles += 4u;
                if (p & 1u) {
                    cycles += 2u;
                    multiplier = (uint8_t)(multiplier + 1u);
                    p = s6502_firmware_hle_nz(p, multiplier);
                    cycles += 6u;
                } else {
                    cycles += 3u;
                }
            }
            y = (uint8_t)(y - 1u);
            p = s6502_firmware_hle_nz(p, y);
            cycles += 2u + (y ? 3u : 2u);
        } while (y);
        cycles += 6u;

        if (row > 0x41u) {
            p &= (uint8_t)~1u;
            a = 0x40u;
            p = s6502_firmware_hle_nz(p, a);
            p = s6502_firmware_hle_adc8(p, a, product_high, &a);
            destination_low = a;
            a = 0x0cu;
            p = s6502_firmware_hle_nz(p, a);
            p = s6502_firmware_hle_adc8(p, a, multiplier, &a);
            destination_high = a;
            cycles += 2u + 2u + 4u + 4u + 2u + 4u + 4u;
        } else {
            p |= 1u;
            a = 0x20u;
            p = s6502_firmware_hle_nz(p, a);
            p = s6502_firmware_hle_sbc8(p, a, product_high, &a);
            destination_low = a;
            a = 0x0cu;
            p = s6502_firmware_hle_nz(p, a);
            p = s6502_firmware_hle_sbc8(p, a, multiplier, &a);
            destination_high = a;
            cycles += 2u + 2u + 4u + 4u + 2u + 4u + 4u;
        }
        p |= 1u;
        a = destination_low;
        p = s6502_firmware_hle_nz(p, a);
        p = s6502_firmware_hle_sbc8(p, a, 0x0du, &a);
        left_low = a;
        a = destination_high;
        p = s6502_firmware_hle_nz(p, a);
        p = s6502_firmware_hle_sbc8(p, a, 0u, &a);
        left_high = a;
        cycles += 2u + 4u + 2u + 4u + 4u + 2u + 4u;
    }

    a = x;
    p = s6502_firmware_hle_nz(p, a);
    cycles += 4u;
    p = (uint8_t)((p & ~0x83u) | (a >= 8u) |
        ((uint8_t)(a - 8u) & 0x80u) | (a == 8u ? 0x02u : 0u));
    cycles += 2u;
    if (x < 8u) {
        cycles += 2u + 3u + 6u;
    } else {
        cycles += 3u;
        p |= 1u;
        cycles += 2u;
        a = x;
        p = s6502_firmware_hle_nz(p, a);
        cycles += 4u;
        p = s6502_firmware_hle_sbc8(p, a, 8u, &a);
        cycles += 2u;
        column_remainder = a;
        cycles += 4u;
        for (;;) {
            p |= 1u;
            cycles += 2u;
            a = column_remainder;
            p = s6502_firmware_hle_nz(p, a);
            cycles += 4u;
            p = s6502_firmware_hle_sbc8(p, a, 8u, &a);
            cycles += 2u;
            if (!(p & 1u)) {
                cycles += row > 0x41u ? 2u + 3u : 3u;
                break;
            }
            cycles += row > 0x41u ? 3u : 2u;
            column_remainder = a;
            cycles += 4u;
            p &= (uint8_t)~1u;
            cycles += 2u;
            a = 1u;
            p = s6502_firmware_hle_nz(p, a);
            cycles += 2u;
            p = s6502_firmware_hle_adc8(
                p, a, destination_low, &a);
            destination_low = a;
            cycles += 4u + 4u;
            a = 0u;
            p = s6502_firmware_hle_nz(p, a);
            cycles += 2u;
            p = s6502_firmware_hle_adc8(
                p, a, destination_high, &a);
            destination_high = a;
            cycles += 4u + 4u + 3u;
        }
        cycles += 6u;
    }
    if (cycles > cycle_budget)
        return 0;

    if (internal_return) {
        ram[0x2085u] = multiplier;
        ram[0x2087u] = product_low;
        ram[0x2089u] = product_high;
    }
    ram[0x3au] = destination_low;
    ram[0x3bu] = destination_high;
    ram[0x38u] = left_low;
    ram[0x39u] = left_high;
    ram[0x20b7u] = column_remainder;
    if (internal_return) {
        ram[0x100u | stack_pointer] = (uint8_t)(internal_return >> 8);
        ram[0x100u | (uint8_t)(stack_pointer - 1u)] =
            (uint8_t)internal_return;
    }
    result->cycles = cycles;
    result->pc = (uint16_t)(return_address + 1u);
    result->ac = a;
    result->ix = (uint8_t)ix;
    result->iy = y;
    result->dt = (uint8_t)(stack_pointer + 2u);
    result->status = p;
    return 1;
}

static __attribute__((noinline)) int s6502_firmware_hle_hline_middle(
    uint32_t ac, uint32_t ix, uint32_t iy, uint32_t sp, uint32_t status,
    uint32_t cycle_budget, s6502_hle_direct_result_t *result)
{
    uint8_t *ram = s6502_stack_ram;
    uint16_t destination = (uint16_t)(ram[0x3au] | (ram[0x3bu] << 8));
    uint8_t remaining = ram[0x20d8u];
    uint8_t p = (uint8_t)status;
    uint32_t cycles = 0u;
    uint32_t hits = 0u;

    (void)ac;
    if (!remaining)
        return -1;
    for (;;) {
        uint8_t alias_enabled = mem_read(0x03e5u);
        uint8_t alias_low = mem_read(0x03e6u);
        uint8_t alias_high = mem_read(0x03e7u);
        uint8_t next_remaining = (uint8_t)(remaining - 1u);
        uint32_t iteration_cycles;
        int use_alias = alias_enabled == 1u &&
            (uint8_t)(destination >> 8) == alias_high &&
            (uint8_t)destination == alias_low;

        if (alias_enabled != 1u)
            iteration_cycles = 17u;
        else if ((uint8_t)(destination >> 8) != alias_high)
            iteration_cycles = 27u;
        else if ((uint8_t)destination != alias_low)
            iteration_cycles = 37u;
        else
            iteration_cycles = 71u;
        iteration_cycles += next_remaining ? 37u : 36u;
        if (iteration_cycles > cycle_budget - cycles)
            break;

        if (use_alias) {
            uint16_t alternate = (uint16_t)(
                mem_read(0x03e8u) | (mem_read(0x03e9u) << 8));

            mem_write(alternate, 0xffu);
        } else {
            mem_write(destination, 0xffu);
        }
        p = (uint8_t)((p & ~0x40u) |
            (((uint8_t)destination == 0xffu &&
              (uint8_t)(destination >> 8) == 0x7fu) ? 0x40u : 0u));
        destination = (uint16_t)(destination + 1u);
        remaining = next_remaining;
        cycles += iteration_cycles;
        ++hits;
        if (!remaining)
            break;
    }
    if (!hits)
        return 0;

    ram[0x3au] = (uint8_t)destination;
    ram[0x3bu] = (uint8_t)(destination >> 8);
    ram[0x20d8u] = remaining;
    p = (uint8_t)((p & ~0x83u) | 1u | (remaining & 0x80u) |
        (remaining ? 0u : 0x02u));
    result->cycles = cycles;
    result->hits = hits;
    result->pc = remaining ? 0x8039u : 0x808eu;
    result->ea = destination;
    result->ac = remaining;
    result->ix = (uint8_t)ix;
    result->iy = (uint8_t)iy;
    result->dt = (uint8_t)sp;
    result->status = p;
    return 1;
}

static __attribute__((noinline)) int s6502_firmware_hle_part_picture_row(
    int shift_left, uint32_t ac, uint32_t iy, uint32_t sp, uint32_t status,
    uint32_t cycle_budget, s6502_hle_direct_result_t *result)
{
    uint8_t *ram = s6502_stack_ram;
    uint16_t source = (uint16_t)(ram[0x2fu] | (ram[0x30u] << 8));
    uint16_t destination = (uint16_t)(ram[0x3au] | (ram[0x3bu] << 8));
    uint8_t remaining = ram[0x20d8u];
    uint8_t shift = ram[0x20cfu];
    uint8_t p = (uint8_t)status;
    uint8_t high = ram[0x20e5u];
    uint8_t low = ram[0x20e6u];
    uint32_t cycles = 0u;
    uint32_t hits = 0u;

    (void)ac;
    if (!remaining || shift > 7u)
        return -1;
    for (;;) {
        uint8_t alias_enabled = mem_read(0x03e5u);
        uint8_t alias_low = mem_read(0x03e6u);
        uint8_t alias_high = mem_read(0x03e7u);
        uint8_t next_remaining = (uint8_t)(remaining - 1u);
        uint16_t old_destination = destination;
        uint32_t shift_cycles = shift ? 53u + 19u * shift : 55u;
        uint32_t write_cycles;
        uint32_t iteration_cycles;
        int use_alias = alias_enabled == 1u &&
            (uint8_t)(destination >> 8) == alias_high &&
            (uint8_t)destination == alias_low;

        if (alias_enabled != 1u)
            write_cycles = 19u;
        else if ((uint8_t)(destination >> 8) != alias_high)
            write_cycles = 29u;
        else if ((uint8_t)destination != alias_low)
            write_cycles = 39u;
        else
            write_cycles = 73u;
        iteration_cycles = shift_cycles + write_cycles +
            (next_remaining ? 39u : 37u);
        if (iteration_cycles > cycle_budget - cycles)
            break;

        high = mem_read(source);
        source = (uint16_t)(source + 1u);
        low = mem_read(source);
        if (shift_left) {
            uint16_t pair = (uint16_t)((high << 8) | low);

            pair = (uint16_t)(pair << shift);
            high = (uint8_t)(pair >> 8);
            low = (uint8_t)pair;
        } else {
            uint16_t pair = (uint16_t)((high << 8) | low);

            pair = (uint16_t)(pair >> shift);
            high = (uint8_t)(pair >> 8);
            low = (uint8_t)pair;
        }
        if (use_alias) {
            uint16_t alternate = (uint16_t)(
                mem_read(0x03e8u) | (mem_read(0x03e9u) << 8));

            mem_write(alternate, shift_left ? high : low);
        } else {
            mem_write(destination, shift_left ? high : low);
        }
        p = (uint8_t)((p & ~0x40u) |
            (((uint8_t)old_destination == 0xffu &&
              (uint8_t)(old_destination >> 8) == 0x7fu) ? 0x40u : 0u));
        destination = (uint16_t)(destination + 1u);
        remaining = next_remaining;
        cycles += iteration_cycles;
        ++hits;
        if (!remaining)
            break;
    }
    if (!hits)
        return 0;

    ram[0x2fu] = (uint8_t)source;
    ram[0x30u] = (uint8_t)(source >> 8);
    ram[0x3au] = (uint8_t)destination;
    ram[0x3bu] = (uint8_t)(destination >> 8);
    ram[0x20e5u] = high;
    ram[0x20e6u] = low;
    ram[0x20d8u] = remaining;
    p = (uint8_t)((p & ~0x83u) | 1u | (remaining & 0x80u) |
        (remaining ? 0u : 0x02u));
    result->cycles = cycles;
    result->hits = hits;
    result->pc = remaining ? (shift_left ? 0x5801u : 0x5351u) :
        (shift_left ? 0x588du : 0x53ddu);
    result->ea = destination;
    result->ac = remaining;
    result->ix = 0u;
    result->iy = 0u;
    result->dt = (uint8_t)sp;
    result->status = p;
    return 1;
}

/* Complete the common SysLine/SysRect pixel writer after $876B has converted
 * the X/Y coordinate to LCD byte pointers.  The original routine constructs
 * one bit mask, selects the normal or left-edge pointer, applies the page-3
 * LCD alias when necessary, and returns to its caller. */
static __attribute__((noinline)) int s6502_firmware_hle_pixel_tail(
    uint32_t sp, uint32_t status, uint32_t cycle_budget,
    s6502_hle_direct_result_t *result)
{
    uint8_t *ram = s6502_stack_ram;
    uint8_t x = ram[0x2081u];
    uint8_t bit = (uint8_t)(x & 7u);
    uint8_t mask = (uint8_t)(0x80u >> bit);
    uint8_t inverse = (uint8_t)~mask;
    uint8_t p = (uint8_t)status;
    uint8_t stack_pointer = (uint8_t)sp;
    uint8_t final_a;
    uint16_t destination;
    uint16_t return_address = (uint16_t)(
        ram[0x100u | (uint8_t)(stack_pointer + 1u)] |
        (ram[0x100u | (uint8_t)(stack_pointer + 2u)] << 8));
    uint32_t cycles = 21u + 17u * bit;
    int alias = 0;

    cycles += x < 8u ? 20u : 18u;
    if (x < 8u) {
        destination = (uint16_t)(ram[0x38u] | (ram[0x39u] << 8));
        cycles += ram[0x2080u] ? 32u : 29u;
    } else {
        uint8_t alias_enabled = mem_read(0x03e5u);

        if (alias_enabled != 1u) {
            cycles += 10u;
        } else {
            uint8_t destination_high = ram[0x3bu];
            uint8_t alias_high = mem_read(0x03e7u);

            cycles += 8u;
            if (destination_high != alias_high) {
                cycles += 12u;
            } else {
                uint8_t destination_low = ram[0x3au];
                uint8_t alias_low = mem_read(0x03e6u);

                cycles += 10u;
                if (destination_low != alias_low) {
                    cycles += 12u;
                } else {
                    alias = 1;
                    cycles += 26u;
                }
            }
        }
        if (alias) {
            cycles += ram[0x2080u] ? 54u : 48u;
            destination = (uint16_t)(
                mem_read(0x03e8u) | (mem_read(0x03e9u) << 8));
        } else {
            cycles += ram[0x2080u] ? 32u : 29u;
            destination = (uint16_t)(ram[0x3au] | (ram[0x3bu] << 8));
        }
    }
    cycles += 6u;
    if (cycles > cycle_budget)
        return 0;

    ram[0x20b8u] = mask;
    ram[0x20b9u] = inverse;
    final_a = (uint8_t)(mem_read(destination) & inverse);
    if (ram[0x2080u])
        final_a = (uint8_t)(final_a | mask);
    mem_write(destination, final_a);
    if (alias) {
        ram[0x3au] = mem_read(0x03e6u);
        ram[0x3bu] = mem_read(0x03e7u);
        final_a = ram[0x3bu];
    }
    p = s6502_firmware_hle_nz(p, final_a);
    /* The final CMP #$00 that selects set/clear mode always leaves C set. */
    p |= 1u;

    result->cycles = cycles;
    result->hits = 1u;
    result->pc = (uint16_t)(return_address + 1u);
    result->ea = destination;
    result->ac = final_a;
    result->ix = 0u;
    result->iy = 0u;
    result->dt = (uint8_t)(stack_pointer + 2u);
    result->status = p;
    return 1;
}

static __attribute__((noinline)) int s6502_firmware_hle_picture_head(
    uint32_t ac, uint32_t iy, uint32_t sp, uint32_t status,
    uint32_t cycle_budget, s6502_hle_direct_result_t *result
)
{
    uint8_t *ram = s6502_stack_ram;
    uint16_t argument_pointer = s6502_firmware_hle_zp16(ram, 0x28u);
    uint32_t cycles = 0u;
    uint8_t x1 = (uint8_t)ac;
    uint8_t y1 = mem_read(argument_pointer);
    uint8_t x2 = mem_read((uint16_t)(argument_pointer + 1u));
    uint8_t y2 = mem_read((uint16_t)(argument_pointer + 2u));
    uint8_t flag = mem_read((uint16_t)(argument_pointer + 5u));
    uint8_t shift;
    uint8_t right_shift;
    uint8_t temporary;
    uint8_t stack_pointer = (uint8_t)sp;
    uint8_t pointer_low = (uint8_t)argument_pointer;

    (void)iy;
    if (x2 >= 0xa0u || y2 >= 0x60u || flag != 0u)
        return -1;

    cycles += 4u + 2u;
    cycles += s6502_firmware_hle_indirect_y_cycles(pointer_low, 0u) + 4u;
    cycles += 2u +
        s6502_firmware_hle_indirect_y_cycles(pointer_low, 1u) + 4u + 2u + 3u;
    if (x2 < x1)
        cycles += 15u;
    cycles += 2u +
        s6502_firmware_hle_indirect_y_cycles(pointer_low, 2u) + 4u + 2u + 3u;
    if (y2 < y1)
        cycles += 15u;
    cycles += 2u +
        s6502_firmware_hle_indirect_y_cycles(pointer_low, 3u) + 4u;
    cycles += 2u +
        s6502_firmware_hle_indirect_y_cycles(pointer_low, 4u) + 4u;
    cycles += 2u +
        s6502_firmware_hle_indirect_y_cycles(pointer_low, 5u) + 2u + 3u;

    if (x2 < x1) {
        temporary = x1;
        x1 = x2;
        x2 = temporary;
    }
    if (y2 < y1) {
        temporary = y1;
        y1 = y2;
        y2 = temporary;
    }
    shift = (uint8_t)(x1 & 7u);
    right_shift = (uint8_t)(7u - (x2 & 7u));
    cycles += 22u + (shift ? 15u * shift + 4u : 9u);
    cycles += 28u + (right_shift ? 15u * right_shift + 4u : 9u);
    /* $68D3-$690C, including both absolute read/modify/write sequences and
     * the six-cycle JSR at the boundary. */
    cycles += 26u + 14u + 14u + 46u;
    if (cycles > cycle_budget)
        return 0;

    ram[0x2081u] = x1;
    ram[0x2082u] = y1;
    ram[0x2083u] = x2;
    ram[0x2084u] = y2;
    ram[0x2fu] = mem_read((uint16_t)(argument_pointer + 3u));
    ram[0x30u] = mem_read((uint16_t)(argument_pointer + 4u));
    ram[0x20cfu] = shift;
    ram[0x20e3u] = shift ? (uint8_t)(0xffu << (8u - shift)) : 0u;
    ram[0x20e4u] = right_shift
        ? (uint8_t)(0xffu >> (8u - right_shift)) : 0u;
    ram[0x20dau] = (uint8_t)(y2 - y1 + 1u);
    ram[0x20e5u] = (uint8_t)(x1 >> 3);
    ram[0x20e6u] = (uint8_t)(x2 >> 3);
    ram[0x20d8u] = (uint8_t)(ram[0x20e6u] - ram[0x20e5u]);
    ram[0x20e8u] = ram[0x20d8u];
    ram[0x20e7u] = 0u;
    ram[0x100u | stack_pointer] = 0x69u;
    ram[0x100u | (uint8_t)(stack_pointer - 1u)] = 0x0eu;

    result->cycles = cycles;
    result->pc = 0x876bu;
    result->ac = 0u;
    result->ix = 0u;
    result->iy = 5u;
    result->dt = (uint8_t)(stack_pointer - 2u);
    result->status = (uint8_t)((status & ~0xc3u) | 0x03u);
    return 1;
}

static __attribute__((noinline)) int s6502_firmware_hle_picture_resume(
    uint32_t ac, uint32_t ix, uint32_t iy, uint32_t sp, uint32_t status,
    uint32_t cycle_budget, s6502_hle_direct_result_t *result
)
{
    uint8_t *ram = s6502_stack_ram;
    uint8_t x = ram[0x2081u];
    uint8_t y = ram[0x2082u];
    uint32_t cycles = 16u + 4u + 2u;
    uint16_t next_pc;
    uint8_t compare = (uint8_t)(x - 8u);

    (void)ac;
    if (y == 0x41u) {
        cycles += 3u + 4u + 2u;
        if (x < 8u) {
            cycles += 3u;
            next_pc = 0x6930u;
        } else {
            cycles += 2u + 3u;
            next_pc = 0x69d9u;
        }
    } else {
        cycles += 2u + 3u + 4u + 2u;
        if (x < 8u) {
            cycles += 2u;
            next_pc = 0x6988u;
        } else {
            cycles += 3u;
            next_pc = 0x69d9u;
        }
    }
    if (cycles > cycle_budget)
        return 0;

    ram[0x20e9u] = ram[0x3au];
    ram[0x20eau] = ram[0x3bu];
    status &= ~0x83u;
    status |= x >= 8u ? 0x01u : 0u;
    status |= compare ? 0u : 0x02u;
    status |= compare & 0x80u;
    result->cycles = cycles;
    result->pc = next_pc;
    result->ac = x;
    result->ix = (uint8_t)ix;
    result->iy = (uint8_t)iy;
    result->dt = (uint8_t)sp;
    result->status = (uint8_t)status;
    return 1;
}

static __attribute__((noinline)) int s6502_firmware_hle_picture_tail(
    int has_next_source_byte, uint32_t sp, uint32_t status,
    uint32_t cycle_budget, s6502_hle_region_result_t *result
);
static __attribute__((noinline)) int s6502_firmware_hle_picture_head(
    uint32_t ac, uint32_t iy, uint32_t sp, uint32_t status,
    uint32_t cycle_budget, s6502_hle_direct_result_t *result
);
static __attribute__((noinline)) int s6502_firmware_hle_picture_resume(
    uint32_t ac, uint32_t ix, uint32_t iy, uint32_t sp, uint32_t status,
    uint32_t cycle_budget, s6502_hle_direct_result_t *result
);
#endif
static inline __attribute__((always_inline)) int s6502_firmware_hle_ram_span(
    uint8_t *ram, uint16_t address, uint32_t size, uint8_t **result
)
{
    uint8_t *base = 0;
    uint32_t consumed = 0u;

    if (!ram || !result || !size ||
        !s6502_firmware_hle_mem_pages || !s6502_firmware_hle_banks ||
        (uint32_t)address + size > 0x10000u)
        return 0;
    while (consumed < size) {
        uint16_t current = (uint16_t)(address + consumed);
        uint32_t physical =
            ((uint32_t)s6502_firmware_hle_banks[current >> 12] << 12) |
            (current & 0x0fffu);
        uint32_t chunk = 0x100u - (current & 0xffu);
        uint8_t *page = s6502_firmware_hle_mem_pages[current >> 8];
        uint8_t *pointer;

        if (chunk > size - consumed)
            chunk = size - consumed;
        if (physical >= GAM4980_RAM_SIZE ||
            chunk > GAM4980_RAM_SIZE - physical || !page ||
            page != ram + (physical & ~0xffu))
            return 0;
        pointer = page + (current & 0xffu);
        if (!base)
            base = pointer;
        else if (pointer != base + consumed)
            return 0;
        consumed += chunk;
    }
    *result = base;
    return 1;
}

static inline __attribute__((always_inline)) int s6502_firmware_hle_read_span(
    uint16_t address, uint32_t size, uint8_t **result
)
{
    uint32_t cache_index = (
        (uint32_t)address ^ (size << 3) ^ (size >> 5)
    ) & (S6502_RESOURCE_SPAN_CACHE_SIZE - 1u);
    s6502_resource_span_cache_t *cache =
        &s6502_resource_span_cache[cache_index];
    uint8_t *base = 0;
    uint32_t consumed = 0u;

    if (!result || !size || !s6502_firmware_hle_mem_pages ||
        (uint32_t)address + size > 0x10000u)
        return 0;
    if (cache->valid && cache->address == address && cache->size == size &&
        cache->first_page ==
            s6502_firmware_hle_mem_pages[address >> 8] &&
        cache->last_page == s6502_firmware_hle_mem_pages[
            (uint16_t)(address + size - 1u) >> 8]) {
        *result = cache->pointer;
        ++s6502_resource_span_cache_hits;
        return 1;
    }
    ++s6502_resource_span_cache_misses;
    while (consumed < size) {
        uint16_t current = (uint16_t)(address + consumed);
        uint32_t chunk = 0x100u - (current & 0xffu);
        uint8_t *page = s6502_firmware_hle_mem_pages[current >> 8];
        uint8_t *pointer;

        if (chunk > size - consumed)
            chunk = size - consumed;
        if (!page)
            return 0;
        pointer = page + (current & 0xffu);
        if (!base)
            base = pointer;
        else if (pointer != base + consumed)
            return 0;
        consumed += chunk;
    }
    *result = base;
    cache->address = address;
    cache->size = size;
    cache->pointer = base;
    cache->first_page = s6502_firmware_hle_mem_pages[address >> 8];
    cache->last_page = s6502_firmware_hle_mem_pages[
        (uint16_t)(address + size - 1u) >> 8];
    cache->valid = 1u;
    return 1;
}
#ifdef GAM4980_ENABLE_GAME_LOAD_AOT
static __attribute__((noinline)) uint16_t s6502_game_hle_counter_cycles(
    const s6502_game_hle_counter_t *match
);
static __attribute__((noinline)) uint16_t
s6502_game_hle_counter_tail_cycles(
    const s6502_game_hle_counter_t *match
);
static __attribute__((noinline)) uint16_t s6502_game_hle_bitmap_cycles(
    const s6502_game_hle_bitmap_t *match, uint32_t ix
);
static __attribute__((noinline)) uint16_t
s6502_game_hle_bitmap_outer_cycles(
    const s6502_game_hle_bitmap_t *match, uint32_t ix
);
static __attribute__((noinline)) uint16_t
s6502_game_hle_bitmap_iteration_cycles(
    const s6502_game_hle_bitmap_t *match, uint32_t ix
);
static __attribute__((noinline)) uint16_t
s6502_game_hle_bitmap_outer_prefix_cycles(
    const s6502_game_hle_bitmap_t *match
);
static __attribute__((noinline)) uint16_t
s6502_game_hle_bitmap_subpixel_tail_cycles(
    const s6502_game_hle_bitmap_t *match
);
static __attribute__((noinline)) uint16_t
s6502_game_hle_bitmap_group_tail_cycles(
    const s6502_game_hle_bitmap_t *match
);
static __attribute__((noinline)) uint16_t
s6502_game_hle_bitmap_row_cycles(
    const s6502_game_hle_bitmap_t *match, uint32_t ix
);
static __attribute__((noinline)) int s6502_game_hle_scan_region(
    const s6502_game_hle_scan_t *match, uint32_t status,
    uint32_t cycle_budget, s6502_hle_game_scan_result_t *result
);
static __attribute__((noinline)) int s6502_game_hle_record_scan_region(
    const s6502_game_hle_record_scan_t *match, uint32_t status,
    uint32_t cycle_budget, s6502_hle_game_scan_result_t *result
);
static __attribute__((noinline)) int
s6502_game_hle_record_scan_resume_region(
    const s6502_game_hle_record_scan_t *match, uint32_t pc,
    uint32_t ac, uint32_t iy, uint32_t status, uint32_t cycle_budget,
    s6502_hle_game_scan_result_t *result
);
static __attribute__((noinline)) int s6502_game_hle_record_reverse_region(
    const s6502_game_hle_record_reverse_t *match, uint32_t status,
    uint32_t cycle_budget, s6502_hle_game_scan_result_t *result
);
static __attribute__((noinline)) int s6502_game_hle_table_chain_region(
    const s6502_game_hle_table_chain_t *match, uint32_t pc,
    uint32_t sp, uint32_t status, uint32_t cycle_budget,
    s6502_hle_game_table_result_t *result
);
static __attribute__((noinline)) int s6502_game_hle_object_flow_region(
    const s6502_game_hle_object_flow_t *match, uint32_t pc,
    uint32_t sp, uint32_t status, uint32_t cycle_budget,
    s6502_hle_game_table_result_t *result
);
#endif
__attribute__((noinline)) uint32_t
s6502_firmware_hle_glyph_bits(uint32_t value, uint32_t shift);
S6502_HLE_GLYPH_ROW_ATTRIBUTE void s6502_firmware_hle_glyph_row(
    uint32_t ix, uint32_t sp, uint32_t status,
    s6502_hle_glyph_result_t *result
);
S6502_HLE_GLYPH_ROW_ATTRIBUTE void s6502_firmware_hle_wide_glyph_row(
    uint32_t ix, uint32_t sp, uint32_t status,
    s6502_hle_glyph_result_t *result
);
#if defined(GAM4980_AOT_DIAGNOSTICS) || \
    defined(GAM4980_RUNTIME_PERFORMANCE_LOG) || \
    defined(GAM4980_ENABLE_FIRMWARE_HLE)
#define S6502_HLE_ATTEMPT(id) do {                                         \
    if (s6502_performance_debug)                                           \
        ++s6502_firmware_hle_attempts[(id)];                               \
} while (0)
#define S6502_HLE_ATTEMPT_EXTRA(id, count) do {                            \
    if (s6502_performance_debug)                                           \
        s6502_firmware_hle_attempts[(id)] += (count);                      \
} while (0)
#define S6502_HLE_CONDITION_REJECT(id) do {                                \
    if (s6502_performance_debug)                                           \
        ++s6502_firmware_hle_condition_rejects[(id)];                      \
} while (0)
#define S6502_HLE_BUDGET_REJECT(id) do {                                   \
    if (s6502_performance_debug)                                           \
        ++s6502_firmware_hle_budget_rejects[(id)];                         \
} while (0)
#define S6502_HLE_RECORD(id, cycles) do {                                  \
    if (s6502_performance_debug) {                                         \
        ++s6502_firmware_hle_hits;                                         \
        s6502_firmware_hle_guest_cycles += (cycles);                       \
        ++s6502_firmware_hle_path_hits[(id)];                              \
        s6502_firmware_hle_path_guest_cycles[(id)] += (cycles);            \
    }                                                                      \
} while (0)
#define S6502_HLE_RECORD_BATCH(id, hits, cycles) do {                       \
    if (s6502_performance_debug) {                                         \
        s6502_firmware_hle_hits += (hits);                                 \
        s6502_firmware_hle_guest_cycles += (cycles);                       \
        s6502_firmware_hle_path_hits[(id)] += (hits);                      \
        s6502_firmware_hle_path_guest_cycles[(id)] += (cycles);            \
        ++s6502_firmware_hle_batch_groups[(id)];                           \
        s6502_firmware_hle_batch_iterations[(id)] += (hits);               \
        if ((hits) > s6502_firmware_hle_batch_max[(id)])                   \
            s6502_firmware_hle_batch_max[(id)] = (hits);                   \
    }                                                                      \
} while (0)
#define S6502_HLE_RECORD_DIRECT_BATCH(id, hits) do {                        \
    if (s6502_performance_debug) {                                         \
        ++s6502_firmware_hle_direct_groups[(id)];                          \
        s6502_firmware_hle_direct_iterations[(id)] += (hits);              \
    }                                                                      \
} while (0)
#ifdef GAM4980_RUNTIME_PERFORMANCE_LOG
#define S6502_HLE_DIRECT_LCD_WRITE(pointer, value) do {                    \
    uint8_t hle_lcd_value_ = (uint8_t)(value);                             \
    if (s6502_performance_debug)                                           \
        ++performance_lcd_write_calls;                                    \
    if (*(pointer) != hle_lcd_value_) {                                   \
        lcd_dirty = 1;                                                     \
        if (s6502_performance_debug)                                       \
            ++performance_lcd_changed_writes;                             \
    }                                                                      \
    *(pointer) = hle_lcd_value_;                                          \
} while (0)
#else
#define S6502_HLE_DIRECT_LCD_WRITE(pointer, value) do {                    \
    uint8_t hle_lcd_value_ = (uint8_t)(value);                             \
    if (*(pointer) != hle_lcd_value_)                                     \
        lcd_dirty = 1;                                                     \
    *(pointer) = hle_lcd_value_;                                          \
} while (0)
#endif
#else
#define S6502_HLE_ATTEMPT(id) ((void)0)
#define S6502_HLE_ATTEMPT_EXTRA(id, count) ((void)0)
#define S6502_HLE_CONDITION_REJECT(id) ((void)0)
#define S6502_HLE_BUDGET_REJECT(id) ((void)0)
#define S6502_HLE_RECORD(id, cycles) ((void)0)
#define S6502_HLE_RECORD_BATCH(id, hits, cycles) ((void)0)
#define S6502_HLE_RECORD_DIRECT_BATCH(id, hits) ((void)0)
#define S6502_HLE_DIRECT_LCD_WRITE(pointer, value) do {                    \
    uint8_t hle_lcd_value_ = (uint8_t)(value);                             \
    if (*(pointer) != hle_lcd_value_)                                     \
        lcd_dirty = 1;                                                     \
    *(pointer) = hle_lcd_value_;                                          \
} while (0)
#endif
#define S6502_HLE_HELPER_6646_CYCLES(row, column) (                         \
    (row) < 0x40u ? 59u :                                                   \
    (row) == 0x40u ? 51u :                                                  \
    (row) == 0x41u ?                                                        \
        ((column) < 8u ? 53u :                                              \
            (uint16_t)(76u + 40u * (((column) - 8u) >> 3))) :              \
        67u                                                                  \
)
static __attribute__((noinline)) uint16_t
s6502_firmware_hle_glyph_row_cycles(uint32_t source_index, int wide)
{
    uint8_t *ram = s6502_stack_ram;
    uint16_t source = (uint16_t)(ram[0x2fu] | (ram[0x30u] << 8));
    uint16_t destination = (uint16_t)(ram[0x3au] | (ram[0x3bu] << 8));
    uint8_t shift = mem_read(0x208bu);
    uint8_t column = mem_read(0x2081u);
    uint8_t lcd_alias_enabled = mem_read(0x03e5u);
    uint8_t lcd_alias_low = mem_read(0x03e6u);
    uint8_t lcd_alias_high = mem_read(0x03e7u);
    uint16_t value;

    source_index &= 0xffu;
    if (wide) {
        value = (uint16_t)(51u + 43u * shift +
            !!(0xff00u & (source ^ (uint16_t)(source + source_index))) +
            !!(0xff00u & (source ^
                (uint16_t)(source + source_index + 1u))));
        if (column == 0x90u) {
            value = (uint16_t)(value + 67u +
                !!(0xff00u & (destination ^
                    (uint16_t)(destination + 1u))));
        } else if (column < 8u) {
            value = (uint16_t)(value +
                (lcd_alias_enabled != 1u ? 88u :
                 ram[0x3bu] != lcd_alias_high ? 98u :
                 ram[0x3au] != lcd_alias_low ? 108u : 142u) +
                !!(0xff00u & (destination ^
                    (uint16_t)(destination + 1u))));
        } else {
            value = (uint16_t)(value +
                (lcd_alias_enabled != 1u ? 87u :
                 ram[0x3bu] != lcd_alias_high ? 97u :
                 ram[0x3au] != lcd_alias_low ? 107u : 140u) +
                !!(0xff00u & (destination ^
                    (uint16_t)(destination + 2u))));
        }
        return (uint16_t)(value + 13u +
            S6502_HLE_HELPER_6646_CYCLES(mem_read(0x2082u), column));
    }

    value = (uint16_t)(40u + 37u * shift +
        !!(0xff00u & (source ^ (uint16_t)(source + source_index))));
    if (column == 0x98u) {
        value = (uint16_t)(value + 57u);
    } else if (lcd_alias_enabled == 1u &&
               destination == (uint16_t)(lcd_alias_low |
                                          (lcd_alias_high << 8))) {
        value = (uint16_t)(value + (column < 8u ? 137u :
            (uint16_t)(136u + ((uint8_t)destination == 0xffu))));
    } else {
        value = (uint16_t)(value + (column < 8u ?
            (uint16_t)(78u +
                (lcd_alias_enabled != 1u ? 9u :
                 ram[0x3bu] != lcd_alias_high ? 19u : 29u)) :
            (uint16_t)(76u +
                    (lcd_alias_enabled != 1u ? 9u :
                     ram[0x3bu] != lcd_alias_high ? 19u : 29u) +
                ((uint8_t)destination == 0xffu))));
    }
    return (uint16_t)(value +
        S6502_HLE_HELPER_6646_CYCLES(mem_read(0x2082u), column));
}
#define S6502_HLE_DISPATCH_608A() do {                                      \
    S6502_HLE_ATTEMPT(S6502_HLE_ID_WIDE_GLYPH);                            \
    if ((GAM4980_FIRMWARE_HLE_MASK & S6502_HLE_WIDE_GLYPH) &&               \
        s6502_firmware_hle_enabled && !DECIMAL_p && ix < 32u &&             \
        s6502_firmware_hle_banks[6] == 0x0eb5u &&                           \
        (s6502_firmware_hle_glyph_validation == 1u ||                       \
            s6502_firmware_hle_glyph_match()) &&                            \
        (s6502_firmware_hle_wide_glyph_validation == 1u ||                  \
            s6502_firmware_hle_wide_glyph_match()) &&                       \
        READ8(0x208bu) <= 7u) {                                             \
        et = s6502_firmware_hle_glyph_row_cycles(ix, 1);                    \
        if ((uint32_t)et <= cycles - executed)                             \
            goto _hle_ebin_wide_glyph_row;                                \
        S6502_HLE_BUDGET_REJECT(S6502_HLE_ID_WIDE_GLYPH);                  \
    } else {                                                               \
        S6502_HLE_CONDITION_REJECT(S6502_HLE_ID_WIDE_GLYPH);               \
    }                                                                       \
} while (0)
#define S6502_HLE_DISPATCH_650F() do {                                      \
    S6502_HLE_ATTEMPT(S6502_HLE_ID_GLYPH_ROW);                             \
    if ((GAM4980_FIRMWARE_HLE_MASK & S6502_HLE_GLYPH_ROW) &&                \
        s6502_firmware_hle_enabled && !DECIMAL_p && ix < 16u &&             \
        s6502_firmware_hle_banks[6] == 0x0eb5u &&                           \
        (s6502_firmware_hle_glyph_validation == 1u ||                       \
            s6502_firmware_hle_glyph_match()) && READ8(0x208bu) <= 7u) {    \
        et = s6502_firmware_hle_glyph_row_cycles(ix, 0);                    \
        if ((uint32_t)et <= cycles - executed)                             \
            goto _hle_ebin_glyph_row;                                     \
        S6502_HLE_BUDGET_REJECT(S6502_HLE_ID_GLYPH_ROW);                   \
    } else {                                                               \
        S6502_HLE_CONDITION_REJECT(S6502_HLE_ID_GLYPH_ROW);                \
    }                                                                       \
} while (0)
#define S6502_HLE_DISPATCH_6A75() do {                                      \
    uint8_t hle_shift;                                                     \
    S6502_HLE_ATTEMPT(S6502_HLE_ID_SHIFT_BLIT);                            \
    hle_shift = READ8(0x20cfu);                                            \
    if ((GAM4980_FIRMWARE_HLE_MASK & S6502_HLE_SHIFT_BLIT) &&               \
        s6502_firmware_hle_enabled && !DECIMAL_p &&                         \
        s6502_firmware_hle_banks[6] == 0x0eb5u &&                           \
        (s6502_firmware_hle_validation == 1u ||                             \
            s6502_firmware_hle_match()) && hle_shift <= 7u) {              \
        ea = (uint16_t)(                                                   \
            s6502_stack_ram[0x3au] | (s6502_stack_ram[0x3bu] << 8)         \
        );                                                                  \
        dt = (uint8_t)(READ8(0x20d8u) - 1u);                               \
        et = (uint16_t)((hle_shift == 0u ? 55u :                          \
            (uint16_t)(53u + 19u * hle_shift)) +                          \
            (ea == 0x0400u ? 75u : ((ea >> 8) != 0x04u ? 31u : 41u)) +    \
            (dt ? 39u : 37u));                                             \
        if ((uint32_t)et <= cycles - executed)                             \
            goto _hle_ebin_shift_blit;                                    \
        S6502_HLE_BUDGET_REJECT(S6502_HLE_ID_SHIFT_BLIT);                  \
    } else {                                                               \
        S6502_HLE_CONDITION_REJECT(S6502_HLE_ID_SHIFT_BLIT);               \
    }                                                                       \
} while (0)
#define S6502_HLE_DISPATCH_6AA7() do {                                      \
    S6502_HLE_ATTEMPT(S6502_HLE_ID_SHIFT_BLIT);                            \
    if ((GAM4980_FIRMWARE_HLE_MASK & S6502_HLE_SHIFT_BLIT) &&               \
        s6502_firmware_hle_enabled && !DECIMAL_p && ix == 0u &&             \
        s6502_firmware_hle_banks[6] == 0x0eb5u &&                           \
        (s6502_firmware_hle_validation == 1u ||                             \
            s6502_firmware_hle_match())) {                                 \
        ea = (uint16_t)(                                                   \
            s6502_stack_ram[0x3au] | (s6502_stack_ram[0x3bu] << 8)         \
        );                                                                  \
        dt = (uint8_t)(READ8(0x20d8u) - 1u);                               \
        et = (uint16_t)(                                                   \
            (ea == 0x0400u ? 75u :                                        \
                ((ea >> 8) != 0x04u ? 31u : 41u)) +                       \
            (dt ? 39u : 37u)                                              \
        );                                                                  \
        if ((uint32_t)et <= cycles - executed)                             \
            goto _hle_ebin_shift_blit_suffix_6aa7;                        \
        S6502_HLE_BUDGET_REJECT(S6502_HLE_ID_SHIFT_BLIT);                  \
    } else {                                                               \
        S6502_HLE_CONDITION_REJECT(S6502_HLE_ID_SHIFT_BLIT);               \
    }                                                                       \
} while (0)
#define S6502_HLE_DISPATCH_6AE0() do {                                      \
    S6502_HLE_ATTEMPT(S6502_HLE_ID_SHIFT_BLIT);                            \
    if ((GAM4980_FIRMWARE_HLE_MASK & S6502_HLE_SHIFT_BLIT) &&               \
        s6502_firmware_hle_enabled && !DECIMAL_p &&                         \
        s6502_firmware_hle_banks[6] == 0x0eb5u &&                           \
        (s6502_firmware_hle_validation == 1u ||                             \
            s6502_firmware_hle_match())) {                                 \
        dt = (uint8_t)(READ8(0x20d8u) - 1u);                               \
        et = (uint16_t)(12u + (dt ? 39u : 37u));                           \
        if ((uint32_t)et <= cycles - executed)                             \
            goto _hle_ebin_shift_blit_suffix_6ae0;                        \
        S6502_HLE_BUDGET_REJECT(S6502_HLE_ID_SHIFT_BLIT);                  \
    } else {                                                               \
        S6502_HLE_CONDITION_REJECT(S6502_HLE_ID_SHIFT_BLIT);               \
    }                                                                       \
} while (0)
#ifdef GAM4980_ENABLE_AGGRESSIVE_REGION_HLE
#define S6502_HLE_DISPATCH_682D() do {                                      \
    S6502_HLE_ATTEMPT(S6502_HLE_ID_PICTURE_HEAD);                          \
    if ((GAM4980_FIRMWARE_HLE_MASK & S6502_HLE_SHIFT_BLIT) &&              \
        s6502_firmware_hle_enabled && !DECIMAL_p &&                        \
        s6502_firmware_hle_banks[6] == 0x0eb5u &&                          \
        (s6502_firmware_hle_picture_head_validation == 1u ||               \
            s6502_firmware_hle_picture_head_match()))                     \
        goto _hle_ebin_picture_head;                                       \
    S6502_HLE_CONDITION_REJECT(S6502_HLE_ID_PICTURE_HEAD);                 \
} while (0)
#define S6502_HLE_DISPATCH_690F() do {                                      \
    S6502_HLE_ATTEMPT(S6502_HLE_ID_PICTURE_RESUME);                        \
    if ((GAM4980_FIRMWARE_HLE_MASK & S6502_HLE_SHIFT_BLIT) &&              \
        s6502_firmware_hle_enabled && !DECIMAL_p &&                        \
        s6502_firmware_hle_banks[6] == 0x0eb5u &&                          \
        (s6502_firmware_hle_picture_head_validation == 1u ||               \
            s6502_firmware_hle_picture_head_match()))                     \
        goto _hle_ebin_picture_resume;                                     \
    S6502_HLE_CONDITION_REJECT(S6502_HLE_ID_PICTURE_RESUME);               \
} while (0)
#define S6502_HLE_DISPATCH_6988() do {                                      \
    S6502_HLE_ATTEMPT(S6502_HLE_ID_SHIFT_REGION);                          \
    S6502_PICTURE_TAIL_TEST_TRACE(ea, et, dt, executed);                    \
    if ((GAM4980_FIRMWARE_HLE_MASK & S6502_HLE_SHIFT_BLIT) &&               \
        s6502_firmware_hle_enabled && !DECIMAL_p &&                         \
        s6502_firmware_hle_banks[6] == 0x0eb5u &&                           \
        (s6502_firmware_hle_shift_region_validation == 1u ||                \
            s6502_firmware_hle_shift_region_match()))                      \
        goto _hle_ebin_shift_region;                                       \
    S6502_HLE_CONDITION_REJECT(S6502_HLE_ID_SHIFT_REGION);                 \
} while (0)
#define S6502_HLE_PICTURE_TAIL_DISPATCH(label) do {                          \
    S6502_HLE_ATTEMPT(S6502_HLE_ID_PICTURE_TAIL);                            \
    if (S6502_PICTURE_TAIL_TEST_ENABLED &&                                  \
        (GAM4980_FIRMWARE_HLE_MASK & S6502_HLE_SHIFT_BLIT) &&                \
        s6502_firmware_hle_enabled && !DECIMAL_p &&                          \
        s6502_firmware_hle_banks[6] == 0x0eb5u &&                            \
        (s6502_firmware_hle_shift_region_validation == 1u ||                 \
            s6502_firmware_hle_shift_region_match()))                       \
        goto label;                                                          \
    S6502_HLE_CONDITION_REJECT(S6502_HLE_ID_PICTURE_TAIL);                  \
} while (0)
#define S6502_HLE_DISPATCH_6B1A()                                           \
    S6502_HLE_PICTURE_TAIL_DISPATCH(_hle_ebin_picture_tail_next)
#define S6502_HLE_DISPATCH_6BA4()                                           \
    S6502_HLE_PICTURE_TAIL_DISPATCH(_hle_ebin_picture_tail_zero)
#define S6502_HLE_DISPATCH_5C5D() do {                                      \
    S6502_HLE_ATTEMPT(S6502_HLE_ID_BITMAP_REGION);                         \
    if ((GAM4980_FIRMWARE_HLE_MASK & S6502_HLE_BITMAP_COPY) &&              \
        s6502_firmware_hle_enabled && !DECIMAL_p &&                         \
        s6502_firmware_hle_banks[5] == 0x0eb8u &&                           \
        (s6502_firmware_hle_bitmap_region_validation == 1u ||               \
            s6502_firmware_hle_bitmap_region_match()))                     \
        goto _hle_ebin_bitmap_region;                                      \
    S6502_HLE_CONDITION_REJECT(S6502_HLE_ID_BITMAP_REGION);                \
} while (0)
#else
#define S6502_HLE_DISPATCH_682D() ((void)0)
#define S6502_HLE_DISPATCH_690F() ((void)0)
#define S6502_HLE_DISPATCH_6988() ((void)0)
#define S6502_HLE_DISPATCH_6B1A() ((void)0)
#define S6502_HLE_DISPATCH_6BA4() ((void)0)
#define S6502_HLE_DISPATCH_5C5D() ((void)0)
#endif
#define S6502_HLE_DISPATCH_5CB3() do {                                      \
    uint8_t hle_shift;                                                     \
    S6502_HLE_ATTEMPT(S6502_HLE_ID_BITMAP_COPY);                           \
    hle_shift = READ8(0x20cfu);                                            \
    if ((GAM4980_FIRMWARE_HLE_MASK & S6502_HLE_BITMAP_COPY) &&              \
        s6502_firmware_hle_enabled && !DECIMAL_p &&                         \
        s6502_firmware_hle_banks[5] == 0x0eb8u &&                           \
        (s6502_firmware_hle_bitmap_validation == 1u ||                      \
            s6502_firmware_hle_bitmap_match()) && hle_shift <= 7u) {      \
        dt = (uint8_t)(READ8(0x20d8u) - 1u);                               \
        et = (uint16_t)((hle_shift == 0u ? 55u :                           \
            (uint16_t)(53u + 19u * hle_shift)) +                           \
            (dt ? 48u : 46u));                                             \
        if ((uint32_t)et <= cycles - executed)                             \
            goto _hle_ebin_bitmap_copy;                                   \
        S6502_HLE_BUDGET_REJECT(S6502_HLE_ID_BITMAP_COPY);                 \
    } else {                                                               \
        S6502_HLE_CONDITION_REJECT(S6502_HLE_ID_BITMAP_COPY);              \
    }                                                                       \
} while (0)
#define S6502_HLE_DISPATCH_5CE5() do {                                      \
    S6502_HLE_ATTEMPT(S6502_HLE_ID_BITMAP_COPY);                           \
    if ((GAM4980_FIRMWARE_HLE_MASK & S6502_HLE_BITMAP_COPY) &&              \
        s6502_firmware_hle_enabled && !DECIMAL_p && ix == 0u &&             \
        s6502_firmware_hle_banks[5] == 0x0eb8u &&                           \
        (s6502_firmware_hle_bitmap_validation == 1u ||                      \
            s6502_firmware_hle_bitmap_match())) {                          \
        dt = (uint8_t)(READ8(0x20d8u) - 1u);                               \
        et = (uint16_t)(dt ? 48u : 46u);                                   \
        if ((uint32_t)et <= cycles - executed)                             \
            goto _hle_ebin_bitmap_copy_suffix_5ce5;                       \
        S6502_HLE_BUDGET_REJECT(S6502_HLE_ID_BITMAP_COPY);                 \
    } else {                                                               \
        S6502_HLE_CONDITION_REJECT(S6502_HLE_ID_BITMAP_COPY);              \
    }                                                                       \
} while (0)
#define S6502_HLE_DISPATCH_7937() do {                                      \
    uint32_t hle_partial;                                                  \
    S6502_HLE_ATTEMPT(S6502_HLE_ID_BYTE_FILL);                             \
    if ((GAM4980_FIRMWARE_HLE_MASK & S6502_HLE_BYTE_FILL) &&                \
        s6502_firmware_hle_enabled && !DECIMAL_p && ix != 0u &&             \
        s6502_firmware_hle_banks[7] == 0x0ebeu &&                           \
        (s6502_firmware_hle_fill_validation == 1u ||                        \
            s6502_firmware_hle_fill_match())) {                            \
        et = (uint16_t)(20u * (uint16_t)ix + 1u);                          \
        if ((uint32_t)et <= cycles - executed)                             \
            goto _hle_ebin_byte_fill;                                     \
        hle_partial = (cycles - executed) / 20u;                           \
        if (hle_partial >= ix)                                             \
            hle_partial = (uint32_t)ix - 1u;                               \
        if (hle_partial) {                                                 \
            ea = (uint16_t)hle_partial;                                    \
            et = (uint16_t)(20u * hle_partial);                            \
            goto _hle_ebin_byte_fill_partial;                             \
        }                                                                  \
        S6502_HLE_BUDGET_REJECT(S6502_HLE_ID_BYTE_FILL);                   \
    } else {                                                               \
        S6502_HLE_CONDITION_REJECT(S6502_HLE_ID_BYTE_FILL);                \
    }                                                                       \
} while (0)
#define S6502_HLE_DISPATCH_876B() do {                                      \
    S6502_HLE_ATTEMPT(S6502_HLE_ID_GRAPHICS_ADDRESS);                      \
    if ((GAM4980_FIRMWARE_HLE_MASK & S6502_HLE_GRAPHICS_ADDRESS) &&        \
        s6502_firmware_hle_enabled && !DECIMAL_p &&                        \
        (s6502_firmware_hle_graphics_address_validation == 1u ||           \
            s6502_firmware_hle_graphics_address_match()))                 \
        goto _hle_ebin_graphics_address;                                   \
    S6502_HLE_CONDITION_REJECT(S6502_HLE_ID_GRAPHICS_ADDRESS);             \
} while (0)
#define S6502_HLE_DISPATCH_8039() do {                                      \
    S6502_HLE_ATTEMPT(S6502_HLE_ID_HLINE_MIDDLE);                          \
    if ((GAM4980_FIRMWARE_HLE_MASK & S6502_HLE_GRAPHICS_ADDRESS) &&        \
        s6502_firmware_hle_enabled && !DECIMAL_p && iy == 0u &&            \
        (s6502_firmware_hle_hline_validation == 1u ||                      \
            s6502_firmware_hle_hline_match()))                            \
        goto _hle_ebin_hline_middle;                                       \
    S6502_HLE_CONDITION_REJECT(S6502_HLE_ID_HLINE_MIDDLE);                 \
} while (0)
#define S6502_HLE_DISPATCH_859E() do {                                      \
    S6502_HLE_ATTEMPT(S6502_HLE_ID_PIXEL_TAIL);                            \
    if ((GAM4980_FIRMWARE_HLE_MASK & S6502_HLE_GRAPHICS_ADDRESS) &&        \
        s6502_firmware_hle_enabled &&                                      \
        (s6502_firmware_hle_pixel_tail_validation == 1u ||                 \
            s6502_firmware_hle_pixel_tail_match()))                       \
        goto _hle_ebin_pixel_tail;                                         \
    S6502_HLE_CONDITION_REJECT(S6502_HLE_ID_PIXEL_TAIL);                   \
} while (0)
#define S6502_HLE_PART_PICTURE_ROW_DISPATCH(label) do {                    \
    S6502_HLE_ATTEMPT(S6502_HLE_ID_PART_PICTURE_ROW);                      \
    if ((GAM4980_FIRMWARE_HLE_MASK & S6502_HLE_GRAPHICS_ADDRESS) &&        \
        s6502_firmware_hle_enabled && !DECIMAL_p &&                        \
        s6502_firmware_hle_banks[5] == 0x0eb8u &&                          \
        (s6502_firmware_hle_part_picture_validation == 1u ||               \
            s6502_firmware_hle_part_picture_match()))                     \
        goto label;                                                         \
    S6502_HLE_CONDITION_REJECT(S6502_HLE_ID_PART_PICTURE_ROW);             \
} while (0)
#define S6502_HLE_DISPATCH_5351()                                          \
    S6502_HLE_PART_PICTURE_ROW_DISPATCH(                                   \
        _hle_ebin_part_picture_row_right)
#define S6502_HLE_DISPATCH_5801()                                          \
    S6502_HLE_PART_PICTURE_ROW_DISPATCH(                                   \
        _hle_ebin_part_picture_row_left)
#define S6502_HLE_DISPATCH_D1A2() do {                                      \
    S6502_HLE_ATTEMPT(S6502_HLE_ID_MULTIPLY16);                            \
    if ((GAM4980_FIRMWARE_HLE_MASK & S6502_HLE_MULTIPLY16) &&               \
        s6502_firmware_hle_enabled && !DECIMAL_p &&                         \
        s6502_firmware_hle_banks[0x0du] == 0x0ea8u &&                       \
        (s6502_firmware_hle_multiply_validation == 1u ||                    \
            s6502_firmware_hle_multiply_match())) {                         \
        et = s6502_firmware_hle_multiply_cycles();                          \
        if ((uint32_t)et <= cycles - executed)                             \
            goto _hle_ebin_multiply16;                                    \
        S6502_HLE_BUDGET_REJECT(S6502_HLE_ID_MULTIPLY16);                  \
    } else {                                                               \
        S6502_HLE_CONDITION_REJECT(S6502_HLE_ID_MULTIPLY16);               \
    }                                                                       \
} while (0)
#define S6502_HLE_DISPATCH_D2CA() do {                                      \
    uint16_t hle_left = READ16W(0x0020u);                                  \
    uint16_t hle_right = READ16W(0x0023u);                                 \
    uint16_t hle_output = READ16W(0x002au);                                \
    uint16_t hle_output_start = (uint16_t)(hle_output + 8u);               \
    uint16_t hle_output_end = (uint16_t)(hle_output + 11u);                \
    uint16_t hle_index;                                                     \
    S6502_HLE_ATTEMPT(S6502_HLE_ID_AND_LONG);                              \
    if ((GAM4980_FIRMWARE_HLE_MASK & S6502_HLE_C_RUNTIME) &&                \
        s6502_firmware_hle_enabled && !DECIMAL_p &&                         \
        s6502_firmware_hle_banks[0x0du] == 0x0ea8u &&                       \
        hle_left <= 0xfffcu && hle_right <= 0xfffcu &&                     \
        hle_output <= 0xfff4u &&                                           \
        (hle_output_end < 0x0020u || hle_output_start > 0x002bu) &&        \
        (s6502_firmware_hle_c_runtime_validation == 1u ||                   \
            s6502_firmware_hle_c_runtime_match())) {                        \
        et = 123u;                                                          \
        for (hle_index = 1u; hle_index < 4u; ++hle_index) {                \
            et = (uint16_t)(et +                                           \
                (((hle_left & 0xffu) + hle_index) > 0xffu) +               \
                (((hle_right & 0xffu) + hle_index) > 0xffu));              \
        }                                                                   \
        if ((uint32_t)et <= cycles - executed)                             \
            goto _hle_ebin_and_long;                                      \
        S6502_HLE_BUDGET_REJECT(S6502_HLE_ID_AND_LONG);                    \
    } else {                                                               \
        S6502_HLE_CONDITION_REJECT(S6502_HLE_ID_AND_LONG);                 \
    }                                                                      \
} while (0)
#define S6502_HLE_DISPATCH_D596() do {                                      \
    S6502_HLE_ATTEMPT(S6502_HLE_ID_LOAD_OPER1_TEMP);                       \
    if ((GAM4980_FIRMWARE_HLE_MASK & S6502_HLE_C_RUNTIME) &&                \
        s6502_firmware_hle_enabled && !DECIMAL_p &&                         \
        s6502_firmware_hle_banks[0x0du] == 0x0ea8u &&                       \
        (s6502_firmware_hle_c_runtime_validation == 1u ||                   \
            s6502_firmware_hle_c_runtime_match())) {                        \
        et = 31u;                                                           \
        if ((uint32_t)et <= cycles - executed)                             \
            goto _hle_ebin_load_oper1_temp;                               \
        S6502_HLE_BUDGET_REJECT(S6502_HLE_ID_LOAD_OPER1_TEMP);             \
    } else {                                                               \
        S6502_HLE_CONDITION_REJECT(S6502_HLE_ID_LOAD_OPER1_TEMP);          \
    }                                                                      \
} while (0)
#define S6502_HLE_DISPATCH_D572() do {                                      \
    S6502_HLE_ATTEMPT(S6502_HLE_ID_INDIRECT_CALL);                         \
    if ((GAM4980_FIRMWARE_HLE_MASK & S6502_HLE_C_RUNTIME) &&                \
        s6502_firmware_hle_enabled && !DECIMAL_p &&                         \
        s6502_firmware_hle_banks[0x0du] == 0x0ea8u &&                       \
        (s6502_firmware_hle_indirect_call_validation == 1u ||              \
            s6502_firmware_hle_indirect_call_match())) {                   \
        et = 37u;                                                          \
        if ((uint32_t)et <= cycles - executed)                             \
            goto _hle_ebin_indirect_call;                                 \
        S6502_HLE_BUDGET_REJECT(S6502_HLE_ID_INDIRECT_CALL);               \
    } else {                                                               \
        S6502_HLE_CONDITION_REJECT(S6502_HLE_ID_INDIRECT_CALL);            \
    }                                                                      \
} while (0)
#define S6502_HLE_DISPATCH_D362() do {                                      \
    uint16_t hle_left = READ16W(0x0020u);                                  \
    uint16_t hle_right = READ16W(0x0023u);                                 \
    uint16_t hle_index;                                                     \
    uint16_t hle_nonzero = 0u;                                             \
    uint16_t hle_difference;                                               \
    uint8_t hle_carry = 1u;                                                \
    S6502_HLE_ATTEMPT(S6502_HLE_ID_COMPARE_LONG);                          \
    if ((GAM4980_FIRMWARE_HLE_MASK & S6502_HLE_C_RUNTIME) &&                \
        s6502_firmware_hle_enabled && !DECIMAL_p &&                         \
        s6502_firmware_hle_banks[0x0du] == 0x0ea8u &&                       \
        hle_left >= 0x0100u && hle_left <= 0xfffcu &&                       \
        hle_right >= 0x0100u && hle_right <= 0xfffcu &&                     \
        s6502_firmware_hle_mem_pages[hle_left >> 8] &&                      \
        s6502_firmware_hle_mem_pages[(hle_left + 3u) >> 8] &&               \
        s6502_firmware_hle_mem_pages[hle_right >> 8] &&                     \
        s6502_firmware_hle_mem_pages[(hle_right + 3u) >> 8] &&              \
        (s6502_firmware_hle_compare_long_validation == 1u ||               \
            s6502_firmware_hle_compare_long_match())) {                    \
        et = 0u;                                                           \
        for (hle_index = 0u; hle_index < 4u; ++hle_index) {                \
            hle_difference = (uint16_t)(                                   \
                READ8((uint16_t)(hle_left + hle_index)) +                  \
                (uint8_t)~READ8((uint16_t)(hle_right + hle_index)) +       \
                hle_carry);                                                \
            hle_carry = hle_difference > 0xffu;                            \
            hle_nonzero += (uint8_t)hle_difference != 0u;                  \
            if (hle_index) {                                               \
                et = (uint16_t)(et +                                       \
                    (((hle_left & 0xffu) + hle_index) > 0xffu) +           \
                    (((hle_right & 0xffu) + hle_index) > 0xffu));          \
            }                                                              \
        }                                                                  \
        et = (uint16_t)(et + (hle_nonzero ? 92u : 91u) +                  \
            8u * hle_nonzero);                                             \
        if ((uint32_t)et <= cycles - executed)                             \
            goto _hle_ebin_compare_long;                                  \
        S6502_HLE_BUDGET_REJECT(S6502_HLE_ID_COMPARE_LONG);                \
    } else {                                                               \
        S6502_HLE_CONDITION_REJECT(S6502_HLE_ID_COMPARE_LONG);             \
    }                                                                      \
} while (0)
#define S6502_HLE_BANK_SWITCH_DISPATCH(cycle_count, target) do {            \
    S6502_HLE_ATTEMPT(S6502_HLE_ID_BANK_SWITCH);                           \
    if ((GAM4980_FIRMWARE_HLE_MASK & S6502_HLE_BANK_SWITCH) &&              \
        s6502_firmware_hle_enabled && !DECIMAL_p &&                         \
        s6502_firmware_hle_banks[0x0fu] == 0x0eaau &&                       \
        (s6502_firmware_hle_bank_switch_validation == 1u ||                 \
            s6502_firmware_hle_bank_switch_match())) {                      \
        et = (uint16_t)(cycle_count);                                       \
        if ((uint32_t)et <= cycles - executed)                             \
            goto target;                                                   \
        S6502_HLE_BUDGET_REJECT(S6502_HLE_ID_BANK_SWITCH);                 \
    } else {                                                               \
        S6502_HLE_CONDITION_REJECT(S6502_HLE_ID_BANK_SWITCH);              \
    }                                                                      \
} while (0)
#define S6502_HLE_DISPATCH_F52A()                                          \
    S6502_HLE_BANK_SWITCH_DISPATCH(                                        \
        ac < 0xe0u ? 207u : 213u, _hle_ebin_bank_switch)
#define S6502_HLE_DISPATCH_F549()                                          \
    S6502_HLE_BANK_SWITCH_DISPATCH(185u, _hle_ebin_bank_switch_f549)
#define S6502_HLE_DISPATCH_F55B()                                          \
    S6502_HLE_BANK_SWITCH_DISPATCH(161u, _hle_ebin_bank_switch_f55b)
#define S6502_HLE_DISPATCH_D340() do {                                      \
    S6502_HLE_ATTEMPT(S6502_HLE_ID_COMPARE16);                             \
    if ((GAM4980_FIRMWARE_HLE_MASK & S6502_HLE_COMPARE16) &&               \
        s6502_firmware_hle_enabled && !DECIMAL_p &&                        \
        s6502_firmware_hle_banks[0x0du] == 0x0ea8u &&                      \
        (s6502_firmware_hle_compare_validation == 1u ||                    \
            s6502_firmware_hle_compare_match())) {                         \
        et = s6502_firmware_hle_compare_cycles();                          \
        if ((uint32_t)et <= cycles - executed)                             \
            goto _hle_ebin_compare16;                                     \
        S6502_HLE_BUDGET_REJECT(S6502_HLE_ID_COMPARE16);                   \
    } else {                                                               \
        S6502_HLE_CONDITION_REJECT(S6502_HLE_ID_COMPARE16);                \
    }                                                                      \
} while (0)
#define S6502_HLE_COMPARE_SUFFIX_COMMON(cycle_count) do {                   \
    S6502_HLE_ATTEMPT(S6502_HLE_ID_COMPARE16);                             \
    if ((GAM4980_FIRMWARE_HLE_MASK & S6502_HLE_COMPARE16) &&                \
        s6502_firmware_hle_enabled && !DECIMAL_p &&                         \
        s6502_firmware_hle_banks[0x0du] == 0x0ea8u &&                      \
        (s6502_firmware_hle_compare_validation == 1u ||                    \
            s6502_firmware_hle_compare_match())) {                         \
        et = (uint16_t)(cycle_count);                                       \
        if ((uint32_t)et <= cycles - executed)                             \
            goto _hle_ebin_compare16_suffix;                              \
        S6502_HLE_BUDGET_REJECT(S6502_HLE_ID_COMPARE16);                   \
    } else {                                                               \
        S6502_HLE_CONDITION_REJECT(S6502_HLE_ID_COMPARE16);                \
    }                                                                      \
} while (0)
#define S6502_HLE_DISPATCH_D349() do {                                      \
    uint16_t hle_difference = (uint16_t)(                                  \
        (uint16_t)s6502_stack_ram[0x21u] +                                 \
        (uint8_t)~s6502_stack_ram[0x24u] + CARRY                           \
    );                                                                      \
    S6502_HLE_COMPARE_SUFFIX_COMMON((uint8_t)hle_difference ? 54u : 46u);  \
} while (0)
#define S6502_HLE_DISPATCH_D352()                                          \
    S6502_HLE_COMPARE_SUFFIX_COMMON(37u)
#define S6502_HLE_DISPATCH_D35D()                                          \
    S6502_HLE_COMPARE_SUFFIX_COMMON(15u)
#define S6502_HLE_DISPATCH_D35F()                                          \
    S6502_HLE_COMPARE_SUFFIX_COMMON(13u)
#ifdef GAM4980_ENABLE_GAME_LOAD_AOT
#define S6502_GAME_HLE_LOCALS                                               \
    const s6502_game_hle_counter_t *game_hle_counter = 0;                  \
    const s6502_game_hle_bitmap_t *game_hle_bitmap = 0;                    \
    const s6502_game_hle_scan_t *game_hle_scan = 0;                        \
    const s6502_game_hle_record_scan_t *game_hle_record_scan = 0;          \
    const s6502_game_hle_record_reverse_t *game_hle_record_reverse = 0;    \
    const s6502_game_hle_table_chain_t *game_hle_table_chain = 0;          \
    const s6502_game_hle_object_flow_t *game_hle_object_flow = 0;          \
    int game_hle_scan_status = 0;                                          \
    int game_hle_record_scan_status = 0;                                   \
    int game_hle_record_reverse_status = 0;                                \
    int game_hle_table_chain_status = 0;                                   \
    int game_hle_object_flow_status = 0;                                   \
    uint8_t game_hle_bitmap_partial = 0;                                   \
    uint8_t game_hle_bitmap_outer = 0;
#define S6502_GAME_HLE_DISPATCH() do {                                      \
    if (s6502_firmware_hle_enabled && !DECIMAL_p) {                        \
        game_hle_counter = s6502_game_hle_find_counter(                    \
            game_aot_physical_pc                                            \
        );                                                                  \
        if (game_hle_counter) {                                            \
            S6502_HLE_ATTEMPT(S6502_HLE_ID_GAME_COUNTER);                  \
            if (pc == (uint16_t)(game_hle_counter->virtual_pc + 0x15u) && \
                (GAM4980_FIRMWARE_HLE_MASK & S6502_HLE_COMPARE16)) {       \
                if (CARRY_p)                                               \
                    et = 5u;                                               \
                else                                                       \
                    et = s6502_game_hle_counter_tail_cycles(               \
                        game_hle_counter                                   \
                    );                                                     \
                if (!et) {                                                 \
                    S6502_HLE_CONDITION_REJECT(                            \
                        S6502_HLE_ID_GAME_COUNTER                          \
                    );                                                     \
                } else if ((uint32_t)et <= cycles - executed) {            \
                    goto _hle_game_counter_suffix_15;                      \
                } else {                                                   \
                    S6502_HLE_BUDGET_REJECT(                               \
                        S6502_HLE_ID_GAME_COUNTER                          \
                    );                                                     \
                }                                                          \
            } else if (pc == game_hle_counter->virtual_pc &&              \
                (GAM4980_FIRMWARE_HLE_MASK & S6502_HLE_COMPARE16) &&       \
                (s6502_firmware_hle_compare_validation == 1u ||            \
                    s6502_firmware_hle_compare_match())) {                 \
                et = s6502_game_hle_counter_cycles(game_hle_counter);      \
                if (!et) {                                                 \
                    S6502_HLE_CONDITION_REJECT(                            \
                        S6502_HLE_ID_GAME_COUNTER                          \
                    );                                                     \
                } else if ((uint32_t)et <= cycles - executed) {            \
                    goto _hle_game_counter;                                \
                } else {                                                   \
                    S6502_HLE_BUDGET_REJECT(                               \
                        S6502_HLE_ID_GAME_COUNTER                          \
                    );                                                     \
                }                                                          \
            } else {                                                       \
                S6502_HLE_CONDITION_REJECT(                                \
                    S6502_HLE_ID_GAME_COUNTER                              \
                );                                                         \
            }                                                              \
        }                                                                  \
        game_hle_bitmap_partial = 0u;                                     \
        game_hle_bitmap_outer = 0u;                                       \
        game_hle_bitmap = s6502_game_hle_find_bitmap(                      \
            game_aot_physical_pc                                            \
        );                                                                  \
        if (game_hle_bitmap) {                                             \
            S6502_HLE_ATTEMPT(S6502_HLE_ID_GAME_BITMAP);                   \
            if (pc == (uint16_t)(game_hle_bitmap->virtual_pc + 0x2fu) &&  \
                game_aot_physical_pc ==                                    \
                    game_hle_bitmap->physical_pc + 0x2fu) {               \
                et = s6502_game_hle_bitmap_subpixel_tail_cycles(           \
                    game_hle_bitmap                                       \
                );                                                         \
                if ((uint32_t)et <= cycles - executed) {                   \
                    goto _hle_game_bitmap_subpixel_tail;                   \
                } else {                                                   \
                    S6502_HLE_BUDGET_REJECT(                               \
                        S6502_HLE_ID_GAME_BITMAP                           \
                    );                                                     \
                }                                                          \
            } else if (pc ==                                               \
                    (uint16_t)(game_hle_bitmap->virtual_pc + 0x40u) &&     \
                game_aot_physical_pc ==                                    \
                    game_hle_bitmap->physical_pc + 0x40u) {               \
                et = s6502_game_hle_bitmap_group_tail_cycles(              \
                    game_hle_bitmap                                       \
                );                                                         \
                if ((uint32_t)et <= cycles - executed) {                   \
                    goto _hle_game_bitmap_group_tail;                      \
                } else {                                                   \
                    S6502_HLE_BUDGET_REJECT(                               \
                        S6502_HLE_ID_GAME_BITMAP                           \
                    );                                                     \
                }                                                          \
            } else if (pc == game_hle_bitmap->outer_exit_pc &&            \
                game_aot_physical_pc ==                                    \
                    game_hle_bitmap->row_physical_pc) {                    \
                et = s6502_game_hle_bitmap_row_cycles(                     \
                    game_hle_bitmap, ix                                    \
                );                                                         \
                if (!et) {                                                 \
                    S6502_HLE_CONDITION_REJECT(                            \
                        S6502_HLE_ID_GAME_BITMAP                           \
                    );                                                     \
                } else if ((uint32_t)et <= cycles - executed) {            \
                    goto _hle_game_bitmap_row;                             \
                } else {                                                   \
                    S6502_HLE_BUDGET_REJECT(                               \
                        S6502_HLE_ID_GAME_BITMAP                           \
                    );                                                     \
                }                                                          \
            } else if (pc == game_hle_bitmap->outer_virtual_pc &&         \
                game_aot_physical_pc ==                                    \
                    game_hle_bitmap->outer_physical_pc) {                  \
                et = s6502_game_hle_bitmap_outer_cycles(                   \
                    game_hle_bitmap, ix                                    \
                );                                                         \
                if (!et) {                                                 \
                    S6502_HLE_CONDITION_REJECT(                            \
                        S6502_HLE_ID_GAME_BITMAP                           \
                    );                                                     \
                } else if ((uint32_t)et <= cycles - executed) {            \
                    game_hle_bitmap_outer = 1u;                            \
                    goto _hle_game_bitmap;                                 \
                } else {                                                   \
                    et = s6502_game_hle_bitmap_outer_prefix_cycles(        \
                        game_hle_bitmap                                   \
                    );                                                     \
                    if (et && (uint32_t)et <= cycles - executed) {         \
                        goto _hle_game_bitmap_outer_prefix;                \
                    } else {                                               \
                        S6502_HLE_BUDGET_REJECT(                           \
                            S6502_HLE_ID_GAME_BITMAP                       \
                        );                                                 \
                    }                                                      \
                }                                                          \
            } else if (pc != game_hle_bitmap->virtual_pc) {                \
                S6502_HLE_CONDITION_REJECT(S6502_HLE_ID_GAME_BITMAP);      \
            } else {                                                       \
                et = s6502_game_hle_bitmap_cycles(game_hle_bitmap, ix);    \
                if (!et) {                                                 \
                    S6502_HLE_CONDITION_REJECT(                            \
                        S6502_HLE_ID_GAME_BITMAP                           \
                    );                                                     \
                } else if ((uint32_t)et <= cycles - executed) {            \
                    goto _hle_game_bitmap;                                 \
                } else {                                                   \
                    et = s6502_game_hle_bitmap_iteration_cycles(           \
                        game_hle_bitmap, ix                                \
                    );                                                     \
                    if (et && (uint32_t)et <= cycles - executed) {         \
                        game_hle_bitmap_partial = 1u;                      \
                        goto _hle_game_bitmap;                             \
                    } else {                                               \
                        S6502_HLE_BUDGET_REJECT(                           \
                            S6502_HLE_ID_GAME_BITMAP                       \
                        );                                                 \
                    }                                                      \
                }                                                          \
            }                                                              \
        }                                                                  \
        game_hle_scan = s6502_game_hle_find_scan(                          \
            game_aot_physical_pc                                            \
        );                                                                  \
        if (game_hle_scan && pc == game_hle_scan->virtual_pc) {            \
            S6502_HLE_ATTEMPT(S6502_HLE_ID_GAME_SCAN);                     \
            game_hle_scan_status = s6502_game_hle_scan_region(             \
                game_hle_scan, status, cycles - executed,                  \
                &s6502_game_hle_scan_result                                \
            );                                                             \
            if (game_hle_scan_status > 0) {                                \
                goto _hle_game_scan;                                       \
            } else if (game_hle_scan_status < 0) {                         \
                S6502_HLE_CONDITION_REJECT(S6502_HLE_ID_GAME_SCAN);        \
            } else {                                                       \
                S6502_HLE_BUDGET_REJECT(S6502_HLE_ID_GAME_SCAN);           \
            }                                                              \
        }                                                                  \
        game_hle_record_scan = s6502_game_hle_find_record_scan(            \
            game_aot_physical_pc                                           \
        );                                                                 \
        if (game_hle_record_scan) {                                        \
            S6502_HLE_ATTEMPT(S6502_HLE_ID_GAME_RECORD_SCAN);              \
            if (pc == game_hle_record_scan->virtual_pc) {                  \
                game_hle_record_scan_status =                              \
                    s6502_game_hle_record_scan_region(                     \
                        game_hle_record_scan, status, cycles - executed,   \
                        &s6502_game_hle_scan_result                        \
                    );                                                     \
            } else if (pc == (uint16_t)(                                  \
                    game_hle_record_scan->virtual_pc + 0x60u) ||           \
                pc == (uint16_t)(                                         \
                    game_hle_record_scan->virtual_pc + 0xa8u) ||           \
                pc == (uint16_t)(                                         \
                    game_hle_record_scan->virtual_pc + 0xa5u) ||           \
                pc == (uint16_t)(                                         \
                    game_hle_record_scan->virtual_pc - 0x26u)) {           \
                game_hle_record_scan_status =                              \
                    s6502_game_hle_record_scan_resume_region(              \
                        game_hle_record_scan, pc, ac, iy, status,         \
                        cycles - executed,                                \
                        &s6502_game_hle_scan_result                        \
                    );                                                     \
            } else {                                                       \
                game_hle_record_scan_status = -1;                         \
            }                                                              \
            if (game_hle_record_scan_status > 0) {                         \
                goto _hle_game_record_scan;                               \
            } else if (game_hle_record_scan_status < 0) {                  \
                S6502_HLE_CONDITION_REJECT(                               \
                    S6502_HLE_ID_GAME_RECORD_SCAN                         \
                );                                                         \
            } else {                                                       \
                S6502_HLE_BUDGET_REJECT(                                  \
                    S6502_HLE_ID_GAME_RECORD_SCAN                         \
                );                                                         \
            }                                                              \
        }                                                                  \
        game_hle_record_reverse = s6502_game_hle_find_record_reverse(      \
            game_aot_physical_pc                                           \
        );                                                                 \
        if (game_hle_record_reverse &&                                     \
            pc == game_hle_record_reverse->virtual_pc) {                   \
            S6502_HLE_ATTEMPT(S6502_HLE_ID_GAME_RECORD_REVERSE);           \
            game_hle_record_reverse_status =                               \
                s6502_game_hle_record_reverse_region(                      \
                    game_hle_record_reverse, status, cycles - executed,    \
                    &s6502_game_hle_scan_result                            \
                );                                                         \
            if (game_hle_record_reverse_status > 0) {                      \
                goto _hle_game_record_reverse;                             \
            } else if (game_hle_record_reverse_status < 0) {               \
                S6502_HLE_CONDITION_REJECT(                                \
                    S6502_HLE_ID_GAME_RECORD_REVERSE                       \
                );                                                         \
            } else {                                                       \
                S6502_HLE_BUDGET_REJECT(                                   \
                    S6502_HLE_ID_GAME_RECORD_REVERSE                       \
                );                                                         \
            }                                                              \
        }                                                                  \
        game_hle_object_flow = s6502_game_hle_find_object_flow(            \
            game_aot_physical_pc                                           \
        );                                                                 \
        if (game_hle_object_flow) {                                        \
            S6502_HLE_ATTEMPT(S6502_HLE_ID_GAME_OBJECT_FLOW);              \
            game_hle_object_flow_status =                                  \
                s6502_game_hle_object_flow_region(                         \
                    game_hle_object_flow, pc, sp, status,                  \
                    cycles - executed, &s6502_game_hle_table_result        \
                );                                                         \
            if (game_hle_object_flow_status > 0) {                         \
                goto _hle_game_object_flow;                                \
            } else if (game_hle_object_flow_status < 0) {                  \
                S6502_HLE_CONDITION_REJECT(                                \
                    S6502_HLE_ID_GAME_OBJECT_FLOW                          \
                );                                                         \
            } else {                                                       \
                S6502_HLE_BUDGET_REJECT(                                   \
                    S6502_HLE_ID_GAME_OBJECT_FLOW                          \
                );                                                         \
            }                                                              \
        }                                                                  \
        game_hle_table_chain = s6502_game_hle_find_table_chain(            \
            game_aot_physical_pc                                           \
        );                                                                 \
        if (game_hle_table_chain) {                                        \
            S6502_HLE_ATTEMPT(S6502_HLE_ID_GAME_TABLE_CHAIN);              \
            game_hle_table_chain_status =                                  \
                s6502_game_hle_table_chain_region(                         \
                    game_hle_table_chain, pc, sp, status,                  \
                    cycles - executed, &s6502_game_hle_table_result        \
                );                                                         \
            if (game_hle_table_chain_status > 0) {                         \
                goto _hle_game_table_chain;                                \
            } else if (game_hle_table_chain_status < 0) {                  \
                S6502_HLE_CONDITION_REJECT(                                \
                    S6502_HLE_ID_GAME_TABLE_CHAIN                          \
                );                                                         \
            } else {                                                       \
                S6502_HLE_BUDGET_REJECT(                                   \
                    S6502_HLE_ID_GAME_TABLE_CHAIN                          \
                );                                                         \
            }                                                              \
        }                                                                  \
    }                                                                      \
} while (0)
#define S6502_GAME_HLE_EMIT_BLOCKS
#else
#define S6502_GAME_HLE_DISPATCH() ((void)0)
#endif
static __attribute__((noinline)) int s6502_hle_try_direct_bitmap_copy(
    uint8_t *ram, uint8_t initial_count, uint32_t cycle_budget,
    uint16_t first_cycles, uint8_t initial_status
)
{
    s6502_hle_direct_result_t *result = &s6502_hle_direct_result;
    uint8_t *source = 0;
    uint8_t *destination = 0;
    uint16_t source_address = (uint16_t)(
        ram[0x2fu] | (ram[0x30u] << 8)
    );
    uint16_t destination_address = (uint16_t)(
        ram[0x31u] | (ram[0x32u] << 8)
    );
    uint32_t remaining = (uint16_t)initial_count + 1u;
    uint32_t destination_physical;
    uint8_t shift = ram[0x20cfu];
    int destination_is_lcd = 0;
    uint32_t consumed = 0u;
    uint32_t hits = 0u;
    uint16_t next_cycles = first_cycles;
    uint16_t shifted = 0u;
    uint16_t ea = destination_address;
    uint8_t ac = 0u;
    uint8_t count = 0u;
    uint8_t status = initial_status;

    if (s6502_firmware_hle_banks[2] != 0x0002u ||
        s6502_firmware_hle_mem_pages[0x20] != ram + 0x2000u ||
        !s6502_firmware_hle_read_span(
            source_address, remaining + 1u, &source
        ) ||
        !s6502_firmware_hle_ram_span(
            ram, destination_address, remaining, &destination
        ))
        return 0;
    destination_physical = (uint32_t)(destination - ram);
    if (destination_physical >= 0x0400u &&
        destination_physical + remaining <= 0x1001u) {
        destination_is_lcd = 1;
    } else if (!((destination_physical + remaining <= 0x0400u ||
                  destination_physical > 0x1000u) &&
                 !(destination_physical <= _PB &&
                   destination_physical + remaining > _PB) &&
                 !(destination_physical <= 0x2028u &&
                   destination_physical + remaining > 0x2028u))) {
        return 0;
    }

    for (;;) {
        uint8_t source_high;
        uint8_t destination_high;
        uint8_t destination_carry;

        consumed += next_cycles;
        ++hits;
        ac = *source;
        ram[0x20e5u] = ac;
        ++source;
        ++source_address;
        ram[0x2fu] = (uint8_t)source_address;
        ram[0x30u] = (uint8_t)(source_address >> 8);
        source_high = *source;
        ram[0x20e6u] = source_high;
        shifted = (uint16_t)(((uint16_t)ac << 8) | source_high);
        shifted = (uint16_t)(shifted >> shift);
        ram[0x20e5u] = (uint8_t)(shifted >> 8);
        ram[0x20e6u] = (uint8_t)shifted;

        ac = (uint8_t)shifted;
        if (destination_is_lcd)
            S6502_HLE_DIRECT_LCD_WRITE(destination, ac);
        else
            *destination = ac;
        destination_high = (uint8_t)(ea >> 8);
        destination_carry = (uint8_t)((uint8_t)ea == 0xffu);
        ++destination;
        ++ea;
        ram[0x31u] = (uint8_t)ea;
        ram[0x32u] = (uint8_t)(ea >> 8);
        status &= (uint8_t)~0x40u;
        if (destination_carry && destination_high == 0x7fu)
            status |= 0x40u;

        --remaining;
        count = (uint8_t)remaining;
        ram[0x20d8u] = count;
        status = (uint8_t)(status & (uint8_t)~0x83u);
        status |= 0x01u;
        status |= count & 0x80u;
        if (!count)
            status |= 0x02u;
        if (!count)
            break;
        count = (uint8_t)(remaining - 1u);
        next_cycles = (uint16_t)((shift == 0u ? 55u :
            (uint16_t)(53u + 19u * shift)) +
            (count ? 48u : 46u));
        if ((uint32_t)next_cycles > cycle_budget - consumed)
            break;
    }
    result->cycles = consumed;
    result->hits = hits;
    result->pc = remaining ? 0x5cb3u : 0x5d05u;
    result->ea = ea;
    result->ac = (uint8_t)remaining;
    result->ix = 0u;
    result->iy = 0u;
    result->dt = (uint8_t)remaining;
    result->status = status;
    return 1;
}

static __attribute__((noinline)) int s6502_hle_try_direct_shift_blit(
    uint8_t *ram, uint8_t initial_count, uint32_t cycle_budget,
    uint16_t first_cycles, uint8_t initial_status
)
{
    s6502_hle_direct_result_t *result = &s6502_hle_direct_result;
    uint8_t *source = 0;
    uint8_t *destination = 0;
    uint8_t *destination_1000 = 0;
    uint16_t source_address = (uint16_t)(
        ram[0x2fu] | (ram[0x30u] << 8)
    );
    uint16_t destination_address = (uint16_t)(
        ram[0x3au] | (ram[0x3bu] << 8)
    );
    uint32_t remaining = (uint16_t)initial_count + 1u;
    uint8_t shift = ram[0x20cfu];
    uint32_t consumed = 0u;
    uint32_t hits = 0u;
    uint16_t next_cycles = first_cycles;
    uint16_t shifted = 0u;
    uint16_t ea = destination_address;
    uint8_t ac = 0u;
    uint8_t count = 0u;
    uint8_t status = initial_status;
#ifdef GAM4980_ENABLE_AGGRESSIVE_REGION_HLE
    int using_cache = 0;
#endif

#ifdef GAM4980_ENABLE_AGGRESSIVE_REGION_HLE
    if (s6502_hle_shift_cache.valid &&
        source_address >= s6502_hle_shift_cache.source_address &&
        destination_address >= s6502_hle_shift_cache.destination_address &&
        (uint32_t)(source_address -
            s6502_hle_shift_cache.source_address) + remaining + 1u <=
                s6502_hle_shift_cache.source_size &&
        (uint32_t)(destination_address -
            s6502_hle_shift_cache.destination_address) + remaining <=
                s6502_hle_shift_cache.destination_size) {
        uint32_t source_offset = (uint16_t)(
            source_address - s6502_hle_shift_cache.source_address
        );
        uint32_t destination_offset = (uint16_t)(
            destination_address - s6502_hle_shift_cache.destination_address
        );
        uint8_t *source_first = s6502_hle_shift_cache.source + source_offset;
        uint8_t *source_last = source_first + remaining;
        uint8_t *destination_first =
            s6502_hle_shift_cache.destination + destination_offset;
        uint8_t *destination_last = destination_first + remaining - 1u;
        uint8_t *source_first_page =
            s6502_firmware_hle_mem_pages[source_address >> 8];
        uint8_t *source_last_page = s6502_firmware_hle_mem_pages[
            (uint16_t)(source_address + remaining) >> 8
        ];
        uint8_t *destination_first_page =
            s6502_firmware_hle_mem_pages[destination_address >> 8];
        uint8_t *destination_last_page = s6502_firmware_hle_mem_pages[
            (uint16_t)(destination_address + remaining - 1u) >> 8
        ];

        if (source_first_page && source_last_page &&
            destination_first_page && destination_last_page &&
            source_first_page + (source_address & 0xffu) == source_first &&
            source_last_page +
                ((source_address + remaining) & 0xffu) == source_last &&
            destination_first_page + (destination_address & 0xffu) ==
                destination_first &&
            destination_last_page +
                ((destination_address + remaining - 1u) & 0xffu) ==
                    destination_last) {
            source = source_first;
            destination = destination_first;
            destination_1000 = s6502_hle_shift_cache.destination_1000;
            using_cache = 1;
        }
    }
    if (!using_cache)
#endif
    {
#ifdef GAM4980_ENABLE_DIRECT_RAM_HLE
    if (s6502_firmware_hle_banks[2] != 0x0002u ||
        s6502_firmware_hle_mem_pages[0x20] != ram + 0x2000u ||
        !s6502_firmware_hle_read_span(
            source_address, remaining + 1u, &source
        ) ||
        !s6502_firmware_hle_ram_span(
            ram, destination_address, remaining, &destination
        ) ||
        destination < ram + 0x0400u ||
        destination + remaining > ram + 0x1001u ||
        (destination_address == 0x0400u &&
            (!s6502_firmware_hle_ram_span(
                 ram, 0x1000u, 1u, &destination_1000
             ) || destination_1000 != ram + 0x1000u)))
        return 0;
#else
        return 0;
#endif
    }

    for (;;) {
        uint8_t source_high;

        consumed += next_cycles;
        ++hits;
        ac = *source;
        ram[0x20e5u] = ac;
        ++source;
        ++source_address;
        ram[0x2fu] = (uint8_t)source_address;
        ram[0x30u] = (uint8_t)(source_address >> 8);
        source_high = *source;
        ram[0x20e6u] = source_high;
        shifted = (uint16_t)(((uint16_t)ac << 8) | source_high);
        shifted = (uint16_t)(shifted >> shift);
        ram[0x20e5u] = (uint8_t)(shifted >> 8);
        ram[0x20e6u] = (uint8_t)shifted;

        ac = (uint8_t)shifted;
        if (ea == 0x0400u)
            S6502_HLE_DIRECT_LCD_WRITE(destination_1000, ac);
        else
            S6502_HLE_DIRECT_LCD_WRITE(destination, ac);
        ++destination;
        ++ea;
        ram[0x3au] = (uint8_t)ea;
        ram[0x3bu] = (uint8_t)(ea >> 8);

        --remaining;
        count = (uint8_t)remaining;
        ram[0x20d8u] = count;
        status = (uint8_t)(status & (uint8_t)~0x83u);
        status |= 0x01u;
        status |= count & 0x80u;
        if (!count)
            status |= 0x02u;
        if (!count)
            break;
        count = (uint8_t)(remaining - 1u);
        next_cycles = (uint16_t)((shift == 0u ? 55u :
            (uint16_t)(53u + 19u * shift)) +
            (ea == 0x0400u ? 75u :
                ((ea >> 8) != 0x04u ? 31u : 41u)) +
            (count ? 39u : 37u));
        if ((uint32_t)next_cycles > cycle_budget - consumed)
            break;
    }
    result->cycles = consumed;
    result->hits = hits;
    result->pc = remaining ? 0x6a75u : 0x6b05u;
    result->ea = ea;
    result->ac = (uint8_t)remaining;
    result->ix = 0u;
    result->iy = 0u;
    result->dt = (uint8_t)remaining;
    result->status = status;
#ifdef GAM4980_ENABLE_AGGRESSIVE_REGION_HLE
    if (using_cache && !remaining)
        s6502_hle_shift_cache.valid = 0u;
#endif
    return 1;
}
#define S6502_HLE_EMIT_BLOCKS
#define S6502_AOT_ENTRY_5C5D_HOOK() S6502_HLE_DISPATCH_5C5D()
#define S6502_AOT_ENTRY_5CB3_HOOK() S6502_HLE_DISPATCH_5CB3()
#define S6502_AOT_ENTRY_5CE5_HOOK() S6502_HLE_DISPATCH_5CE5()
#define S6502_AOT_ENTRY_608A_HOOK() S6502_HLE_DISPATCH_608A()
#define S6502_AOT_ENTRY_650F_HOOK() S6502_HLE_DISPATCH_650F()
#define S6502_AOT_ENTRY_682D_HOOK() S6502_HLE_DISPATCH_682D()
#define S6502_AOT_ENTRY_690F_HOOK() S6502_HLE_DISPATCH_690F()
#define S6502_AOT_ENTRY_6988_HOOK() S6502_HLE_DISPATCH_6988()
#define S6502_AOT_ENTRY_6A75_HOOK() S6502_HLE_DISPATCH_6A75()
#define S6502_AOT_ENTRY_6AA7_HOOK() S6502_HLE_DISPATCH_6AA7()
#define S6502_AOT_ENTRY_6AE0_HOOK() S6502_HLE_DISPATCH_6AE0()
#define S6502_AOT_ENTRY_6B1A_HOOK() S6502_HLE_DISPATCH_6B1A()
#define S6502_AOT_ENTRY_6BA4_HOOK() S6502_HLE_DISPATCH_6BA4()
#define S6502_AOT_ENTRY_7937_HOOK() S6502_HLE_DISPATCH_7937()
#define S6502_AOT_ENTRY_5351_HOOK() S6502_HLE_DISPATCH_5351()
#define S6502_AOT_ENTRY_5801_HOOK() S6502_HLE_DISPATCH_5801()
#define S6502_AOT_ENTRY_8039_HOOK() S6502_HLE_DISPATCH_8039()
#define S6502_AOT_ENTRY_859E_HOOK() S6502_HLE_DISPATCH_859E()
#define S6502_AOT_ENTRY_876B_HOOK() S6502_HLE_DISPATCH_876B()
#define S6502_AOT_ENTRY_D1A2_HOOK() S6502_HLE_DISPATCH_D1A2()
#define S6502_AOT_ENTRY_D2CA_HOOK() S6502_HLE_DISPATCH_D2CA()
#define S6502_AOT_ENTRY_D340_HOOK() S6502_HLE_DISPATCH_D340()
#define S6502_AOT_ENTRY_D349_HOOK() S6502_HLE_DISPATCH_D349()
#define S6502_AOT_ENTRY_D352_HOOK() S6502_HLE_DISPATCH_D352()
#define S6502_AOT_ENTRY_D35D_HOOK() S6502_HLE_DISPATCH_D35D()
#define S6502_AOT_ENTRY_D35F_HOOK() S6502_HLE_DISPATCH_D35F()
#define S6502_AOT_ENTRY_D596_HOOK() S6502_HLE_DISPATCH_D596()
#define S6502_AOT_ENTRY_D362_HOOK() S6502_HLE_DISPATCH_D362()
#define S6502_AOT_ENTRY_D572_HOOK() S6502_HLE_DISPATCH_D572()
#define S6502_AOT_ENTRY_F52A_HOOK() S6502_HLE_DISPATCH_F52A()
#define S6502_AOT_ENTRY_F549_HOOK() S6502_HLE_DISPATCH_F549()
#define S6502_AOT_ENTRY_F55B_HOOK() S6502_HLE_DISPATCH_F55B()
#elif defined(GAM4980_ENABLE_AOT)
#define S6502_AOT_ENTRY_5C5D_HOOK() ((void)0)
#define S6502_AOT_ENTRY_5CB3_HOOK() ((void)0)
#define S6502_AOT_ENTRY_5CE5_HOOK() ((void)0)
#define S6502_AOT_ENTRY_608A_HOOK() ((void)0)
#define S6502_AOT_ENTRY_650F_HOOK() ((void)0)
#define S6502_AOT_ENTRY_682D_HOOK() ((void)0)
#define S6502_AOT_ENTRY_690F_HOOK() ((void)0)
#define S6502_AOT_ENTRY_6988_HOOK() ((void)0)
#define S6502_AOT_ENTRY_6A75_HOOK() ((void)0)
#define S6502_AOT_ENTRY_6AA7_HOOK() ((void)0)
#define S6502_AOT_ENTRY_6AE0_HOOK() ((void)0)
#define S6502_AOT_ENTRY_6B1A_HOOK() ((void)0)
#define S6502_AOT_ENTRY_6BA4_HOOK() ((void)0)
#define S6502_AOT_ENTRY_7937_HOOK() ((void)0)
#define S6502_AOT_ENTRY_5351_HOOK() ((void)0)
#define S6502_AOT_ENTRY_5801_HOOK() ((void)0)
#define S6502_AOT_ENTRY_8039_HOOK() ((void)0)
#define S6502_AOT_ENTRY_859E_HOOK() ((void)0)
#define S6502_AOT_ENTRY_876B_HOOK() ((void)0)
#define S6502_AOT_ENTRY_D1A2_HOOK() ((void)0)
#define S6502_AOT_ENTRY_D2CA_HOOK() ((void)0)
#define S6502_AOT_ENTRY_D340_HOOK() ((void)0)
#define S6502_AOT_ENTRY_D349_HOOK() ((void)0)
#define S6502_AOT_ENTRY_D352_HOOK() ((void)0)
#define S6502_AOT_ENTRY_D35D_HOOK() ((void)0)
#define S6502_AOT_ENTRY_D35F_HOOK() ((void)0)
#define S6502_AOT_ENTRY_D596_HOOK() ((void)0)
#define S6502_AOT_ENTRY_D362_HOOK() ((void)0)
#define S6502_AOT_ENTRY_D572_HOOK() ((void)0)
#define S6502_AOT_ENTRY_F52A_HOOK() ((void)0)
#define S6502_AOT_ENTRY_F549_HOOK() ((void)0)
#define S6502_AOT_ENTRY_F55B_HOOK() ((void)0)
#ifdef GAM4980_ENABLE_GAME_LOAD_AOT
#define S6502_GAME_HLE_DISPATCH() ((void)0)
#endif
#endif
#ifdef GAM4980_ENABLE_PROFILING
static void profile_instruction(uint16_t virtual_pc, uint8_t opcode);
#define S6502_INSTRUCTION_HOOK(pc, opcode) profile_instruction(pc, opcode)
#endif

#define READ8(addr)       mem_read(addr)
#define READX8(addr)      mem_readx(addr)
#define READ16(addr)      mem_read16(addr)
#define READX16(addr)     mem_readx16(addr)
#define READ16W(addr)     mem_read16_wrapped(addr)
#define WRITE8(addr, val) mem_write(addr, val)
static uint8_t *s6502_stack_ram;
static uint8_t *s6502_page3;
#define S6502_FAST_STACK_RAM s6502_stack_ram
#define BRK_HOOK                 \
    {                            \
        executed = cycles;       \
        shutdown_pc = pc - 1u;   \
        pc = _MACCTL;            \
        shutdown_requested = 1;  \
    }
#include "s6502.c"
#ifdef GAM4980_ENABLE_GAME_LOAD_AOT
#undef S6502_GAME_AOT_EMIT_BLOCKS
#undef S6502_GAME_AOT_DISPATCH
#undef S6502_GAME_AOT_HIT
#undef S6502_GAME_AOT_SEMANTIC_HIT
#undef S6502_GAME_HLE_DISPATCH
#endif
#ifdef GAM4980_ENABLE_AOT
#define S6502_AOT_UNDEFINE
#include "s6502_aot_ebin_generated.h"
#undef S6502_AOT_UNDEFINE
#undef S6502_AOT_HIT
#undef S6502_AOT_ENTRY_5C5D_HOOK
#undef S6502_AOT_ENTRY_5CB3_HOOK
#undef S6502_AOT_ENTRY_5CE5_HOOK
#undef S6502_AOT_ENTRY_608A_HOOK
#undef S6502_AOT_ENTRY_650F_HOOK
#undef S6502_AOT_ENTRY_682D_HOOK
#undef S6502_AOT_ENTRY_690F_HOOK
#undef S6502_AOT_ENTRY_6988_HOOK
#undef S6502_AOT_ENTRY_6A75_HOOK
#undef S6502_AOT_ENTRY_6AA7_HOOK
#undef S6502_AOT_ENTRY_6AE0_HOOK
#undef S6502_AOT_ENTRY_6B1A_HOOK
#undef S6502_AOT_ENTRY_6BA4_HOOK
#undef S6502_AOT_ENTRY_7937_HOOK
#undef S6502_AOT_ENTRY_5351_HOOK
#undef S6502_AOT_ENTRY_5801_HOOK
#undef S6502_AOT_ENTRY_8039_HOOK
#undef S6502_AOT_ENTRY_859E_HOOK
#undef S6502_AOT_ENTRY_876B_HOOK
#undef S6502_AOT_ENTRY_D1A2_HOOK
#undef S6502_AOT_ENTRY_D2CA_HOOK
#undef S6502_AOT_ENTRY_D340_HOOK
#undef S6502_AOT_ENTRY_D349_HOOK
#undef S6502_AOT_ENTRY_D352_HOOK
#undef S6502_AOT_ENTRY_D35D_HOOK
#undef S6502_AOT_ENTRY_D35F_HOOK
#undef S6502_AOT_ENTRY_D596_HOOK
#undef S6502_AOT_ENTRY_D362_HOOK
#undef S6502_AOT_ENTRY_D572_HOOK
#undef S6502_AOT_ENTRY_F52A_HOOK
#undef S6502_AOT_ENTRY_F549_HOOK
#undef S6502_AOT_ENTRY_F55B_HOOK
#endif
#ifdef GAM4980_ENABLE_FIRMWARE_HLE
#undef S6502_HLE_EMIT_BLOCKS
#undef S6502_HLE_DISPATCH_5C5D
#undef S6502_HLE_DISPATCH_5CB3
#undef S6502_HLE_DISPATCH_5CE5
#undef S6502_HLE_DISPATCH_608A
#undef S6502_HLE_DISPATCH_650F
#undef S6502_HLE_DISPATCH_682D
#undef S6502_HLE_DISPATCH_690F
#undef S6502_HLE_DISPATCH_6988
#undef S6502_HLE_DISPATCH_6A75
#undef S6502_HLE_DISPATCH_6AA7
#undef S6502_HLE_DISPATCH_6AE0
#undef S6502_HLE_DISPATCH_6B1A
#undef S6502_HLE_DISPATCH_6BA4
#undef S6502_HLE_PICTURE_TAIL_DISPATCH
#undef S6502_PICTURE_TAIL_TEST_ENABLED
#undef S6502_PICTURE_TAIL_TEST_TRACE
#undef S6502_HLE_DISPATCH_7937
#undef S6502_HLE_DISPATCH_5351
#undef S6502_HLE_DISPATCH_5801
#undef S6502_HLE_PART_PICTURE_ROW_DISPATCH
#undef S6502_HLE_DISPATCH_8039
#undef S6502_HLE_DISPATCH_859E
#undef S6502_HLE_DISPATCH_876B
#undef S6502_HLE_DISPATCH_D1A2
#undef S6502_HLE_DISPATCH_D2CA
#undef S6502_HLE_DISPATCH_D340
#undef S6502_HLE_DISPATCH_D349
#undef S6502_HLE_DISPATCH_D352
#undef S6502_HLE_DISPATCH_D35D
#undef S6502_HLE_DISPATCH_D35F
#undef S6502_HLE_DISPATCH_D596
#undef S6502_HLE_DISPATCH_D362
#undef S6502_HLE_DISPATCH_D572
#undef S6502_HLE_DISPATCH_F52A
#undef S6502_HLE_DISPATCH_F549
#undef S6502_HLE_DISPATCH_F55B
#undef S6502_HLE_BANK_SWITCH_DISPATCH
#undef S6502_HLE_COMPARE_SUFFIX_COMMON
#undef S6502_HLE_HELPER_6646_CYCLES
#undef S6502_HLE_ATTEMPT
#undef S6502_HLE_ATTEMPT_EXTRA
#undef S6502_HLE_CONDITION_REJECT
#undef S6502_HLE_BUDGET_REJECT
#undef S6502_HLE_RECORD
#undef S6502_HLE_RECORD_BATCH
#undef S6502_HLE_RECORD_DIRECT_BATCH
#undef S6502_HLE_DIRECT_LCD_WRITE
#undef S6502_HLE_BITMAP_COPY
#undef S6502_HLE_GLYPH_ROW
#undef S6502_HLE_SHIFT_BLIT
#undef S6502_HLE_BYTE_FILL
#undef S6502_HLE_WIDE_GLYPH
#undef S6502_HLE_MULTIPLY16
#undef S6502_HLE_COMPARE16
#undef S6502_HLE_BANK_SWITCH
#undef S6502_HLE_C_RUNTIME
#undef S6502_HLE_GRAPHICS_ADDRESS
#ifdef GAM4980_ENABLE_GAME_LOAD_AOT
#undef S6502_GAME_HLE_EMIT_BLOCKS
#undef S6502_GAME_HLE_DISPATCH
#undef S6502_GAME_HLE_LOCALS
#endif
#endif
#ifdef GAM4980_ENABLE_PROFILING
#undef S6502_INSTRUCTION_HOOK
#endif
static struct {
    s6502_t      cpu;
    uint8_t     *mem_r[0x100];
    uint8_t    (*mem_ir[0x100])(uint16_t);
    void       (*mem_iw[0x100])(uint16_t, uint8_t);
    uint8_t     *ram;
    uint8_t     *flash;
    uint32_t     flash_size;
    uint8_t      flash_cmd;
    uint8_t      flash_cycles;
    uint8_t     *rom_8;                  /* font rom */
    uint8_t     *rom_e;                  /* os rom */
    gam4980_rom_read_fn rom_read;
    void        *rom_context;
    uint8_t      bk_sel;
    uint16_t     bk_tab[16];
    uint16_t     bk_sys_d;
} sys;

#ifdef GAM4980_ENABLE_PROFILING
static gam4980_instruction_profile_fn instruction_profile_callback;
static void *instruction_profile_context;
#endif

#ifdef GAM4980_ENABLE_AOT
#ifdef GAM4980_AOT_DIAGNOSTICS
static uint64_t s6502_aot_block_hits[S6502_AOT_BLOCK_COUNT];
static uint64_t s6502_aot_instruction_hits;
static uint16_t s6502_aot_bank2[S6502_AOT_BLOCK_COUNT];
static uint8_t s6502_aot_bank2_varies[S6502_AOT_BLOCK_COUNT];
#endif
#endif

#ifdef GAM4980_RUNTIME_PERFORMANCE_LOG
#define GAM4980_PERFORMANCE_SAMPLE_CAPACITY 512u
#define GAM4980_PERFORMANCE_SAMPLE_MAX_RECORDS 256u
typedef struct T_GAM4980_PerformanceSample {
    uint32_t physical_pc;
    uint32_t hits;
    uint16_t virtual_pc;
    uint16_t reserved;
} T_GAM4980_PerformanceSample;

static T_GAM4980_PerformanceSample
    performance_samples[GAM4980_PERFORMANCE_SAMPLE_CAPACITY]
    GAM4980_CACHE_STORAGE;
static uint64_t performance_guest_cycles;
static uint64_t performance_scheduled_cycles;
static uint64_t performance_halted_cycles;
static uint64_t performance_timer_ticks;
static uint32_t performance_exec_calls;
static uint32_t performance_step_frames;
static uint32_t performance_render_calls;
static uint32_t performance_dirty_render_calls;
static uint32_t performance_changed_render_calls;
static uint32_t performance_sample_count;
static uint32_t performance_sample_dropped;
#define GAM4980_PERFORMANCE_PC_SAMPLE_STRIDE 16u
#endif

#define ROM_BANK_SIZE 0x1000u
#define ROM_CACHE_LINES 32u
static uint8_t rom_bank_cache[ROM_CACHE_LINES][ROM_BANK_SIZE]
    GAM4980_CACHE_STORAGE;
static uint8_t rom_direct_cache[ROM_BANK_SIZE] GAM4980_CACHE_STORAGE;
static uint8_t rom_boot_page[0x100] GAM4980_CACHE_STORAGE;
static uint32_t rom_bank_page[ROM_CACHE_LINES];
static uint32_t rom_bank_stamp[ROM_CACHE_LINES];
static uint32_t rom_cache_clock;
static uint8_t rom_bank_region[ROM_CACHE_LINES];
static uint8_t rom_bank_valid[ROM_CACHE_LINES];
static uint8_t rom_slot_line[16];
static uint32_t rom_direct_page;
static uint8_t rom_direct_region;
static uint8_t rom_direct_valid;

#ifdef GAM4980_MEMORY_DIAGNOSTICS
volatile uint32_t g_gam4980_rom_trace_index;
volatile uint32_t g_gam4980_rom_trace[256];
#endif

static int rom_read_range(
    uint8_t region, uint32_t offset, uint8_t *out, uint32_t size
)
{
    const uint8_t *resident =
        region == GAM4980_ROM_REGION_8 ? sys.rom_8 : sys.rom_e;

    if ((!out && size) || offset > GAM4980_ROM_SIZE ||
        size > GAM4980_ROM_SIZE - offset)
        return 0;
    if (resident) {
        gam4980_memcpy(out, resident + offset, size);
        return 1;
    }
    if (!sys.rom_read)
        return 0;
    return sys.rom_read(sys.rom_context, region, offset, out, size);
}

static uint8_t *rom_cached_bank(
    uint8_t slot, uint8_t region, uint32_t page
)
{
    uint32_t oldest_stamp = 0xffffffffu;
    uint8_t selected = 0xffu;
    uint8_t line;

    for (line = 0; line < ROM_CACHE_LINES; ++line) {
        if (rom_bank_valid[line] && rom_bank_region[line] == region &&
            rom_bank_page[line] == page) {
            rom_slot_line[slot] = line;
            rom_bank_stamp[line] = ++rom_cache_clock;
            return rom_bank_cache[line];
        }
    }

    for (line = 0; line < ROM_CACHE_LINES; ++line) {
        uint8_t owner;
        int pinned = 0;

        if (!rom_bank_valid[line]) {
            selected = line;
            break;
        }
        for (owner = 0; owner < 16u; ++owner) {
            if (owner != slot && rom_slot_line[owner] == line) {
                pinned = 1;
                break;
            }
        }
        if (!pinned && rom_bank_stamp[line] < oldest_stamp) {
            oldest_stamp = rom_bank_stamp[line];
            selected = line;
        }
    }
    if (selected == 0xffu)
        return 0;
#ifdef GAM4980_MEMORY_DIAGNOSTICS
    {
        uint32_t trace_index = g_gam4980_rom_trace_index++;

        g_gam4980_rom_trace[trace_index & 255u] =
            ((uint32_t)slot << 28) | ((uint32_t)region << 27) |
            ((page >> 12) & 0x7ffffu);
    }
#endif
    rom_bank_valid[selected] = 0;
    if (!rom_read_range(
            region, page, rom_bank_cache[selected], ROM_BANK_SIZE
        ))
        return 0;
    rom_bank_region[selected] = region;
    rom_bank_page[selected] = page;
    rom_bank_stamp[selected] = ++rom_cache_clock;
    rom_bank_valid[selected] = 1;
    rom_slot_line[slot] = selected;
    return rom_bank_cache[selected];
}

static uint8_t rom_read_byte(uint8_t region, uint32_t offset)
{
    const uint8_t *resident =
        region == GAM4980_ROM_REGION_8 ? sys.rom_8 : sys.rom_e;
    uint32_t page;

    if (offset >= GAM4980_ROM_SIZE)
        return 0;
    if (resident)
        return resident[offset];
    page = offset & ~(ROM_BANK_SIZE - 1u);
    if (!rom_direct_valid || rom_direct_region != region ||
        rom_direct_page != page) {
        if (!rom_read_range(
                region, page, rom_direct_cache, sizeof(rom_direct_cache)
            ))
            return 0;
        rom_direct_region = region;
        rom_direct_page = page;
        rom_direct_valid = 1;
    }
    return rom_direct_cache[offset & (ROM_BANK_SIZE - 1u)];
}

static const uint16_t lcd_theme_colors[GAM4980_LCD_THEME_COUNT][2] = {
    { 0xd6da, 0x0000 },
    { 0x96e1, 0x0882 },
    { 0x3edd, 0x09a8 },
    { 0xf72c, 0x2920 },
};
static uint16_t lcd_bg = 0xd6da;
static uint16_t lcd_fg = 0x0000;

static void init_lcd_lut(void)
{
    uint32_t nibble;

    for (nibble = 0; nibble < 16u; ++nibble) {
        uint16_t p0 = nibble & 0x08u ? lcd_fg : lcd_bg;
        uint16_t p1 = nibble & 0x04u ? lcd_fg : lcd_bg;
        uint16_t p2 = nibble & 0x02u ? lcd_fg : lcd_bg;
        uint16_t p3 = nibble & 0x01u ? lcd_fg : lcd_bg;

        lcd_nibble_lut[nibble][0] = (uint32_t)p0 | (uint32_t)p1 << 16;
        lcd_nibble_lut[nibble][1] = (uint32_t)p2 | (uint32_t)p3 << 16;
    }
}

void gam4980_set_lcd_theme(u32 theme)
{
    if (theme >= GAM4980_LCD_THEME_COUNT)
        theme = GAM4980_LCD_THEME_OFF;
    lcd_bg = lcd_theme_colors[theme][0];
    lcd_fg = lcd_theme_colors[theme][1];
    init_lcd_lut();
}

u16 gam4980_lcd_background_color(void)
{
    return lcd_bg;
}

u16 gam4980_lcd_foreground_color(void)
{
    return lcd_fg;
}

static void s6502_push(uint8_t val)
{
    mem_write(0x100 | sys.cpu.sp--, val);
}

static bool sys_halt_p(void)
{
    return sys.ram[_SYSCON] & 0x08;
}

static inline uint32_t PA(uint16_t addr)
{
    uint8_t bank = addr >> 12;
    return (sys.bk_tab[bank] << 12) | (addr & 0x0fff);
}

#ifdef GAM4980_ENABLE_FIRMWARE_HLE
#ifdef GAM4980_ENABLE_AGGRESSIVE_REGION_HLE
static inline __attribute__((always_inline)) uint16_t
s6502_firmware_hle_zp16(const uint8_t *ram, uint32_t low)
{
    return (uint16_t)(ram[low] | ((uint16_t)ram[low + 1u] << 8));
}

static inline __attribute__((always_inline)) uint16_t
s6502_firmware_hle_shift_pair(uint8_t high, uint8_t low, uint8_t shift)
{
    uint16_t pair = (uint16_t)(((uint16_t)high << 8) | low);

    return shift < 16u ? (uint16_t)(pair >> shift) : 0u;
}

static inline __attribute__((always_inline)) uint32_t
s6502_firmware_hle_shift_cost(uint32_t shift)
{
    return shift ? 53u + 19u * shift : 55u;
}

static inline __attribute__((always_inline)) uint32_t
s6502_firmware_hle_shift_first_cost(uint32_t shift)
{
    return shift ? 26u + 19u * shift : 28u;
}

static inline __attribute__((always_inline)) uint32_t
s6502_firmware_hle_shift_destination_cost(uint16_t address)
{
    if (address == 0x0400u)
        return 75u;
    return (address >> 8) == 0x04u ? 41u : 31u;
}

static inline __attribute__((always_inline)) uint32_t
s6502_firmware_hle_shift_masked_destination_cost(uint16_t address)
{
    if (address == 0x0400u)
        return 87u;
    return (address >> 8) == 0x04u ? 53u : 43u;
}

static inline __attribute__((always_inline)) uint32_t
s6502_firmware_hle_shift_low_tail_destination_cost(uint16_t address)
{
    if (address == 0x0400u)
        return 84u;
    return (address >> 8) == 0x04u ? 51u : 41u;
}

static inline __attribute__((always_inline)) uint32_t
s6502_firmware_hle_address_update_cycles(uint8_t row, uint8_t column)
{
    if (row < 0x40u)
        return 59u;
    if (row == 0x40u)
        return 51u;
    if (row != 0x41u)
        return 67u;
    if (column < 8u)
        return 53u;
    return 76u + 40u * ((column - 8u) >> 3);
}

static inline __attribute__((always_inline)) int
s6502_firmware_hle_region_write_safe(uint16_t address, uint32_t size)
{
    uint32_t end = (uint32_t)address + size;

    return size && end <= 0x10000u && address >= 0x0300u &&
        !(address < 0x2100u && end > 0x2000u);
}

static __attribute__((noinline)) uint32_t
s6502_firmware_hle_shift_row_cycles(const uint8_t *ram)
{
    uint32_t width = ram[0x20e8u];
    uint32_t shift = ram[0x20cfu];
    uint32_t cycles;
    uint32_t remaining;
    uint16_t source = s6502_firmware_hle_zp16(ram, 0x2fu);
    uint16_t first_destination = s6502_firmware_hle_zp16(ram, 0x38u);
    uint16_t destination = s6502_firmware_hle_zp16(ram, 0x3au);
    uint8_t row = ram[0x2082u];
    uint8_t column = ram[0x2081u];

    if (!width || !ram[0x20dau] || shift > 7u ||
        source < 0x0300u || (uint32_t)source + width + 1u > 0x10000u ||
        !s6502_firmware_hle_region_write_safe(first_destination, 1u) ||
        !s6502_firmware_hle_region_write_safe(destination, width) ||
        s6502_page3[0xe5u] != 1u || s6502_page3[0xe6u] != 0u ||
        s6502_page3[0xe7u] != 4u || s6502_page3[0xe8u] != 0u ||
        s6502_page3[0xe9u] != 0x10u)
        return 0u;

    cycles = 23u + s6502_firmware_hle_shift_first_cost(shift) +
        (width > 1u ? 39u : 38u);
    remaining = width - 1u;
    while (remaining) {
        uint32_t after = remaining - 1u;

        cycles += s6502_firmware_hle_shift_cost(shift) +
            s6502_firmware_hle_shift_destination_cost(destination) +
            (after ? 39u : 37u);
        destination = (uint16_t)(destination + 1u);
        remaining = after;
    }

    if ((ram[0x2083u] & 7u) >= (column & 7u)) {
        cycles += 23u + s6502_firmware_hle_shift_cost(shift) + 14u;
        cycles +=
            s6502_firmware_hle_shift_masked_destination_cost(destination);
    } else {
        cycles += 25u + s6502_firmware_hle_shift_first_cost(shift) + 14u;
        cycles +=
            s6502_firmware_hle_shift_low_tail_destination_cost(destination);
    }
    cycles += s6502_firmware_hle_address_update_cycles(row, column);
    cycles += ram[0x20dau] == 1u ? 100u : 96u;
    return cycles;
}

static inline __attribute__((always_inline)) uint8_t
s6502_firmware_hle_add_high_overflow(
    uint8_t value, uint8_t result
)
{
    return (uint8_t)(
        ((uint8_t)~value & (uint8_t)(value ^ result) & 0x80u) != 0u
    );
}

static inline __attribute__((always_inline)) uint8_t
s6502_firmware_hle_sub_high_overflow(uint8_t value, uint8_t result)
{
    return (uint8_t)(((value ^ result) & value & 0x80u) != 0u);
}

static __attribute__((noinline)) int s6502_firmware_hle_shift_prefix(
    uint32_t sp, uint32_t status, uint32_t cycle_budget,
    s6502_hle_region_result_t *result
)
{
    uint8_t *ram = s6502_stack_ram;
    uint32_t width = ram[0x20e8u];
    uint32_t shift = ram[0x20cfu];
    uint32_t consumed = 23u + s6502_firmware_hle_shift_first_cost(shift) +
        (width > 1u ? 39u : 38u);
    uint32_t remaining = width - 1u;
    uint32_t status_value = status & 0xffu;
    uint32_t value;
    uint16_t source = s6502_firmware_hle_zp16(ram, 0x2fu);
    uint16_t first_destination = s6502_firmware_hle_zp16(ram, 0x38u);
    uint16_t destination = s6502_firmware_hle_zp16(ram, 0x3au);
    uint16_t target;
    uint16_t pair;
    uint8_t high;
    uint8_t high_result;

    s6502_hle_shift_cache.valid = 0u;
    if (consumed > cycle_budget)
        return 0;
    {
        uint8_t *source_pointer = 0;
        uint8_t *destination_pointer = 0;
        uint8_t *destination_1000 = 0;

        if (s6502_firmware_hle_read_span(
                source, width + 1u, &source_pointer
            ) &&
            s6502_firmware_hle_ram_span(
                ram, destination, width, &destination_pointer
            ) && destination_pointer >= ram + 0x0400u &&
            destination_pointer + width <= ram + 0x1001u &&
            (destination != 0x0400u ||
                (s6502_firmware_hle_ram_span(
                     ram, 0x1000u, 1u, &destination_1000
                 ) && destination_1000 == ram + 0x1000u))) {
            s6502_hle_shift_cache.source = source_pointer;
            s6502_hle_shift_cache.destination = destination_pointer;
            s6502_hle_shift_cache.destination_1000 = destination_1000;
            s6502_hle_shift_cache.source_address = source;
            s6502_hle_shift_cache.destination_address = destination;
            s6502_hle_shift_cache.source_size = (uint16_t)(width + 1u);
            s6502_hle_shift_cache.destination_size = (uint16_t)width;
            s6502_hle_shift_cache.valid = 1u;
        }
    }

    ram[0x20e7u] = 3u;
    ram[0x20d8u] = (uint8_t)width;
    ram[0x20e6u] = mem_read(source);
    ram[0x20e5u] = 0u;
    pair = s6502_firmware_hle_shift_pair(0u, ram[0x20e6u], (uint8_t)shift);
    ram[0x20e5u] = (uint8_t)(pair >> 8);
    ram[0x20e6u] = (uint8_t)pair;
    value = (uint32_t)mem_read(first_destination) & ram[0x20e3u];
    value |= ram[0x20e6u];
    mem_write(first_destination, (uint8_t)value);
    ram[0x20d8u] = (uint8_t)remaining;

    for (;;) {
        uint32_t after;
        uint32_t iteration_cycles;

        if (!remaining)
            break;
        after = remaining - 1u;
        iteration_cycles = s6502_firmware_hle_shift_cost(shift) +
            s6502_firmware_hle_shift_destination_cost(destination) +
            (after ? 39u : 37u);
        if (iteration_cycles > cycle_budget - consumed)
            break;

        consumed += iteration_cycles;
        ram[0x20e5u] = mem_read(source);
        source = (uint16_t)(source + 1u);
        ram[0x2fu] = (uint8_t)source;
        ram[0x30u] = (uint8_t)(source >> 8);
        ram[0x20e6u] = mem_read(source);
        pair = s6502_firmware_hle_shift_pair(
            ram[0x20e5u], ram[0x20e6u], (uint8_t)shift
        );
        ram[0x20e5u] = (uint8_t)(pair >> 8);
        ram[0x20e6u] = (uint8_t)pair;
        target = destination == 0x0400u ? 0x1000u : destination;
        mem_write(target, ram[0x20e6u]);

        high = (uint8_t)(destination >> 8);
        destination = (uint16_t)(destination + 1u);
        high_result = (uint8_t)(destination >> 8);
        status_value &= ~0x40u;
        if (s6502_firmware_hle_add_high_overflow(high, high_result))
            status_value |= 0x40u;
        ram[0x3au] = (uint8_t)destination;
        ram[0x3bu] = (uint8_t)(destination >> 8);

        remaining = after;
        ram[0x20d8u] = (uint8_t)remaining;
    }

    status_value &= ~0x83u;
    status_value |= 0x01u | ((uint8_t)remaining & 0x80u) |
        (remaining ? 0u : 0x02u);
    result->cycles = consumed;
    result->rows = 1u;
    result->pc = remaining ? 0x6a75u : 0x6b05u;
    result->ac = (uint8_t)remaining;
    result->ix = 0u;
    result->iy = 0u;
    result->sp = (uint8_t)sp;
    result->status = (uint8_t)status_value;
    return 1;
}

static __attribute__((noinline)) int s6502_firmware_hle_shift_region(
    uint32_t sp, uint32_t status, uint32_t cycle_budget,
    s6502_hle_region_result_t *result
)
{
    uint8_t *ram = s6502_stack_ram;
    uint32_t cycles = 0u;
    uint32_t rows = 0u;
    uint32_t status_value = status & 0xffu;
    uint16_t return_pc = 0x6988u;
    uint8_t stack_pointer = (uint8_t)sp;
    int failure = 0;

    for (;;) {
        uint32_t row_cycles = s6502_firmware_hle_shift_row_cycles(ram);
        uint32_t width;
        uint32_t remaining;
        uint32_t value;
        uint32_t sum;
        uint16_t source;
        uint16_t first_destination;
        uint16_t destination;
        uint16_t target;
        uint16_t pair;
        uint8_t shift;
        uint8_t row;
        uint8_t column;
        uint8_t low;
        uint8_t high;
        uint8_t carry;
        uint8_t high_result;
        uint8_t overflow;

        if (!row_cycles) {
            failure = -1;
            break;
        }
        if (row_cycles > cycle_budget - cycles) {
            if (!rows)
                return s6502_firmware_hle_shift_prefix(
                    sp, status, cycle_budget, result
                );
            failure = 0;
            break;
        }

        ++rows;
        cycles += row_cycles;
        width = ram[0x20e8u];
        shift = ram[0x20cfu];
        source = s6502_firmware_hle_zp16(ram, 0x2fu);
        first_destination = s6502_firmware_hle_zp16(ram, 0x38u);
        destination = s6502_firmware_hle_zp16(ram, 0x3au);
        row = ram[0x2082u];
        column = ram[0x2081u];

        ram[0x20e7u] = 3u;
        ram[0x20d8u] = (uint8_t)width;
        ram[0x20e6u] = mem_read(source);
        ram[0x20e5u] = 0u;
        pair = s6502_firmware_hle_shift_pair(0u, ram[0x20e6u], shift);
        ram[0x20e5u] = (uint8_t)(pair >> 8);
        ram[0x20e6u] = (uint8_t)pair;
        value = (uint32_t)mem_read(first_destination) & ram[0x20e3u];
        value |= ram[0x20e6u];
        mem_write(first_destination, (uint8_t)value);
        remaining = width - 1u;
        ram[0x20d8u] = (uint8_t)remaining;

        while (remaining) {
            ram[0x20e5u] = mem_read(source);
            source = (uint16_t)(source + 1u);
            ram[0x2fu] = (uint8_t)source;
            ram[0x30u] = (uint8_t)(source >> 8);
            ram[0x20e6u] = mem_read(source);
            pair = s6502_firmware_hle_shift_pair(
                ram[0x20e5u], ram[0x20e6u], shift
            );
            ram[0x20e5u] = (uint8_t)(pair >> 8);
            ram[0x20e6u] = (uint8_t)pair;

            target = destination == 0x0400u ? 0x1000u : destination;
            mem_write(target, ram[0x20e6u]);
            destination = (uint16_t)(destination + 1u);
            ram[0x3au] = (uint8_t)destination;
            ram[0x3bu] = (uint8_t)(destination >> 8);
            --remaining;
            ram[0x20d8u] = (uint8_t)remaining;
        }

        ram[0x20d0u] = (uint8_t)(column & 7u);
        if ((ram[0x2083u] & 7u) >= ram[0x20d0u]) {
            ram[0x20e5u] = mem_read(source);
            source = (uint16_t)(source + 1u);
            ram[0x2fu] = (uint8_t)source;
            ram[0x30u] = (uint8_t)(source >> 8);
            ram[0x20e6u] = mem_read(source);
        } else {
            ram[0x20e5u] = mem_read(source);
            ram[0x20e6u] = 0u;
        }
        pair = s6502_firmware_hle_shift_pair(
            ram[0x20e5u], ram[0x20e6u], shift
        );
        ram[0x20e5u] = (uint8_t)(pair >> 8);
        ram[0x20e6u] = (uint8_t)pair;
        ram[0x20e6u] = (uint8_t)(
            ram[0x20e6u] & (uint8_t)~ram[0x20e4u]
        );
        target = destination == 0x0400u ? 0x1000u : destination;
        value = (uint32_t)mem_read(target) & ram[0x20e4u];
        value |= ram[0x20e6u];
        mem_write(target, (uint8_t)value);

        low = (uint8_t)source;
        sum = (uint32_t)low + 1u;
        source = (uint16_t)(source + 1u);
        ram[0x2fu] = (uint8_t)source;
        ram[0x30u] = (uint8_t)(source >> 8);
        carry = (uint8_t)(sum >> 8);
        high = (uint8_t)((source - 1u) >> 8);
        high_result = (uint8_t)(high + carry);
        overflow = s6502_firmware_hle_add_high_overflow(
            high, high_result
        );

        ram[0x3au] = ram[0x20e9u];
        ram[0x3bu] = ram[0x20eau];
        ram[0x100u | stack_pointer] = 0x6cu;
        ram[0x100u | (uint8_t)(stack_pointer - 1u)] = 0x37u;

        if (row < 0x40u) {
            target = s6502_firmware_hle_zp16(ram, 0x3au);
            target = (uint16_t)(target - 0x20u);
            ram[0x3au] = (uint8_t)target;
            ram[0x3bu] = (uint8_t)(target >> 8);

            low = ram[0x38u];
            carry = (uint8_t)(low >= 0x20u);
            ram[0x38u] = (uint8_t)(low - 0x20u);
            high = ram[0x39u];
            high_result = (uint8_t)(high - (carry ? 0u : 1u));
            ram[0x39u] = high_result;
            overflow = s6502_firmware_hle_sub_high_overflow(
                high, high_result
            );
        } else if (row == 0x40u) {
            low = ram[0x3au];
            carry = (uint8_t)(low >= 0x20u);
            ram[0x3au] = (uint8_t)(low - 0x20u);
            high = ram[0x3bu];
            high_result = (uint8_t)(high - (carry ? 0u : 1u));
            ram[0x3bu] = high_result;
            overflow = s6502_firmware_hle_sub_high_overflow(
                high, high_result
            );
            ram[0x38u] = 0xf3u;
            ram[0x39u] = 0x0fu;
        } else if (row == 0x41u) {
            ram[0x38u] = 0x33u;
            ram[0x39u] = 0x0cu;
            ram[0x3au] = 0x40u;
            ram[0x3bu] = 0x0cu;
            if (column >= 8u) {
                uint8_t offset = (uint8_t)(column - 8u);
                uint8_t increments = (uint8_t)(offset >> 3);

                ram[0x20b7u] = (uint8_t)(offset & 7u);
                target = (uint16_t)(0x0c40u + increments);
                ram[0x3au] = (uint8_t)target;
                ram[0x3bu] = (uint8_t)(target >> 8);
                overflow = 0u;
            }
        } else {
            target = s6502_firmware_hle_zp16(ram, 0x3au);
            target = (uint16_t)(target + 0x20u);
            ram[0x3au] = (uint8_t)target;
            ram[0x3bu] = (uint8_t)(target >> 8);

            low = ram[0x38u];
            sum = (uint32_t)low + 0x20u;
            carry = (uint8_t)(sum >> 8);
            ram[0x38u] = (uint8_t)sum;
            high = ram[0x39u];
            high_result = (uint8_t)(high + carry);
            ram[0x39u] = high_result;
            overflow = s6502_firmware_hle_add_high_overflow(
                high, high_result
            );
        }

        ram[0x20e9u] = ram[0x3au];
        ram[0x20eau] = ram[0x3bu];
        ram[0x2082u] = (uint8_t)(row + 1u);
        ram[0x20dau] = (uint8_t)(ram[0x20dau] - 1u);
        value = ram[0x20dau];
        status_value &= ~0xc3u;
        status_value |= 0x01u | (overflow ? 0x40u : 0u) |
            (value & 0x80u) | (value ? 0u : 0x02u);

        if (!value) {
            stack_pointer = (uint8_t)(stack_pointer + 1u);
            return_pc = ram[0x100u | stack_pointer];
            stack_pointer = (uint8_t)(stack_pointer + 1u);
            return_pc = (uint16_t)(
                return_pc |
                ((uint16_t)ram[0x100u | stack_pointer] << 8)
            );
            return_pc = (uint16_t)(return_pc + 1u);
            break;
        }
        if (sys_halt_p())
            break;
    }

    if (!rows)
        return failure;
    result->cycles = cycles;
    result->rows = rows;
    result->pc = return_pc;
    result->ac = ram[0x20dau];
    result->ix = 0u;
    result->iy = 0u;
    result->sp = stack_pointer;
    result->status = (uint8_t)status_value;
    return 1;
}

static __attribute__((noinline)) int s6502_firmware_hle_picture_tail(
    int has_next_source_byte, uint32_t sp, uint32_t status,
    uint32_t cycle_budget, s6502_hle_region_result_t *result
)
{
    uint8_t *ram = s6502_stack_ram;
    uint32_t cycles;
    uint32_t pair;
    uint32_t sum;
    uint32_t status_value = status & 0xffu;
    uint16_t source = s6502_firmware_hle_zp16(ram, 0x2fu);
    uint16_t destination = s6502_firmware_hle_zp16(ram, 0x3au);
    uint16_t target;
    uint16_t return_pc;
    uint8_t shift = ram[0x20cfu];
    uint8_t picture_mode = ram[0x20e7u];
    uint8_t row = ram[0x2082u];
    uint8_t column = ram[0x2081u];
    uint8_t stack_pointer = (uint8_t)sp;
    uint8_t low;
    uint8_t high;
    uint8_t carry;
    uint8_t high_result;
    uint8_t overflow;
    uint8_t remaining;

    /* Common unconverted-picture row suffix at E.BIN
     * $6B1A/$6BA4-$6C80.  Limit the native form to picture modes 3/4 and the
     * standard $0400 -> $1000 LCD alias; unusual mappings keep the original
     * path. */
    if (!ram[0x20dau] || (picture_mode != 3u && picture_mode != 4u) ||
        shift > 7u ||
        source < 0x0300u ||
        (uint32_t)source + (has_next_source_byte ? 2u : 1u) > 0x10000u ||
        destination < 0x0400u ||
        !s6502_firmware_hle_region_write_safe(destination, 1u) ||
        s6502_page3[0xe5u] != 1u || s6502_page3[0xe6u] != 0u ||
        s6502_page3[0xe7u] != 4u || s6502_page3[0xe8u] != 0u ||
        s6502_page3[0xe9u] != 0x10u ||
        (!!has_next_source_byte !=
         ((ram[0x2083u] & 7u) >= (column & 7u))))
        return -1;

    cycles = has_next_source_byte
        ? 23u + s6502_firmware_hle_shift_cost(shift) + 14u +
            s6502_firmware_hle_shift_masked_destination_cost(destination)
        : 25u + s6502_firmware_hle_shift_first_cost(shift) + 14u +
            s6502_firmware_hle_shift_low_tail_destination_cost(destination);
    cycles += s6502_firmware_hle_address_update_cycles(row, column);
    cycles += ram[0x20dau] == 1u
        ? (picture_mode == 3u ? 100u : 106u)
        : (picture_mode == 3u ? 96u : 102u);
    /* In the production AOT chain, reaching $6988 after this suffix charges
     * 23/25 fewer cycles than instruction-by-instruction dispatcher re-entry.
     * Preserve that scheduling boundary as well as the architectural state. */
    cycles -= has_next_source_byte ? 23u : 25u;
    if (cycles > cycle_budget)
        return 0;

    ram[0x20e5u] = mem_read(source);
    if (has_next_source_byte) {
        source = (uint16_t)(source + 1u);
        ram[0x2fu] = (uint8_t)source;
        ram[0x30u] = (uint8_t)(source >> 8);
        ram[0x20e6u] = mem_read(source);
    } else {
        ram[0x20e6u] = 0u;
    }
    pair = s6502_firmware_hle_shift_pair(
        ram[0x20e5u], ram[0x20e6u], shift
    );
    ram[0x20e5u] = (uint8_t)(pair >> 8);
    ram[0x20e6u] = (uint8_t)pair;
    ram[0x20e6u] = (uint8_t)(
        ram[0x20e6u] & (uint8_t)~ram[0x20e4u]
    );
    target = destination == 0x0400u ? 0x1000u : destination;
    pair = (uint32_t)mem_read(target) & ram[0x20e4u];
    pair |= ram[0x20e6u];
    mem_write(target, (uint8_t)pair);

    low = (uint8_t)source;
    sum = (uint32_t)low + 1u;
    source = (uint16_t)(source + 1u);
    ram[0x2fu] = (uint8_t)source;
    ram[0x30u] = (uint8_t)(source >> 8);
    carry = (uint8_t)(sum >> 8);
    high = (uint8_t)((source - 1u) >> 8);
    high_result = (uint8_t)(high + carry);
    overflow = s6502_firmware_hle_add_high_overflow(high, high_result);

    ram[0x3au] = ram[0x20e9u];
    ram[0x3bu] = ram[0x20eau];
    /* Preserve the two stack bytes written by the real JSR $6646. */
    ram[0x100u | stack_pointer] = 0x6cu;
    ram[0x100u | (uint8_t)(stack_pointer - 1u)] = 0x37u;

    if (row < 0x40u) {
        target = s6502_firmware_hle_zp16(ram, 0x3au);
        target = (uint16_t)(target - 0x20u);
        ram[0x3au] = (uint8_t)target;
        ram[0x3bu] = (uint8_t)(target >> 8);

        low = ram[0x38u];
        carry = (uint8_t)(low >= 0x20u);
        ram[0x38u] = (uint8_t)(low - 0x20u);
        high = ram[0x39u];
        high_result = (uint8_t)(high - (carry ? 0u : 1u));
        ram[0x39u] = high_result;
        overflow = s6502_firmware_hle_sub_high_overflow(high, high_result);
    } else if (row == 0x40u) {
        low = ram[0x3au];
        carry = (uint8_t)(low >= 0x20u);
        ram[0x3au] = (uint8_t)(low - 0x20u);
        high = ram[0x3bu];
        high_result = (uint8_t)(high - (carry ? 0u : 1u));
        ram[0x3bu] = high_result;
        overflow = s6502_firmware_hle_sub_high_overflow(high, high_result);
        ram[0x38u] = 0xf3u;
        ram[0x39u] = 0x0fu;
    } else if (row == 0x41u) {
        ram[0x38u] = 0x33u;
        ram[0x39u] = 0x0cu;
        ram[0x3au] = 0x40u;
        ram[0x3bu] = 0x0cu;
        if (column >= 8u) {
            uint8_t offset = (uint8_t)(column - 8u);
            uint8_t increments = (uint8_t)(offset >> 3);

            ram[0x20b7u] = (uint8_t)(offset & 7u);
            target = (uint16_t)(0x0c40u + increments);
            ram[0x3au] = (uint8_t)target;
            ram[0x3bu] = (uint8_t)(target >> 8);
            overflow = 0u;
        }
    } else {
        target = s6502_firmware_hle_zp16(ram, 0x3au);
        target = (uint16_t)(target + 0x20u);
        ram[0x3au] = (uint8_t)target;
        ram[0x3bu] = (uint8_t)(target >> 8);

        low = ram[0x38u];
        sum = (uint32_t)low + 0x20u;
        carry = (uint8_t)(sum >> 8);
        ram[0x38u] = (uint8_t)sum;
        high = ram[0x39u];
        high_result = (uint8_t)(high + carry);
        ram[0x39u] = high_result;
        overflow = s6502_firmware_hle_add_high_overflow(high, high_result);
    }

    ram[0x20e9u] = ram[0x3au];
    ram[0x20eau] = ram[0x3bu];
    ram[0x2082u] = (uint8_t)(row + 1u);
    remaining = (uint8_t)(ram[0x20dau] - 1u);
    ram[0x20dau] = remaining;

    status_value &= ~0xc3u;
    status_value |= 0x01u | (overflow ? 0x40u : 0u) |
        (remaining & 0x80u) | (remaining ? 0u : 0x02u);
    if (remaining) {
        return_pc = picture_mode == 3u ? 0x6988u : 0x69d9u;
    } else {
        stack_pointer = (uint8_t)(stack_pointer + 1u);
        return_pc = ram[0x100u | stack_pointer];
        stack_pointer = (uint8_t)(stack_pointer + 1u);
        return_pc = (uint16_t)(
            return_pc | ((uint16_t)ram[0x100u | stack_pointer] << 8)
        );
        return_pc = (uint16_t)(return_pc + 1u);
    }

    result->cycles = cycles;
    result->rows = 1u;
    result->pc = return_pc;
    result->ac = remaining;
    result->ix = 0u;
    result->iy = 0u;
    result->sp = stack_pointer;
    result->status = (uint8_t)status_value;
    return 1;
}

static __attribute__((noinline)) uint32_t
s6502_firmware_hle_bitmap_row_cycles(const uint8_t *ram)
{
    uint32_t width = ram[0x20e8u];
    uint32_t shift = ram[0x20cfu];
    uint32_t source = s6502_firmware_hle_zp16(ram, 0x2fu);
    uint32_t destination = s6502_firmware_hle_zp16(ram, 0x31u);
    uint32_t source_bytes = width ? width + 1u : 1u;
    uint32_t destination_bytes = width ? width + 1u : 1u;
    uint32_t common = ram[0x20dau] == 1u ? 67u : 63u;
    uint32_t first_shift;
    uint32_t inner_nonfinal;
    uint32_t inner_final;
    uint32_t tail;
    uint32_t cycles;

    /* The row estimator is side-effect free.  Limit the accelerated form to
     * ordinary source memory and the LCD/buffer window, which guarantees an
     * indirect write cannot alias the routine's zero-page/control fields. */
    if (source < 0x0300u || source + source_bytes > 0x10000u ||
        destination < 0x0400u ||
        destination + destination_bytes > 0x10000u ||
        (destination < 0x2100u &&
         destination + destination_bytes > 0x2000u))
        return 0u;
    if (!width)
        return 19u + (shift == 0u ? 64u : 62u + 19u * shift) + common;

    first_shift = shift == 0u ? 28u : 26u + 19u * shift;
    inner_nonfinal = shift == 0u ? 103u : 101u + 19u * shift;
    inner_final = shift == 0u ? 101u : 99u + 19u * shift;
    cycles = 17u + first_shift + 43u + (width == 1u ? 16u : 14u);
    if (width > 1u)
        cycles += (width - 2u) * inner_nonfinal + inner_final;
    if ((ram[0x2083u] & 7u) >= (ram[0x2081u] & 7u))
        tail = 22u + (shift == 0u ? 93u : 91u + 19u * shift);
    else
        tail = 23u + (shift == 0u ? 63u : 61u + 19u * shift);
    return cycles + tail + common;
}

static __attribute__((noinline)) int s6502_firmware_hle_bitmap_region(
    uint32_t sp, uint32_t status, uint32_t cycle_budget,
    s6502_hle_region_result_t *result
)
{
    uint8_t *ram = s6502_stack_ram;
    uint32_t cycles = 0u;
    uint32_t rows = 0u;
    uint32_t status_value = status & 0xffu;
    uint32_t value;
    uint32_t sum;
    uint16_t address;
    uint16_t pair;
    uint8_t width;
    uint8_t remaining;
    uint8_t shift;
    uint8_t low;
    uint8_t high;
    uint8_t carry;
    int failure = 0;

    /* This is an instruction-order specialization of E.BIN
     * $5C5D-$5DC9, including its width-zero tail at $5DCA.  RAM control
     * fields are intentionally re-read after every indirect write so even
     * unusual aliasing follows the original routine rather than a cached
     * rectangle description. */
    for (;;) {
        uint32_t row_cycles = s6502_firmware_hle_bitmap_row_cycles(ram);

        if (!row_cycles) {
            failure = -1;
            break;
        }
        if (row_cycles > cycle_budget - cycles) {
            failure = 0;
            break;
        }
        ++rows;
        width = ram[0x20e8u];
        ram[0x20d8u] = width;
        cycles += 14u; /* LDA/STA/LDA/CMP */

        if (width) {
            cycles += 3u; /* taken BNE $5C6D */

            address = s6502_firmware_hle_zp16(ram, 0x2fu);
            ram[0x20e6u] = mem_read(address);
            ram[0x20e5u] = 0u;
            shift = ram[0x20cfu];
            pair = s6502_firmware_hle_shift_pair(
                ram[0x20e5u], ram[0x20e6u], shift
            );
            ram[0x20e5u] = (uint8_t)(pair >> 8);
            ram[0x20e6u] = (uint8_t)pair;
            cycles += shift == 0u ? 28u : 26u + 19u * shift;

            address = s6502_firmware_hle_zp16(ram, 0x31u);
            value = (uint32_t)mem_read(address) & ram[0x20e3u];
            value |= ram[0x20e6u];
            mem_write(address, (uint8_t)value);
            cycles += 21u;

            sum = (uint32_t)ram[0x31u] + 1u;
            ram[0x31u] = (uint8_t)sum;
            sum = (uint32_t)ram[0x32u] + (sum >> 8);
            ram[0x32u] = (uint8_t)sum;
            cycles += 22u;

            remaining = (uint8_t)(ram[0x20d8u] - 1u);
            ram[0x20d8u] = remaining;
            cycles += remaining ? 14u : 16u;

            while (remaining) {
                address = s6502_firmware_hle_zp16(ram, 0x2fu);
                ram[0x20e5u] = mem_read(address);
                sum = (uint32_t)ram[0x2fu] + 1u;
                ram[0x2fu] = (uint8_t)sum;
                sum = (uint32_t)ram[0x30u] + (sum >> 8);
                ram[0x30u] = (uint8_t)sum;
                address = s6502_firmware_hle_zp16(ram, 0x2fu);
                ram[0x20e6u] = mem_read(address);

                shift = ram[0x20cfu];
                pair = s6502_firmware_hle_shift_pair(
                    ram[0x20e5u], ram[0x20e6u], shift
                );
                ram[0x20e5u] = (uint8_t)(pair >> 8);
                ram[0x20e6u] = (uint8_t)pair;

                address = s6502_firmware_hle_zp16(ram, 0x31u);
                mem_write(address, ram[0x20e6u]);
                sum = (uint32_t)ram[0x31u] + 1u;
                ram[0x31u] = (uint8_t)sum;
                sum = (uint32_t)ram[0x32u] + (sum >> 8);
                ram[0x32u] = (uint8_t)sum;

                remaining = (uint8_t)(ram[0x20d8u] - 1u);
                ram[0x20d8u] = remaining;
                cycles += (shift == 0u ? 55u : 53u + 19u * shift) +
                    32u + (remaining ? 16u : 14u);
            }

            ram[0x20d0u] = (uint8_t)(ram[0x2081u] & 7u);
            if ((ram[0x2083u] & 7u) >= ram[0x20d0u]) {
                cycles += 22u;
                address = s6502_firmware_hle_zp16(ram, 0x2fu);
                ram[0x20e5u] = mem_read(address);
                sum = (uint32_t)ram[0x2fu] + 1u;
                ram[0x2fu] = (uint8_t)sum;
                sum = (uint32_t)ram[0x30u] + (sum >> 8);
                ram[0x30u] = (uint8_t)sum;
                address = s6502_firmware_hle_zp16(ram, 0x2fu);
                ram[0x20e6u] = mem_read(address);
                shift = ram[0x20cfu];
                pair = s6502_firmware_hle_shift_pair(
                    ram[0x20e5u], ram[0x20e6u], shift
                );
                ram[0x20e5u] = (uint8_t)(pair >> 8);
                ram[0x20e6u] = (uint8_t)pair;
                ram[0x20e6u] = (uint8_t)(
                    ram[0x20e6u] & (uint8_t)~ram[0x20e4u]
                );
                address = s6502_firmware_hle_zp16(ram, 0x31u);
                value = (uint32_t)mem_read(address) & ram[0x20e4u];
                value |= ram[0x20e6u];
                mem_write(address, (uint8_t)value);
                cycles += shift == 0u ? 93u : 91u + 19u * shift;
            } else {
                cycles += 23u;
                address = s6502_firmware_hle_zp16(ram, 0x2fu);
                ram[0x20e5u] = mem_read(address);
                ram[0x20e6u] = 0u;
                shift = ram[0x20cfu];
                pair = s6502_firmware_hle_shift_pair(
                    ram[0x20e5u], ram[0x20e6u], shift
                );
                ram[0x20e5u] = (uint8_t)(pair >> 8);
                ram[0x20e6u] = (uint8_t)pair;
                ram[0x20e6u] = (uint8_t)(
                    ram[0x20e6u] & (uint8_t)~ram[0x20e4u]
                );
                address = s6502_firmware_hle_zp16(ram, 0x31u);
                value = (uint32_t)mem_read(address) & ram[0x20e4u];
                value |= ram[0x20e6u];
                mem_write(address, (uint8_t)value);
                cycles += shift == 0u ? 63u : 61u + 19u * shift;
            }
        } else {
            cycles += 5u; /* not-taken BNE plus JMP $5DCA */
            ram[0x20e3u] = (uint8_t)(ram[0x20e3u] | ram[0x20e4u]);
            address = s6502_firmware_hle_zp16(ram, 0x2fu);
            ram[0x20e6u] = mem_read(address);
            ram[0x20e5u] = 0u;
            shift = ram[0x20cfu];
            pair = s6502_firmware_hle_shift_pair(
                ram[0x20e5u], ram[0x20e6u], shift
            );
            ram[0x20e5u] = (uint8_t)(pair >> 8);
            ram[0x20e6u] = (uint8_t)pair;
            address = s6502_firmware_hle_zp16(ram, 0x31u);
            value = (uint32_t)mem_read(address) & ram[0x20e3u];
            value |= ram[0x20e6u];
            mem_write(address, (uint8_t)value);
            cycles += shift == 0u ? 64u : 62u + 19u * shift;
        }

        sum = (uint32_t)ram[0x2fu] + 1u;
        ram[0x2fu] = (uint8_t)sum;
        sum = (uint32_t)ram[0x30u] + (sum >> 8);
        ram[0x30u] = (uint8_t)sum;
        cycles += 22u;

        low = ram[0x31u];
        sum = (uint32_t)ram[0x20deu] + low;
        ram[0x31u] = (uint8_t)sum;
        carry = (uint8_t)(sum >> 8);
        high = ram[0x32u];
        sum = (uint32_t)high + carry;
        ram[0x32u] = (uint8_t)sum;
        status_value &= ~0x40u;
        if (high == 0x7fu && carry)
            status_value |= 0x40u;
        cycles += 24u;

        ram[0x20dau] = (uint8_t)(ram[0x20dau] - 1u);
        if (ram[0x20dau])
            cycles += 17u; /* DEC/LDA/CMP, not-taken BEQ, JMP */
        else
            cycles += 21u; /* DEC/LDA/CMP, taken BEQ, RTS */
        if (!ram[0x20dau] || sys_halt_p())
            break;
    }

    if (!rows)
        return failure;
    result->cycles = cycles;
    result->rows = rows;
    result->ix = 0u;
    result->iy = 0u;
    if (!ram[0x20dau]) {
        status_value = (status_value & ~0x83u) | 0x03u;
        sp = (uint8_t)(sp + 1u);
        address = ram[0x100u | (uint8_t)sp];
        sp = (uint8_t)(sp + 1u);
        address = (uint16_t)(
            address | ((uint16_t)ram[0x100u | (uint8_t)sp] << 8)
        );
        result->pc = (uint16_t)(address + 1u);
        result->ac = 0u;
        result->sp = (uint8_t)sp;
        result->status = (uint8_t)status_value;
    } else {
        value = ram[0x20dau];
        status_value = (status_value & ~0x83u) | 0x01u | (value & 0x80u);
        result->pc = 0x5c5du;
        result->ac = (uint8_t)value;
        result->sp = (uint8_t)sp;
        result->status = (uint8_t)status_value;
    }
    return 1;
}
#endif

static __attribute__((noinline)) uint16_t
s6502_firmware_hle_compare_cycles(void)
{
    uint8_t low = s6502_stack_ram[0x20u];
    uint8_t high = s6502_stack_ram[0x21u];
    uint8_t other_low = s6502_stack_ram[0x23u];
    uint8_t other_high = s6502_stack_ram[0x24u];
    uint16_t difference = (uint16_t)(
        (uint16_t)low + (uint8_t)~other_low + 1u
    );
    uint8_t low_result = (uint8_t)difference;
    uint8_t carry = difference > 0xffu;
    uint8_t high_result = (uint8_t)(
        (uint16_t)high + (uint8_t)~other_high + carry
    );

    if (low_result)
        return high_result ? 66u : 58u;
    return high_result ? 58u : 49u;
}

static __attribute__((noinline)) void s6502_firmware_hle_compare16(
    uint32_t sp, uint32_t status, s6502_hle_compare_result_t *result
)
{
    uint8_t low = s6502_stack_ram[0x20u];
    uint8_t high = s6502_stack_ram[0x21u];
    uint8_t other_low = s6502_stack_ram[0x23u];
    uint8_t other_high = s6502_stack_ram[0x24u];
    uint16_t difference;
    uint8_t low_result;
    uint8_t high_result;
    uint8_t carry;
    uint8_t index = 0u;
    uint8_t final_status;
    uint8_t stack_pointer = (uint8_t)sp;

    difference = (uint16_t)(
        (uint16_t)low + (uint8_t)~other_low + 1u
    );
    low_result = (uint8_t)difference;
    carry = difference > 0xffu;
    if (low_result)
        ++index;

    difference = (uint16_t)(
        (uint16_t)high + (uint8_t)~other_high + carry
    );
    high_result = (uint8_t)difference;
    if (high_result)
        ++index;

    final_status = (uint8_t)(status & ~(0xc3u));
    if (difference > 0xffu)
        final_status |= 0x01u;
    if (!index)
        final_status |= 0x02u;
    if (((high ^ difference) &
         ((uint8_t)~other_high ^ difference) & 0x80u) != 0u)
        final_status |= 0x40u;
    final_status |= high_result & 0x80u;
    final_status |= 0x30u;

    /* PHP/PLA/PHA/PLP leaves the edited status byte at the current stack
     * slot.  RTS then consumes the caller's two-byte return address. */
    s6502_stack_ram[0x100u | stack_pointer] = final_status;
    stack_pointer = (uint8_t)(stack_pointer + 1u);
    result->pc = s6502_stack_ram[0x100u | stack_pointer];
    stack_pointer = (uint8_t)(stack_pointer + 1u);
    result->pc = (uint16_t)(
        result->pc |
        ((uint16_t)s6502_stack_ram[0x100u | stack_pointer] << 8)
    );
    result->pc = (uint16_t)(result->pc + 1u);
    result->ac = final_status;
    result->ix = index;
    result->sp = stack_pointer;
    result->status = final_status;
}

static __attribute__((noinline)) int s6502_firmware_hle_compare_match(void)
{
    static const uint8_t signature[] = {
        0xa2, 0x00, 0xa5, 0x20, 0x38, 0xe5, 0x23, 0xf0, 0x03,
        0x08, 0xe8, 0x28, 0xa5, 0x21, 0xe5, 0x24, 0xf0, 0x03,
        0x08, 0xe8, 0x28, 0x08, 0x68, 0x09, 0x02, 0xe0, 0x00,
        0xf0, 0x02, 0x29, 0xfd, 0x48, 0x28, 0x60,
    };
    uint32_t index;

    if (PA(0xd340u) != 0xea8340u)
        return 0;
    if (s6502_firmware_hle_compare_validation == 1u)
        return 1;
    if (s6502_firmware_hle_compare_validation == 2u)
        return 0;
    for (index = 0; index < sizeof(signature); ++index) {
        if (mem_readx((uint16_t)(0xd340u + index)) != signature[index]) {
            s6502_firmware_hle_compare_validation = 2u;
            return 0;
        }
    }
    s6502_firmware_hle_compare_validation = 1u;
    return 1;
}

static __attribute__((noinline)) uint16_t
s6502_firmware_hle_multiply_cycles(void)
{
    uint16_t multiplicand = (uint16_t)(
        s6502_stack_ram[0x20u] | (s6502_stack_ram[0x21u] << 8)
    );
    uint16_t multiplier = (uint16_t)(
        s6502_stack_ram[0x23u] | (s6502_stack_ram[0x24u] << 8)
    );
    uint16_t bits;

    if (multiplicand == 0u)
        return 61u;
    if (multiplier == 0u)
        return 69u;

    bits = multiplier;
    bits = (uint16_t)(bits - ((bits >> 1) & 0x5555u));
    bits = (uint16_t)((bits & 0x3333u) + ((bits >> 2) & 0x3333u));
    bits = (uint16_t)((bits + (bits >> 4)) & 0x0f0fu);
    bits = (uint16_t)((bits + (bits >> 8)) & 0x001fu);
    return (uint16_t)(
        ((multiplier & 0xff00u) ? 443u : 259u) + 19u * bits
    );
}

static __attribute__((noinline)) int s6502_firmware_hle_multiply_match(void)
{
    uint32_t fnv = 2166136261u;
    uint32_t sdbm = 0u;
    uint16_t address;

    if (PA(0xd1a2u) != 0xea81a2u)
        return 0;
    if (s6502_firmware_hle_multiply_validation == 1u)
        return 1;
    if (s6502_firmware_hle_multiply_validation == 2u)
        return 0;
    for (address = 0xd1a2u; address < 0xd201u; ++address) {
        uint8_t byte = mem_readx(address);

        fnv = (fnv ^ byte) * 16777619u;
        sdbm = byte + (sdbm << 6) + (sdbm << 16) - sdbm;
    }
    if (fnv != 0x968ee6ebu || sdbm != 0x73f4773au) {
        s6502_firmware_hle_multiply_validation = 2u;
        return 0;
    }
    s6502_firmware_hle_multiply_validation = 1u;
    return 1;
}

static __attribute__((noinline)) int
s6502_firmware_hle_bank_switch_match(void)
{
    uint32_t fnv = 2166136261u;
    uint32_t sdbm = 0u;
    uint16_t address;

    if (PA(0xf52au) != 0xeaa52au)
        return 0;
    if (s6502_firmware_hle_bank_switch_validation == 1u)
        return 1;
    if (s6502_firmware_hle_bank_switch_validation == 2u)
        return 0;
    for (address = 0xf52au; address < 0xf5bdu; ++address) {
        uint8_t byte = mem_readx(address);

        fnv = (fnv ^ byte) * 16777619u;
        sdbm = byte + (sdbm << 6) + (sdbm << 16) - sdbm;
    }
    if (fnv != 0xc617743du || sdbm != 0x82080ad6u) {
        s6502_firmware_hle_bank_switch_validation = 2u;
        return 0;
    }
    s6502_firmware_hle_bank_switch_validation = 1u;
    return 1;
}

static __attribute__((noinline)) int
s6502_firmware_hle_c_runtime_match(void)
{
    uint32_t and_fnv = 2166136261u;
    uint32_t and_sdbm = 0u;
    uint32_t load_fnv = 2166136261u;
    uint32_t load_sdbm = 0u;
    uint16_t address;

    if (PA(0xd2cau) != 0xea82cau || PA(0xd596u) != 0xea8596u)
        return 0;
    if (s6502_firmware_hle_c_runtime_validation == 1u)
        return 1;
    if (s6502_firmware_hle_c_runtime_validation == 2u)
        return 0;
    for (address = 0xd2cau; address < 0xd2f6u; ++address) {
        uint8_t byte = mem_readx(address);

        and_fnv = (and_fnv ^ byte) * 16777619u;
        and_sdbm = byte + (and_sdbm << 6) +
            (and_sdbm << 16) - and_sdbm;
    }
    for (address = 0xd596u; address < 0xd5a6u; ++address) {
        uint8_t byte = mem_readx(address);

        load_fnv = (load_fnv ^ byte) * 16777619u;
        load_sdbm = byte + (load_sdbm << 6) +
            (load_sdbm << 16) - load_sdbm;
    }
    if (and_fnv != 0x6fd059c6u || and_sdbm != 0x66ec6555u ||
        load_fnv != 0x2a1dd7d9u || load_sdbm != 0x46c88a40u) {
        s6502_firmware_hle_c_runtime_validation = 2u;
        return 0;
    }
    s6502_firmware_hle_c_runtime_validation = 1u;
    return 1;
}

static __attribute__((noinline)) int
s6502_firmware_hle_compare_long_match(void)
{
    uint32_t fnv = 2166136261u;
    uint32_t sdbm = 0u;
    uint16_t address;

    if (PA(0xd362u) != 0xea8362u)
        return 0;
    if (s6502_firmware_hle_compare_long_validation == 1u)
        return 1;
    if (s6502_firmware_hle_compare_long_validation == 2u)
        return 0;
    for (address = 0xd362u; address < 0xd39bu; ++address) {
        uint8_t byte = mem_readx(address);

        fnv = (fnv ^ byte) * 16777619u;
        sdbm = byte + (sdbm << 6) + (sdbm << 16) - sdbm;
    }
    if (fnv != 0x050f7506u || sdbm != 0x10161945u) {
        s6502_firmware_hle_compare_long_validation = 2u;
        return 0;
    }
    s6502_firmware_hle_compare_long_validation = 1u;
    return 1;
}

static __attribute__((noinline)) int
s6502_firmware_hle_indirect_call_match(void)
{
    uint32_t fnv = 2166136261u;
    uint32_t sdbm = 0u;
    uint16_t address;

    if (PA(0xd572u) != 0xea8572u)
        return 0;
    if (s6502_firmware_hle_indirect_call_validation == 1u)
        return 1;
    if (s6502_firmware_hle_indirect_call_validation == 2u)
        return 0;
    for (address = 0xd572u; address < 0xd586u; ++address) {
        uint8_t byte = mem_readx(address);

        fnv = (fnv ^ byte) * 16777619u;
        sdbm = byte + (sdbm << 6) + (sdbm << 16) - sdbm;
    }
    if (fnv != 0x1ea69e7fu || sdbm != 0x52f6da4cu) {
        s6502_firmware_hle_indirect_call_validation = 2u;
        return 0;
    }
    s6502_firmware_hle_indirect_call_validation = 1u;
    return 1;
}

static const uint8_t s6502_firmware_hle_blit_signature[] = {
    0xa0, 0x00, 0xb1, 0x2f, 0x8d, 0xe5, 0x20, 0x18, 0xad, 0x2f, 0x00, 0x69,
    0x01, 0x8d, 0x2f, 0x00, 0xad, 0x30, 0x00, 0x69, 0x00, 0x8d, 0x30, 0x00,
    0xa0, 0x00, 0xb1, 0x2f, 0x8d, 0xe6, 0x20, 0xad, 0xcf, 0x20, 0xaa, 0xe0,
    0x00, 0xf0, 0x0b, 0x4e, 0xe5, 0x20, 0x6e, 0xe6, 0x20, 0xca, 0xe0, 0x00,
    0xd0, 0xf5, 0xad, 0xe5, 0x03, 0xc9, 0x01, 0xd0, 0x32, 0xad, 0x3b, 0x00,
    0xcd, 0xe7, 0x03, 0xd0, 0x2a, 0xad, 0x3a, 0x00, 0xcd, 0xe6, 0x03, 0xd0,
    0x22, 0xad, 0xe8, 0x03, 0x8d, 0x3a, 0x00, 0xad, 0xe9, 0x03, 0x8d, 0x3b,
    0x00, 0xa0, 0x00, 0xad, 0xe6, 0x20, 0x91, 0x3a, 0xad, 0xe6, 0x03, 0x8d,
    0x3a, 0x00, 0xad, 0xe7, 0x03, 0x8d, 0x3b, 0x00, 0x4c, 0xe7, 0x6a, 0xa0,
    0x00, 0xad, 0xe6, 0x20, 0x91, 0x3a, 0x18, 0xad, 0x3a, 0x00, 0x69, 0x01,
    0x8d, 0x3a, 0x00, 0xad, 0x3b, 0x00, 0x69, 0x00, 0x8d, 0x3b, 0x00, 0xce,
    0xd8, 0x20, 0xad, 0xd8, 0x20, 0xc9, 0x00, 0xf0, 0x03, 0x4c, 0x75, 0x6a,
};

static __attribute__((noinline)) int s6502_firmware_hle_match(void)
{
    uint32_t index;

    if (PA(0x6a75u) != 0xeb5a75u)
        return 0;
    if (s6502_page3[0xe5u] != 0x01u || s6502_page3[0xe6u] != 0x00u ||
        s6502_page3[0xe7u] != 0x04u || s6502_page3[0xe8u] != 0x00u ||
        s6502_page3[0xe9u] != 0x10u)
        return 0;
    if (s6502_firmware_hle_validation == 1u)
        return 1;
    if (s6502_firmware_hle_validation == 2u)
        return 0;
    for (index = 0;
         index < sizeof(s6502_firmware_hle_blit_signature); ++index) {
        if (mem_readx((uint16_t)(0x6a75u + index)) !=
            s6502_firmware_hle_blit_signature[index]) {
            s6502_firmware_hle_validation = 2u;
            return 0;
        }
    }
    s6502_firmware_hle_validation = 1u;
    return 1;
}

static __attribute__((noinline)) int s6502_firmware_hle_glyph_match(void)
{
    uint32_t fnv = 2166136261u;
    uint32_t sdbm = 0u;
    uint16_t address;

    if (PA(0x650bu) != 0xeb550bu)
        return 0;
    if (s6502_firmware_hle_glyph_validation == 1u)
        return 1;
    if (s6502_firmware_hle_glyph_validation == 2u)
        return 0;
    for (address = 0x650bu; address < 0x66fdu; ++address) {
        uint8_t byte = mem_readx(address);

        fnv = (fnv ^ byte) * 16777619u;
        sdbm = byte + (sdbm << 6) + (sdbm << 16) - sdbm;
    }
    if (fnv != 0x2b9d8422u || sdbm != 0x842e5893u) {
        s6502_firmware_hle_glyph_validation = 2u;
        return 0;
    }
    s6502_firmware_hle_glyph_validation = 1u;
    return 1;
}

static __attribute__((noinline)) int s6502_firmware_hle_wide_glyph_match(void)
{
    uint32_t fnv = 2166136261u;
    uint32_t sdbm = 0u;
    uint16_t address;

    if (PA(0x6086u) != 0xeb5086u)
        return 0;
    if (s6502_firmware_hle_wide_glyph_validation == 1u)
        return 1;
    if (s6502_firmware_hle_wide_glyph_validation == 2u)
        return 0;
    for (address = 0x6086u; address < 0x61c5u; ++address) {
        uint8_t byte = mem_readx(address);

        fnv = (fnv ^ byte) * 16777619u;
        sdbm = byte + (sdbm << 6) + (sdbm << 16) - sdbm;
    }
    if (fnv != 0x6b27cb48u || sdbm != 0xcba8f879u) {
        s6502_firmware_hle_wide_glyph_validation = 2u;
        return 0;
    }
    s6502_firmware_hle_wide_glyph_validation = 1u;
    return 1;
}

__attribute__((noinline)) uint32_t
s6502_firmware_hle_glyph_bits(uint32_t value, uint32_t shift)
{
    uint8_t high;
    uint8_t low;
    uint8_t left_mask;
    uint8_t right_mask;

    /*
     * S1C33 keeps narrow C values in full-width registers.  Canonicalize the
     * input explicitly before a right shift so stale upper bits cannot enter
     * the low byte.  Keep every shift count constant as well.  A separate
     * noinline helper keeps these temporaries out of the register-heavy CPU
     * dispatcher and makes both requirements visible in target disassembly.
     */
    value &= 0xffu;
    shift &= 7u;
    switch (shift) {
    default:
    case 0u:
        high = value;
        low = 0u;
        left_mask = 0u;
        right_mask = 0xffu;
        break;
    case 1u:
        high = (uint8_t)(value >> 1);
        low = (uint8_t)(value << 7);
        left_mask = 0x80u;
        right_mask = 0x7fu;
        break;
    case 2u:
        high = (uint8_t)(value >> 2);
        low = (uint8_t)(value << 6);
        left_mask = 0xc0u;
        right_mask = 0x3fu;
        break;
    case 3u:
        high = (uint8_t)(value >> 3);
        low = (uint8_t)(value << 5);
        left_mask = 0xe0u;
        right_mask = 0x1fu;
        break;
    case 4u:
        high = (uint8_t)(value >> 4);
        low = (uint8_t)(value << 4);
        left_mask = 0xf0u;
        right_mask = 0x0fu;
        break;
    case 5u:
        high = (uint8_t)(value >> 5);
        low = (uint8_t)(value << 3);
        left_mask = 0xf8u;
        right_mask = 0x07u;
        break;
    case 6u:
        high = (uint8_t)(value >> 6);
        low = (uint8_t)(value << 2);
        left_mask = 0xfcu;
        right_mask = 0x03u;
        break;
    case 7u:
        high = (uint8_t)(value >> 7);
        low = (uint8_t)(value << 1);
        left_mask = 0xfeu;
        right_mask = 0x01u;
        break;
    }

    return (uint32_t)high | ((uint32_t)low << 8) |
           ((uint32_t)left_mask << 16) | ((uint32_t)right_mask << 24);
}

S6502_HLE_GLYPH_ROW_ATTRIBUTE void s6502_firmware_hle_glyph_row(
    uint32_t ix, uint32_t sp, uint32_t status,
    s6502_hle_glyph_result_t *result)
{
    uint8_t *ram = s6502_stack_ram;
    uint32_t source_index = ix & 0xffu;
    uint32_t stack_pointer = sp & 0xffu;
    uint32_t status_value = status & 0xffu;
    uint32_t source;
    uint32_t destination;
    uint32_t alternate;
    uint32_t value;
    uint32_t bits;
    uint32_t column;
    uint32_t row;
    uint32_t return_address;
    uint32_t carry;

    /*
     * Keep the complete HLE row outside s6502_exec().  The S1C33 backend can
     * otherwise reuse dirty upper bits of its byte-sized CPU-register locals
     * across the many indirect memory calls in this block.  Full-width values
     * with explicit masks make each 6502 wrap point unambiguous.
     */
    source = (uint32_t)ram[0x2fu] | ((uint32_t)ram[0x30u] << 8);
    value = (uint32_t)mem_read((uint16_t)((source + source_index) & 0xffffu));
    bits = s6502_firmware_hle_glyph_bits(
        value & 0xffu, (uint32_t)mem_read(0x208bu) & 7u
    );
    mem_write(0x20b3u, (uint8_t)bits);
    mem_write(0x20b4u, (uint8_t)(bits >> 8));
    mem_write(0x20e5u, (uint8_t)(bits >> 16));
    mem_write(0x20e6u, (uint8_t)(bits >> 24));

    column = (uint32_t)mem_read(0x2081u) & 0xffu;
    destination = (uint32_t)ram[0x3au] | ((uint32_t)ram[0x3bu] << 8);
    if (column == 0x98u) {
        result->iy = 0u;
        value = (uint32_t)mem_read(0x20b3u) & 0xfeu;
        mem_write(0x20e6u, (uint8_t)value);
        value = ((uint32_t)mem_read((uint16_t)destination) & 0x01u) | value;
        mem_write((uint16_t)destination, (uint8_t)value);
        return_address = 0x663du;
    } else if (column >= 8u) {
        result->iy = 1u;
        alternate = (uint32_t)s6502_page3[0xe6u] |
                    ((uint32_t)s6502_page3[0xe7u] << 8);
        if (s6502_page3[0xe5u] == 1u && destination == alternate) {
            alternate = (uint32_t)s6502_page3[0xe8u] |
                        ((uint32_t)s6502_page3[0xe9u] << 8);
            value = ((uint32_t)mem_read((uint16_t)alternate) &
                     (uint32_t)mem_read(0x20e5u)) |
                    (uint32_t)mem_read(0x20b3u);
            mem_write((uint16_t)alternate, (uint8_t)value);
            return_address = 0x6598u;
        } else {
            value = ((uint32_t)mem_read((uint16_t)destination) &
                     (uint32_t)mem_read(0x20e5u)) |
                    (uint32_t)mem_read(0x20b3u);
            mem_write((uint16_t)destination, (uint8_t)value);
            return_address = 0x65b9u;
        }
        value = ((uint32_t)mem_read(
                     (uint16_t)((destination + 1u) & 0xffffu)) &
                 (uint32_t)mem_read(0x20e6u)) |
                (uint32_t)mem_read(0x20b4u);
        mem_write(
            (uint16_t)((destination + 1u) & 0xffffu), (uint8_t)value
        );
    } else {
        result->iy = 0u;
        alternate = (uint32_t)ram[0x38u] | ((uint32_t)ram[0x39u] << 8);
        value = ((uint32_t)mem_read((uint16_t)alternate) &
                 (uint32_t)mem_read(0x20e5u)) |
                (uint32_t)mem_read(0x20b3u);
        mem_write((uint16_t)alternate, (uint8_t)value);

        alternate = (uint32_t)s6502_page3[0xe6u] |
                    ((uint32_t)s6502_page3[0xe7u] << 8);
        if (s6502_page3[0xe5u] == 1u && destination == alternate) {
            alternate = (uint32_t)s6502_page3[0xe8u] |
                        ((uint32_t)s6502_page3[0xe9u] << 8);
            value = ((uint32_t)mem_read((uint16_t)alternate) &
                     (uint32_t)mem_read(0x20e6u)) |
                    (uint32_t)mem_read(0x20b4u);
            mem_write((uint16_t)alternate, (uint8_t)value);
            return_address = 0x660au;
        } else {
            value = ((uint32_t)mem_read((uint16_t)destination) &
                     (uint32_t)mem_read(0x20e6u)) |
                    (uint32_t)mem_read(0x20b4u);
            mem_write((uint16_t)destination, (uint8_t)value);
            return_address = 0x6620u;
        }
    }

    /* Preserve the two stack bytes written by the inlined JSR. */
    ram[0x100u | stack_pointer] = (uint8_t)(return_address >> 8);
    ram[0x100u | ((stack_pointer - 1u) & 0xffu)] =
        (uint8_t)return_address;

    row = (uint32_t)mem_read(0x2082u) & 0xffu;
    if (row < 0x40u) {
        destination = (uint32_t)ram[0x3au] | ((uint32_t)ram[0x3bu] << 8);
        destination = (destination - 0x20u) & 0xffffu;
        ram[0x3au] = (uint8_t)destination;
        ram[0x3bu] = (uint8_t)(destination >> 8);

        destination = (uint32_t)ram[0x38u] | ((uint32_t)ram[0x39u] << 8);
        carry = destination >= 0x20u;
        destination = (destination - 0x20u) & 0xffffu;
        ram[0x38u] = (uint8_t)destination;
        ram[0x39u] = (uint8_t)(destination >> 8);
        value = destination >> 8;
        status_value = (status_value & ~1u) | carry;
    } else if (row == 0x40u) {
        destination = (uint32_t)ram[0x3au] | ((uint32_t)ram[0x3bu] << 8);
        carry = destination >= 0x20u;
        destination = (destination - 0x20u) & 0xffffu;
        ram[0x3au] = (uint8_t)destination;
        ram[0x3bu] = (uint8_t)(destination >> 8);
        ram[0x38u] = 0xf3u;
        ram[0x39u] = 0x0fu;
        value = 0x0fu;
        status_value = (status_value & ~1u) | carry;
    } else if (row == 0x41u) {
        ram[0x38u] = 0x33u;
        ram[0x39u] = 0x0cu;
        ram[0x3au] = 0x40u;
        ram[0x3bu] = 0x0cu;
        value = (uint32_t)mem_read(0x2081u) & 0xffu;
        status_value &= ~1u;
        if (value >= 8u) {
            column = (value - 8u) & 0xffu;
            mem_write(0x20b7u, (uint8_t)column);
            while (column >= 8u) {
                column = (column - 8u) & 0xffu;
                mem_write(0x20b7u, (uint8_t)column);
                destination = (uint32_t)ram[0x3au] |
                              ((uint32_t)ram[0x3bu] << 8);
                destination = (destination + 1u) & 0xffffu;
                ram[0x3au] = (uint8_t)destination;
                ram[0x3bu] = (uint8_t)(destination >> 8);
            }
            value = (column - 8u) & 0xffu;
        }
    } else {
        destination = (uint32_t)ram[0x3au] | ((uint32_t)ram[0x3bu] << 8);
        destination = (destination + 0x20u) & 0xffffu;
        ram[0x3au] = (uint8_t)destination;
        ram[0x3bu] = (uint8_t)(destination >> 8);

        destination = (uint32_t)ram[0x38u] | ((uint32_t)ram[0x39u] << 8);
        carry = destination > 0xffdfu;
        destination = (destination + 0x20u) & 0xffffu;
        ram[0x38u] = (uint8_t)destination;
        ram[0x39u] = (uint8_t)(destination >> 8);
        value = destination >> 8;
        status_value = (status_value & ~1u) | carry;
    }

    mem_write(0x2082u, (uint8_t)((row + 1u) & 0xffu));
    source_index = (source_index + 1u) & 0xffu;
    status_value = (status_value & ~0x82u) | (source_index & 0x80u) |
                   (source_index ? 0u : 0x02u);

    result->ac = (uint8_t)value;
    result->ix = (uint8_t)source_index;
    result->status = (uint8_t)status_value;
}

S6502_HLE_GLYPH_ROW_ATTRIBUTE void s6502_firmware_hle_wide_glyph_row(
    uint32_t ix, uint32_t sp, uint32_t status,
    s6502_hle_glyph_result_t *result)
{
    uint8_t *ram = s6502_stack_ram;
    uint32_t source_index = ix & 0xffu;
    uint32_t stack_pointer = sp & 0xffu;
    uint32_t status_value = status & 0xffu;
    uint32_t source;
    uint32_t destination;
    uint32_t alternate;
    uint32_t raw;
    uint32_t bits;
    uint32_t value;
    uint32_t b3;
    uint32_t b4;
    uint32_t b5;
    uint32_t left_mask;
    uint32_t right_mask;
    uint32_t shift;
    uint32_t column;
    uint32_t row;
    uint32_t return_address;
    uint32_t carry;

    source = (uint32_t)ram[0x2fu] | ((uint32_t)ram[0x30u] << 8);
    b3 = (uint32_t)mem_read(
        (uint16_t)((source + source_index) & 0xffffu)
    );
    b4 = (uint32_t)mem_read(
        (uint16_t)((source + source_index + 1u) & 0xffffu)
    );
    raw = ((b3 & 0xffu) << 16) | ((b4 & 0xffu) << 8);
    shift = (uint32_t)mem_read(0x208bu) & 7u;
    switch (shift) {
    default:
    case 0u:
        bits = raw;
        left_mask = 0u;
        right_mask = 0xffu;
        break;
    case 1u:
        bits = raw >> 1;
        left_mask = 0x80u;
        right_mask = 0x7fu;
        break;
    case 2u:
        bits = raw >> 2;
        left_mask = 0xc0u;
        right_mask = 0x3fu;
        break;
    case 3u:
        bits = raw >> 3;
        left_mask = 0xe0u;
        right_mask = 0x1fu;
        break;
    case 4u:
        bits = raw >> 4;
        left_mask = 0xf0u;
        right_mask = 0x0fu;
        break;
    case 5u:
        bits = raw >> 5;
        left_mask = 0xf8u;
        right_mask = 0x07u;
        break;
    case 6u:
        bits = raw >> 6;
        left_mask = 0xfcu;
        right_mask = 0x03u;
        break;
    case 7u:
        bits = raw >> 7;
        left_mask = 0xfeu;
        right_mask = 0x01u;
        break;
    }
    b3 = (bits >> 16) & 0xffu;
    b4 = (bits >> 8) & 0xffu;
    b5 = bits & 0xffu;
    mem_write(0x20b3u, (uint8_t)b3);
    mem_write(0x20b4u, (uint8_t)b4);
    mem_write(0x20b5u, (uint8_t)b5);
    mem_write(0x20e5u, (uint8_t)left_mask);
    mem_write(0x20e6u, (uint8_t)right_mask);

    column = (uint32_t)mem_read(0x2081u) & 0xffu;
    destination = (uint32_t)ram[0x3au] | ((uint32_t)ram[0x3bu] << 8);
    if (column == 0x90u) {
        result->iy = 0u;
        value = ((uint32_t)mem_read((uint16_t)destination) & left_mask) | b3;
        mem_write((uint16_t)destination, (uint8_t)value);
        result->iy = 1u;
        right_mask = b4 & 0xfeu;
        mem_write(0x20e6u, (uint8_t)right_mask);
        value = ((uint32_t)mem_read(
                     (uint16_t)((destination + 1u) & 0xffffu)) & 0x01u) |
                right_mask;
        mem_write(
            (uint16_t)((destination + 1u) & 0xffffu), (uint8_t)value
        );
        return_address = 0x61bbu;
    } else if (column >= 8u) {
        result->iy = 0u;
        alternate = (uint32_t)s6502_page3[0xe6u] |
                    ((uint32_t)s6502_page3[0xe7u] << 8);
        if (s6502_page3[0xe5u] == 1u && destination == alternate) {
            alternate = (uint32_t)s6502_page3[0xe8u] |
                        ((uint32_t)s6502_page3[0xe9u] << 8);
            value = ((uint32_t)mem_read((uint16_t)alternate) & left_mask) |
                    b3;
            mem_write((uint16_t)alternate, (uint8_t)value);
        } else {
            value = ((uint32_t)mem_read((uint16_t)destination) & left_mask) |
                    b3;
            mem_write((uint16_t)destination, (uint8_t)value);
        }
        mem_write(
            (uint16_t)((destination + 1u) & 0xffffu), (uint8_t)b4
        );
        value = ((uint32_t)mem_read(
                     (uint16_t)((destination + 2u) & 0xffffu)) & right_mask) |
                b5;
        mem_write(
            (uint16_t)((destination + 2u) & 0xffffu), (uint8_t)value
        );
        result->iy = 2u;
        return_address = 0x6130u;
    } else {
        result->iy = 0u;
        alternate = (uint32_t)ram[0x38u] | ((uint32_t)ram[0x39u] << 8);
        value = ((uint32_t)mem_read((uint16_t)alternate) & left_mask) | b3;
        mem_write((uint16_t)alternate, (uint8_t)value);

        alternate = (uint32_t)s6502_page3[0xe6u] |
                    ((uint32_t)s6502_page3[0xe7u] << 8);
        if (s6502_page3[0xe5u] == 1u && destination == alternate) {
            alternate = (uint32_t)s6502_page3[0xe8u] |
                        ((uint32_t)s6502_page3[0xe9u] << 8);
            mem_write((uint16_t)alternate, (uint8_t)b4);
        } else {
            mem_write((uint16_t)destination, (uint8_t)b4);
        }
        value = ((uint32_t)mem_read(
                     (uint16_t)((destination + 1u) & 0xffffu)) & right_mask) |
                b5;
        mem_write(
            (uint16_t)((destination + 1u) & 0xffffu), (uint8_t)value
        );
        result->iy = 1u;
        return_address = 0x6192u;
    }

    ram[0x100u | stack_pointer] = (uint8_t)(return_address >> 8);
    ram[0x100u | ((stack_pointer - 1u) & 0xffu)] =
        (uint8_t)return_address;

    /* Inline the shared $6646 address-update helper exactly as the regular
     * glyph-row HLE does. */
    row = (uint32_t)mem_read(0x2082u) & 0xffu;
    if (row < 0x40u) {
        destination = (uint32_t)ram[0x3au] | ((uint32_t)ram[0x3bu] << 8);
        destination = (destination - 0x20u) & 0xffffu;
        ram[0x3au] = (uint8_t)destination;
        ram[0x3bu] = (uint8_t)(destination >> 8);

        destination = (uint32_t)ram[0x38u] | ((uint32_t)ram[0x39u] << 8);
        carry = destination >= 0x20u;
        destination = (destination - 0x20u) & 0xffffu;
        ram[0x38u] = (uint8_t)destination;
        ram[0x39u] = (uint8_t)(destination >> 8);
        value = destination >> 8;
        status_value = (status_value & ~1u) | carry;
    } else if (row == 0x40u) {
        destination = (uint32_t)ram[0x3au] | ((uint32_t)ram[0x3bu] << 8);
        carry = destination >= 0x20u;
        destination = (destination - 0x20u) & 0xffffu;
        ram[0x3au] = (uint8_t)destination;
        ram[0x3bu] = (uint8_t)(destination >> 8);
        ram[0x38u] = 0xf3u;
        ram[0x39u] = 0x0fu;
        value = 0x0fu;
        status_value = (status_value & ~1u) | carry;
    } else if (row == 0x41u) {
        ram[0x38u] = 0x33u;
        ram[0x39u] = 0x0cu;
        ram[0x3au] = 0x40u;
        ram[0x3bu] = 0x0cu;
        value = (uint32_t)mem_read(0x2081u) & 0xffu;
        status_value &= ~1u;
        if (value >= 8u) {
            column = (value - 8u) & 0xffu;
            mem_write(0x20b7u, (uint8_t)column);
            while (column >= 8u) {
                column = (column - 8u) & 0xffu;
                mem_write(0x20b7u, (uint8_t)column);
                destination = (uint32_t)ram[0x3au] |
                              ((uint32_t)ram[0x3bu] << 8);
                destination = (destination + 1u) & 0xffffu;
                ram[0x3au] = (uint8_t)destination;
                ram[0x3bu] = (uint8_t)(destination >> 8);
            }
            value = (column - 8u) & 0xffu;
        }
    } else {
        destination = (uint32_t)ram[0x3au] | ((uint32_t)ram[0x3bu] << 8);
        destination = (destination + 0x20u) & 0xffffu;
        ram[0x3au] = (uint8_t)destination;
        ram[0x3bu] = (uint8_t)(destination >> 8);

        destination = (uint32_t)ram[0x38u] | ((uint32_t)ram[0x39u] << 8);
        carry = destination > 0xffdfu;
        destination = (destination + 0x20u) & 0xffffu;
        ram[0x38u] = (uint8_t)destination;
        ram[0x39u] = (uint8_t)(destination >> 8);
        value = destination >> 8;
        status_value = (status_value & ~1u) | carry;
    }

    mem_write(0x2082u, (uint8_t)((row + 1u) & 0xffu));
    source_index = (source_index + 2u) & 0xffu;
    status_value = (status_value & ~0x82u) | (source_index & 0x80u) |
                   (source_index ? 0u : 0x02u);

    result->ac = (uint8_t)value;
    result->ix = (uint8_t)source_index;
    result->status = (uint8_t)status_value;
}

static __attribute__((noinline)) int s6502_firmware_hle_bitmap_match(void)
{
    uint32_t fnv = 2166136261u;
    uint32_t sdbm = 0u;
    uint16_t address;

    if (PA(0x5cb3u) != 0xeb8cb3u)
        return 0;
    if (s6502_firmware_hle_bitmap_validation == 1u)
        return 1;
    if (s6502_firmware_hle_bitmap_validation == 2u)
        return 0;
    for (address = 0x5cb3u; address < 0x5d05u; ++address) {
        uint8_t byte = mem_readx(address);

        fnv = (fnv ^ byte) * 16777619u;
        sdbm = byte + (sdbm << 6) + (sdbm << 16) - sdbm;
    }
    if (fnv != 0xb65dad14u || sdbm != 0xbbe189f5u) {
        s6502_firmware_hle_bitmap_validation = 2u;
        return 0;
    }
    s6502_firmware_hle_bitmap_validation = 1u;
    return 1;
}

static __attribute__((noinline)) int
s6502_firmware_hle_bitmap_region_match(void)
{
    uint32_t fnv = 2166136261u;
    uint32_t sdbm = 0u;
    uint16_t address;

    if (PA(0x5c5du) != 0xeb8c5du)
        return 0;
    if (s6502_firmware_hle_bitmap_region_validation == 1u)
        return 1;
    if (s6502_firmware_hle_bitmap_region_validation == 2u)
        return 0;
    for (address = 0x5c5du; address < 0x5e02u; ++address) {
        uint8_t byte = mem_readx(address);

        fnv = (fnv ^ byte) * 16777619u;
        sdbm = byte + (sdbm << 6) + (sdbm << 16) - sdbm;
    }
    if (fnv != 0xd4189b35u || sdbm != 0x66029860u) {
        s6502_firmware_hle_bitmap_region_validation = 2u;
        return 0;
    }
    s6502_firmware_hle_bitmap_region_validation = 1u;
    return 1;
}

static __attribute__((noinline)) int
s6502_firmware_hle_shift_region_match(void)
{
    uint32_t main_fnv = 2166136261u;
    uint32_t main_sdbm = 0u;
    uint32_t helper_fnv = 2166136261u;
    uint32_t helper_sdbm = 0u;
    uint16_t address;

    if (PA(0x6988u) != 0xeb5988u || PA(0x6646u) != 0xeb5646u)
        return 0;
    if (s6502_firmware_hle_shift_region_validation == 1u)
        return 1;
    if (s6502_firmware_hle_shift_region_validation == 2u)
        return 0;
    for (address = 0x6988u; address < 0x6c76u; ++address) {
        uint8_t byte = mem_readx(address);

        main_fnv = (main_fnv ^ byte) * 16777619u;
        main_sdbm = byte + (main_sdbm << 6) +
            (main_sdbm << 16) - main_sdbm;
    }
    for (address = 0x6646u; address < 0x66fdu; ++address) {
        uint8_t byte = mem_readx(address);

        helper_fnv = (helper_fnv ^ byte) * 16777619u;
        helper_sdbm = byte + (helper_sdbm << 6) +
            (helper_sdbm << 16) - helper_sdbm;
    }
    if (main_fnv != 0x7d71054fu || main_sdbm != 0xd9a6fdb6u ||
        helper_fnv != 0xe034894cu || helper_sdbm != 0x7bcd364fu) {
        s6502_firmware_hle_shift_region_validation = 2u;
        return 0;
    }
    s6502_firmware_hle_shift_region_validation = 1u;
    return 1;
}

static __attribute__((noinline)) int
s6502_firmware_hle_picture_head_match(void)
{
    uint32_t fnv = 2166136261u;
    uint32_t sdbm = 0u;
    uint16_t address;

    if (PA(0x682du) != 0xeb582du || PA(0x690fu) != 0xeb590fu)
        return 0;
    if (s6502_firmware_hle_picture_head_validation == 1u)
        return 1;
    if (s6502_firmware_hle_picture_head_validation == 2u)
        return 0;
    for (address = 0x682du; address < 0x6930u; ++address) {
        uint8_t byte = mem_readx(address);

        fnv = (fnv ^ byte) * 16777619u;
        sdbm = byte + (sdbm << 6) + (sdbm << 16) - sdbm;
    }
    if (fnv != 0xcd732c34u || sdbm != 0xe1b519e9u) {
        s6502_firmware_hle_picture_head_validation = 2u;
        return 0;
    }
    s6502_firmware_hle_picture_head_validation = 1u;
    return 1;
}

static __attribute__((noinline)) int
s6502_firmware_hle_graphics_address_match(void)
{
    uint32_t fnv = 2166136261u;
    uint32_t sdbm = 0u;
    uint16_t address;

    if (PA(0x876bu) != 0xeb776bu)
        return 0;
    if (s6502_firmware_hle_graphics_address_validation == 1u)
        return 1;
    if (s6502_firmware_hle_graphics_address_validation == 2u)
        return 0;
    for (address = 0x876bu; address < 0x8888u; ++address) {
        uint8_t byte = mem_readx(address);

        fnv = (fnv ^ byte) * 16777619u;
        sdbm = byte + (sdbm << 6) + (sdbm << 16) - sdbm;
    }
    if (fnv != 0x70513f73u || sdbm != 0x7421cdcau) {
        s6502_firmware_hle_graphics_address_validation = 2u;
        return 0;
    }
    s6502_firmware_hle_graphics_address_validation = 1u;
    return 1;
}

static __attribute__((noinline)) int
s6502_firmware_hle_hline_match(void)
{
    uint32_t fnv = 2166136261u;
    uint32_t sdbm = 0u;
    uint16_t address;

    if (PA(0x8039u) != 0xeb7039u)
        return 0;
    if (s6502_firmware_hle_hline_validation == 1u)
        return 1;
    if (s6502_firmware_hle_hline_validation == 2u)
        return 0;
    for (address = 0x8039u; address < 0x808eu; ++address) {
        uint8_t byte = mem_readx(address);

        fnv = (fnv ^ byte) * 16777619u;
        sdbm = byte + (sdbm << 6) + (sdbm << 16) - sdbm;
    }
    if (fnv != 0x8e5a0564u || sdbm != 0x5007ae9fu) {
        s6502_firmware_hle_hline_validation = 2u;
        return 0;
    }
    s6502_firmware_hle_hline_validation = 1u;
    return 1;
}

static __attribute__((noinline)) int
s6502_firmware_hle_part_picture_match(void)
{
    uint32_t right_fnv = 2166136261u;
    uint32_t right_sdbm = 0u;
    uint32_t left_fnv = 2166136261u;
    uint32_t left_sdbm = 0u;
    uint16_t address;

    if (PA(0x5351u) != 0xeb8351u || PA(0x5801u) != 0xeb8801u)
        return 0;
    if (s6502_firmware_hle_part_picture_validation == 1u)
        return 1;
    if (s6502_firmware_hle_part_picture_validation == 2u)
        return 0;
    for (address = 0x5351u; address < 0x53ddu; ++address) {
        uint8_t byte = mem_readx(address);

        right_fnv = (right_fnv ^ byte) * 16777619u;
        right_sdbm = byte + (right_sdbm << 6) +
            (right_sdbm << 16) - right_sdbm;
    }
    for (address = 0x5801u; address < 0x588du; ++address) {
        uint8_t byte = mem_readx(address);

        left_fnv = (left_fnv ^ byte) * 16777619u;
        left_sdbm = byte + (left_sdbm << 6) +
            (left_sdbm << 16) - left_sdbm;
    }
    if (right_fnv != 0xfa4930e1u || right_sdbm != 0x051f789eu ||
        left_fnv != 0x0d7c0641u || left_sdbm != 0xe7c9ab9eu) {
        s6502_firmware_hle_part_picture_validation = 2u;
        return 0;
    }
    s6502_firmware_hle_part_picture_validation = 1u;
    return 1;
}

static __attribute__((noinline)) int
s6502_firmware_hle_pixel_tail_match(void)
{
    uint32_t fnv = 2166136261u;
    uint32_t sdbm = 0u;
    uint16_t address;

    if (PA(0x859eu) != 0xeb759eu)
        return 0;
    if (s6502_firmware_hle_pixel_tail_validation == 1u)
        return 1;
    if (s6502_firmware_hle_pixel_tail_validation == 2u)
        return 0;
    for (address = 0x859eu; address < 0x865au; ++address) {
        uint8_t byte = mem_readx(address);

        fnv = (fnv ^ byte) * 16777619u;
        sdbm = byte + (sdbm << 6) + (sdbm << 16) - sdbm;
    }
    if (fnv != 0x737d7a91u || sdbm != 0xebb1a46cu) {
        s6502_firmware_hle_pixel_tail_validation = 2u;
        return 0;
    }
    s6502_firmware_hle_pixel_tail_validation = 1u;
    return 1;
}

static __attribute__((noinline)) int s6502_firmware_hle_fill_match(void)
{
    static const uint8_t signature[] = {
        0xe0, 0x00, 0xf0, 0x09, 0xa5, 0x03, 0x91,
        0x2f, 0xc8, 0xca, 0x4c, 0x33, 0x79, 0x60,
    };
    uint32_t index;

    if (PA(0x7933u) != 0xebe933u)
        return 0;
    if (s6502_firmware_hle_fill_validation == 1u)
        return 1;
    if (s6502_firmware_hle_fill_validation == 2u)
        return 0;
    for (index = 0; index < sizeof(signature); ++index) {
        if (mem_readx((uint16_t)(0x7933u + index)) != signature[index]) {
            s6502_firmware_hle_fill_validation = 2u;
            return 0;
        }
    }
    s6502_firmware_hle_fill_validation = 1u;
    return 1;
}

void gam4980_set_firmware_hle_enabled(int enabled)
{
    s6502_firmware_hle_enabled = enabled != 0;
}

int gam4980_firmware_hle_enabled(void)
{
    return s6502_firmware_hle_enabled;
}

u32 gam4980_firmware_hle_hits(void)
{
    return s6502_firmware_hle_hits;
}

u64 gam4980_firmware_hle_guest_cycles(void)
{
    return s6502_firmware_hle_guest_cycles;
}

u32 gam4980_resource_span_cache_hits(void)
{
    return s6502_resource_span_cache_hits;
}

u32 gam4980_resource_span_cache_misses(void)
{
    return s6502_resource_span_cache_misses;
}

u32 gam4980_firmware_hle_path_count(void)
{
    return S6502_HLE_DIAGNOSTIC_COUNT;
}

u16 gam4980_firmware_hle_path_pc(u32 path_id)
{
    static const uint16_t path_pcs[S6502_HLE_DIAGNOSTIC_COUNT] = {
        0x5cb3u, 0x608au, 0x650fu, 0x6a75u, 0x7937u, 0xd1a2u,
        0xd340u, 0u, 0u, 0x5c5du, 0x6988u, 0u, 0u, 0u, 0u, 0u,
        0xf52au, 0xd2cau, 0xd596u, 0xd362u, 0xd572u, 0x6b1au,
        0x682du, 0x690fu, 0x876bu, 0x8039u, 0x5351u, 0x859eu,
    };

    if (path_id >= S6502_HLE_DIAGNOSTIC_COUNT)
        return 0u;
#ifdef GAM4980_ENABLE_GAME_LOAD_AOT
    if (path_id == S6502_HLE_ID_GAME_COUNTER)
        return s6502_game_hle_counter_count
            ? s6502_game_hle_counters[0].virtual_pc : 0u;
    if (path_id == S6502_HLE_ID_GAME_BITMAP)
        return s6502_game_hle_bitmap_count
            ? s6502_game_hle_bitmaps[0].virtual_pc : 0u;
    if (path_id == S6502_HLE_ID_GAME_SCAN)
        return s6502_game_hle_scan_count
            ? s6502_game_hle_scans[0].virtual_pc : 0u;
    if (path_id == S6502_HLE_ID_GAME_RECORD_SCAN)
        return s6502_game_hle_record_scan_count
            ? s6502_game_hle_record_scans[0].virtual_pc : 0u;
    if (path_id == S6502_HLE_ID_GAME_RECORD_REVERSE)
        return s6502_game_hle_record_reverse_count
            ? s6502_game_hle_record_reverses[0].virtual_pc : 0u;
    if (path_id == S6502_HLE_ID_GAME_TABLE_CHAIN ||
        path_id == S6502_HLE_ID_GAME_OBJECT_FLOW)
        return 0u;
#endif
    return path_pcs[path_id];
}

u32 gam4980_firmware_hle_path_attempts(u32 path_id)
{
    return path_id < S6502_HLE_DIAGNOSTIC_COUNT
        ? s6502_firmware_hle_attempts[path_id] : 0u;
}

u32 gam4980_firmware_hle_path_hits(u32 path_id)
{
    return path_id < S6502_HLE_DIAGNOSTIC_COUNT
        ? s6502_firmware_hle_path_hits[path_id] : 0u;
}

u32 gam4980_firmware_hle_path_condition_rejects(u32 path_id)
{
    return path_id < S6502_HLE_DIAGNOSTIC_COUNT
        ? s6502_firmware_hle_condition_rejects[path_id] : 0u;
}

u32 gam4980_firmware_hle_path_budget_rejects(u32 path_id)
{
    return path_id < S6502_HLE_DIAGNOSTIC_COUNT
        ? s6502_firmware_hle_budget_rejects[path_id] : 0u;
}

u32 gam4980_firmware_hle_path_batch_groups(u32 path_id)
{
    return path_id < S6502_HLE_DIAGNOSTIC_COUNT
        ? s6502_firmware_hle_batch_groups[path_id] : 0u;
}

u32 gam4980_firmware_hle_path_batch_iterations(u32 path_id)
{
    return path_id < S6502_HLE_DIAGNOSTIC_COUNT
        ? s6502_firmware_hle_batch_iterations[path_id] : 0u;
}

u32 gam4980_firmware_hle_path_batch_max(u32 path_id)
{
    return path_id < S6502_HLE_DIAGNOSTIC_COUNT
        ? s6502_firmware_hle_batch_max[path_id] : 0u;
}

u32 gam4980_firmware_hle_path_direct_groups(u32 path_id)
{
    return path_id < S6502_HLE_DIAGNOSTIC_COUNT
        ? s6502_firmware_hle_direct_groups[path_id] : 0u;
}

u32 gam4980_firmware_hle_path_direct_iterations(u32 path_id)
{
    return path_id < S6502_HLE_DIAGNOSTIC_COUNT
        ? s6502_firmware_hle_direct_iterations[path_id] : 0u;
}

u64 gam4980_firmware_hle_path_guest_cycles(u32 path_id)
{
    return path_id < S6502_HLE_DIAGNOSTIC_COUNT
        ? s6502_firmware_hle_path_guest_cycles[path_id] : 0u;
}
#undef S6502_HLE_GLYPH_ROW_ATTRIBUTE
#endif

#ifdef GAM4980_RUNTIME_PERFORMANCE_LOG
static void performance_sample_pc(void)
{
    uint16_t virtual_pc = sys.cpu.pc;
    uint32_t physical_pc = PA(virtual_pc);
    uint32_t slot = (physical_pc ^ (physical_pc >> 8) ^ virtual_pc) &
        (GAM4980_PERFORMANCE_SAMPLE_CAPACITY - 1u);
    uint32_t probes;

    for (probes = 0; probes < GAM4980_PERFORMANCE_SAMPLE_CAPACITY; ++probes) {
        T_GAM4980_PerformanceSample *sample = &performance_samples[slot];

        if (!sample->hits) {
            if (performance_sample_count >=
                GAM4980_PERFORMANCE_SAMPLE_MAX_RECORDS) {
                ++performance_sample_dropped;
                return;
            }
            sample->physical_pc = physical_pc;
            sample->virtual_pc = virtual_pc;
            sample->hits = 1u;
            ++performance_sample_count;
            return;
        }
        if (sample->physical_pc == physical_pc &&
            sample->virtual_pc == virtual_pc) {
            ++sample->hits;
            return;
        }
        slot = (slot + 1u) & (GAM4980_PERFORMANCE_SAMPLE_CAPACITY - 1u);
    }
    ++performance_sample_dropped;
}

u32 gam4980_performance_exec_calls(void)
{
    return performance_exec_calls;
}

u64 gam4980_performance_guest_cycles(void)
{
    return performance_guest_cycles;
}

u64 gam4980_performance_scheduled_cycles(void)
{
    return performance_scheduled_cycles;
}

u64 gam4980_performance_halted_cycles(void)
{
    return performance_halted_cycles;
}

u64 gam4980_performance_timer_ticks(void)
{
    return performance_timer_ticks;
}

u32 gam4980_performance_step_frames(void)
{
    return performance_step_frames;
}

u32 gam4980_performance_lcd_write_calls(void)
{
    return performance_lcd_write_calls;
}

u32 gam4980_performance_lcd_changed_writes(void)
{
    return performance_lcd_changed_writes;
}

u32 gam4980_performance_render_calls(void)
{
    return performance_render_calls;
}

u32 gam4980_performance_dirty_render_calls(void)
{
    return performance_dirty_render_calls;
}

u32 gam4980_performance_changed_render_calls(void)
{
    return performance_changed_render_calls;
}

u32 gam4980_performance_pc_sample_stride(void)
{
    return GAM4980_PERFORMANCE_PC_SAMPLE_STRIDE;
}

u32 gam4980_performance_sample_count(void)
{
    return performance_sample_count;
}

u32 gam4980_performance_sample_capacity(void)
{
    return GAM4980_PERFORMANCE_SAMPLE_CAPACITY;
}

u16 gam4980_performance_sample_virtual_pc(u32 sample_id)
{
    return sample_id < GAM4980_PERFORMANCE_SAMPLE_CAPACITY
        ? performance_samples[sample_id].virtual_pc : 0u;
}

u32 gam4980_performance_sample_physical_pc(u32 sample_id)
{
    return sample_id < GAM4980_PERFORMANCE_SAMPLE_CAPACITY
        ? performance_samples[sample_id].physical_pc : 0u;
}

u32 gam4980_performance_sample_hits(u32 sample_id)
{
    return sample_id < GAM4980_PERFORMANCE_SAMPLE_CAPACITY
        ? performance_samples[sample_id].hits : 0u;
}

u32 gam4980_performance_sample_dropped(void)
{
    return performance_sample_dropped;
}
#endif

#ifdef GAM4980_ENABLE_GAME_LOAD_AOT
#ifdef GAM4980_AOT_DIAGNOSTICS
static __attribute__((noinline)) void s6502_game_aot_hit(
    uint32_t entry_id, uint32_t instructions
)
{
    if (entry_id >= s6502_game_aot_entry_count)
        return;
    ++s6502_game_aot_entry_hits[entry_id];
    s6502_game_aot_instruction_hits += instructions;
}
#endif

static uint16_t s6502_game_aot_hash_slot(uint32_t physical_pc)
{
    return (uint16_t)(
        (physical_pc ^ (physical_pc >> 9) ^ (physical_pc >> 18)) &
        (S6502_GAME_AOT_HASH_SIZE - 1u)
    );
}

#ifdef GAM4980_ENABLE_FIRMWARE_HLE
static uint16_t s6502_game_hle_word(const uint8_t *code, uint32_t offset)
{
    return (uint16_t)(code[offset] | ((uint16_t)code[offset + 1u] << 8));
}

static int s6502_game_hle_match_counter(
    const uint8_t *code, uint32_t remaining, uint32_t physical_pc,
    s6502_game_hle_counter_t *match
)
{
    uint16_t entry;

    if (remaining < 0x2eu ||
        code[0x00] != 0xa9u || code[0x02] != 0x85u ||
        code[0x04] != 0xa9u || code[0x06] != 0x85u ||
        code[0x08] != 0xa0u || code[0x0a] != 0xb1u ||
        code[0x0c] != 0x85u || code[0x0e] != 0xa9u ||
        code[0x10] != 0x85u || code[0x12] != 0x20u ||
        code[0x15] != 0x90u || code[0x16] != 0x03u ||
        code[0x17] != 0x4cu || code[0x1a] != 0x4cu ||
        code[0x1d] != 0xa0u || code[0x1f] != 0xb1u ||
        code[0x21] != 0x18u || code[0x22] != 0x69u ||
        code[0x24] != 0xa0u || code[0x26] != 0x91u ||
        code[0x28] != 0x4cu || code[0x2b] != 0x4cu)
        return 0;
    if (code[0x03] != 0x23u || code[0x07] != 0x24u ||
        code[0x0d] != 0x20u || code[0x11] != 0x21u ||
        s6502_game_hle_word(code, 0x13u) != 0xd340u ||
        code[0x09] != code[0x1eu] || code[0x09] != code[0x25u] ||
        code[0x0b] != code[0x20u] || code[0x0b] != code[0x27u])
        return 0;

    entry = s6502_game_hle_word(code, 0x29u);
    if ((entry & 0x0fffu) != (physical_pc & 0x0fffu) ||
        s6502_game_hle_word(code, 0x18u) != (uint16_t)(entry + 0x2eu) ||
        s6502_game_hle_word(code, 0x1bu) != (uint16_t)(entry + 0x2bu) ||
        s6502_game_hle_word(code, 0x2cu) != (uint16_t)(entry + 0x1du))
        return 0;

    match->physical_pc = physical_pc;
    match->virtual_pc = entry;
    match->exit_pc = (uint16_t)(entry + 0x2eu);
    match->pointer_zp = code[0x0bu];
    match->index = code[0x09u];
    match->limit_low = code[0x01u];
    match->limit_high = code[0x05u];
    match->value_high = code[0x0fu];
    match->increment = code[0x23u];
    return 1;
}

static int s6502_game_hle_match_bitmap(
    const uint8_t *code, uint32_t remaining, uint32_t physical_pc,
    s6502_game_hle_bitmap_t *match
)
{
    uint16_t entry;

    if (remaining < 0x44u ||
        code[0x00] != 0xa5u || code[0x02] != 0x29u ||
        code[0x03] != 0xc0u || code[0x04] != 0xf0u ||
        code[0x05] != 0x0eu || code[0x06] != 0xc9u ||
        code[0x07] != 0x40u || code[0x08] != 0xd0u ||
        code[0x09] != 0x11u || code[0x0a] != 0xbdu ||
        code[0x0d] != 0x05u || code[0x0f] != 0x85u ||
        code[0x11] != 0x4cu || code[0x14] != 0xbdu ||
        code[0x17] != 0x25u || code[0x19] != 0x85u ||
        code[0x1b] != 0xe8u || code[0x1c] != 0xe0u ||
        code[0x1d] != 0x08u || code[0x1e] != 0xd0u ||
        code[0x1f] != 0x0fu || code[0x20] != 0xa5u ||
        code[0x22] != 0xa4u || code[0x24] != 0x91u ||
        code[0x26] != 0xc8u || code[0x27] != 0xb1u ||
        code[0x29] != 0x85u || code[0x2b] != 0x84u ||
        code[0x2d] != 0xa2u || code[0x2e] != 0x00u ||
        code[0x2f] != 0xe6u || code[0x31] != 0xa9u ||
        code[0x32] != 0x04u || code[0x33] != 0xc5u ||
        code[0x35] != 0xf0u || code[0x36] != 0x09u ||
        code[0x37] != 0xa5u || code[0x39] != 0x0au ||
        code[0x3a] != 0x0au || code[0x3b] != 0x85u ||
        code[0x3d] != 0x4cu)
        return 0;
    if (code[0x01] != code[0x38] ||
        code[0x0e] != code[0x10] || code[0x0e] != code[0x18] ||
        code[0x0e] != code[0x1a] || code[0x0e] != code[0x21] ||
        code[0x0e] != code[0x2a] || code[0x23] != code[0x2c] ||
        code[0x25] != code[0x28] || code[0x30] != code[0x34] ||
        code[0x30] != code[0x43])
        return 0;

    entry = s6502_game_hle_word(code, 0x3eu);
    if ((entry & 0x0fffu) != (physical_pc & 0x0fffu) ||
        s6502_game_hle_word(code, 0x12u) != (uint16_t)(entry + 0x1bu))
        return 0;

    match->physical_pc = physical_pc;
    match->virtual_pc = entry;
    match->or_table = s6502_game_hle_word(code, 0x0bu);
    match->and_table = s6502_game_hle_word(code, 0x15u);
    match->source_zp = code[0x01u];
    match->accumulator_zp = code[0x0eu];
    match->destination_index_zp = code[0x23u];
    match->destination_pointer_zp = code[0x25u];
    match->subpixel_zp = code[0x30u];
    match->outer_physical_pc = 0u;
    match->row_physical_pc = 0u;
    match->outer_virtual_pc = 0u;
    match->outer_exit_pc = 0u;
    match->function_exit_pc = 0u;
    match->source_index_zp = 0u;
    match->source_pointer_zp = 0u;
    match->width_zp = 0u;
    match->initial_x_zp = 0u;
    match->row_count_zp = 0u;
    match->height_zp = 0u;
    match->vertical_zp = 0u;
    match->row_stride = 0u;
    return 1;
}

static void s6502_game_hle_match_bitmap_outer(
    const uint8_t *game, uint32_t size, uint32_t offset,
    s6502_game_hle_bitmap_t *match
)
{
    const uint8_t *prefix;
    const uint8_t *code;
    uint16_t outer_entry;
    uint8_t source_index;
    uint8_t source_pointer;
    uint8_t width;
    uint8_t initial_x;
    uint8_t row_count;
    uint8_t height;
    uint8_t vertical;
    uint8_t row_fields[14];
    uint32_t field_index;
    uint32_t other_index;

    if (!game || !match || offset < 8u || size - offset < 0x53u ||
        ((offset - 8u) & 0x0fffu) + 0x5bu > 0x1000u)
        return;
    prefix = game + offset - 8u;
    code = game + offset;
    if (prefix[0x00u] != 0xa4u || prefix[0x02u] != 0xb1u ||
        prefix[0x04u] != 0x84u || prefix[0x06u] != 0x85u ||
        prefix[0x01u] != prefix[0x05u] ||
        prefix[0x07u] != match->source_zp ||
        code[0x40u] != 0xa9u || code[0x41u] != 0x00u ||
        code[0x42u] != 0x85u ||
        code[0x43u] != match->subpixel_zp ||
        code[0x44u] != 0xe6u || code[0x46u] != 0xa5u ||
        code[0x45u] != code[0x47u] ||
        code[0x48u] != 0x0au || code[0x49u] != 0x0au ||
        code[0x4au] != 0xc5u ||
        code[0x4cu] != 0xb0u || code[0x4du] != 0x05u ||
        code[0x4eu] != 0xf0u || code[0x4fu] != 0x03u ||
        code[0x50u] != 0x4cu)
        return;

    outer_entry = s6502_game_hle_word(code, 0x51u);
    if (outer_entry != (uint16_t)(match->virtual_pc - 8u) ||
        (outer_entry & 0x0fffu) !=
            ((match->physical_pc - 8u) & 0x0fffu))
        return;
    source_index = prefix[0x01u];
    source_pointer = prefix[0x03u];
    width = code[0x4bu];

    /* The outer estimator reads these fields before executing a group.
     * Refuse exotic zero-page aliasing so the estimate cannot be changed by
     * the group's own accumulator/index writes. */
    if (source_index == match->source_zp ||
        source_index == match->accumulator_zp ||
        source_index == match->destination_index_zp ||
        source_index == match->destination_pointer_zp ||
        source_index == (uint8_t)(match->destination_pointer_zp + 1u) ||
        source_index == match->subpixel_zp ||
        source_pointer == match->source_zp ||
        (uint8_t)(source_pointer + 1u) == match->source_zp ||
        width == match->source_zp ||
        width == match->accumulator_zp ||
        width == match->destination_index_zp ||
        width == match->destination_pointer_zp ||
        width == (uint8_t)(match->destination_pointer_zp + 1u) ||
        width == match->subpixel_zp || width == source_index ||
        width == source_pointer || width == (uint8_t)(source_pointer + 1u))
        return;

    match->outer_physical_pc = match->physical_pc - 8u;
    match->outer_virtual_pc = outer_entry;
    match->outer_exit_pc = (uint16_t)(match->virtual_pc + 0x53u);
    match->source_index_zp = source_index;
    match->source_pointer_zp = source_pointer;
    match->width_zp = width;

    /* This optional suffix is the hot full-row continuation used by the
     * A-series sprite renderer.  Match its instruction skeleton instead of
     * a whole-GAM signature so relocated variants retain the optimization.
     * The second renderer variant has a different row-stride formula and
     * intentionally keeps the existing per-group HLE for now. */
    if (size - offset < 0xabu ||
        ((offset - 8u) & 0x0fffu) + 0xb3u > 0x1000u ||
        code[0x53u] != 0xe0u || code[0x54u] != 0x00u ||
        code[0x55u] != 0xf0u || code[0x56u] != 0x06u ||
        code[0x57u] != 0xa5u || code[0x59u] != 0xa4u ||
        code[0x5bu] != 0x91u || code[0x5du] != 0xe6u ||
        code[0x5fu] != 0xa5u || code[0x61u] != 0xc5u ||
        code[0x63u] != 0xf0u || code[0x64u] != 0x45u ||
        code[0x65u] != 0xe6u || code[0x67u] != 0xa9u ||
        code[0x68u] != 0x5fu ||
        code[0x69u] != 0xc5u || code[0x6bu] != 0xb0u ||
        code[0x6cu] != 0x01u || code[0x6du] != 0x60u ||
        code[0x6eu] != 0x18u || code[0x6fu] != 0xa9u ||
        code[0x71u] != 0x65u || code[0x73u] != 0x85u ||
        code[0x75u] != 0xa5u || code[0x77u] != 0x69u ||
        code[0x78u] != 0x00u || code[0x79u] != 0x85u ||
        code[0x7bu] != 0xa0u || code[0x7cu] != 0x00u ||
        code[0x7du] != 0xb1u || code[0x7fu] != 0x85u ||
        code[0x81u] != 0xa9u || code[0x82u] != 0x07u ||
        code[0x83u] != 0x25u || code[0x85u] != 0x85u ||
        code[0x87u] != 0xa5u || code[0x89u] != 0x4au ||
        code[0x8au] != 0x4au || code[0x8bu] != 0x4au ||
        code[0x8cu] != 0xa4u || code[0x8eu] != 0xf0u ||
        code[0x8fu] != 0x03u || code[0x90u] != 0x18u ||
        code[0x91u] != 0x69u || code[0x92u] != 0x01u ||
        code[0x93u] != 0x0au || code[0x94u] != 0x18u ||
        code[0x95u] != 0x65u || code[0x97u] != 0x85u ||
        code[0x99u] != 0xa5u || code[0x9bu] != 0x69u ||
        code[0x9cu] != 0x00u || code[0x9du] != 0x85u ||
        code[0x9fu] != 0xa9u || code[0xa0u] != 0x00u ||
        code[0xa1u] != 0x85u || code[0xa3u] != 0x85u ||
        code[0xa5u] != 0xa6u || code[0xa7u] != 0x4cu ||
        code[0xaau] != 0x60u)
        return;
    if (code[0x58u] != match->accumulator_zp ||
        code[0x5au] != match->destination_index_zp ||
        code[0x5cu] != match->destination_pointer_zp ||
        code[0x72u] != match->destination_pointer_zp ||
        code[0x74u] != match->destination_pointer_zp ||
        code[0x76u] != (uint8_t)(match->destination_pointer_zp + 1u) ||
        code[0x7au] != (uint8_t)(match->destination_pointer_zp + 1u) ||
        code[0x7eu] != match->destination_pointer_zp ||
        code[0x80u] != match->accumulator_zp ||
        code[0x84u] != match->width_zp ||
        code[0x88u] != match->width_zp ||
        code[0x86u] != match->source_index_zp ||
        code[0x8du] != match->source_index_zp ||
        code[0x96u] != match->source_pointer_zp ||
        code[0x98u] != match->source_pointer_zp ||
        code[0x9au] != (uint8_t)(match->source_pointer_zp + 1u) ||
        code[0x9eu] != (uint8_t)(match->source_pointer_zp + 1u) ||
        code[0xa2u] != match->source_index_zp ||
        s6502_game_hle_word(code, 0xa8u) != outer_entry)
        return;

    row_count = code[0x5eu];
    height = code[0x60u];
    vertical = code[0x66u];
    initial_x = code[0xa6u];
    if (code[0x62u] != row_count || code[0x6au] != vertical ||
        match->destination_pointer_zp == 0xffu ||
        match->source_pointer_zp == 0xffu)
        return;

    row_fields[0] = match->source_zp;
    row_fields[1] = match->accumulator_zp;
    row_fields[2] = match->destination_index_zp;
    row_fields[3] = match->destination_pointer_zp;
    row_fields[4] = (uint8_t)(match->destination_pointer_zp + 1u);
    row_fields[5] = match->subpixel_zp;
    row_fields[6] = match->source_index_zp;
    row_fields[7] = match->source_pointer_zp;
    row_fields[8] = (uint8_t)(match->source_pointer_zp + 1u);
    row_fields[9] = match->width_zp;
    row_fields[10] = initial_x;
    row_fields[11] = row_count;
    row_fields[12] = height;
    row_fields[13] = vertical;
    for (field_index = 0u; field_index < 14u; ++field_index) {
        for (other_index = field_index + 1u;
             other_index < 14u; ++other_index) {
            if (row_fields[field_index] == row_fields[other_index])
                return;
        }
    }

    match->row_physical_pc = match->physical_pc + 0x53u;
    match->function_exit_pc = (uint16_t)(match->virtual_pc + 0xaau);
    match->initial_x_zp = initial_x;
    match->row_count_zp = row_count;
    match->height_zp = height;
    match->vertical_zp = vertical;
    match->row_stride = code[0x70u];
}

static int s6502_game_hle_match_scan(
    const uint8_t *game, uint32_t size, uint32_t offset,
    s6502_game_hle_scan_t *match
)
{
    const uint8_t *compare;
    const uint8_t *code;
    const uint8_t *secondary;
    uint32_t secondary_offset;
    uint16_t entry;
    uint16_t secondary_entry;
    uint16_t increment_entry;

    if (!game || !match || offset < 0x1fu || size - offset < 0x26u ||
        ((offset - 0x1fu) & 0x0fffu) + 0x42eu > 0x1000u)
        return 0;
    compare = game + offset - 0x1fu;
    code = game + offset;
    entry = s6502_game_hle_word(compare, 0x11u);
    if ((entry & 0x0fffu) != ((0x20d000u + offset) & 0x0fffu) ||
        compare[0x00u] != 0xa0u || compare[0x02u] != 0xb1u ||
        compare[0x04u] != 0xa0u || compare[0x06u] != 0x38u ||
        compare[0x07u] != 0xf1u || compare[0x09u] != 0xf0u ||
        compare[0x0au] != 0x05u || compare[0x0bu] != 0x90u ||
        compare[0x0cu] != 0x03u || compare[0x0du] != 0x4cu ||
        compare[0x10u] != 0x4cu ||
        compare[0x01u] != code[0x01u] ||
        compare[0x03u] != code[0x03u] ||
        compare[0x03u] != compare[0x08u] ||
        code[0x00u] != 0xa0u || code[0x02u] != 0xb1u ||
        code[0x04u] != 0x85u || code[0x06u] != 0xa9u ||
        code[0x07u] != 0x00u || code[0x08u] != 0x85u ||
        code[0x0au] != 0xa0u || code[0x0cu] != 0x18u ||
        code[0x0du] != 0xb1u || code[0x0fu] != 0x65u ||
        code[0x11u] != 0x85u || code[0x13u] != 0xc8u ||
        code[0x14u] != 0xb1u || code[0x16u] != 0x65u ||
        code[0x18u] != 0x85u || code[0x1au] != 0xa0u ||
        code[0x1bu] != 0x00u || code[0x1cu] != 0xb1u ||
        code[0x1eu] != 0x38u || code[0x1fu] != 0xe9u ||
        code[0x20u] != 0x01u || code[0x21u] != 0xf0u ||
        code[0x22u] != 0x03u || code[0x23u] != 0x4cu)
        return 0;
    if (code[0x03u] != compare[0x03u] ||
        code[0x05u] != code[0x10u] || code[0x05u] != code[0x12u] ||
        code[0x09u] != code[0x17u] || code[0x09u] != code[0x19u] ||
        code[0x09u] != (uint8_t)(code[0x05u] + 1u))
        return 0;

    secondary_entry = s6502_game_hle_word(code, 0x24u);
    secondary_offset = (uint16_t)(secondary_entry - entry);
    if (secondary_offset < 0x26u || secondary_offset + 0x4fu > size - offset)
        return 0;
    secondary = code + secondary_offset;
    increment_entry = (uint16_t)(entry - 0x0cu);
    if (secondary[0x00u] != 0xa0u || secondary[0x02u] != 0xb1u ||
        secondary[0x04u] != 0x85u || secondary[0x06u] != 0xa9u ||
        secondary[0x07u] != 0x00u || secondary[0x08u] != 0x85u ||
        secondary[0x0au] != 0xa0u || secondary[0x0cu] != 0x18u ||
        secondary[0x0du] != 0xb1u || secondary[0x0fu] != 0x65u ||
        secondary[0x11u] != 0x85u || secondary[0x13u] != 0xc8u ||
        secondary[0x14u] != 0xb1u || secondary[0x16u] != 0x65u ||
        secondary[0x18u] != 0x85u || secondary[0x1au] != 0xa0u ||
        secondary[0x1bu] != 0x00u || secondary[0x1cu] != 0xb1u ||
        secondary[0x1eu] != 0x18u || secondary[0x1fu] != 0x69u ||
        secondary[0x20u] != 0x01u || secondary[0x21u] != 0x38u ||
        secondary[0x22u] != 0xe9u || secondary[0x23u] != 0x01u ||
        secondary[0x24u] != 0xd0u || secondary[0x25u] != 0x03u ||
        secondary[0x26u] != 0x4cu || secondary[0x29u] != 0xa0u ||
        secondary[0x2bu] != 0xb1u || secondary[0x2du] != 0x85u ||
        secondary[0x2fu] != 0xa9u || secondary[0x30u] != 0x00u ||
        secondary[0x31u] != 0x85u || secondary[0x33u] != 0xa0u ||
        secondary[0x35u] != 0x18u || secondary[0x36u] != 0xb1u ||
        secondary[0x38u] != 0x65u || secondary[0x3au] != 0x85u ||
        secondary[0x3cu] != 0xc8u || secondary[0x3du] != 0xb1u ||
        secondary[0x3fu] != 0x65u || secondary[0x41u] != 0x85u ||
        secondary[0x43u] != 0xa0u || secondary[0x44u] != 0x00u ||
        secondary[0x45u] != 0xb1u || secondary[0x47u] != 0x38u ||
        secondary[0x48u] != 0xe9u || secondary[0x49u] != 0x01u ||
        secondary[0x4au] != 0x91u || secondary[0x4cu] != 0x4cu ||
        s6502_game_hle_word(secondary, 0x27u) !=
            (uint16_t)(secondary_entry + 0x4cu) ||
        s6502_game_hle_word(secondary, 0x4du) != increment_entry)
        return 0;
    if (secondary[0x01u] != code[0x01u] ||
        secondary[0x03u] != code[0x03u] ||
        secondary[0x05u] != code[0x05u] ||
        secondary[0x09u] != code[0x09u] ||
        secondary[0x0bu] != code[0x0bu] ||
        secondary[0x0eu] != code[0x0eu] ||
        secondary[0x10u] != code[0x10u] ||
        secondary[0x12u] != code[0x12u] ||
        secondary[0x15u] != code[0x15u] ||
        secondary[0x17u] != code[0x17u] ||
        secondary[0x19u] != code[0x19u] ||
        secondary[0x1du] != code[0x1du] ||
        secondary[0x2au] != code[0x01u] ||
        secondary[0x2cu] != code[0x03u] ||
        secondary[0x2eu] != code[0x05u] ||
        secondary[0x32u] != code[0x09u] ||
        secondary[0x34u] != code[0x0bu] ||
        secondary[0x37u] != code[0x0eu] ||
        secondary[0x39u] != code[0x10u] ||
        secondary[0x3bu] != code[0x12u] ||
        secondary[0x3eu] != code[0x15u] ||
        secondary[0x40u] != code[0x17u] ||
        secondary[0x42u] != code[0x19u] ||
        secondary[0x46u] != code[0x1du] ||
        secondary[0x4bu] != code[0x1du])
        return 0;

    match->physical_pc = 0x20d000u + offset;
    match->virtual_pc = entry;
    match->special_pc = (uint16_t)(entry + 0x26u);
    match->exit_pc = s6502_game_hle_word(compare, 0x0eu);
    match->object_pointer_zp = code[0x03u];
    match->temporary_pointer_zp = code[0x05u];
    match->index_offset = code[0x01u];
    match->limit_offset = compare[0x05u];
    match->array_pointer_offset = code[0x0bu];
    return 1;
}

static int s6502_game_hle_match_record_scan(
    const uint8_t *game, uint32_t size, uint32_t offset,
    s6502_game_hle_record_scan_t *match
)
{
    static const uint8_t signature[0x111u] = {
        0xa0,0x06,0xb1,0x28,0x85,0x23,0xc8,0xb1,0x28,0x85,0x24,0xa0,0x0a,0xb1,0x28,0x85,
        0x20,0xc8,0xb1,0x28,0x85,0x21,0x18,0xa5,0x20,0x65,0x23,0x85,0x20,0xa5,0x21,0x65,
        0x24,0x85,0x21,0xa9,0x02,0x85,0x23,0xa9,0x00,0x85,0x24,0x38,0xa5,0x20,0xe5,0x23,
        0x85,0x20,0xa5,0x21,0xe5,0x24,0x85,0x21,0xa0,0x00,0xb1,0x20,0xa0,0x0c,0x91,0x28,
        0xa0,0x0e,0xb1,0x28,0x85,0x26,0xc8,0xb1,0x28,0x85,0x27,0xa0,0x00,0xb1,0x26,0x85,
        0x23,0xa0,0x0c,0xb1,0x28,0x38,0xe5,0x23,0xd0,0x03,0x4c,0x00,0x00,0x4c,0x00,0x00,
        0xa0,0x06,0xb1,0x28,0x85,0x23,0xc8,0xb1,0x28,0x85,0x24,0xa0,0x0a,0xb1,0x28,0x85,
        0x20,0xc8,0xb1,0x28,0x85,0x21,0x18,0xa5,0x20,0x65,0x23,0x85,0x20,0xa5,0x21,0x65,
        0x24,0x85,0x21,0xa0,0x00,0xb1,0x20,0xa0,0x0c,0x91,0x28,0xa0,0x0e,0xb1,0x28,0x85,
        0x26,0xc8,0xb1,0x28,0x85,0x27,0xa0,0x02,0xb1,0x26,0x85,0x23,0xa0,0x0c,0xb1,0x28,
        0x38,0xe5,0x23,0xf0,0x03,0x4c,0x00,0x00,0xa0,0x06,0xb1,0x28,0x85,0x23,0xc8,0xb1,
        0x28,0x85,0x24,0xa0,0x0a,0xb1,0x28,0x85,0x20,0xc8,0xb1,0x28,0x85,0x21,0x18,0xa5,
        0x20,0x65,0x23,0x85,0x20,0xa5,0x21,0x65,0x24,0x85,0x21,0xa9,0x01,0x85,0x23,0xa9,
        0x00,0x85,0x24,0x38,0xa5,0x20,0xe5,0x23,0x85,0x20,0xa5,0x21,0xe5,0x24,0x85,0x21,
        0xa0,0x00,0xb1,0x20,0xa0,0x0c,0x91,0x28,0xa0,0x0e,0xb1,0x28,0x85,0x26,0xc8,0xb1,
        0x28,0x85,0x27,0xa0,0x01,0xb1,0x26,0x85,0x23,0xa0,0x0c,0xb1,0x28,0x38,0xe5,0x23,
        0xf0,0x03,0x4c,0x00,0x00,0xa0,0x09,0xa9,0x01,0x91,0x28,0x4c,0x00,0x00,0x4c,0x00,
        0x00
    };
    static const uint8_t increment_signature[0x24u] = {
        0xa0,0x06,0xb1,0x28,0x85,0x20,0xc8,0xb1,0x28,0x85,0x21,0xa5,0x20,0x18,0x69,0x03,
        0x85,0x20,0xa5,0x21,0x69,0x00,0x85,0x21,0xa0,0x06,0xa5,0x20,0x91,0x28,0xc8,0xa5,
        0x21,0x91,0x28,0x4c
    };
    const uint8_t *code;
    const uint8_t *increment;
    uint16_t entry;
    uint16_t exit_pc;
    uint32_t index;

    if (!game || !match || offset < 0x29u || size - offset < 0x111u ||
        ((offset - 0x29u) & 0x0fffu) + 0x13au > 0x1000u)
        return 0;
    code = game + offset;
    increment = code - 0x26u;
    for (index = 0u; index < sizeof(signature); ++index) {
        if (index == 0x5bu || index == 0x5cu ||
            index == 0x5eu || index == 0x5fu ||
            index == 0xa6u || index == 0xa7u ||
            index == 0x103u || index == 0x104u ||
            index == 0x10cu || index == 0x10du ||
            index == 0x10fu || index == 0x110u)
            continue;
        if (code[index] != signature[index])
            return 0;
    }
    for (index = 0u; index < 0x24u; ++index) {
        if (increment[index] != increment_signature[index])
            return 0;
    }
    entry = s6502_game_hle_word(code - 0x29u, 1u);
    exit_pc = s6502_game_hle_word(code, 0x5eu);
    if ((entry & 0x0fffu) != ((0x20d000u + offset) & 0x0fffu) ||
        s6502_game_hle_word(code, 0x5bu) != (uint16_t)(entry + 0x60u) ||
        s6502_game_hle_word(code, 0xa6u) != (uint16_t)(entry + 0x10eu) ||
        s6502_game_hle_word(code, 0x103u) != (uint16_t)(entry + 0x10eu) ||
        s6502_game_hle_word(code, 0x10cu) != exit_pc ||
        s6502_game_hle_word(code, 0x10fu) != (uint16_t)(entry - 0x26u) ||
        s6502_game_hle_word(increment, 0x24u) !=
            (uint16_t)(entry - 0x29u))
        return 0;

    match->physical_pc = 0x20d000u + offset;
    match->virtual_pc = entry;
    match->exit_pc = exit_pc;
    match->object_pointer_zp = code[0x03u];
    match->index_offset = code[0x01u];
    match->data_pointer_offset = code[0x0cu];
    match->candidate_offset = code[0x3du];
    match->reference_pointer_offset = code[0x41u];
    match->found_offset = code[0x106u];
    return 1;
}

static int s6502_game_hle_match_record_reverse(
    const uint8_t *game, uint32_t size, uint32_t offset,
    s6502_game_hle_record_reverse_t *match
)
{
    const uint8_t *code;
    uint32_t fnv = 2166136261u;
    uint32_t sdbm = 0u;
    uint32_t index;
    uint16_t entry;

    if (!game || !match || offset > size || size - offset < 0x182u ||
        (offset & 0x0fffu) + 0x182u > 0x1000u)
        return 0;
    code = game + offset;
    if (code[0x00u] != 0xa0u || code[0x02u] != 0xb1u ||
        code[0x04u] != 0x85u || code[0x05u] != 0x23u ||
        code[0x06u] != 0xc8u || code[0x07u] != 0xb1u ||
        code[0x09u] != 0x85u || code[0x0au] != 0x24u ||
        code[0x0bu] != 0xa0u || code[0x0du] != 0xb1u ||
        code[0x5au] != 0x4cu || code[0x5du] != 0x4cu ||
        code[0x16eu] != 0x4cu || code[0x171u] != 0xa0u ||
        code[0x173u] != 0xb1u || code[0x177u] != 0x4cu ||
        code[0x17au] != 0xa9u || code[0x17cu] != 0x8du ||
        code[0x17fu] != 0x4cu || code[0x03u] != code[0x08u])
        return 0;
    for (index = 0u; index < 0x182u; ++index) {
        uint8_t byte = code[index];

        fnv = (fnv ^ byte) * 16777619u;
        sdbm = byte + (sdbm << 6) + (sdbm << 16) - sdbm;
    }
    if (fnv != 0xf251f681u || sdbm != 0x3902aa0au)
        return 0;

    entry = (uint16_t)(s6502_game_hle_word(code, 0x16fu) + 0x26u);
    if ((entry & 0x0fffu) != ((0x20d000u + offset) & 0x0fffu) ||
        s6502_game_hle_word(code, 0x5bu) != (uint16_t)(entry + 0x60u) ||
        s6502_game_hle_word(code, 0x5eu) != (uint16_t)(entry + 0x171u) ||
        s6502_game_hle_word(code, 0x16fu) != (uint16_t)(entry - 0x26u))
        return 0;

    match->physical_pc = 0x20d000u + offset;
    match->virtual_pc = entry;
    match->success_pc = s6502_game_hle_word(code, 0x178u);
    match->failure_pc = s6502_game_hle_word(code, 0x180u);
    match->error_address = s6502_game_hle_word(code, 0x17du);
    match->object_pointer_zp = code[0x03u];
    match->index_offset = code[0x01u];
    match->data_pointer_offset = code[0x0cu];
    match->candidate_offset = code[0x3du];
    match->reference_pointer_offset = code[0x41u];
    match->found_offset = code[0x172u];
    match->error_value = code[0x17bu];
    return 1;
}

static int s6502_game_hle_match_table_chain(
    const uint8_t *game, uint32_t size, uint32_t offset,
    s6502_game_hle_table_chain_t *match
)
{
    static const uint8_t signature[0x92u] = {
        0x68,0x85,0x23,0x68,0x85,0x24,0xa0,0x00,0x18,0xb1,0x28,0x65,0x20,0x85,0x20,0xc8,
        0xb1,0x28,0x65,0x21,0x85,0x21,0xa0,0x04,0xb1,0x20,0x85,0x20,0xa9,0x00,0x85,0x21,
        0x06,0x20,0x26,0x21,0xa0,0x25,0x18,0xb1,0x28,0x65,0x20,0x85,0x20,0xc8,0xb1,0x28,
        0x65,0x21,0x85,0x21,0xa0,0x00,0xb1,0x20,0xaa,0xc8,0xb1,0x20,0x85,0x21,0x86,0x20,
        0xa5,0x20,0x85,0x23,0xa5,0x21,0x85,0x24,0xa9,0x00,0x85,0x20,0xa9,0x00,0x85,0x21,
        0x18,0xa5,0x20,0x65,0x23,0x85,0x20,0xa5,0x21,0x65,0x24,0x85,0x21,0xa0,0x00,0xb1,
        0x20,0x85,0x20,0xa9,0x00,0x85,0x21,0xa0,0x00,0xa5,0x20,0x91,0x28,0xc8,0xa5,0x21,
        0x91,0x28,0xa0,0x10,0xb1,0x28,0xa0,0x1e,0x18,0x71,0x28,0x85,0x20,0xa9,0x00,0x85,
        0x21,0xa5,0x24,0x48,0xa5,0x23,0x48,0xa9,0x05,0x85,0x23,0xa9,0x00,0x85,0x24,0x20,
        0xaf,0xd6
    };
    const uint8_t *code;
    uint32_t index;

    if (!game || !match || offset > size || size - offset < sizeof(signature) ||
        (offset & 0x0fffu) + sizeof(signature) > 0x1000u)
        return 0;
    code = game + offset;
    for (index = 0u; index < sizeof(signature); ++index) {
        if (index == 0x49u || index == 0x68u)
            continue;
        if (code[index] != signature[index])
            return 0;
    }
    if ((uint8_t)(code[0x68u] + 1u) == 0u)
        return 0;

    match->physical_pc = 0x20d000u + offset;
    match->firmware_pc = s6502_game_hle_word(code, 0x90u);
    match->field_offset = code[0x49u];
    match->output_offset = code[0x68u];
    return 1;
}

static int s6502_game_hle_match_object_flow(
    const uint8_t *game, uint32_t size, uint32_t offset,
    s6502_game_hle_object_flow_t *match
)
{
    static const uint8_t signature[0x101u] = {
        0xa0,0x1b,0xb1,0x28,0x85,0x23,0xc8,0xb1,0x28,0x85,0x24,0xa0,0x0b,0xb1,0x28,0x85,
        0x20,0xa9,0x00,0x85,0x21,0x18,0xa5,0x20,0x65,0x23,0x85,0x20,0xa5,0x21,0x65,0x24,
        0x85,0x21,0xa5,0x20,0x38,0xe9,0x01,0x85,0x20,0xa5,0x21,0xe9,0x00,0x85,0x21,0xa0,
        0x27,0xa5,0x20,0x91,0x28,0xc8,0xa5,0x21,0x91,0x28,0xa0,0x19,0xb1,0x28,0x85,0x23,
        0xc8,0xb1,0x28,0x85,0x24,0xa0,0x0a,0xb1,0x28,0x85,0x20,0xa9,0x00,0x85,0x21,0x18,
        0xa5,0x20,0x65,0x23,0x85,0x20,0xa5,0x21,0x65,0x24,0x85,0x21,0xa5,0x20,0x38,0xe9,
        0x01,0x85,0x20,0xa5,0x21,0xe9,0x00,0x85,0x21,0xa0,0x29,0xa5,0x20,0x91,0x28,0xc8,
        0xa5,0x21,0x91,0x28,0xa0,0x10,0xb1,0x28,0xa0,0x1e,0x18,0x71,0x28,0x85,0x20,0xa9,
        0x00,0x85,0x21,0xa5,0x24,0x48,0xa5,0x23,0x48,0xa9,0x05,0x85,0x23,0xa9,0x00,0x85,
        0x24,0x20,0xaf,0xd6,0x68,0x85,0x23,0x68,0x85,0x24,0xa0,0x00,0x18,0xb1,0x28,0x65,
        0x20,0x85,0x20,0xc8,0xb1,0x28,0x65,0x21,0x85,0x21,0xa0,0x04,0xb1,0x20,0x85,0x20,
        0xa9,0x00,0x85,0x21,0x06,0x20,0x26,0x21,0xa0,0x25,0x18,0xb1,0x28,0x65,0x20,0x85,
        0x20,0xc8,0xb1,0x28,0x65,0x21,0x85,0x21,0xa0,0x00,0xb1,0x20,0xaa,0xc8,0xb1,0x20,
        0x85,0x21,0x86,0x20,0xa5,0x20,0x85,0x23,0xa5,0x21,0x85,0x24,0xa9,0x06,0x85,0x20,
        0xa9,0x00,0x85,0x21,0x18,0xa5,0x20,0x65,0x23,0x85,0x20,0xa5,0x21,0x65,0x24,0x85,
        0x21,0xa0,0x2b,0xa5,0x20,0x91,0x28,0xc8,0xa5,0x21,0x91,0x28,0xa9,0x00,0x20,0xaa,
        0xda,
    };
    const uint8_t *code;
    uint32_t index;

    if (!game || !match || offset > size ||
        size - offset < sizeof(signature))
        return 0;
    code = game + offset;
    for (index = 0u; index < sizeof(signature); ++index) {
        if (code[index] != signature[index])
            return 0;
    }

    match->physical_pc = 0x20d000u + offset;
    match->firmware_pc = s6502_game_hle_word(code, 0x92u);
    match->exit_firmware_pc = s6502_game_hle_word(code, 0xffu);
    match->object_pointer_zp = code[0x03u];
    match->first_pointer_offset = code[0x01u];
    match->first_value_offset = code[0x0cu];
    match->first_output_offset = code[0x30u];
    match->second_pointer_offset = code[0x3bu];
    match->second_value_offset = code[0x46u];
    match->second_output_offset = code[0x6au];
    match->index_left_offset = code[0x75u];
    match->index_right_offset = code[0x79u];
    match->table_pointer_offset = code[0xb9u];
    match->table_bias = code[0xddu];
    match->output_offset = code[0xf2u];
    return match->firmware_pc == 0xd6afu &&
        match->exit_firmware_pc == 0xdaaau;
}

static void s6502_game_hle_prepare(const uint8_t *game, uint32_t size)
{
    uint32_t offset;

    s6502_game_hle_counter_count = 0u;
    s6502_game_hle_bitmap_count = 0u;
    s6502_game_hle_scan_count = 0u;
    s6502_game_hle_record_scan_count = 0u;
    s6502_game_hle_record_reverse_count = 0u;
    s6502_game_hle_table_chain_count = 0u;
    s6502_game_hle_object_flow_count = 0u;
    if (!game)
        return;
    for (offset = 0; offset + 4u <= size; ++offset) {
        uint32_t remaining = size - offset;
        uint32_t physical_pc = 0x20d000u + offset;

        if ((offset & 0x0fffu) + 0x44u > 0x1000u)
            continue;
        if (game[offset] == 0xa9u &&
            s6502_game_hle_counter_count < S6502_GAME_HLE_MAX_MATCHES &&
            s6502_game_hle_match_counter(
                game + offset, remaining, physical_pc,
                &s6502_game_hle_counters[s6502_game_hle_counter_count]
            ))
            ++s6502_game_hle_counter_count;
        if (game[offset] == 0xa5u &&
            s6502_game_hle_bitmap_count < S6502_GAME_HLE_MAX_MATCHES &&
            s6502_game_hle_match_bitmap(
                game + offset, remaining, physical_pc,
                &s6502_game_hle_bitmaps[s6502_game_hle_bitmap_count]
            )) {
            s6502_game_hle_match_bitmap_outer(
                game, size, offset,
                &s6502_game_hle_bitmaps[s6502_game_hle_bitmap_count]
            );
            ++s6502_game_hle_bitmap_count;
        }
        if (game[offset] == 0xa0u &&
            s6502_game_hle_scan_count < S6502_GAME_HLE_MAX_MATCHES &&
            s6502_game_hle_match_scan(
                game, size, offset,
                &s6502_game_hle_scans[s6502_game_hle_scan_count]
            ))
            ++s6502_game_hle_scan_count;
        if (game[offset] == 0xa0u &&
            s6502_game_hle_record_scan_count <
                S6502_GAME_HLE_MAX_MATCHES &&
            s6502_game_hle_match_record_scan(
                game, size, offset,
                &s6502_game_hle_record_scans[
                    s6502_game_hle_record_scan_count
                ]
            ))
            ++s6502_game_hle_record_scan_count;
        if (game[offset] == 0xa0u &&
            s6502_game_hle_record_reverse_count <
                S6502_GAME_HLE_MAX_MATCHES &&
            s6502_game_hle_match_record_reverse(
                game, size, offset,
                &s6502_game_hle_record_reverses[
                    s6502_game_hle_record_reverse_count
                ]
            ))
            ++s6502_game_hle_record_reverse_count;
        if (game[offset] == 0x68u &&
            s6502_game_hle_table_chain_count <
                S6502_GAME_HLE_MAX_MATCHES &&
            s6502_game_hle_match_table_chain(
                game, size, offset,
                &s6502_game_hle_table_chains[
                    s6502_game_hle_table_chain_count
                ]
            ))
            ++s6502_game_hle_table_chain_count;
        if (game[offset] == 0xa0u &&
            s6502_game_hle_object_flow_count <
                S6502_GAME_HLE_MAX_MATCHES &&
            s6502_game_hle_match_object_flow(
                game, size, offset,
                &s6502_game_hle_object_flows[
                    s6502_game_hle_object_flow_count
                ]
            ))
            ++s6502_game_hle_object_flow_count;
    }
}

static const s6502_game_hle_counter_t *
s6502_game_hle_find_counter(uint32_t physical_pc)
{
    uint32_t index;

    for (index = 0; index < s6502_game_hle_counter_count; ++index) {
        if (s6502_game_hle_counters[index].physical_pc == physical_pc ||
            s6502_game_hle_counters[index].physical_pc + 0x15u ==
                physical_pc)
            return &s6502_game_hle_counters[index];
    }
    return 0;
}

static const s6502_game_hle_bitmap_t *
s6502_game_hle_find_bitmap(uint32_t physical_pc)
{
    uint32_t index;

    for (index = 0; index < s6502_game_hle_bitmap_count; ++index) {
        if (s6502_game_hle_bitmaps[index].physical_pc == physical_pc ||
            s6502_game_hle_bitmaps[index].physical_pc + 0x2fu == physical_pc ||
            s6502_game_hle_bitmaps[index].physical_pc + 0x40u == physical_pc ||
            (s6502_game_hle_bitmaps[index].outer_physical_pc &&
             s6502_game_hle_bitmaps[index].outer_physical_pc == physical_pc) ||
            (s6502_game_hle_bitmaps[index].row_physical_pc &&
             s6502_game_hle_bitmaps[index].row_physical_pc == physical_pc))
            return &s6502_game_hle_bitmaps[index];
    }
    return 0;
}

static const s6502_game_hle_scan_t *
s6502_game_hle_find_scan(uint32_t physical_pc)
{
    uint32_t index;

    for (index = 0; index < s6502_game_hle_scan_count; ++index) {
        if (s6502_game_hle_scans[index].physical_pc == physical_pc)
            return &s6502_game_hle_scans[index];
    }
    return 0;
}

static const s6502_game_hle_record_scan_t *
s6502_game_hle_find_record_scan(uint32_t physical_pc)
{
    uint32_t index;

    for (index = 0u; index < s6502_game_hle_record_scan_count; ++index) {
        uint32_t entry = s6502_game_hle_record_scans[index].physical_pc;

        if (entry == physical_pc || entry + 0x60u == physical_pc ||
            entry + 0xa5u == physical_pc || entry + 0xa8u == physical_pc ||
            entry - 0x26u == physical_pc)
            return &s6502_game_hle_record_scans[index];
    }
    return 0;
}

static const s6502_game_hle_record_reverse_t *
s6502_game_hle_find_record_reverse(uint32_t physical_pc)
{
    uint32_t index;

    for (index = 0u; index < s6502_game_hle_record_reverse_count; ++index) {
        if (s6502_game_hle_record_reverses[index].physical_pc == physical_pc)
            return &s6502_game_hle_record_reverses[index];
    }
    return 0;
}

static const s6502_game_hle_table_chain_t *
s6502_game_hle_find_table_chain(uint32_t physical_pc)
{
    uint32_t index;

    for (index = 0u; index < s6502_game_hle_table_chain_count; ++index) {
        if (s6502_game_hle_table_chains[index].physical_pc == physical_pc)
            return &s6502_game_hle_table_chains[index];
    }
    return 0;
}

static const s6502_game_hle_object_flow_t *
s6502_game_hle_find_object_flow(uint32_t physical_pc)
{
    uint32_t index;

    for (index = 0u; index < s6502_game_hle_object_flow_count; ++index) {
        if (s6502_game_hle_object_flows[index].physical_pc == physical_pc)
            return &s6502_game_hle_object_flows[index];
    }
    return 0;
}

static uint16_t s6502_game_hle_compare_cycles_for(
    uint8_t low, uint8_t high, uint8_t other_low, uint8_t other_high
)
{
    uint16_t difference = (uint16_t)(
        (uint16_t)low + (uint8_t)~other_low + 1u
    );
    uint8_t low_result = (uint8_t)difference;
    uint8_t carry = difference > 0xffu;
    uint8_t high_result = (uint8_t)(
        (uint16_t)high + (uint8_t)~other_high + carry
    );

    if (low_result)
        return high_result ? 66u : 58u;
    return high_result ? 58u : 49u;
}

static __attribute__((noinline)) uint16_t s6502_game_hle_counter_cycles(
    const s6502_game_hle_counter_t *match
)
{
    uint8_t pointer_zp = match->pointer_zp;
    uint16_t base = (uint16_t)(
        s6502_stack_ram[pointer_zp] |
        ((uint16_t)s6502_stack_ram[(uint8_t)(pointer_zp + 1u)] << 8)
    );
    uint16_t address = (uint16_t)(base + match->index);
    uint8_t value;
    uint16_t value16;
    uint16_t limit16;
    uint16_t result;

    if (address >= GAM4980_RAM_SIZE)
        return 0u;
    value = s6502_stack_ram[address];
    value16 = (uint16_t)(value | ((uint16_t)match->value_high << 8));
    limit16 = (uint16_t)(
        match->limit_low | ((uint16_t)match->limit_high << 8)
    );
    result = (uint16_t)(
        31u + !!(0xff00u & (base ^ address)) +
        s6502_game_hle_compare_cycles_for(
            value, match->value_high,
            match->limit_low, match->limit_high
        )
    );
    if (value16 >= limit16)
        return (uint16_t)(result + 5u);
    return (uint16_t)(
        result + 31u + !!(0xff00u & (base ^ address)) +
        !!(0xff00u &
            ((uint16_t)(match->virtual_pc + 0x17u) ^
             (uint16_t)(match->virtual_pc + 0x1au)))
    );
}

static __attribute__((noinline)) uint16_t
s6502_game_hle_counter_tail_cycles(
    const s6502_game_hle_counter_t *match
)
{
    uint8_t pointer_zp = match->pointer_zp;
    uint16_t base = (uint16_t)(
        s6502_stack_ram[pointer_zp] |
        ((uint16_t)s6502_stack_ram[(uint8_t)(pointer_zp + 1u)] << 8)
    );
    uint16_t address = (uint16_t)(base + match->index);

    if (address >= GAM4980_RAM_SIZE)
        return 0u;
    return (uint16_t)(
        31u + !!(0xff00u & (base ^ address)) +
        !!(0xff00u &
            ((uint16_t)(match->virtual_pc + 0x17u) ^
             (uint16_t)(match->virtual_pc + 0x1au)))
    );
}

static uint16_t s6502_game_hle_bitmap_cycles_for(
    const s6502_game_hle_bitmap_t *match, uint32_t ix, uint8_t source
)
{
    uint8_t destination_index =
        s6502_stack_ram[match->destination_index_zp];
    uint8_t subpixel = s6502_stack_ram[match->subpixel_zp];
    uint8_t x = (uint8_t)ix;
    uint8_t pointer_zp = match->destination_pointer_zp;
    uint16_t destination = (uint16_t)(
        s6502_stack_ram[pointer_zp] |
        ((uint16_t)s6502_stack_ram[(uint8_t)(pointer_zp + 1u)] << 8)
    );
    uint16_t cycles = 0u;

    if (subpixel >= 4u || x >= 8u || destination >= GAM4980_RAM_SIZE)
        return 0u;
    do {
        uint8_t bits = source & 0xc0u;

        if (!bits) {
            cycles = (uint16_t)(cycles + 18u +
                !!(0xff00u &
                    ((uint16_t)(match->virtual_pc + 0x06u) ^
                     (uint16_t)(match->virtual_pc + 0x14u))) +
                !!(0xff00u &
                    (match->and_table ^ (uint16_t)(match->and_table + x))));
        } else if (bits == 0x40u) {
            cycles = (uint16_t)(cycles + 24u +
                !!(0xff00u &
                    (match->or_table ^ (uint16_t)(match->or_table + x))));
        } else {
            cycles = (uint16_t)(cycles + 12u +
                !!(0xff00u &
                    ((uint16_t)(match->virtual_pc + 0x0au) ^
                     (uint16_t)(match->virtual_pc + 0x1bu))));
        }

        ++x;
        if (x == 8u) {
            uint8_t next_index = (uint8_t)(destination_index + 1u);
            uint16_t next_address = (uint16_t)(destination + next_index);

            if (next_address >= GAM4980_RAM_SIZE)
                return 0u;
            cycles = (uint16_t)(cycles + 33u +
                !!(0xff00u & (destination ^ next_address)));
            destination_index = next_index;
            x = 0u;
        } else {
            cycles = (uint16_t)(cycles + 7u +
                !!(0xff00u &
                    ((uint16_t)(match->virtual_pc + 0x20u) ^
                     (uint16_t)(match->virtual_pc + 0x2fu))));
        }

        ++subpixel;
        if (subpixel == 4u)
            cycles = (uint16_t)(cycles + 13u +
                !!(0xff00u &
                    ((uint16_t)(match->virtual_pc + 0x37u) ^
                     (uint16_t)(match->virtual_pc + 0x40u))));
        else {
            cycles = (uint16_t)(cycles + 25u);
            source = (uint8_t)(source << 2);
        }
    } while (subpixel != 4u);
    return cycles;
}

static __attribute__((noinline)) uint16_t s6502_game_hle_bitmap_cycles(
    const s6502_game_hle_bitmap_t *match, uint32_t ix
)
{
    return s6502_game_hle_bitmap_cycles_for(
        match, ix, s6502_stack_ram[match->source_zp]
    );
}

static __attribute__((noinline)) uint16_t
s6502_game_hle_bitmap_outer_cycles(
    const s6502_game_hle_bitmap_t *match, uint32_t ix
)
{
    uint8_t source_index;
    uint8_t next_index;
    uint8_t width;
    uint8_t compared;
    uint16_t source_base;
    uint16_t source_address;
    uint16_t destination_base;
    uint16_t inner_cycles;
    uint16_t cycles;

    if (!match->outer_physical_pc ||
        s6502_stack_ram[match->subpixel_zp] != 0u)
        return 0u;
    source_index = s6502_stack_ram[match->source_index_zp];
    source_base = (uint16_t)(
        s6502_stack_ram[match->source_pointer_zp] |
        ((uint16_t)s6502_stack_ram[
            (uint8_t)(match->source_pointer_zp + 1u)
        ] << 8)
    );
    source_address = (uint16_t)(source_base + source_index);
    destination_base = (uint16_t)(
        s6502_stack_ram[match->destination_pointer_zp] |
        ((uint16_t)s6502_stack_ram[
            (uint8_t)(match->destination_pointer_zp + 1u)
        ] << 8)
    );
    /* The matched routine normally reads immutable sprite data and writes a
     * screen buffer.  Keep the fused estimate away from zero-page/control
     * aliasing; unusual callers retain the exact guest path. */
    if (source_address < 0x0300u || destination_base < 0x0300u)
        return 0u;
    inner_cycles = s6502_game_hle_bitmap_cycles_for(
        match, ix, mem_read(source_address)
    );
    if (!inner_cycles)
        return 0u;

    next_index = (uint8_t)(source_index + 1u);
    compared = (uint8_t)(next_index << 2);
    width = s6502_stack_ram[match->width_zp];
    cycles = (uint16_t)(
        14u + !!(0xff00u & (source_base ^ source_address)) + inner_cycles
    );
    /* LDA/STA/INC/LDA/ASL/ASL/CMP followed either by taken BCS or by
     * untaken BCS+BEQ and JMP back to the outer group. */
    return (uint16_t)(cycles + (compared >= width ? 23u : 27u));
}

static __attribute__((noinline)) uint16_t
s6502_game_hle_bitmap_iteration_cycles(
    const s6502_game_hle_bitmap_t *match, uint32_t ix
)
{
    uint8_t source = s6502_stack_ram[match->source_zp];
    uint8_t destination_index =
        s6502_stack_ram[match->destination_index_zp];
    uint8_t subpixel = s6502_stack_ram[match->subpixel_zp];
    uint8_t x = (uint8_t)ix;
    uint8_t pointer_zp = match->destination_pointer_zp;
    uint16_t destination = (uint16_t)(
        s6502_stack_ram[pointer_zp] |
        ((uint16_t)s6502_stack_ram[(uint8_t)(pointer_zp + 1u)] << 8)
    );
    uint16_t cycles;
    uint8_t bits;

    if (subpixel >= 4u || x >= 8u || destination >= GAM4980_RAM_SIZE)
        return 0u;
    bits = source & 0xc0u;
    if (!bits) {
        cycles = (uint16_t)(18u +
            !!(0xff00u &
                ((uint16_t)(match->virtual_pc + 0x06u) ^
                 (uint16_t)(match->virtual_pc + 0x14u))) +
            !!(0xff00u &
                (match->and_table ^ (uint16_t)(match->and_table + x))));
    } else if (bits == 0x40u) {
        cycles = (uint16_t)(24u +
            !!(0xff00u &
                (match->or_table ^ (uint16_t)(match->or_table + x))));
    } else {
        cycles = (uint16_t)(12u +
            !!(0xff00u &
                ((uint16_t)(match->virtual_pc + 0x0au) ^
                 (uint16_t)(match->virtual_pc + 0x1bu))));
    }

    ++x;
    if (x == 8u) {
        uint8_t next_index = (uint8_t)(destination_index + 1u);
        uint16_t next_address = (uint16_t)(destination + next_index);

        if (next_address >= GAM4980_RAM_SIZE)
            return 0u;
        cycles = (uint16_t)(cycles + 33u +
            !!(0xff00u & (destination ^ next_address)));
    } else {
        cycles = (uint16_t)(cycles + 7u +
            !!(0xff00u &
                ((uint16_t)(match->virtual_pc + 0x20u) ^
                 (uint16_t)(match->virtual_pc + 0x2fu))));
    }

    ++subpixel;
    if (subpixel == 4u) {
        cycles = (uint16_t)(cycles + 13u +
            !!(0xff00u &
                ((uint16_t)(match->virtual_pc + 0x37u) ^
                 (uint16_t)(match->virtual_pc + 0x40u))));
    } else {
        cycles = (uint16_t)(cycles + 25u);
    }
    return cycles;
}

static __attribute__((noinline)) uint16_t
s6502_game_hle_bitmap_outer_prefix_cycles(
    const s6502_game_hle_bitmap_t *match
)
{
    uint8_t source_index;
    uint16_t source_base;
    uint16_t source_address;

    if (!match->outer_physical_pc ||
        s6502_stack_ram[match->subpixel_zp] != 0u)
        return 0u;
    source_index = s6502_stack_ram[match->source_index_zp];
    source_base = (uint16_t)(
        s6502_stack_ram[match->source_pointer_zp] |
        ((uint16_t)s6502_stack_ram[
            (uint8_t)(match->source_pointer_zp + 1u)
        ] << 8)
    );
    source_address = (uint16_t)(source_base + source_index);
    return (uint16_t)(
        14u + !!(0xff00u & (source_base ^ source_address))
    );
}

static __attribute__((noinline)) uint16_t
s6502_game_hle_bitmap_subpixel_tail_cycles(
    const s6502_game_hle_bitmap_t *match
)
{
    uint8_t next_subpixel = (uint8_t)(
        s6502_stack_ram[match->subpixel_zp] + 1u
    );

    return next_subpixel == 4u ? 13u : 25u;
}

static __attribute__((noinline)) uint16_t
s6502_game_hle_bitmap_group_tail_cycles(
    const s6502_game_hle_bitmap_t *match
)
{
    uint8_t next_index = (uint8_t)(
        s6502_stack_ram[match->source_index_zp] + 1u
    );
    uint8_t compared = (uint8_t)(next_index << 2);

    return compared >= s6502_stack_ram[match->width_zp] ? 23u : 27u;
}

static __attribute__((noinline)) uint16_t
s6502_game_hle_bitmap_row_cycles(
    const s6502_game_hle_bitmap_t *match, uint32_t ix
)
{
    uint8_t next_row;
    uint8_t next_vertical;
    uint16_t cycles;

    if (!match->row_physical_pc)
        return 0u;
    next_row = (uint8_t)(
        s6502_stack_ram[match->row_count_zp] + 1u
    );
    cycles = (uint16_t)(ix ? 30u : 19u);
    if (s6502_stack_ram[match->height_zp] == next_row)
        return cycles;

    next_vertical = (uint8_t)(
        s6502_stack_ram[match->vertical_zp] + 1u
    );
    /* The unmatched branch executes an RTS immediately.  Leave that rare
     * clipping exit to the guest so this helper never has to synthesize a
     * return-stack transition. */
    if (next_vertical > 0x5fu)
        return 0u;

    cycles = (uint16_t)(ix ? 125u : 114u);
    if (s6502_stack_ram[match->width_zp] & 0x07u)
        cycles = (uint16_t)(cycles + 3u);
    return cycles;
}

static inline __attribute__((always_inline)) uint8_t
s6502_game_hle_status_nz(uint8_t status, uint8_t value)
{
    return (uint8_t)(
        (status & (uint8_t)~0x82u) | (value & 0x80u) |
        (value ? 0u : 0x02u)
    );
}

static inline __attribute__((always_inline)) uint8_t
s6502_game_hle_adc8(uint8_t *status, uint8_t left, uint8_t right)
{
    uint16_t sum = (uint16_t)(
        (uint16_t)left + right + ((*status & 0x01u) ? 1u : 0u)
    );
    uint8_t result = (uint8_t)sum;
    uint8_t next = (uint8_t)(*status & (uint8_t)~0xc3u);

    if (sum > 0xffu)
        next |= 0x01u;
    if ((left ^ result) & (right ^ result) & 0x80u)
        next |= 0x40u;
    *status = s6502_game_hle_status_nz(next, result);
    return result;
}

static inline __attribute__((always_inline)) uint8_t
s6502_game_hle_sbc8(uint8_t *status, uint8_t left, uint8_t right)
{
    uint8_t complemented = (uint8_t)~right;
    uint16_t sum = (uint16_t)(
        (uint16_t)left + complemented +
        ((*status & 0x01u) ? 1u : 0u)
    );
    uint8_t result = (uint8_t)sum;
    uint8_t next = (uint8_t)(*status & (uint8_t)~0xc3u);

    if (sum > 0xffu)
        next |= 0x01u;
    if ((left ^ result) & (complemented ^ result) & 0x80u)
        next |= 0x40u;
    *status = s6502_game_hle_status_nz(next, result);
    return result;
}

static __attribute__((noinline)) int s6502_game_hle_scan_region(
    const s6502_game_hle_scan_t *match, uint32_t initial_status,
    uint32_t cycle_budget, s6502_hle_game_scan_result_t *result
)
{
    uint8_t *ram = s6502_stack_ram;
    uint32_t cycles = 0u;
    uint32_t iterations = 0u;
    uint8_t status = (uint8_t)initial_status;
    uint8_t ac = 0u;
    uint8_t iy = 0u;
    uint16_t return_pc = match->virtual_pc;
    int failure = 0;

    for (;;) {
        uint8_t *object_pointer = 0;
        uint8_t *array_pointer = 0;
        uint32_t object_size;
        uint32_t iteration_cycles;
        uint16_t object;
        uint16_t array;
        uint16_t address;
        uint16_t index_address;
        uint16_t limit_address;
        uint8_t index;
        uint8_t next_index;
        uint8_t limit;
        uint8_t value;
        uint8_t cross_index;
        uint8_t cross_limit;
        uint8_t cross_array_low;
        uint8_t cross_array_high;
        uint8_t common_crosses;
        uint8_t max_offset = match->index_offset;

        if (match->limit_offset > max_offset)
            max_offset = match->limit_offset;
        if ((uint8_t)(match->array_pointer_offset + 1u) > max_offset)
            max_offset = (uint8_t)(match->array_pointer_offset + 1u);
        object_size = (uint32_t)max_offset + 1u;
        object = (uint16_t)(
            ram[match->object_pointer_zp] |
            ((uint16_t)ram[
                (uint8_t)(match->object_pointer_zp + 1u)
            ] << 8)
        );
        if (object < 0x0300u ||
            (uint32_t)object + object_size > 0x10000u ||
            !s6502_firmware_hle_ram_span(
                ram, object, object_size, &object_pointer
            )) {
            failure = -1;
            break;
        }
        index_address = (uint16_t)(object + match->index_offset);
        limit_address = (uint16_t)(object + match->limit_offset);
        index = object_pointer[match->index_offset];
        limit = object_pointer[match->limit_offset];
        array = (uint16_t)(
            object_pointer[match->array_pointer_offset] |
            ((uint16_t)object_pointer[
                (uint8_t)(match->array_pointer_offset + 1u)
            ] << 8)
        );
        if ((uint32_t)array + index > 0xffffu) {
            failure = -1;
            break;
        }
        address = (uint16_t)(array + index);
        if (address < 0x0300u ||
            !s6502_firmware_hle_ram_span(
                ram, address, 1u, &array_pointer
            ) ||
            (array_pointer >= object_pointer &&
             array_pointer < object_pointer + object_size)) {
            failure = -1;
            break;
        }
        value = *array_pointer;
        cross_index = (uint8_t)!!(0xff00u & (object ^ index_address));
        cross_limit = (uint8_t)!!(0xff00u & (object ^ limit_address));
        cross_array_low = (uint8_t)!!(
            0xff00u &
            (object ^ (uint16_t)(object + match->array_pointer_offset))
        );
        cross_array_high = (uint8_t)!!(
            0xff00u &
            (object ^ (uint16_t)(
                object + match->array_pointer_offset + 1u
            ))
        );
        common_crosses = (uint8_t)(
            cross_index + cross_array_low + cross_array_high
        );

        if (value == 1u) {
            iteration_cycles = 57u + common_crosses;
        } else {
            uint32_t scan_cycles = value
                ? 183u + 3u * common_crosses
                : 125u + 2u * common_crosses;

            next_index = (uint8_t)(index + 1u);
            scan_cycles += 20u + cross_index;
            if (next_index == limit)
                scan_cycles += 22u + cross_index + cross_limit;
            else if (next_index < limit)
                scan_cycles += 24u + cross_index + cross_limit;
            else
                scan_cycles += 23u + cross_index + cross_limit;
            iteration_cycles = scan_cycles;
        }
        if (iteration_cycles > cycle_budget - cycles)
            break;

        cycles += iteration_cycles;
        ++iterations;
        ram[match->temporary_pointer_zp] = (uint8_t)address;
        ram[(uint8_t)(match->temporary_pointer_zp + 1u)] =
            (uint8_t)(address >> 8);
        iy = 0u;
        status = s6502_game_hle_status_nz(status, iy);
        ac = value;
        status = s6502_game_hle_status_nz(status, ac);
        status |= 0x01u;
        ac = s6502_game_hle_sbc8(&status, ac, 1u);
        if (value == 1u) {
            return_pc = match->special_pc;
            break;
        }

        /* The secondary block recomputes the same address.  For non-zero
         * entries it decrements the byte; zero entries only take the shared
         * increment tail. */
        if (value) {
            iy = 0u;
            status = s6502_game_hle_status_nz(status, iy);
            ac = value;
            status = s6502_game_hle_status_nz(status, ac);
            status |= 0x01u;
            ac = s6502_game_hle_sbc8(&status, ac, 1u);
            *array_pointer = ac;
        }

        iy = match->index_offset;
        status = s6502_game_hle_status_nz(status, iy);
        ac = object_pointer[match->index_offset];
        status = s6502_game_hle_status_nz(status, ac);
        status &= (uint8_t)~0x01u;
        ac = s6502_game_hle_adc8(&status, ac, 1u);
        object_pointer[match->index_offset] = ac;

        iy = match->index_offset;
        status = s6502_game_hle_status_nz(status, iy);
        ac = object_pointer[match->index_offset];
        status = s6502_game_hle_status_nz(status, ac);
        iy = match->limit_offset;
        status = s6502_game_hle_status_nz(status, iy);
        status |= 0x01u;
        ac = s6502_game_hle_sbc8(
            &status, ac, object_pointer[match->limit_offset]
        );
        if ((status & 0x02u) || !(status & 0x01u)) {
            return_pc = match->virtual_pc;
        } else {
            return_pc = match->exit_pc;
            break;
        }
        if (sys_halt_p())
            break;
    }

    if (!iterations)
        return failure;
    result->cycles = cycles;
    result->iterations = iterations;
    result->pc = return_pc;
    result->ac = ac;
    result->iy = iy;
    result->status = status;
    return 1;
}

static inline __attribute__((always_inline)) uint32_t
s6502_game_hle_index_cross(uint16_t base, uint8_t index)
{
    return ((uint32_t)(base & 0xffu) + index) > 0xffu;
}

static __attribute__((noinline)) int s6502_game_hle_record_scan_region(
    const s6502_game_hle_record_scan_t *match, uint32_t initial_status,
    uint32_t cycle_budget, s6502_hle_game_scan_result_t *result
)
{
    uint8_t *ram = s6502_stack_ram;
    uint32_t cycles = 0u;
    uint32_t iterations = 0u;
    uint8_t status = (uint8_t)initial_status;
    uint8_t ac = 0u;
    uint8_t iy = 0u;
    uint16_t return_pc = match->virtual_pc;
    int failure = 0;

    for (;;) {
        uint8_t *object_pointer = 0;
        uint8_t *candidate_pointer = 0;
        uint8_t *reference_pointer = 0;
        uint32_t common_crosses;
        uint32_t first_cycles;
        uint32_t iteration_cycles;
        uint16_t object;
        uint16_t index;
        uint16_t data;
        uint16_t reference;
        uint16_t candidate_address;
        uint8_t candidate0;
        uint8_t candidate1 = 0u;
        uint8_t candidate2 = 0u;
        uint8_t reference0;
        uint8_t reference1 = 0u;
        uint8_t reference2 = 0u;
        uint8_t second_matches = 0u;
        uint8_t third_matches = 0u;
        uint8_t continues = 0u;
        uint8_t partial_first = 0u;

        object = (uint16_t)(
            ram[match->object_pointer_zp] |
            ((uint16_t)ram[
                (uint8_t)(match->object_pointer_zp + 1u)
            ] << 8)
        );
        if (object < 0x0300u ||
            (uint32_t)object + 16u > 0x10000u ||
            !s6502_firmware_hle_ram_span(
                ram, object, 16u, &object_pointer
            )) {
            failure = -1;
            break;
        }
        index = (uint16_t)(
            object_pointer[match->index_offset] |
            ((uint16_t)object_pointer[
                (uint8_t)(match->index_offset + 1u)
            ] << 8)
        );
        data = (uint16_t)(
            object_pointer[match->data_pointer_offset] |
            ((uint16_t)object_pointer[
                (uint8_t)(match->data_pointer_offset + 1u)
            ] << 8)
        );
        reference = (uint16_t)(
            object_pointer[match->reference_pointer_offset] |
            ((uint16_t)object_pointer[
                (uint8_t)(match->reference_pointer_offset + 1u)
            ] << 8)
        );
        candidate_address = (uint16_t)(data + index - 2u);
        if (candidate_address < 0x0300u || reference < 0x0300u ||
            (uint32_t)reference + 3u > 0x10000u ||
            !s6502_firmware_hle_read_span(
                candidate_address, 1u, &candidate_pointer
            ) ||
            !s6502_firmware_hle_read_span(
                reference, 3u, &reference_pointer
            ) ||
            (candidate_pointer >= object_pointer &&
             candidate_pointer < object_pointer + 16u) ||
            (reference_pointer < object_pointer + 16u &&
             reference_pointer + 3u > object_pointer)) {
            failure = -1;
            break;
        }
        candidate0 = *candidate_pointer;
        reference0 = reference_pointer[0];
        common_crosses =
            s6502_game_hle_index_cross(object, match->index_offset) +
            s6502_game_hle_index_cross(
                object, (uint8_t)(match->index_offset + 1u)
            ) +
            s6502_game_hle_index_cross(
                object, match->data_pointer_offset
            ) +
            s6502_game_hle_index_cross(
                object, (uint8_t)(match->data_pointer_offset + 1u)
            ) +
            s6502_game_hle_index_cross(object, match->candidate_offset) +
            s6502_game_hle_index_cross(
                object, match->reference_pointer_offset
            ) +
            s6502_game_hle_index_cross(
                object, (uint8_t)(match->reference_pointer_offset + 1u)
            );
        first_cycles = 153u + common_crosses;
        iteration_cycles = first_cycles;

        if (candidate0 == reference0) {
            first_cycles = 152u + common_crosses;
            candidate_address = (uint16_t)(data + index);
            if (candidate_address < 0x0300u ||
                !s6502_firmware_hle_read_span(
                    candidate_address, 1u, &candidate_pointer
                ) ||
                (candidate_pointer >= object_pointer &&
                 candidate_pointer < object_pointer + 16u)) {
                failure = -1;
                break;
            }
            candidate2 = *candidate_pointer;
            reference2 = reference_pointer[2];
            second_matches = (uint8_t)(candidate2 == reference2);
            iteration_cycles = first_cycles +
                (second_matches ? 120u : 125u) + common_crosses +
                s6502_game_hle_index_cross(reference, 2u);
            if (second_matches) {
                candidate_address = (uint16_t)(data + index - 1u);
                if (candidate_address < 0x0300u ||
                    !s6502_firmware_hle_read_span(
                        candidate_address, 1u, &candidate_pointer
                    ) ||
                    (candidate_pointer >= object_pointer &&
                     candidate_pointer < object_pointer + 16u)) {
                    failure = -1;
                    break;
                }
                candidate1 = *candidate_pointer;
                reference1 = reference_pointer[1];
                third_matches = (uint8_t)(candidate1 == reference1);
                iteration_cycles +=
                    (third_matches ? 163u : 155u) + common_crosses +
                    s6502_game_hle_index_cross(reference, 1u);
            }
            continues = (uint8_t)(
                !second_matches || (second_matches && !third_matches)
            );
            if (continues) {
                iteration_cycles += 66u +
                    s6502_game_hle_index_cross(
                        object, match->index_offset
                    ) +
                    s6502_game_hle_index_cross(
                        object, (uint8_t)(match->index_offset + 1u)
                    );
            }
        }
        if (iteration_cycles > cycle_budget - cycles) {
            if (first_cycles > cycle_budget - cycles)
                break;
            iteration_cycles = first_cycles;
            partial_first = 1u;
        }

        cycles += iteration_cycles;
        ++iterations;

        /* First comparison: data[index - 2] against reference[0]. */
        iy = match->index_offset;
        status = s6502_game_hle_status_nz(status, iy);
        ac = object_pointer[match->index_offset];
        status = s6502_game_hle_status_nz(status, ac);
        ram[0x23u] = ac;
        ++iy;
        status = s6502_game_hle_status_nz(status, iy);
        ac = object_pointer[(uint8_t)(match->index_offset + 1u)];
        status = s6502_game_hle_status_nz(status, ac);
        ram[0x24u] = ac;
        iy = match->data_pointer_offset;
        status = s6502_game_hle_status_nz(status, iy);
        ac = object_pointer[match->data_pointer_offset];
        status = s6502_game_hle_status_nz(status, ac);
        ram[0x20u] = ac;
        ++iy;
        status = s6502_game_hle_status_nz(status, iy);
        ac = object_pointer[(uint8_t)(match->data_pointer_offset + 1u)];
        status = s6502_game_hle_status_nz(status, ac);
        ram[0x21u] = ac;
        status &= (uint8_t)~0x01u;
        ac = ram[0x20u];
        status = s6502_game_hle_status_nz(status, ac);
        ac = s6502_game_hle_adc8(&status, ac, ram[0x23u]);
        ram[0x20u] = ac;
        ac = ram[0x21u];
        status = s6502_game_hle_status_nz(status, ac);
        ac = s6502_game_hle_adc8(&status, ac, ram[0x24u]);
        ram[0x21u] = ac;
        ac = 2u;
        status = s6502_game_hle_status_nz(status, ac);
        ram[0x23u] = ac;
        ac = 0u;
        status = s6502_game_hle_status_nz(status, ac);
        ram[0x24u] = ac;
        status |= 0x01u;
        ac = ram[0x20u];
        status = s6502_game_hle_status_nz(status, ac);
        ac = s6502_game_hle_sbc8(&status, ac, ram[0x23u]);
        ram[0x20u] = ac;
        ac = ram[0x21u];
        status = s6502_game_hle_status_nz(status, ac);
        ac = s6502_game_hle_sbc8(&status, ac, ram[0x24u]);
        ram[0x21u] = ac;
        iy = 0u;
        status = s6502_game_hle_status_nz(status, iy);
        ac = candidate0;
        status = s6502_game_hle_status_nz(status, ac);
        iy = match->candidate_offset;
        status = s6502_game_hle_status_nz(status, iy);
        object_pointer[match->candidate_offset] = ac;
        iy = match->reference_pointer_offset;
        status = s6502_game_hle_status_nz(status, iy);
        ac = (uint8_t)reference;
        status = s6502_game_hle_status_nz(status, ac);
        ram[0x26u] = ac;
        ++iy;
        status = s6502_game_hle_status_nz(status, iy);
        ac = (uint8_t)(reference >> 8);
        status = s6502_game_hle_status_nz(status, ac);
        ram[0x27u] = ac;
        iy = 0u;
        status = s6502_game_hle_status_nz(status, iy);
        ac = reference0;
        status = s6502_game_hle_status_nz(status, ac);
        ram[0x23u] = ac;
        iy = match->candidate_offset;
        status = s6502_game_hle_status_nz(status, iy);
        ac = object_pointer[match->candidate_offset];
        status = s6502_game_hle_status_nz(status, ac);
        status |= 0x01u;
        ac = s6502_game_hle_sbc8(&status, ac, ram[0x23u]);
        if (candidate0 != reference0) {
            return_pc = match->exit_pc;
            break;
        }
        if (partial_first) {
            return_pc = (uint16_t)(match->virtual_pc + 0x60u);
            break;
        }

        /* Second comparison: data[index] against reference[2]. */
        iy = match->index_offset;
        status = s6502_game_hle_status_nz(status, iy);
        ac = object_pointer[match->index_offset];
        status = s6502_game_hle_status_nz(status, ac);
        ram[0x23u] = ac;
        ++iy;
        status = s6502_game_hle_status_nz(status, iy);
        ac = object_pointer[(uint8_t)(match->index_offset + 1u)];
        status = s6502_game_hle_status_nz(status, ac);
        ram[0x24u] = ac;
        iy = match->data_pointer_offset;
        status = s6502_game_hle_status_nz(status, iy);
        ac = object_pointer[match->data_pointer_offset];
        status = s6502_game_hle_status_nz(status, ac);
        ram[0x20u] = ac;
        ++iy;
        status = s6502_game_hle_status_nz(status, iy);
        ac = object_pointer[(uint8_t)(match->data_pointer_offset + 1u)];
        status = s6502_game_hle_status_nz(status, ac);
        ram[0x21u] = ac;
        status &= (uint8_t)~0x01u;
        ac = ram[0x20u];
        status = s6502_game_hle_status_nz(status, ac);
        ac = s6502_game_hle_adc8(&status, ac, ram[0x23u]);
        ram[0x20u] = ac;
        ac = ram[0x21u];
        status = s6502_game_hle_status_nz(status, ac);
        ac = s6502_game_hle_adc8(&status, ac, ram[0x24u]);
        ram[0x21u] = ac;
        iy = 0u;
        status = s6502_game_hle_status_nz(status, iy);
        ac = candidate2;
        status = s6502_game_hle_status_nz(status, ac);
        iy = match->candidate_offset;
        status = s6502_game_hle_status_nz(status, iy);
        object_pointer[match->candidate_offset] = ac;
        iy = match->reference_pointer_offset;
        status = s6502_game_hle_status_nz(status, iy);
        ac = (uint8_t)reference;
        status = s6502_game_hle_status_nz(status, ac);
        ram[0x26u] = ac;
        ++iy;
        status = s6502_game_hle_status_nz(status, iy);
        ac = (uint8_t)(reference >> 8);
        status = s6502_game_hle_status_nz(status, ac);
        ram[0x27u] = ac;
        iy = 2u;
        status = s6502_game_hle_status_nz(status, iy);
        ac = reference2;
        status = s6502_game_hle_status_nz(status, ac);
        ram[0x23u] = ac;
        iy = match->candidate_offset;
        status = s6502_game_hle_status_nz(status, iy);
        ac = object_pointer[match->candidate_offset];
        status = s6502_game_hle_status_nz(status, ac);
        status |= 0x01u;
        ac = s6502_game_hle_sbc8(&status, ac, ram[0x23u]);

        if (second_matches) {
            /* Third comparison: data[index - 1] against reference[1]. */
            iy = match->index_offset;
            status = s6502_game_hle_status_nz(status, iy);
            ac = object_pointer[match->index_offset];
            status = s6502_game_hle_status_nz(status, ac);
            ram[0x23u] = ac;
            ++iy;
            status = s6502_game_hle_status_nz(status, iy);
            ac = object_pointer[(uint8_t)(match->index_offset + 1u)];
            status = s6502_game_hle_status_nz(status, ac);
            ram[0x24u] = ac;
            iy = match->data_pointer_offset;
            status = s6502_game_hle_status_nz(status, iy);
            ac = object_pointer[match->data_pointer_offset];
            status = s6502_game_hle_status_nz(status, ac);
            ram[0x20u] = ac;
            ++iy;
            status = s6502_game_hle_status_nz(status, iy);
            ac = object_pointer[(uint8_t)(match->data_pointer_offset + 1u)];
            status = s6502_game_hle_status_nz(status, ac);
            ram[0x21u] = ac;
            status &= (uint8_t)~0x01u;
            ac = ram[0x20u];
            status = s6502_game_hle_status_nz(status, ac);
            ac = s6502_game_hle_adc8(&status, ac, ram[0x23u]);
            ram[0x20u] = ac;
            ac = ram[0x21u];
            status = s6502_game_hle_status_nz(status, ac);
            ac = s6502_game_hle_adc8(&status, ac, ram[0x24u]);
            ram[0x21u] = ac;
            ac = 1u;
            status = s6502_game_hle_status_nz(status, ac);
            ram[0x23u] = ac;
            ac = 0u;
            status = s6502_game_hle_status_nz(status, ac);
            ram[0x24u] = ac;
            status |= 0x01u;
            ac = ram[0x20u];
            status = s6502_game_hle_status_nz(status, ac);
            ac = s6502_game_hle_sbc8(&status, ac, ram[0x23u]);
            ram[0x20u] = ac;
            ac = ram[0x21u];
            status = s6502_game_hle_status_nz(status, ac);
            ac = s6502_game_hle_sbc8(&status, ac, ram[0x24u]);
            ram[0x21u] = ac;
            iy = 0u;
            status = s6502_game_hle_status_nz(status, iy);
            ac = candidate1;
            status = s6502_game_hle_status_nz(status, ac);
            iy = match->candidate_offset;
            status = s6502_game_hle_status_nz(status, iy);
            object_pointer[match->candidate_offset] = ac;
            iy = match->reference_pointer_offset;
            status = s6502_game_hle_status_nz(status, iy);
            ac = (uint8_t)reference;
            status = s6502_game_hle_status_nz(status, ac);
            ram[0x26u] = ac;
            ++iy;
            status = s6502_game_hle_status_nz(status, iy);
            ac = (uint8_t)(reference >> 8);
            status = s6502_game_hle_status_nz(status, ac);
            ram[0x27u] = ac;
            iy = 1u;
            status = s6502_game_hle_status_nz(status, iy);
            ac = reference1;
            status = s6502_game_hle_status_nz(status, ac);
            ram[0x23u] = ac;
            iy = match->candidate_offset;
            status = s6502_game_hle_status_nz(status, iy);
            ac = object_pointer[match->candidate_offset];
            status = s6502_game_hle_status_nz(status, ac);
            status |= 0x01u;
            ac = s6502_game_hle_sbc8(&status, ac, ram[0x23u]);
            if (third_matches) {
                iy = match->found_offset;
                status = s6502_game_hle_status_nz(status, iy);
                ac = 1u;
                status = s6502_game_hle_status_nz(status, ac);
                object_pointer[match->found_offset] = ac;
                return_pc = match->exit_pc;
                break;
            }
        }

        /* Failed second/third comparison: advance the record index by 3. */
        iy = match->index_offset;
        status = s6502_game_hle_status_nz(status, iy);
        ac = object_pointer[match->index_offset];
        status = s6502_game_hle_status_nz(status, ac);
        ram[0x20u] = ac;
        ++iy;
        status = s6502_game_hle_status_nz(status, iy);
        ac = object_pointer[(uint8_t)(match->index_offset + 1u)];
        status = s6502_game_hle_status_nz(status, ac);
        ram[0x21u] = ac;
        ac = ram[0x20u];
        status = s6502_game_hle_status_nz(status, ac);
        status &= (uint8_t)~0x01u;
        ac = s6502_game_hle_adc8(&status, ac, 3u);
        ram[0x20u] = ac;
        ac = ram[0x21u];
        status = s6502_game_hle_status_nz(status, ac);
        ac = s6502_game_hle_adc8(&status, ac, 0u);
        ram[0x21u] = ac;
        iy = match->index_offset;
        status = s6502_game_hle_status_nz(status, iy);
        ac = ram[0x20u];
        status = s6502_game_hle_status_nz(status, ac);
        object_pointer[match->index_offset] = ac;
        ++iy;
        status = s6502_game_hle_status_nz(status, iy);
        ac = ram[0x21u];
        status = s6502_game_hle_status_nz(status, ac);
        object_pointer[(uint8_t)(match->index_offset + 1u)] = ac;
        return_pc = match->virtual_pc;
        if (sys_halt_p())
            break;
    }

    if (!iterations)
        return failure;
    result->cycles = cycles;
    result->iterations = iterations;
    result->pc = return_pc;
    result->ac = ac;
    result->iy = iy;
    result->status = status;
    return 1;
}

static inline __attribute__((always_inline)) void
s6502_game_hle_record_compare_state_fields(
    uint8_t index_offset, uint8_t data_pointer_offset,
    uint8_t candidate_offset, uint8_t reference_pointer_offset,
    uint8_t *object_pointer, uint16_t data, uint16_t reference,
    uint8_t subtract, uint8_t reference_index,
    uint8_t candidate_value, uint8_t reference_value,
    uint8_t *ac_value, uint8_t *iy_value, uint8_t *status_value
)
{
    uint8_t *ram = s6502_stack_ram;
    uint8_t ac = *ac_value;
    uint8_t iy = *iy_value;
    uint8_t status = *status_value;

    iy = index_offset;
    status = s6502_game_hle_status_nz(status, iy);
    ac = object_pointer[index_offset];
    status = s6502_game_hle_status_nz(status, ac);
    ram[0x23u] = ac;
    ++iy;
    status = s6502_game_hle_status_nz(status, iy);
    ac = object_pointer[(uint8_t)(index_offset + 1u)];
    status = s6502_game_hle_status_nz(status, ac);
    ram[0x24u] = ac;

    iy = data_pointer_offset;
    status = s6502_game_hle_status_nz(status, iy);
    ac = object_pointer[data_pointer_offset];
    status = s6502_game_hle_status_nz(status, ac);
    ram[0x20u] = ac;
    ++iy;
    status = s6502_game_hle_status_nz(status, iy);
    ac = object_pointer[(uint8_t)(data_pointer_offset + 1u)];
    status = s6502_game_hle_status_nz(status, ac);
    ram[0x21u] = ac;

    status &= (uint8_t)~0x01u;
    ac = ram[0x20u];
    status = s6502_game_hle_status_nz(status, ac);
    ac = s6502_game_hle_adc8(&status, ac, ram[0x23u]);
    ram[0x20u] = ac;
    ac = ram[0x21u];
    status = s6502_game_hle_status_nz(status, ac);
    ac = s6502_game_hle_adc8(&status, ac, ram[0x24u]);
    ram[0x21u] = ac;

    if (subtract) {
        ac = subtract;
        status = s6502_game_hle_status_nz(status, ac);
        ram[0x23u] = ac;
        ac = 0u;
        status = s6502_game_hle_status_nz(status, ac);
        ram[0x24u] = ac;
        status |= 0x01u;
        ac = ram[0x20u];
        status = s6502_game_hle_status_nz(status, ac);
        ac = s6502_game_hle_sbc8(&status, ac, ram[0x23u]);
        ram[0x20u] = ac;
        ac = ram[0x21u];
        status = s6502_game_hle_status_nz(status, ac);
        ac = s6502_game_hle_sbc8(&status, ac, ram[0x24u]);
        ram[0x21u] = ac;
    }

    iy = 0u;
    status = s6502_game_hle_status_nz(status, iy);
    ac = candidate_value;
    status = s6502_game_hle_status_nz(status, ac);
    iy = candidate_offset;
    status = s6502_game_hle_status_nz(status, iy);
    object_pointer[candidate_offset] = ac;

    iy = reference_pointer_offset;
    status = s6502_game_hle_status_nz(status, iy);
    ac = (uint8_t)reference;
    status = s6502_game_hle_status_nz(status, ac);
    ram[0x26u] = ac;
    ++iy;
    status = s6502_game_hle_status_nz(status, iy);
    ac = (uint8_t)(reference >> 8);
    status = s6502_game_hle_status_nz(status, ac);
    ram[0x27u] = ac;

    iy = reference_index;
    status = s6502_game_hle_status_nz(status, iy);
    ac = reference_value;
    status = s6502_game_hle_status_nz(status, ac);
    ram[0x23u] = ac;
    iy = candidate_offset;
    status = s6502_game_hle_status_nz(status, iy);
    ac = object_pointer[candidate_offset];
    status = s6502_game_hle_status_nz(status, ac);
    status |= 0x01u;
    ac = s6502_game_hle_sbc8(&status, ac, ram[0x23u]);

    *ac_value = ac;
    *iy_value = iy;
    *status_value = status;
    (void)data;
}

static __attribute__((noinline)) int
s6502_game_hle_record_scan_resume_region(
    const s6502_game_hle_record_scan_t *match, uint32_t initial_pc,
    uint32_t initial_ac, uint32_t initial_iy, uint32_t initial_status,
    uint32_t cycle_budget,
    s6502_hle_game_scan_result_t *result
)
{
    uint8_t *ram = s6502_stack_ram;
    uint8_t *object_pointer = 0;
    uint8_t *reference_pointer = 0;
    uint16_t object;
    uint16_t index;
    uint16_t data;
    uint16_t reference;
    uint16_t pc = (uint16_t)initial_pc;
    uint32_t common_crosses;
    uint32_t prefix_crosses;
    uint32_t cycles = 0u;
    uint32_t iterations = 0u;
    uint8_t status = (uint8_t)initial_status;
    uint8_t ac = (uint8_t)initial_ac;
    uint8_t iy = (uint8_t)initial_iy;
    int failure = 0;

    if (!match || !result ||
        (pc != (uint16_t)(match->virtual_pc + 0x60u) &&
         pc != (uint16_t)(match->virtual_pc + 0xa5u) &&
         pc != (uint16_t)(match->virtual_pc + 0xa8u) &&
         pc != (uint16_t)(match->virtual_pc - 0x26u)))
        return -1;
    object = (uint16_t)(
        ram[match->object_pointer_zp] |
        ((uint16_t)ram[(uint8_t)(match->object_pointer_zp + 1u)] << 8)
    );
    if (object < 0x0300u || (uint32_t)object + 16u > 0x10000u ||
        !s6502_firmware_hle_ram_span(
            ram, object, 16u, &object_pointer
        ))
        return -1;
    index = (uint16_t)(
        object_pointer[match->index_offset] |
        ((uint16_t)object_pointer[
            (uint8_t)(match->index_offset + 1u)
        ] << 8)
    );
    data = (uint16_t)(
        object_pointer[match->data_pointer_offset] |
        ((uint16_t)object_pointer[
            (uint8_t)(match->data_pointer_offset + 1u)
        ] << 8)
    );
    reference = (uint16_t)(
        object_pointer[match->reference_pointer_offset] |
        ((uint16_t)object_pointer[
            (uint8_t)(match->reference_pointer_offset + 1u)
        ] << 8)
    );
    if (reference < 0x0300u ||
        (uint32_t)reference + 3u > 0x10000u ||
        !s6502_firmware_hle_read_span(
            reference, 3u, &reference_pointer
        ) ||
        (reference_pointer < object_pointer + 16u &&
         reference_pointer + 3u > object_pointer))
        return -1;
    prefix_crosses =
        s6502_game_hle_index_cross(object, match->index_offset) +
        s6502_game_hle_index_cross(
            object, (uint8_t)(match->index_offset + 1u)
        ) +
        s6502_game_hle_index_cross(
            object, match->data_pointer_offset
        ) +
        s6502_game_hle_index_cross(
            object, (uint8_t)(match->data_pointer_offset + 1u)
        );
    common_crosses = prefix_crosses +
        s6502_game_hle_index_cross(object, match->candidate_offset) +
        s6502_game_hle_index_cross(
            object, match->reference_pointer_offset
        ) +
        s6502_game_hle_index_cross(
            object, (uint8_t)(match->reference_pointer_offset + 1u)
        );

    for (;;) {
        uint8_t *candidate_pointer = 0;
        uint16_t candidate_address;
        uint32_t stage_cycles;
        uint8_t candidate_value;
        uint8_t reference_value;
        uint8_t matches;

        if (pc == (uint16_t)(match->virtual_pc + 0x60u)) {
            candidate_address = (uint16_t)(data + index);
            if (candidate_address < 0x0300u ||
                !s6502_firmware_hle_read_span(
                    candidate_address, 1u, &candidate_pointer
                ) ||
                (candidate_pointer >= object_pointer &&
                 candidate_pointer < object_pointer + 16u)) {
                failure = -1;
                break;
            }
            candidate_value = *candidate_pointer;
            reference_value = reference_pointer[2u];
            matches = (uint8_t)(candidate_value == reference_value);
            /* $7BFE-$7C42 is one load-time AOT superblock.  The terminating
             * BEQ is part of that block: a match lands at $7C46, while the
             * untaken path returns at the following JMP ($7C43). */
            stage_cycles = (matches ? 120u : 119u) + common_crosses +
                s6502_game_hle_index_cross(reference, 2u);
            if (cycles && (cycles >= cycle_budget ||
                stage_cycles > cycle_budget - cycles))
                break;
            cycles += stage_cycles;
            ++iterations;
            s6502_game_hle_record_compare_state_fields(
                match->index_offset, match->data_pointer_offset,
                match->candidate_offset, match->reference_pointer_offset,
                object_pointer, data, reference, 0u, 2u,
                candidate_value, reference_value, &ac, &iy, &status
            );
            pc = matches
                ? (uint16_t)(match->virtual_pc + 0xa8u)
                : (uint16_t)(match->virtual_pc + 0xa5u);
            if (stage_cycles > cycle_budget)
                break;
        } else if (pc == (uint16_t)(match->virtual_pc + 0xa5u)) {
            stage_cycles = 6u;
            if (cycles && (cycles >= cycle_budget ||
                stage_cycles > cycle_budget - cycles))
                break;
            cycles += stage_cycles;
            ++iterations;
            pc = (uint16_t)(match->virtual_pc - 0x26u);
            if (stage_cycles > cycle_budget)
                break;
        } else if (pc == (uint16_t)(match->virtual_pc + 0xa8u)) {
            candidate_address = (uint16_t)(data + index - 1u);
            if (candidate_address < 0x0300u ||
                !s6502_firmware_hle_read_span(
                    candidate_address, 1u, &candidate_pointer
                ) ||
                (candidate_pointer >= object_pointer &&
                 candidate_pointer < object_pointer + 16u)) {
                failure = -1;
                break;
            }
            candidate_value = *candidate_pointer;
            reference_value = reference_pointer[1u];
            matches = (uint8_t)(candidate_value == reference_value);
            stage_cycles = (matches ? 163u : 155u) + common_crosses +
                s6502_game_hle_index_cross(reference, 1u);
            if (stage_cycles > cycle_budget - cycles)
                break;
            cycles += stage_cycles;
            ++iterations;
            s6502_game_hle_record_compare_state_fields(
                match->index_offset, match->data_pointer_offset,
                match->candidate_offset, match->reference_pointer_offset,
                object_pointer, data, reference, 1u, 1u,
                candidate_value, reference_value, &ac, &iy, &status
            );
            if (matches) {
                iy = match->found_offset;
                status = s6502_game_hle_status_nz(status, iy);
                ac = 1u;
                status = s6502_game_hle_status_nz(status, ac);
                object_pointer[match->found_offset] = ac;
                pc = match->exit_pc;
                break;
            }
            pc = (uint16_t)(match->virtual_pc - 0x26u);
        } else {
            stage_cycles = 66u +
                s6502_game_hle_index_cross(
                    object, match->index_offset
                ) +
                s6502_game_hle_index_cross(
                    object, (uint8_t)(match->index_offset + 1u)
                );
            if (stage_cycles > cycle_budget - cycles)
                break;
            cycles += stage_cycles;
            ++iterations;

            iy = match->index_offset;
            status = s6502_game_hle_status_nz(status, iy);
            ac = object_pointer[match->index_offset];
            status = s6502_game_hle_status_nz(status, ac);
            ram[0x20u] = ac;
            ++iy;
            status = s6502_game_hle_status_nz(status, iy);
            ac = object_pointer[(uint8_t)(match->index_offset + 1u)];
            status = s6502_game_hle_status_nz(status, ac);
            ram[0x21u] = ac;
            ac = ram[0x20u];
            status = s6502_game_hle_status_nz(status, ac);
            status &= (uint8_t)~0x01u;
            ac = s6502_game_hle_adc8(&status, ac, 3u);
            ram[0x20u] = ac;
            ac = ram[0x21u];
            status = s6502_game_hle_status_nz(status, ac);
            ac = s6502_game_hle_adc8(&status, ac, 0u);
            ram[0x21u] = ac;
            iy = match->index_offset;
            status = s6502_game_hle_status_nz(status, iy);
            ac = ram[0x20u];
            status = s6502_game_hle_status_nz(status, ac);
            object_pointer[match->index_offset] = ac;
            ++iy;
            status = s6502_game_hle_status_nz(status, iy);
            ac = ram[0x21u];
            status = s6502_game_hle_status_nz(status, ac);
            object_pointer[(uint8_t)(match->index_offset + 1u)] = ac;
            pc = match->virtual_pc;
            break;
        }
    }

    if (!iterations)
        return failure;
    result->cycles = cycles;
    result->iterations = iterations;
    result->pc = pc;
    result->ac = ac;
    result->iy = iy;
    result->status = status;
    return 1;
}

static __attribute__((noinline)) int s6502_game_hle_record_reverse_region(
    const s6502_game_hle_record_reverse_t *match, uint32_t initial_status,
    uint32_t cycle_budget, s6502_hle_game_scan_result_t *result
)
{
    uint8_t *ram = s6502_stack_ram;
    uint8_t *object_pointer = 0;
    uint16_t object;
    uint32_t cycles = 0u;
    uint32_t iterations = 0u;
    uint8_t status = (uint8_t)initial_status;
    uint8_t ac = 0u;
    uint8_t iy = 0u;
    uint16_t return_pc = match ? match->virtual_pc : 0u;
    int failure = 0;

    if (!match || !result || match->error_address >= GAM4980_RAM_SIZE)
        return -1;
    object = (uint16_t)(
        ram[match->object_pointer_zp] |
        ((uint16_t)ram[(uint8_t)(match->object_pointer_zp + 1u)] << 8)
    );
    if (object < 0x0300u || (uint32_t)object + 16u > 0x10000u ||
        !s6502_firmware_hle_ram_span(ram, object, 16u, &object_pointer))
        return -1;

    for (;;) {
        uint8_t *candidate0_pointer = 0;
        uint8_t *candidate1_pointer = 0;
        uint8_t *candidate2_pointer = 0;
        uint8_t *reference_pointer = 0;
        uint16_t index = (uint16_t)(
            object_pointer[match->index_offset] |
            ((uint16_t)object_pointer[
                (uint8_t)(match->index_offset + 1u)
            ] << 8)
        );
        uint16_t data = (uint16_t)(
            object_pointer[match->data_pointer_offset] |
            ((uint16_t)object_pointer[
                (uint8_t)(match->data_pointer_offset + 1u)
            ] << 8)
        );
        uint16_t reference = (uint16_t)(
            object_pointer[match->reference_pointer_offset] |
            ((uint16_t)object_pointer[
                (uint8_t)(match->reference_pointer_offset + 1u)
            ] << 8)
        );
        uint16_t candidate0_address = (uint16_t)(data + index - 2u);
        uint16_t candidate1_address = (uint16_t)(data + index - 1u);
        uint16_t candidate2_address = (uint16_t)(data + index);
        uint32_t common_crosses;
        uint32_t iteration_cycles;
        uint8_t candidate0;
        uint8_t candidate1;
        uint8_t candidate2;
        uint8_t reference0;
        uint8_t reference1;
        uint8_t reference2;
        uint8_t second_matches;
        uint8_t third_matches;
        uint8_t terminal;
        uint8_t found_after;

        if (candidate0_address < 0x0300u ||
            candidate1_address < 0x0300u ||
            candidate2_address < 0x0300u || reference < 0x0300u ||
            (uint32_t)reference + 3u > 0x10000u ||
            !s6502_firmware_hle_read_span(
                candidate0_address, 1u, &candidate0_pointer
            ) ||
            !s6502_firmware_hle_read_span(
                candidate1_address, 1u, &candidate1_pointer
            ) ||
            !s6502_firmware_hle_read_span(
                candidate2_address, 1u, &candidate2_pointer
            ) ||
            !s6502_firmware_hle_read_span(
                reference, 3u, &reference_pointer
            ) ||
            (candidate0_address >= object &&
             candidate0_address < (uint16_t)(object + 16u)) ||
            (candidate1_address >= object &&
             candidate1_address < (uint16_t)(object + 16u)) ||
            (candidate2_address >= object &&
             candidate2_address < (uint16_t)(object + 16u)) ||
            (reference < (uint16_t)(object + 16u) &&
             (uint32_t)reference + 3u > object)) {
            failure = -1;
            break;
        }

        candidate0 = candidate0_pointer[0];
        candidate1 = candidate1_pointer[0];
        candidate2 = candidate2_pointer[0];
        reference0 = reference_pointer[0];
        reference1 = reference_pointer[1];
        reference2 = reference_pointer[2];
        second_matches = (uint8_t)(candidate2 == reference2);
        third_matches = (uint8_t)(candidate1 == reference1);
        terminal = (uint8_t)(
            candidate0 != reference0 ||
            (second_matches && third_matches)
        );
        found_after = (uint8_t)(
            (candidate0 == reference0 && second_matches && third_matches) ? 1u :
            object_pointer[match->found_offset]
        );
        common_crosses =
            s6502_game_hle_index_cross(object, match->index_offset) +
            s6502_game_hle_index_cross(
                object, (uint8_t)(match->index_offset + 1u)
            ) +
            s6502_game_hle_index_cross(
                object, match->data_pointer_offset
            ) +
            s6502_game_hle_index_cross(
                object, (uint8_t)(match->data_pointer_offset + 1u)
            ) +
            s6502_game_hle_index_cross(object, match->candidate_offset) +
            s6502_game_hle_index_cross(
                object, match->reference_pointer_offset
            ) +
            s6502_game_hle_index_cross(
                object, (uint8_t)(match->reference_pointer_offset + 1u)
            );

        if (candidate0 != reference0) {
            iteration_cycles = 153u + common_crosses +
                (found_after ? 12u : 19u) +
                s6502_game_hle_index_cross(object, match->found_offset);
        } else if (second_matches && third_matches) {
            iteration_cycles = 152u + common_crosses +
                120u + common_crosses +
                s6502_game_hle_index_cross(reference, 2u) +
                163u + common_crosses +
                s6502_game_hle_index_cross(reference, 1u) +
                12u +
                s6502_game_hle_index_cross(object, match->found_offset);
        } else if (!second_matches) {
            iteration_cycles = 152u + common_crosses +
                122u + common_crosses +
                s6502_game_hle_index_cross(reference, 2u) +
                155u + common_crosses + 66u +
                s6502_game_hle_index_cross(object, match->index_offset) +
                s6502_game_hle_index_cross(
                    object, (uint8_t)(match->index_offset + 1u)
                );
        } else {
            iteration_cycles = 152u + common_crosses +
                120u + common_crosses +
                s6502_game_hle_index_cross(reference, 2u) +
                152u + common_crosses +
                s6502_game_hle_index_cross(reference, 1u) +
                155u + common_crosses + 66u +
                s6502_game_hle_index_cross(object, match->index_offset) +
                s6502_game_hle_index_cross(
                    object, (uint8_t)(match->index_offset + 1u)
                );
        }
        if (iteration_cycles > cycle_budget - cycles)
            break;

        s6502_game_hle_record_compare_state_fields(
            match->index_offset, match->data_pointer_offset,
            match->candidate_offset, match->reference_pointer_offset,
            object_pointer, data, reference, 2u, 0u,
            candidate0, reference0, &ac, &iy, &status
        );
        if (candidate0 == reference0) {
            s6502_game_hle_record_compare_state_fields(
                match->index_offset, match->data_pointer_offset,
                match->candidate_offset, match->reference_pointer_offset,
                object_pointer, data, reference, 0u, 2u,
                candidate2, reference2, &ac, &iy, &status
            );
            if (second_matches) {
                s6502_game_hle_record_compare_state_fields(
                    match->index_offset, match->data_pointer_offset,
                    match->candidate_offset,
                    match->reference_pointer_offset,
                    object_pointer, data, reference, 1u, 1u,
                    candidate1, reference1, &ac, &iy, &status
                );
                if (third_matches) {
                    iy = match->found_offset;
                    status = s6502_game_hle_status_nz(status, iy);
                    ac = 1u;
                    status = s6502_game_hle_status_nz(status, ac);
                    object_pointer[match->found_offset] = ac;
                }
            }
        }

        if (!terminal) {
            uint16_t decremented = (uint16_t)(index - 3u);

            /* The original failure path rechecks the first byte before it
             * backs up to the preceding three-byte record. */
            s6502_game_hle_record_compare_state_fields(
                match->index_offset, match->data_pointer_offset,
                match->candidate_offset, match->reference_pointer_offset,
                object_pointer, data, reference, 2u, 0u,
                candidate0, reference0, &ac, &iy, &status
            );

            iy = match->index_offset;
            status = s6502_game_hle_status_nz(status, iy);
            ac = object_pointer[match->index_offset];
            status = s6502_game_hle_status_nz(status, ac);
            ram[0x20u] = ac;
            ++iy;
            status = s6502_game_hle_status_nz(status, iy);
            ac = object_pointer[(uint8_t)(match->index_offset + 1u)];
            status = s6502_game_hle_status_nz(status, ac);
            ram[0x21u] = ac;
            ac = ram[0x20u];
            status = s6502_game_hle_status_nz(status, ac);
            status |= 0x01u;
            ac = s6502_game_hle_sbc8(&status, ac, 3u);
            ram[0x20u] = ac;
            ac = ram[0x21u];
            status = s6502_game_hle_status_nz(status, ac);
            ac = s6502_game_hle_sbc8(&status, ac, 0u);
            ram[0x21u] = ac;
            iy = match->index_offset;
            status = s6502_game_hle_status_nz(status, iy);
            ac = (uint8_t)decremented;
            status = s6502_game_hle_status_nz(status, ac);
            object_pointer[match->index_offset] = ac;
            ++iy;
            status = s6502_game_hle_status_nz(status, iy);
            ac = (uint8_t)(decremented >> 8);
            status = s6502_game_hle_status_nz(status, ac);
            object_pointer[(uint8_t)(match->index_offset + 1u)] = ac;
            return_pc = match->virtual_pc;
        } else {
            iy = match->found_offset;
            status = s6502_game_hle_status_nz(status, iy);
            ac = object_pointer[match->found_offset];
            status = s6502_game_hle_status_nz(status, ac);
            if (!ac) {
                ac = match->error_value;
                status = s6502_game_hle_status_nz(status, ac);
                ram[match->error_address] = ac;
                return_pc = match->failure_pc;
            } else {
                return_pc = match->success_pc;
            }
        }

        cycles += iteration_cycles;
        ++iterations;
        if (terminal || sys_halt_p())
            break;
    }

    if (!iterations)
        return failure;
    result->cycles = cycles;
    result->iterations = iterations;
    result->pc = return_pc;
    result->ac = ac;
    result->iy = iy;
    result->status = status;
    return 1;
}

static __attribute__((noinline)) int s6502_game_hle_table_chain_region(
    const s6502_game_hle_table_chain_t *match, uint32_t initial_pc,
    uint32_t initial_sp, uint32_t initial_status, uint32_t cycle_budget,
    s6502_hle_game_table_result_t *result
)
{
    uint8_t *ram = s6502_stack_ram;
    uint8_t *object_pointer = 0;
    uint8_t *array_pointer = 0;
    uint8_t *table_pointer = 0;
    uint8_t *field_pointer = 0;
    uint16_t object;
    uint16_t array;
    uint16_t table;
    uint16_t field;
    uint16_t product;
    uint16_t table_entry;
    uint16_t return_pc;
    uint32_t cycles;
    uint8_t ac;
    uint8_t ix;
    uint8_t iy;
    uint8_t sp = (uint8_t)initial_sp;
    uint8_t status = (uint8_t)initial_status;
    uint8_t value;
    uint8_t temporary;

    if (!match || !result || match->firmware_pc != 0xd6afu)
        return -1;
    object = (uint16_t)(ram[0x28u] | ((uint16_t)ram[0x29u] << 8));
    if (object < 0x0300u ||
        !s6502_firmware_hle_ram_span(
            ram, object, 0x31u, &object_pointer
        ))
        return -1;

    product = (uint16_t)(ram[0x20u] | ((uint16_t)ram[0x21u] << 8));
    array = (uint16_t)(
        object_pointer[0] | ((uint16_t)object_pointer[1] << 8)
    );
    array = (uint16_t)(array + product);
    if (!s6502_firmware_hle_read_span(
            (uint16_t)(array + 4u), 1u, &array_pointer
        ))
        return -1;
    value = array_pointer[0];
    table = (uint16_t)(
        object_pointer[0x25u] |
        ((uint16_t)object_pointer[0x26u] << 8)
    );
    table = (uint16_t)(table + ((uint16_t)value << 1));
    if (!s6502_firmware_hle_read_span(table, 2u, &table_pointer))
        return -1;
    table_entry = (uint16_t)(
        table_pointer[0] | ((uint16_t)table_pointer[1] << 8)
    );
    field = (uint16_t)(table_entry + match->field_offset);
    if (!s6502_firmware_hle_read_span(field, 1u, &field_pointer))
        return -1;

    cycles = 248u +
        s6502_game_hle_index_cross(object, 1u) +
        s6502_game_hle_index_cross(array, 4u) +
        s6502_game_hle_index_cross(object, 0x25u) +
        s6502_game_hle_index_cross(object, 0x26u) +
        s6502_game_hle_index_cross(table, 1u) +
        s6502_game_hle_index_cross(object, 0x10u) +
        s6502_game_hle_index_cross(object, 0x1eu);
    if (cycles > cycle_budget)
        return 0;

    /* Restore the caller's saved $23/$24 operands. */
    sp = (uint8_t)(sp + 1u);
    ac = ram[0x100u | sp];
    status = s6502_game_hle_status_nz(status, ac);
    ram[0x23u] = ac;
    sp = (uint8_t)(sp + 1u);
    ac = ram[0x100u | sp];
    status = s6502_game_hle_status_nz(status, ac);
    ram[0x24u] = ac;

    /* Add the object base to the multiplication result. */
    iy = 0u;
    status = s6502_game_hle_status_nz(status, iy);
    status &= (uint8_t)~0x01u;
    ac = object_pointer[0];
    status = s6502_game_hle_status_nz(status, ac);
    ac = s6502_game_hle_adc8(&status, ac, ram[0x20u]);
    ram[0x20u] = ac;
    ++iy;
    status = s6502_game_hle_status_nz(status, iy);
    ac = object_pointer[1];
    status = s6502_game_hle_status_nz(status, ac);
    ac = s6502_game_hle_adc8(&status, ac, ram[0x21u]);
    ram[0x21u] = ac;

    /* Load the table selector and multiply it by two. */
    iy = 4u;
    status = s6502_game_hle_status_nz(status, iy);
    ac = value;
    status = s6502_game_hle_status_nz(status, ac);
    ram[0x20u] = ac;
    ac = 0u;
    status = s6502_game_hle_status_nz(status, ac);
    ram[0x21u] = ac;
    temporary = ram[0x20u];
    status = (uint8_t)(
        (status & (uint8_t)~0x01u) | ((temporary >> 7) & 0x01u)
    );
    temporary = (uint8_t)(temporary << 1);
    status = s6502_game_hle_status_nz(status, temporary);
    ram[0x20u] = temporary;
    temporary = ram[0x21u];
    value = (uint8_t)(status & 0x01u);
    status = (uint8_t)(
        (status & (uint8_t)~0x01u) | ((temporary >> 7) & 0x01u)
    );
    temporary = (uint8_t)((temporary << 1) | value);
    status = s6502_game_hle_status_nz(status, temporary);
    ram[0x21u] = temporary;

    /* Resolve the two-byte pointer from the object's table. */
    iy = 0x25u;
    status = s6502_game_hle_status_nz(status, iy);
    status &= (uint8_t)~0x01u;
    ac = object_pointer[0x25u];
    status = s6502_game_hle_status_nz(status, ac);
    ac = s6502_game_hle_adc8(&status, ac, ram[0x20u]);
    ram[0x20u] = ac;
    ++iy;
    status = s6502_game_hle_status_nz(status, iy);
    ac = object_pointer[0x26u];
    status = s6502_game_hle_status_nz(status, ac);
    ac = s6502_game_hle_adc8(&status, ac, ram[0x21u]);
    ram[0x21u] = ac;
    iy = 0u;
    status = s6502_game_hle_status_nz(status, iy);
    ac = table_pointer[0];
    status = s6502_game_hle_status_nz(status, ac);
    ix = ac;
    status = s6502_game_hle_status_nz(status, ix);
    ++iy;
    status = s6502_game_hle_status_nz(status, iy);
    ac = table_pointer[1];
    status = s6502_game_hle_status_nz(status, ac);
    ram[0x21u] = ac;
    ram[0x20u] = ix;
    ac = ram[0x20u];
    status = s6502_game_hle_status_nz(status, ac);
    ram[0x23u] = ac;
    ac = ram[0x21u];
    status = s6502_game_hle_status_nz(status, ac);
    ram[0x24u] = ac;

    /* Read the selected structure field and store its zero-extended value. */
    ac = match->field_offset;
    status = s6502_game_hle_status_nz(status, ac);
    ram[0x20u] = ac;
    ac = 0u;
    status = s6502_game_hle_status_nz(status, ac);
    ram[0x21u] = ac;
    status &= (uint8_t)~0x01u;
    ac = ram[0x20u];
    status = s6502_game_hle_status_nz(status, ac);
    ac = s6502_game_hle_adc8(&status, ac, ram[0x23u]);
    ram[0x20u] = ac;
    ac = ram[0x21u];
    status = s6502_game_hle_status_nz(status, ac);
    ac = s6502_game_hle_adc8(&status, ac, ram[0x24u]);
    ram[0x21u] = ac;
    iy = 0u;
    status = s6502_game_hle_status_nz(status, iy);
    ac = field_pointer[0];
    status = s6502_game_hle_status_nz(status, ac);
    ram[0x20u] = ac;
    ac = 0u;
    status = s6502_game_hle_status_nz(status, ac);
    ram[0x21u] = ac;
    iy = match->output_offset;
    status = s6502_game_hle_status_nz(status, iy);
    ac = ram[0x20u];
    status = s6502_game_hle_status_nz(status, ac);
    mem_write((uint16_t)(object + iy), ac);
    ++iy;
    status = s6502_game_hle_status_nz(status, iy);
    ac = ram[0x21u];
    status = s6502_game_hle_status_nz(status, ac);
    mem_write((uint16_t)(object + iy), ac);

    /* Prepare the next multiply-by-five firmware call and its stack image. */
    iy = 0x10u;
    status = s6502_game_hle_status_nz(status, iy);
    ac = object_pointer[0x10u];
    status = s6502_game_hle_status_nz(status, ac);
    iy = 0x1eu;
    status = s6502_game_hle_status_nz(status, iy);
    status &= (uint8_t)~0x01u;
    ac = s6502_game_hle_adc8(&status, ac, object_pointer[0x1eu]);
    ram[0x20u] = ac;
    ac = 0u;
    status = s6502_game_hle_status_nz(status, ac);
    ram[0x21u] = ac;
    ac = ram[0x24u];
    status = s6502_game_hle_status_nz(status, ac);
    ram[0x100u | sp] = ac;
    sp = (uint8_t)(sp - 1u);
    ac = ram[0x23u];
    status = s6502_game_hle_status_nz(status, ac);
    ram[0x100u | sp] = ac;
    sp = (uint8_t)(sp - 1u);
    ac = 5u;
    status = s6502_game_hle_status_nz(status, ac);
    ram[0x23u] = ac;
    ac = 0u;
    status = s6502_game_hle_status_nz(status, ac);
    ram[0x24u] = ac;
    return_pc = (uint16_t)(initial_pc + 0x91u);
    ram[0x100u | sp] = (uint8_t)(return_pc >> 8);
    sp = (uint8_t)(sp - 1u);
    ram[0x100u | sp] = (uint8_t)return_pc;
    sp = (uint8_t)(sp - 1u);

    result->cycles = cycles;
    result->pc = match->firmware_pc;
    result->ac = ac;
    result->ix = ix;
    result->iy = iy;
    result->sp = sp;
    result->status = status;
    return 1;
}

static __attribute__((noinline)) int
s6502_game_hle_object_flow_firmware_match(void)
{
    static const uint8_t signature[12u] = {
        0xa5,0x21,0x30,0x08,0xa5,0x24,0x30,0x12,
        0x20,0xa2,0xd1,0x60,
    };
    uint32_t index;

    if (PA(0xd6afu) != 0xea86afu || PA(0xd1a2u) != 0xea81a2u)
        return 0;
    if (s6502_game_hle_object_flow_firmware_validation == 2u)
        return 0;
    if (s6502_game_hle_object_flow_firmware_validation == 0u) {
        for (index = 0u; index < sizeof(signature); ++index) {
            if (mem_readx((uint16_t)(0xd6afu + index)) != signature[index]) {
                s6502_game_hle_object_flow_firmware_validation = 2u;
                return 0;
            }
        }
        s6502_game_hle_object_flow_firmware_validation = 1u;
    }
    return s6502_firmware_hle_multiply_validation == 1u ||
        s6502_firmware_hle_multiply_match();
}

static __attribute__((noinline)) int s6502_game_hle_object_flow_region(
    const s6502_game_hle_object_flow_t *match, uint32_t initial_pc,
    uint32_t initial_sp, uint32_t initial_status, uint32_t cycle_budget,
    s6502_hle_game_table_result_t *result
)
{
    uint8_t *ram = s6502_stack_ram;
    uint8_t *object_pointer = 0;
    uint8_t *selector_pointer = 0;
    uint8_t *table_pointer = 0;
    uint16_t object;
    uint16_t first_value;
    uint16_t second_value;
    uint16_t multiplicand;
    uint16_t product;
    uint16_t array;
    uint16_t table;
    uint16_t table_entry;
    uint16_t final_value;
    uint16_t caller_return;
    uint16_t first_output;
    uint16_t second_output;
    uint16_t selector_address;
    uint32_t cycles;
    uint8_t old_23;
    uint8_t old_24;
    uint8_t sp;
    uint8_t status;
    uint8_t ac;
    uint8_t iy;

    if (!match || !result || match->firmware_pc != 0xd6afu ||
        !s6502_game_hle_object_flow_firmware_match())
        return -1;
    if (PA((uint16_t)initial_pc) != match->physical_pc ||
        PA((uint16_t)(initial_pc + 0x94u)) !=
            match->physical_pc + 0x94u)
        return -1;

    object = (uint16_t)(
        ram[match->object_pointer_zp] |
        ((uint16_t)ram[(uint8_t)(match->object_pointer_zp + 1u)] << 8)
    );
    if (object < 0x0300u ||
        !s6502_firmware_hle_ram_span(
            ram, object, (uint16_t)(match->output_offset + 2u),
            &object_pointer
        ))
        return -1;

    first_value = (uint16_t)(
        object_pointer[match->first_pointer_offset] |
        ((uint16_t)object_pointer[
            (uint8_t)(match->first_pointer_offset + 1u)
        ] << 8)
    );
    first_value = (uint16_t)(
        first_value + object_pointer[match->first_value_offset] - 1u
    );
    second_value = (uint16_t)(
        object_pointer[match->second_pointer_offset] |
        ((uint16_t)object_pointer[
            (uint8_t)(match->second_pointer_offset + 1u)
        ] << 8)
    );
    second_value = (uint16_t)(
        second_value + object_pointer[match->second_value_offset] - 1u
    );
    multiplicand = (uint8_t)(
        object_pointer[match->index_left_offset] +
        object_pointer[match->index_right_offset]
    );
    product = (uint16_t)(multiplicand * 5u);
    array = (uint16_t)(
        object_pointer[0] | ((uint16_t)object_pointer[1] << 8)
    );
    array = (uint16_t)(array + product);
    selector_address = (uint16_t)(array + 4u);
    if (!s6502_firmware_hle_read_span(
            selector_address, 1u, &selector_pointer
        ))
        return -1;
    table = (uint16_t)(
        object_pointer[match->table_pointer_offset] |
        ((uint16_t)object_pointer[
            (uint8_t)(match->table_pointer_offset + 1u)
        ] << 8)
    );
    table = (uint16_t)(table + ((uint16_t)selector_pointer[0] << 1));
    if (!s6502_firmware_hle_read_span(table, 2u, &table_pointer))
        return -1;

    /* The two boundary stores precede both lookup reads in guest order.
     * Unusual aliasing retains the original path instead of observing data
     * that this helper inspected before committing those stores. */
    first_output = (uint16_t)(object + match->first_output_offset);
    second_output = (uint16_t)(object + match->second_output_offset);
    if (selector_address == first_output ||
        selector_address == (uint16_t)(first_output + 1u) ||
        selector_address == second_output ||
        selector_address == (uint16_t)(second_output + 1u) ||
        table == first_output ||
        table == (uint16_t)(first_output + 1u) ||
        (uint16_t)(table + 1u) == first_output ||
        (uint16_t)(table + 1u) == (uint16_t)(first_output + 1u) ||
        table == second_output ||
        table == (uint16_t)(second_output + 1u) ||
        (uint16_t)(table + 1u) == second_output ||
        (uint16_t)(table + 1u) == (uint16_t)(second_output + 1u))
        return -1;

    table_entry = (uint16_t)(
        table_pointer[0] | ((uint16_t)table_pointer[1] << 8)
    );
    final_value = (uint16_t)(table_entry + match->table_bias);

    /* $6F80-$707B contributes 423 base cycles and the following LDA/JSR
     * into $DAAA contributes 8.  The positive signed multiply wrapper adds
     * 22 cycles around the validated D1A2 unsigned multiply: 61 cycles for
     * zero, otherwise 297 for x5. */
    cycles = 453u + (multiplicand ? 297u : 61u) +
        s6502_game_hle_index_cross(
            object, match->first_pointer_offset
        ) +
        s6502_game_hle_index_cross(
            object, (uint8_t)(match->first_pointer_offset + 1u)
        ) +
        s6502_game_hle_index_cross(object, match->first_value_offset) +
        s6502_game_hle_index_cross(
            object, match->second_pointer_offset
        ) +
        s6502_game_hle_index_cross(
            object, (uint8_t)(match->second_pointer_offset + 1u)
        ) +
        s6502_game_hle_index_cross(object, match->second_value_offset) +
        s6502_game_hle_index_cross(object, match->index_left_offset) +
        s6502_game_hle_index_cross(object, match->index_right_offset) +
        s6502_game_hle_index_cross(object, 1u) +
        s6502_game_hle_index_cross(array, 4u) +
        s6502_game_hle_index_cross(object, match->table_pointer_offset) +
        s6502_game_hle_index_cross(
            object, (uint8_t)(match->table_pointer_offset + 1u)
        ) +
        s6502_game_hle_index_cross(table, 1u);
    if (cycles > cycle_budget)
        return 0;

    old_23 = ram[0x23u];
    old_24 = ram[0x24u];
    object_pointer[match->first_output_offset] = (uint8_t)first_value;
    object_pointer[(uint8_t)(match->first_output_offset + 1u)] =
        (uint8_t)(first_value >> 8);
    object_pointer[match->second_output_offset] = (uint8_t)second_value;
    object_pointer[(uint8_t)(match->second_output_offset + 1u)] =
        (uint8_t)(second_value >> 8);

    /* Preserve every byte left behind by the two caller pushes, both JSRs,
     * and D1A2's saved multiplier, even though the final SP is unchanged. */
    sp = (uint8_t)initial_sp;
    ram[0x100u | sp] = old_24;
    sp = (uint8_t)(sp - 1u);
    ram[0x100u | sp] = old_23;
    sp = (uint8_t)(sp - 1u);
    caller_return = (uint16_t)(initial_pc + 0x93u);
    ram[0x100u | sp] = (uint8_t)(caller_return >> 8);
    sp = (uint8_t)(sp - 1u);
    ram[0x100u | sp] = (uint8_t)caller_return;
    sp = (uint8_t)(sp - 1u);
    ram[0x100u | sp] = 0xd6u;
    sp = (uint8_t)(sp - 1u);
    ram[0x100u | sp] = 0xb9u;
    sp = (uint8_t)(sp - 1u);
    ram[0x100u | sp] = 0u;
    sp = (uint8_t)(sp - 1u);
    ram[0x100u | sp] = 5u;

    ram[0x26u] = (uint8_t)product;
    ram[0x27u] = (uint8_t)(product >> 8);
    ram[0x23u] = (uint8_t)table_entry;
    ram[0x24u] = (uint8_t)(table_entry >> 8);
    ram[0x20u] = (uint8_t)final_value;
    ram[0x21u] = (uint8_t)(final_value >> 8);

    status = (uint8_t)initial_status;
    status &= (uint8_t)~0x01u;
    ac = match->table_bias;
    status = s6502_game_hle_status_nz(status, ac);
    ac = s6502_game_hle_adc8(&status, ac, (uint8_t)table_entry);
    ac = 0u;
    status = s6502_game_hle_status_nz(status, ac);
    ac = s6502_game_hle_adc8(
        &status, ac, (uint8_t)(table_entry >> 8)
    );
    iy = match->output_offset;
    status = s6502_game_hle_status_nz(status, iy);
    ac = (uint8_t)final_value;
    status = s6502_game_hle_status_nz(status, ac);
    ++iy;
    status = s6502_game_hle_status_nz(status, iy);
    ac = (uint8_t)(final_value >> 8);
    status = s6502_game_hle_status_nz(status, ac);
    object_pointer[match->output_offset] = (uint8_t)final_value;
    object_pointer[(uint8_t)(match->output_offset + 1u)] = ac;

    /* LDA #0; JSR $DAAA is part of the original linear block and therefore
     * the first scheduling boundary after this flow. */
    ac = 0u;
    status = s6502_game_hle_status_nz(status, ac);
    sp = (uint8_t)initial_sp;
    caller_return = (uint16_t)(initial_pc + 0x100u);
    ram[0x100u | sp] = (uint8_t)(caller_return >> 8);
    sp = (uint8_t)(sp - 1u);
    ram[0x100u | sp] = (uint8_t)caller_return;
    sp = (uint8_t)(sp - 1u);

    result->cycles = cycles;
    result->pc = match->exit_firmware_pc;
    result->ac = ac;
    result->ix = (uint8_t)table_entry;
    result->iy = iy;
    result->sp = sp;
    result->status = status;
    return 1;
}
#endif

static uint8_t s6502_game_aot_legacy_pattern_at(
    const uint8_t *code, uint32_t remaining
)
{
    if (!remaining)
        return 0u;
    if (code[0] == 0xa9u && remaining >= 21u &&
        code[2] == 0x85u &&
        code[4] == 0xa9u && code[6] == 0x85u &&
        code[8] == 0xa0u && code[10] == 0xb1u &&
        code[12] == 0x85u && code[14] == 0xa9u &&
        code[16] == 0x85u && code[18] == 0x20u)
        return 1u;
    if (code[0] == 0xa0u && remaining >= 38u &&
        code[2] == 0xb1u &&
        code[4] == 0x85u && code[6] == 0xc8u &&
        code[7] == 0xb1u && code[9] == 0x85u &&
        code[11] == 0xa5u && code[13] == 0x18u &&
        code[14] == 0x69u && code[16] == 0x85u &&
        code[18] == 0xa5u && code[20] == 0x69u &&
        code[22] == 0x85u && code[24] == 0xa0u &&
        code[26] == 0xa5u && code[28] == 0x91u &&
        code[30] == 0xc8u && code[31] == 0xa5u &&
        code[33] == 0x91u && code[35] == 0x4cu)
        return 7u;
    if (code[0] == 0xa0u && remaining >= 14u &&
        code[2] == 0xb1u &&
        code[4] == 0x18u && code[5] == 0x69u &&
        code[7] == 0xa0u && code[9] == 0x91u &&
        code[11] == 0x4cu)
        return 2u;
    if (code[0] == 0xa5u && remaining >= 9u &&
        code[2] == 0x0au &&
        code[3] == 0x0au && code[4] == 0x85u &&
        code[6] == 0x4cu)
        return 3u;
    if (code[0] == 0xa5u && remaining >= 6u &&
        code[2] == 0x29u &&
        code[4] == 0xf0u)
        return 5u;
    if (code[0] == 0xe8u && remaining >= 5u &&
        code[1] == 0xe0u &&
        code[3] == 0xd0u)
        return 4u;
    if (code[0] == 0xc9u && remaining >= 4u &&
        code[2] == 0xd0u)
        return 6u;
    return 0u;
}

static uint8_t s6502_game_aot_template_at(
    const uint8_t *code, uint32_t remaining, uint8_t semantic
)
{
    const c6502_template_spec_t *spec;
    uint32_t index;

    if (!semantic || semantic > C6502_TEMPLATE_SPEC_COUNT)
        return 0u;
    spec = &c6502_template_specs[semantic - 1u];
    if (remaining < spec->size)
        return 0u;
    for (index = 0u; index < spec->size; ++index) {
        if ((code[index] & spec->mask[index]) !=
            (spec->bytes[index] & spec->mask[index]))
            return 0u;
    }
    return (uint8_t)(7u + semantic);
}

/* The bounded AOT table is filled by priority.  The old collector called a
 * full 20-pattern classifier once for every priority, repeating nearly all
 * load-time work five times.  Each priority now checks only the templates it
 * can actually accept, while preserving the exact collection order. */
static uint8_t s6502_game_aot_pattern_at_priority(
    const uint8_t *code, uint32_t remaining, uint8_t priority
)
{
    uint8_t encoded_pattern;

    if (!remaining)
        return 0u;
    if (priority == 0u)
        return s6502_game_aot_legacy_pattern_at(code, remaining);
    if (priority == 1u) {
        if (code[0] != 0xa2u)
            return 0u;
        return s6502_game_aot_template_at(
            code, remaining, C6502_TEMPLATE_FAR_CALL
        );
    }
    if (priority == 2u) {
        uint8_t first_kind;
        uint8_t second_kind = C6502_TEMPLATE_NONE;

        if (code[0] == 0x08u) {
            first_kind = C6502_TEMPLATE_STACK_ADD16;
            second_kind = C6502_TEMPLATE_STACK_SUB16;
        } else if (code[0] == 0x18u) {
            first_kind = C6502_TEMPLATE_ADD16_OPER1_OPER2;
        } else if (code[0] == 0x38u) {
            first_kind = C6502_TEMPLATE_SUB16_OPER1_OPER2;
        } else {
            return 0u;
        }
        encoded_pattern = s6502_game_aot_template_at(
            code, remaining, first_kind
        );
        if (encoded_pattern || !second_kind)
            return encoded_pattern;
        return s6502_game_aot_template_at(
            code, remaining, second_kind
        );
    }
    if (priority == 3u) {
        if (code[0] != 0xa9u)
            return 0u;
        encoded_pattern = s6502_game_aot_template_at(
            code, remaining, C6502_TEMPLATE_LOAD_OPER1_IMM16
        );
        return encoded_pattern ? encoded_pattern :
            s6502_game_aot_template_at(
                code, remaining, C6502_TEMPLATE_LOAD_OPER2_IMM16
            );
    }
    if (priority == 4u) {
        uint8_t first_kind;
        uint8_t second_kind = C6502_TEMPLATE_NONE;

        if (code[0] == 0xa9u) {
            first_kind = C6502_TEMPLATE_STORE_CHAR_ARG_IMM;
        } else if (code[0] == 0x20u) {
            first_kind = C6502_TEMPLATE_STORE_INT_ARG_OPER1;
        } else if (code[0] == 0xa5u) {
            first_kind = C6502_TEMPLATE_LOAD_OPER1_ZP16;
            second_kind = C6502_TEMPLATE_LOAD_OPER2_ZP16;
        } else if (code[0] == 0xa0u) {
            first_kind = C6502_TEMPLATE_LOAD_OPER1_INDY16;
            second_kind = C6502_TEMPLATE_STORE_OPER1_INDY16;
        } else {
            return 0u;
        }
        encoded_pattern = s6502_game_aot_template_at(
            code, remaining, first_kind
        );
        if (encoded_pattern || !second_kind)
            return encoded_pattern;
        return s6502_game_aot_template_at(
            code, remaining, second_kind
        );
    }
    return 0u;
}

/* Classify an offset once, in the same priority order used by the bounded
 * collector.  The result and its priority are then cached in the CFG work
 * queue, which is no longer needed after reachability recovery. */
static uint8_t s6502_game_aot_pattern_at(
    const uint8_t *code, uint32_t remaining, uint8_t *matched_priority
)
{
    uint8_t priority;

    for (priority = 0u; priority <= 4u; ++priority) {
        uint8_t encoded_pattern = s6502_game_aot_pattern_at_priority(
            code, remaining, priority
        );

        if (encoded_pattern) {
            *matched_priority = priority;
            return encoded_pattern;
        }
    }
    return 0u;
}

static uint8_t s6502_game_aot_pattern_size(uint8_t encoded_pattern)
{
    static const uint8_t sizes[7] = {21u, 14u, 9u, 5u, 6u, 4u, 38u};
    uint8_t semantic;

    if (encoded_pattern >= 1u && encoded_pattern <= 7u)
        return sizes[encoded_pattern - 1u];
    semantic = (uint8_t)(encoded_pattern - 7u);
    if (semantic >= 1u && semantic <= C6502_TEMPLATE_SPEC_COUNT)
        return c6502_template_specs[semantic - 1u].size;
    return 0u;
}

/* Official NMOS 6502 instruction sizes.  C6502 emits only this subset; the
 * remaining opcodes deliberately decode as one byte so an unexpected byte
 * can only reduce reachability confidence, never skip over a real target. */
static const uint8_t s6502_game_aot_opcode_size[256] = {
    1u, 2u, 1u, 1u, 1u, 2u, 2u, 1u, 1u, 2u, 1u, 1u, 1u, 3u, 3u, 1u,
    2u, 2u, 1u, 1u, 1u, 2u, 2u, 1u, 1u, 3u, 1u, 1u, 1u, 3u, 3u, 1u,
    3u, 2u, 1u, 1u, 1u, 2u, 2u, 1u, 1u, 2u, 1u, 1u, 3u, 3u, 3u, 1u,
    2u, 2u, 1u, 1u, 1u, 2u, 2u, 1u, 1u, 3u, 1u, 1u, 1u, 3u, 3u, 1u,
    1u, 2u, 1u, 1u, 1u, 2u, 2u, 1u, 1u, 2u, 1u, 1u, 3u, 3u, 3u, 1u,
    2u, 2u, 1u, 1u, 1u, 2u, 2u, 1u, 1u, 3u, 1u, 1u, 1u, 3u, 3u, 1u,
    1u, 2u, 1u, 1u, 1u, 2u, 2u, 1u, 1u, 2u, 1u, 1u, 3u, 3u, 3u, 1u,
    2u, 2u, 1u, 1u, 1u, 2u, 2u, 1u, 1u, 3u, 1u, 1u, 1u, 3u, 3u, 1u,
    1u, 2u, 1u, 1u, 2u, 2u, 2u, 1u, 1u, 1u, 1u, 1u, 3u, 3u, 3u, 1u,
    2u, 2u, 1u, 1u, 2u, 2u, 2u, 1u, 1u, 3u, 1u, 1u, 1u, 3u, 1u, 1u,
    2u, 2u, 2u, 1u, 2u, 2u, 2u, 1u, 1u, 2u, 1u, 1u, 3u, 3u, 3u, 1u,
    2u, 2u, 1u, 1u, 2u, 2u, 2u, 1u, 1u, 3u, 1u, 1u, 3u, 3u, 3u, 1u,
    2u, 2u, 1u, 1u, 2u, 2u, 2u, 1u, 1u, 2u, 1u, 1u, 3u, 3u, 3u, 1u,
    2u, 2u, 1u, 1u, 1u, 2u, 2u, 1u, 1u, 3u, 1u, 1u, 1u, 3u, 3u, 1u,
    2u, 2u, 1u, 1u, 2u, 2u, 2u, 1u, 1u, 2u, 1u, 1u, 3u, 3u, 3u, 1u,
    2u, 2u, 1u, 1u, 1u, 2u, 2u, 1u, 1u, 3u, 1u, 1u, 1u, 3u, 3u, 1u,
};

static int s6502_game_aot_cfg_test(uint32_t offset)
{
    return offset < S6502_GAME_AOT_CFG_LIMIT &&
        (s6502_game_aot_cfg_bits[offset >> 3] &
            (uint8_t)(1u << (offset & 7u))) != 0u;
}

static void s6502_game_aot_cfg_set(uint32_t offset)
{
    if (offset < S6502_GAME_AOT_CFG_LIMIT)
        s6502_game_aot_cfg_bits[offset >> 3] |=
            (uint8_t)(1u << (offset & 7u));
}

static void s6502_game_aot_cfg_enqueue(
    uint32_t offset, uint16_t virtual_pc, uint32_t code_size,
    uint16_t *queue_tail
)
{
    s6502_game_aot_cfg_item_t *item;

    if (offset >= code_size || offset >= S6502_GAME_AOT_CFG_LIMIT ||
        virtual_pc < 0x5000u || virtual_pc >= 0x9000u ||
        s6502_game_aot_cfg_test(offset))
        return;
    s6502_game_aot_cfg_set(offset);
    if (*queue_tail >= S6502_GAME_AOT_CFG_QUEUE_SIZE)
        return;
    item = &s6502_game_aot_cfg_queue[*queue_tail];
    item->offset = offset;
    item->virtual_pc = virtual_pc;
    ++*queue_tail;
}

static int s6502_game_aot_resolve_far_call(
    uint16_t table_address, uint16_t *virtual_pc, uint32_t *physical_pc
)
{
    uint16_t target;
    uint16_t base_page;
    uint8_t bank_number;

    if (!virtual_pc || !physical_pc)
        return 0;
    target = (uint16_t)(
        mem_readx(table_address) |
        ((uint16_t)mem_readx((uint16_t)(table_address + 1u)) << 8)
    );
    bank_number = mem_readx((uint16_t)(table_address + 2u));
    if (target < 0x5000u || target >= 0x9000u)
        return 0;
    if (bank_number >= 0xe0u) {
        /* The loader installs this base immediately before AOT preparation;
         * $2029/$202A are written just after it and are therefore not yet a
         * valid source here. */
        base_page = (uint16_t)(
            0x020du + ((uint16_t)(bank_number - 0xe0u) << 2)
        );
    } else {
        base_page = (uint16_t)(
            mem_readx(0x03d6u) | ((uint16_t)mem_readx(0x03d5u) << 8)
        );
        base_page = (uint16_t)(base_page + ((uint16_t)bank_number << 2));
    }
    *virtual_pc = target;
    *physical_pc = ((uint32_t)base_page << 12) +
        (uint32_t)(target - 0x5000u);
    return 1;
}

static uint32_t s6502_game_aot_signature_hash(
    uint16_t first, uint16_t last
)
{
    uint32_t fnv = 2166136261u;
    uint16_t address;

    for (address = first; address != last; ++address)
        fnv = (fnv ^ mem_readx(address)) * 16777619u;
    return fnv;
}

/* Validate every firmware fragment whose stack and bank side effects are
 * folded by the direct .bf_call link.  An unknown firmware revision always
 * falls back to the real trampoline. */
static int s6502_game_aot_direct_link_match(void)
{
    if (s6502_game_aot_direct_link_validation == 1u)
        return 1;
    if (s6502_game_aot_direct_link_validation == 2u)
        return 0;
    if (PA(0xd2f6u) != 0xea82f6u || PA(0xd572u) != 0xea8572u ||
        PA(0xe8f8u) != 0xea98f8u || PA(0xf4a5u) != 0xeaa4a5u ||
        s6502_game_aot_signature_hash(0xd2f6u, 0xd313u) != 0xe15c6e63u ||
        s6502_game_aot_signature_hash(0xd572u, 0xd586u) != 0x1ea69e7fu ||
        s6502_game_aot_signature_hash(0xe8f8u, 0xe8feu) != 0xb45e3097u ||
        s6502_game_aot_signature_hash(0xf4a5u, 0xf4f4u) != 0x3ae34dc8u) {
        s6502_game_aot_direct_link_validation = 2u;
        return 0;
    }
    s6502_game_aot_direct_link_validation = 1u;
    return 1;
}

static void s6502_game_aot_recover_cfg(
    const uint8_t *game, uint32_t code_size, uint16_t start
)
{
    uint16_t queue_head = 0u;
    uint16_t queue_tail = 0u;

    gam4980_memset(
        s6502_game_aot_cfg_bits, 0, sizeof(s6502_game_aot_cfg_bits)
    );
    if (start < 0x5000u || start >= 0x9000u)
        return;
    s6502_game_aot_cfg_enqueue(
        (uint32_t)(start - 0x5000u), start, code_size, &queue_tail
    );
    while (queue_head < queue_tail) {
        uint32_t offset = s6502_game_aot_cfg_queue[queue_head].offset;
        uint16_t virtual_pc =
            s6502_game_aot_cfg_queue[queue_head].virtual_pc;
        int first = 1;

        ++queue_head;
        while (offset < code_size && offset < S6502_GAME_AOT_CFG_LIMIT &&
               virtual_pc >= 0x5000u && virtual_pc < 0x9000u) {
            uint8_t opcode;
            uint8_t instruction_size;
            uint32_t next_offset;
            uint16_t next_pc;
            uint8_t encoded_pattern;

            if (!first && s6502_game_aot_cfg_test(offset))
                break;
            first = 0;
            s6502_game_aot_cfg_set(offset);
            opcode = game[offset];
            instruction_size = s6502_game_aot_opcode_size[opcode];
            if (!instruction_size || instruction_size > code_size - offset)
                break;
            next_offset = offset + instruction_size;
            next_pc = (uint16_t)(virtual_pc + instruction_size);

            encoded_pattern = s6502_game_aot_pattern_at_priority(
                game + offset, code_size - offset, 1u
            );
            if (encoded_pattern ==
                (uint8_t)(7u + C6502_TEMPLATE_FAR_CALL)) {
                uint16_t table_address = (uint16_t)(
                    game[offset + 1u] |
                    ((uint16_t)game[offset + 5u] << 8)
                );
                uint16_t linked_pc;
                uint32_t linked_physical_pc;

                if (s6502_game_aot_resolve_far_call(
                        table_address, &linked_pc, &linked_physical_pc) &&
                    linked_physical_pc >= 0x20d000u &&
                    linked_physical_pc - 0x20d000u < code_size)
                    s6502_game_aot_cfg_enqueue(
                        linked_physical_pc - 0x20d000u, linked_pc,
                        code_size, &queue_tail
                    );
            }

            if (opcode == 0x20u && instruction_size == 3u) {
                uint16_t target = (uint16_t)(
                    game[offset + 1u] |
                    ((uint16_t)game[offset + 2u] << 8)
                );
                if (target >= 0x5000u && target < 0x9000u)
                    s6502_game_aot_cfg_enqueue(
                        (offset & ~0x3fffu) + (uint32_t)(target - 0x5000u),
                        target, code_size, &queue_tail
                    );
            } else if ((opcode & 0x1fu) == 0x10u) {
                long branch_offset = (int8_t)game[offset + 1u];
                uint32_t target_offset = (uint32_t)(
                    (long)next_offset + branch_offset
                );
                uint16_t target_pc = (uint16_t)(next_pc + branch_offset);

                s6502_game_aot_cfg_enqueue(
                    target_offset, target_pc, code_size, &queue_tail
                );
            } else if (opcode == 0x4cu && instruction_size == 3u) {
                uint16_t target = (uint16_t)(
                    game[offset + 1u] |
                    ((uint16_t)game[offset + 2u] << 8)
                );
                if (target >= 0x5000u && target < 0x9000u)
                    s6502_game_aot_cfg_enqueue(
                        (offset & ~0x3fffu) + (uint32_t)(target - 0x5000u),
                        target, code_size, &queue_tail
                    );
                break;
            } else if (opcode == 0x00u || opcode == 0x40u ||
                       opcode == 0x60u || opcode == 0x6cu) {
                break;
            }

            if (next_pc >= 0x9000u)
                break;
            offset = next_offset;
            virtual_pc = next_pc;
        }
    }
}

static uint8_t s6502_game_aot_semantic_cycles(uint8_t semantic)
{
    switch (semantic) {
    case C6502_TEMPLATE_FAR_CALL: return 16u;
    case C6502_TEMPLATE_LOAD_OPER1_IMM16:
    case C6502_TEMPLATE_LOAD_OPER2_IMM16: return 10u;
    case C6502_TEMPLATE_STACK_ADD16:
    case C6502_TEMPLATE_STACK_SUB16: return 27u;
    case C6502_TEMPLATE_STORE_CHAR_ARG_IMM: return 8u;
    case C6502_TEMPLATE_STORE_INT_ARG_OPER1: return 6u;
    case C6502_TEMPLATE_LOAD_OPER1_ZP16:
    case C6502_TEMPLATE_LOAD_OPER2_ZP16: return 12u;
    case C6502_TEMPLATE_ADD16_OPER1_OPER2:
    case C6502_TEMPLATE_SUB16_OPER1_OPER2: return 20u;
    case C6502_TEMPLATE_LOAD_OPER1_INDY16: return 20u;
    case C6502_TEMPLATE_STORE_OPER1_INDY16: return 22u;
    default: return 0u;
    }
}

static int s6502_game_aot_add_entry(
    const uint8_t *game, uint32_t code_size, uint32_t offset,
    uint8_t encoded_pattern, int reachable
)
{
    uint8_t pattern_size = s6502_game_aot_pattern_size(encoded_pattern);
    uint32_t physical_pc;
    uint16_t slot;
    s6502_game_aot_entry_t *entry;

    if (!pattern_size || pattern_size > code_size - offset ||
        (offset & 0x0fffu) + pattern_size > 0x1000u ||
        s6502_game_aot_entry_count >= S6502_GAME_AOT_MAX_ENTRIES)
        return 0;
    physical_pc = 0x20d000u + offset;
    slot = s6502_game_aot_hash_slot(physical_pc);
    while (s6502_game_aot_hash[slot]) {
        if (s6502_game_aot_entries[
                s6502_game_aot_hash[slot] - 1u].physical_pc == physical_pc)
            return 1;
        slot = (uint16_t)((slot + 1u) & (S6502_GAME_AOT_HASH_SIZE - 1u));
    }

    entry = &s6502_game_aot_entries[s6502_game_aot_entry_count];
    entry->physical_pc = physical_pc;
    entry->linked_physical_pc = 0u;
    entry->linked_table_address = 0u;
    entry->linked_virtual_pc = 0u;
    entry->pattern = (uint8_t)(encoded_pattern - 1u);
    entry->size = pattern_size;
    entry->semantic = encoded_pattern > 7u
        ? (uint8_t)(encoded_pattern - 7u) : 0u;
    entry->memory_class = entry->semantic == C6502_TEMPLATE_FAR_CALL
        ? C6502_REGION_GAME_CODE : C6502_REGION_RAM;
    entry->cycle_cost = s6502_game_aot_semantic_cycles(entry->semantic);
    entry->linked_bank_number = 0u;
    if (entry->semantic)
        ++s6502_game_aot_semantic_count;
    if (reachable)
        ++s6502_game_aot_reachable_count;
    if (entry->semantic == C6502_TEMPLATE_FAR_CALL) {
        uint16_t linked_virtual_pc;

        entry->linked_table_address = (uint16_t)(
            game[offset + 1u] | ((uint16_t)game[offset + 5u] << 8)
        );
        if (s6502_game_aot_resolve_far_call(
                entry->linked_table_address, &linked_virtual_pc,
                &entry->linked_physical_pc)) {
            entry->linked_virtual_pc = linked_virtual_pc;
            entry->linked_bank_number = mem_readx((uint16_t)(
                entry->linked_table_address + 2u));
            ++s6502_game_aot_linked_call_count;
        }
    }
    ++s6502_game_aot_entry_count;
    s6502_game_aot_hash[slot] = s6502_game_aot_entry_count;
    return 1;
}

static void s6502_game_aot_prepare(const uint8_t *game, uint32_t size)
{
    uint32_t offset;
    uint32_t code_size;
    uint16_t candidate_count = 0u;
    uint16_t start;
    int candidate_overflow = 0;
    uint8_t reachable_pass;
    uint8_t priority;
    uint8_t aot_progress = 0u;

    s6502_game_aot_entry_count = 0;
    s6502_game_aot_direct_link_hits = 0u;
#ifdef GAM4980_AOT_DIAGNOSTICS
    gam4980_memset(
        s6502_game_aot_direct_link_stage_hits, 0,
        sizeof(s6502_game_aot_direct_link_stage_hits)
    );
#endif
    s6502_game_aot_direct_link_validation = 0u;
    s6502_game_aot_bank_mask = 0;
    s6502_game_aot_enabled = 0;
    s6502_game_aot_semantic_count = 0u;
    s6502_game_aot_linked_call_count = 0u;
    s6502_game_aot_reachable_count = 0u;
    s6502_game_aot_code_size = 0u;
    s6502_game_aot_code_base = game;
    gam4980_memset(
        s6502_game_aot_semantic_hits, 0,
        sizeof(s6502_game_aot_semantic_hits)
    );
#ifdef GAM4980_ENABLE_FIRMWARE_HLE
    s6502_game_hle_prepare(0, 0u);
    if (s6502_firmware_hle_enabled) {
        gam4980_memset(
            s6502_resource_span_cache, 0, sizeof(s6502_resource_span_cache)
        );
        s6502_resource_span_cache_hits = 0u;
        s6502_resource_span_cache_misses = 0u;
    }
#endif
    s6502_game_aot_physical_end = 0x20d000u + size;
    s6502_game_aot_storage_end = 0x15000u + size;
#ifdef GAM4980_AOT_DIAGNOSTICS
    s6502_game_aot_instruction_hits = 0;
    gam4980_memset(
        s6502_game_aot_entry_hits, 0, sizeof(s6502_game_aot_entry_hits)
    );
#endif
    if (!game || size < GAM4980_GAME_HEADER_SIZE)
        return;

    start = (uint16_t)(game[0x40u] | ((uint16_t)game[0x41u] << 8));
    code_size = (uint32_t)game[0x42u] |
        ((uint32_t)game[0x43u] << 8) |
        ((uint32_t)game[0x44u] << 16) |
        ((uint32_t)game[0x45u] << 24);
    if (code_size < GAM4980_GAME_HEADER_SIZE || code_size > size)
        code_size = size;
    s6502_game_aot_code_size = code_size;
#ifdef GAM4980_ENABLE_FIRMWARE_HLE
    if (s6502_firmware_hle_enabled) {
        gam4980_report_load_progress(
            GAM4980_LOAD_STAGE_GAME_HLE, 0u, 1u
        );
        /* Game HLE signatures only describe executable C6502 code.  Large
         * GAM files keep pictures and other resources after code_size; the
         * old full-file scan wasted most startup time on data that cannot be
         * a callable function. */
        s6502_game_hle_prepare(game, code_size);
        gam4980_report_load_progress(
            GAM4980_LOAD_STAGE_GAME_HLE, 1u, 1u
        );
    }
#endif
    if (s6502_game_aot_requested) {
        gam4980_memset(
            s6502_game_aot_hash, 0, sizeof(s6502_game_aot_hash)
        );
        gam4980_report_load_progress(GAM4980_LOAD_STAGE_CFG, 0u, 1u);
        s6502_game_aot_recover_cfg(game, code_size, start);
        gam4980_report_load_progress(GAM4980_LOAD_STAGE_CFG, 1u, 1u);
    }

    /* Reachable compiler templates are collected before the conservative
     * whole-code fallback.  Within each pass, long/expensive semantics win
     * over tiny argument shuffles, so a bounded table cannot be monopolized
     * by whichever template happens to occur near the file header. */
    if (s6502_game_aot_requested)
        gam4980_report_load_progress(
            GAM4980_LOAD_STAGE_AOT_INDEX, 0u, 10u
        );
    /* The previous implementation traversed every code byte ten times: two
     * reachability passes times five priorities.  Match each byte once and
     * reuse the already allocated CFG queue as a compact candidate list.
     * The 16-bit metadata stores encoded pattern, reachability and priority.
     * If an unusually template-dense GAM exceeds the fixed queue, retain the
     * exact old collector as a no-allocation fallback. */
    for (offset = 0u;
         s6502_game_aot_requested && offset + 3u <= code_size;
         ++offset) {
        uint8_t matched_priority = 0u;
        uint8_t encoded_pattern = s6502_game_aot_pattern_at(
            game + offset, code_size - offset, &matched_priority
        );

        if (!encoded_pattern)
            continue;
        if (candidate_count >= S6502_GAME_AOT_CFG_QUEUE_SIZE) {
            candidate_overflow = 1;
            break;
        }
        s6502_game_aot_cfg_queue[candidate_count].offset = offset;
        s6502_game_aot_cfg_queue[candidate_count].virtual_pc = (uint16_t)(
            encoded_pattern |
            (s6502_game_aot_cfg_test(offset) ? 0x0100u : 0u) |
            ((uint16_t)matched_priority << 9)
        );
        ++candidate_count;
    }
    if (s6502_game_aot_requested && !candidate_overflow) {
        gam4980_report_load_progress(
            GAM4980_LOAD_STAGE_AOT_INDEX, 5u, 10u
        );
        for (reachable_pass = 1u; reachable_pass <= 2u; ++reachable_pass) {
            for (priority = 0u; priority <= 4u; ++priority) {
                uint16_t candidate;

                for (candidate = 0u;
                     candidate < candidate_count; ++candidate) {
                    uint16_t metadata =
                        s6502_game_aot_cfg_queue[candidate].virtual_pc;
                    int reachable = (metadata & 0x0100u) != 0u;

                    if ((reachable_pass == 1u) != reachable ||
                        ((metadata >> 9) & 7u) != priority)
                        continue;
                    if (!s6502_game_aot_add_entry(
                            game, code_size,
                            s6502_game_aot_cfg_queue[candidate].offset,
                            (uint8_t)metadata, reachable))
                        break;
                }
                if (s6502_game_aot_entry_count >=
                    S6502_GAME_AOT_MAX_ENTRIES)
                    break;
            }
            if (s6502_game_aot_entry_count >= S6502_GAME_AOT_MAX_ENTRIES)
                break;
        }
        aot_progress = 10u;
    } else if (s6502_game_aot_requested) {
        for (reachable_pass = 1u; reachable_pass <= 2u; ++reachable_pass) {
            for (priority = 0u; priority <= 4u; ++priority) {
                for (offset = 0u; offset + 3u <= code_size; ++offset) {
                    uint8_t encoded_pattern;
                    int reachable = s6502_game_aot_cfg_test(offset);

                    if ((reachable_pass == 1u) != reachable)
                        continue;
                    encoded_pattern = s6502_game_aot_pattern_at_priority(
                        game + offset, code_size - offset, priority
                    );
                    if (!encoded_pattern)
                        continue;
                    if (!s6502_game_aot_add_entry(
                            game, code_size, offset, encoded_pattern,
                            reachable))
                        break;
                }
                aot_progress = (uint8_t)(
                    (reachable_pass - 1u) * 5u + priority + 1u
                );
                gam4980_report_load_progress(
                    GAM4980_LOAD_STAGE_AOT_INDEX, aot_progress, 10u
                );
                if (s6502_game_aot_entry_count >=
                    S6502_GAME_AOT_MAX_ENTRIES)
                    break;
            }
            if (s6502_game_aot_entry_count >= S6502_GAME_AOT_MAX_ENTRIES)
                break;
        }
    }
    if (s6502_game_aot_requested && aot_progress < 10u)
        gam4980_report_load_progress(
            GAM4980_LOAD_STAGE_AOT_INDEX, 10u, 10u
        );
    s6502_game_aot_enabled =
        s6502_game_aot_requested && s6502_game_aot_entry_count != 0u;
#ifdef GAM4980_ENABLE_FIRMWARE_HLE
    if (s6502_firmware_hle_enabled &&
        (s6502_game_hle_counter_count || s6502_game_hle_bitmap_count ||
         s6502_game_hle_scan_count || s6502_game_hle_record_scan_count ||
         s6502_game_hle_record_reverse_count ||
         s6502_game_hle_table_chain_count ||
         s6502_game_hle_object_flow_count))
        s6502_game_aot_enabled = 1;
#endif
    if (s6502_game_aot_enabled) {
        uint8_t bank;

        for (bank = 0; bank < 16u; ++bank) {
            uint32_t physical_pc = (uint32_t)sys.bk_tab[bank] << 12;

            if (physical_pc >= 0x20d000u &&
                physical_pc < s6502_game_aot_physical_end)
                s6502_game_aot_bank_mask |= (uint16_t)(1u << bank);
        }
    }
}

static void s6502_game_aot_invalidate(uint32_t offset, uint32_t size)
{
    if (!s6502_game_aot_enabled || !size)
        return;
    if (offset < s6502_game_aot_storage_end && offset + size > 0x15000u)
        s6502_game_aot_enabled = 0;
    if (!s6502_game_aot_enabled)
        s6502_game_aot_bank_mask = 0;
#ifdef GAM4980_ENABLE_FIRMWARE_HLE
    if (!s6502_game_aot_enabled) {
        s6502_game_hle_counter_count = 0u;
        s6502_game_hle_bitmap_count = 0u;
        s6502_game_hle_scan_count = 0u;
        s6502_game_hle_record_scan_count = 0u;
        s6502_game_hle_table_chain_count = 0u;
        s6502_game_hle_object_flow_count = 0u;
    }
#endif
}
#endif

#ifdef GAM4980_ENABLE_AOT
static __attribute__((noinline)) int s6502_aot_validate(uint32_t block_id)
{
    const s6502_aot_block_t *block;
    uint32_t index;

    if (block_id >= S6502_AOT_BLOCK_COUNT)
        return 0;
    block = &s6502_aot_blocks[block_id];
    for (index = 0; index < block->signature_size; ++index) {
        if (mem_readx((uint16_t)(block->virtual_pc + index)) !=
            s6502_aot_signature[block->signature_offset + index]) {
            s6502_aot_validation[block_id] = 2u;
            return 0;
        }
    }
    s6502_aot_validation[block_id] = 1u;
    return 1;
}

static __attribute__((noinline)) int s6502_aot_match(uint32_t block_id)
{
    const s6502_aot_block_t *block;
    uint8_t validation;

    if (block_id >= S6502_AOT_BLOCK_COUNT)
        return 0;
    block = &s6502_aot_blocks[block_id];
    if (PA(block->virtual_pc) != block->physical_pc)
        return 0;
    if (block->requires_bank2 && sys.bk_tab[2] != 0x0002u)
        return 0;
    validation = s6502_aot_validation[block_id];
    if (validation == 1u)
        return 1;
    if (validation == 2u)
        return 0;
    return s6502_aot_validate(block_id);
}

#if defined(GAM4980_AOT_DIAGNOSTICS) || \
    defined(GAM4980_RUNTIME_PERFORMANCE_LOG) || \
    defined(GAM4980_ENABLE_FIRMWARE_HLE)
void gam4980_set_performance_debug(int enabled)
{
#ifdef GAM4980_LIGHTWEIGHT_PERFORMANCE_LOG
    (void)enabled;
#else
    s6502_performance_debug = enabled != 0;
#endif
}

int gam4980_performance_debug_enabled(void)
{
#ifdef GAM4980_LIGHTWEIGHT_PERFORMANCE_LOG
    return 0;
#else
    return s6502_performance_debug;
#endif
}
#endif

#ifdef GAM4980_AOT_DIAGNOSTICS
static void s6502_aot_hit(uint32_t block_id, uint32_t instructions)
{
    uint16_t bank2 = sys.bk_tab[2];

    ++s6502_aot_block_hits[block_id];
    s6502_aot_instruction_hits += instructions;
    if (s6502_aot_bank2[block_id] == 0xffffu)
        s6502_aot_bank2[block_id] = bank2;
    else if (s6502_aot_bank2[block_id] != bank2)
        s6502_aot_bank2_varies[block_id] = 1u;
}

u64 gam4980_aot_instruction_count(void)
{
    return s6502_aot_instruction_hits;
}

u32 gam4980_aot_block_count(void)
{
    return S6502_AOT_BLOCK_COUNT;
}

u64 gam4980_aot_block_hit_count(u32 block_id)
{
    return block_id < S6502_AOT_BLOCK_COUNT
        ? s6502_aot_block_hits[block_id] : 0;
}

u32 gam4980_aot_block_physical_pc(u32 block_id)
{
    return block_id < S6502_AOT_BLOCK_COUNT
        ? s6502_aot_blocks[block_id].physical_pc : 0u;
}

u16 gam4980_aot_block_virtual_pc(u32 block_id)
{
    return block_id < S6502_AOT_BLOCK_COUNT
        ? s6502_aot_blocks[block_id].virtual_pc : 0u;
}

u32 gam4980_aot_block_instruction_count(u32 block_id)
{
    return block_id < S6502_AOT_BLOCK_COUNT
        ? s6502_aot_blocks[block_id].instruction_count : 0u;
}

u16 gam4980_aot_block_bank2(u32 block_id)
{
    return block_id < S6502_AOT_BLOCK_COUNT
        ? s6502_aot_bank2[block_id] : 0xffffu;
}

int gam4980_aot_block_bank2_varies(u32 block_id)
{
    return block_id < S6502_AOT_BLOCK_COUNT
        ? s6502_aot_bank2_varies[block_id] != 0u : 0;
}
#ifdef GAM4980_ENABLE_GAME_LOAD_AOT
u64 gam4980_game_aot_instruction_count(void)
{
    return s6502_game_aot_instruction_hits;
}

u64 gam4980_game_aot_entry_hit_count(u32 entry_id)
{
    return entry_id < s6502_game_aot_entry_count
        ? s6502_game_aot_entry_hits[entry_id] : 0u;
}
#endif
#endif
#endif

#ifdef GAM4980_ENABLE_GAME_LOAD_AOT
u32 gam4980_game_aot_entry_count(void)
{
    return s6502_game_aot_entry_count;
}

int gam4980_game_aot_enabled(void)
{
    return s6502_game_aot_enabled;
}

u32 gam4980_game_hle_match_count(void)
{
#ifdef GAM4980_ENABLE_FIRMWARE_HLE
    return (u32)s6502_game_hle_counter_count +
        (u32)s6502_game_hle_bitmap_count +
        (u32)s6502_game_hle_scan_count +
        (u32)s6502_game_hle_record_scan_count +
        (u32)s6502_game_hle_record_reverse_count +
        (u32)s6502_game_hle_table_chain_count +
        (u32)s6502_game_hle_object_flow_count;
#else
    return 0u;
#endif
}

u32 gam4980_game_aot_entry_physical_pc(u32 entry_id)
{
    return entry_id < s6502_game_aot_entry_count
        ? s6502_game_aot_entries[entry_id].physical_pc : 0u;
}

u32 gam4980_game_aot_entry_pattern(u32 entry_id)
{
    return entry_id < s6502_game_aot_entry_count
        ? s6502_game_aot_entries[entry_id].pattern : 0xffffffffu;
}

u32 gam4980_game_aot_semantic_count(void)
{
    return s6502_game_aot_semantic_count;
}

u32 gam4980_game_aot_linked_call_count(void)
{
    return s6502_game_aot_linked_call_count;
}

u32 gam4980_game_aot_direct_link_hits(void)
{
    return s6502_game_aot_direct_link_hits;
}

u32 gam4980_game_aot_direct_link_stage_hits(u32 stage)
{
#ifdef GAM4980_AOT_DIAGNOSTICS
    return stage < 5u ? s6502_game_aot_direct_link_stage_hits[stage] : 0u;
#else
    (void)stage;
    return 0u;
#endif
}

int gam4980_game_aot_direct_link_available(void)
{
    return s6502_game_aot_direct_link_match();
}

u32 gam4980_game_aot_reachable_count(void)
{
    return s6502_game_aot_reachable_count;
}

u32 gam4980_game_aot_code_size(void)
{
    return s6502_game_aot_code_size;
}

u32 gam4980_game_aot_semantic_hits(u32 semantic_kind)
{
    return semantic_kind >= 1u &&
        semantic_kind <= C6502_TEMPLATE_SPEC_COUNT
        ? s6502_game_aot_semantic_hits[semantic_kind - 1u] : 0u;
}

u32 gam4980_game_aot_semantic_hit_total(void)
{
    uint32_t total = 0u;
    uint32_t semantic;

    for (semantic = 0u;
         semantic < C6502_TEMPLATE_SPEC_COUNT; ++semantic)
        total += s6502_game_aot_semantic_hits[semantic];
    return total;
}
#endif

#ifdef GAM4980_ENABLE_PROFILING
static void profile_instruction(uint16_t virtual_pc, uint8_t opcode)
{
    if (instruction_profile_callback) {
        instruction_profile_callback(
            instruction_profile_context, virtual_pc, PA(virtual_pc), opcode
        );
    }
}

void gam4980_set_instruction_profile(
    gam4980_instruction_profile_fn callback, void *context
)
{
    instruction_profile_callback = callback;
    instruction_profile_context = context;
}
#endif

static uint8_t flash_read(uint32_t addr)
{
    static uint8_t flash_info[0x35] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x51, 0x52, 0x59, 0x01, 0x07, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x27, 0x36, 0x00, 0x00, 0x04,
        0x00, 0x04, 0x06, 0x01, 0x00, 0x01, 0x01, 0x15,
        0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x01, 0x10,
        0x00, 0x1f, 0x00, 0x00, 0x01,
    };
    if (sys.flash_cmd == 0 || sys.flash_cmd == 1) {
        // Rotate last 32KiB to the front for save.
        addr = (addr + 0x8000) % 0x200000;
        return addr < sys.flash_size ? sys.flash[addr] : 0xff;
    } else {
        // Software ID or CFI
        return flash_info[addr];
    }
}

static void flash_erase_range(uint32_t addr, uint32_t size)
{
    if (addr >= sys.flash_size)
        return;
    if (size > sys.flash_size - addr)
        size = sys.flash_size - addr;
#ifdef GAM4980_ENABLE_GAME_LOAD_AOT
    s6502_game_aot_invalidate(addr, size);
#endif
    gam4980_memset(sys.flash + addr, 0xff, size);
}

static void flash_write(uint32_t addr, uint8_t val)
{
    switch (sys.flash_cycles) {
    case 0:
        // 1st Bus Write Cycle
        if (addr == 0x5555 && val == 0xaa)
            sys.flash_cycles += 1;
        else if (val == 0xf0)
            // Software ID Exit / CFI Exit
            sys.flash_cmd = 0;
        break;
    case 1:
    case 4:
        // 2nd Bus Write Cycle / 5th Bus Write Cycle
        if (addr == 0x2aaa && val == 0x55)
            sys.flash_cycles += 1;
        break;
    case 2:
        // 3rd Bus Write Cycle
        if (addr != 0x5555)
            return;
        switch (val) {
        case 0xa0:
            // Byte-Program
            sys.flash_cmd = 1;
            sys.flash_cycles += 1;
            break;
        case 0x80:
            sys.flash_cycles += 1;
            break;
        case 0x90:
            // Software ID Entry
            sys.flash_cmd = 2;
            sys.flash_cycles = 0;
            break;
        case 0x98:
            // CFI Query Entry
            sys.flash_cmd = 3;
            sys.flash_cycles = 0;
            break;
        case 0xf0:
            // Software ID Exit / CFI Exit
            sys.flash_cmd = 0;
            sys.flash_cycles = 0;
            break;
        }
        break;
    case 3:
        // 4th Bus Write Cycle
        if (sys.flash_cmd == 1) {
            sys.flash_cmd = 0;
            sys.flash_cycles = 0;
            // Rotate last 32KiB to the front for save.
            addr = (addr + 0x8000) % 0x200000;
            if (addr < GAM4980_SAVE_SIZE && addr < sys.flash_size &&
                sys.flash[addr] != val)
                save_dirty = 1;
            if (addr < sys.flash_size) {
#ifdef GAM4980_ENABLE_GAME_LOAD_AOT
                s6502_game_aot_invalidate(addr, 1u);
#endif
                sys.flash[addr] = val;
            }
        } else if ((addr == 0x5555) && (val == 0xaa)) {
            sys.flash_cycles += 1;
        }
        break;
    case 5:
        // 6th Bus Write Cycle
        switch (val) {
        case 0x10:
            // Chip-Erase
            if (addr == 0x5555) {
                save_dirty = 1;
                flash_erase_range(0, sys.flash_size);
            }
            break;
        case 0x30:
            // Sector-Erase
            addr = (addr + 0x8000) % 0x200000;
            addr &= 0x1ff000;
            if (addr < GAM4980_SAVE_SIZE)
                save_dirty = 1;
            flash_erase_range(addr, 0x1000);
            break;
        case 0x50:
            // Block-Erase
            addr = ((addr & 0x1f0000) + 0x8000) % 0x200000;
            if (addr < GAM4980_SAVE_SIZE)
                save_dirty = 1;
            flash_erase_range(addr, 0x8000);
            addr = (addr + 0x8000) % 0x200000;
            if (addr < GAM4980_SAVE_SIZE)
                save_dirty = 1;
            flash_erase_range(addr, 0x8000);
            break;
        }
        sys.flash_cmd = 0;
        sys.flash_cycles = 0;
        break;
    }

    // Read CFI/ID info via 'sys.mem_ir'.
    if (sys.flash_cmd == 2 || sys.flash_cmd == 3) {
        for (int i = 0; i < 0x100; i += 1) {
            if (sys.mem_r[i] >= sys.flash &&
                sys.mem_r[i] < sys.flash + sys.flash_size) {
                sys.mem_r[i] = 0;
            }
        }
    }
}

static uint8_t invalid_read(uint16_t addr)
{
    return 0x00;
}

static void invalid_write(uint16_t addr, uint8_t val)
{
}

static uint8_t ram_read(uint16_t addr)
{
    return sys.ram[addr];
}

static void ram_write(uint16_t addr, uint8_t val)
{
    if (addr >= 0x0400u && addr <= 0x1000u) {
#ifdef GAM4980_RUNTIME_PERFORMANCE_LOG
        if (s6502_performance_debug)
            ++performance_lcd_write_calls;
#endif
        if (sys.ram[addr] != val) {
            lcd_dirty = 1;
#ifdef GAM4980_RUNTIME_PERFORMANCE_LOG
            if (s6502_performance_debug)
                ++performance_lcd_changed_writes;
#endif
        }
    }
    sys.ram[addr] = val;

    // XXX: Disable ROM (0x400000-0x7fffff) channels and audio.
    if (addr == _PB)
        sys.ram[addr] = 0;

    // Never return 0 for AutoPowerOffCount to prevent poweroff.
    if (addr == 0x2028)
        sys.ram[addr] = 0xff;
}

static uint8_t direct_read(uint16_t addr)
{
    int _L = _ADDR1L + addr * 3;
    int _M = _L + 1;
    int _H = _M + 1;
    uint32_t paddr = sys.ram[_L] | sys.ram[_M] << 8 | sys.ram[_H] << 16;
    if (sys.ram[_INCR] & (1 << addr)) {
        sys.ram[_L] += 1;
        if (sys.ram[_L] == 0) {
            sys.ram[_M] += 1;
            if (sys.ram[_M] == 0) {
                sys.ram[_H] += 1;
            }
        }
    }
    if (paddr < 0x8000)
        return ram_read(paddr & 0x7fff);
    else if (paddr >= 0x200000 && paddr < 0x400000)
        return flash_read(paddr - 0x200000);
    else if (paddr >= 0x800000 && paddr < 0xa00000)
        return rom_read_byte(GAM4980_ROM_REGION_8, paddr - 0x800000);
    else if (paddr >= 0xe00000 && paddr < 0x1000000)
        return rom_read_byte(GAM4980_ROM_REGION_E, paddr - 0xe00000);
    else
        return 0x00;
}

static void direct_write(uint16_t addr, uint8_t val)
{
    int _L = _ADDR1L + addr * 3;
    int _M = _L + 1;
    int _H = _M + 1;
    uint32_t paddr = sys.ram[_L] | sys.ram[_M] << 8 | sys.ram[_H] << 16;
    if (sys.ram[_INCR] & (1 << addr)) {
        sys.ram[_L] += 1;
        if (sys.ram[_L] == 0) {
            sys.ram[_M] += 1;
            if (sys.ram[_M] == 0) {
                sys.ram[_H] += 1;
            }
        }
    }
    if (paddr < 0x8000)
        ram_write(paddr & 0x7fff, val);
    else if (paddr >= 0x200000 && paddr < 0x400000)
        flash_write(paddr - 0x200000, val);
}

static uint8_t page0_read(uint16_t addr)
{
    switch (addr) {
    case _DATA1:
    case _DATA2:
    case _DATA3:
    case _DATA4:
        return direct_read(addr);
    case _BK_SEL:
        return sys.bk_sel;
    case _BK_ADRL:
        return sys.bk_tab[sys.bk_sel] & 0xff;
    case _BK_ADRH:
        return sys.bk_tab[sys.bk_sel] >> 8;
    }
    return sys.ram[addr];
}

static void page0_write(uint16_t addr, uint8_t val)
{
    switch (addr) {
    case _DATA1:
    case _DATA2:
    case _DATA3:
    case _DATA4:
        direct_write(addr, val);
        return;
    case _ISR:
        sys.ram[_ISR] &= val;
        return;
    case _TISR:
        sys.ram[_TISR] &= val;
        return;
    case _BK_SEL:
        sys.bk_sel = val & 0x0f;
        return;
    case _BK_ADRL:
        sys.bk_tab[sys.bk_sel] &= 0xff00;
        sys.bk_tab[sys.bk_sel] |= val;
        mem_bs(sys.bk_sel);
        return;
    case _BK_ADRH:
        sys.bk_tab[sys.bk_sel] &= 0x00ff;
        sys.bk_tab[sys.bk_sel] |= (val & 0x0f) << 8;
        mem_bs(sys.bk_sel);
        return;
    }
    sys.ram[addr] = val;
}

static int mem_init(void)
{
    for (int i = 0; i < 0x100; i += 1) {
        sys.mem_r[i] = 0;
        sys.mem_ir[i] = invalid_read;
        sys.mem_iw[i] = invalid_write;
    }
    for (int i = 1; i < 16; i += 1) {
        sys.mem_r[i] = sys.ram + i * 0x100;
        sys.mem_ir[i] = ram_read;
        sys.mem_iw[i] = ram_write;
    }
    sys.mem_ir[0x00] = page0_read;
    sys.mem_iw[0x00] = page0_write;
    if (sys.rom_e) {
        sys.mem_r[0x03] = sys.rom_e + 0x1fff00;
    } else {
        if (!rom_read_range(
                GAM4980_ROM_REGION_E, 0x1fff00u, rom_boot_page,
                sizeof(rom_boot_page)
            ))
            return 0;
        sys.mem_r[0x03] = rom_boot_page;
    }
    sys.mem_iw[0x03] = invalid_write;
    s6502_page3 = sys.mem_r[0x03];
    return 1;
}

static uint8_t flash_vread(uint16_t addr)
{
    return flash_read(PA(addr) - 0x200000);
}

static void flash_vwrite(uint16_t addr, uint8_t val)
{
    return flash_write(PA(addr) - 0x200000, val);
}

static uint8_t rom_8_vread(uint16_t addr)
{
    return rom_read_byte(GAM4980_ROM_REGION_8, PA(addr) - 0x800000);
}

static uint8_t rom_e_vread(uint16_t addr)
{
    return rom_read_byte(GAM4980_ROM_REGION_E, PA(addr) - 0xe00000);
}

static uint8_t ram_vread(uint16_t addr)
{
    return ram_read(PA(addr));
}

static void ram_vwrite(uint16_t addr, uint8_t val)
{
    ram_write(PA(addr), val);
}

static void mem_bs(uint8_t sel)
{
    uint32_t paddr = PA(sel * 0x1000);
    if (sel == 0)
        return;
#ifdef GAM4980_ENABLE_GAME_LOAD_AOT
    if (s6502_game_aot_enabled && paddr >= 0x20d000u &&
        paddr < s6502_game_aot_physical_end)
        s6502_game_aot_bank_mask |= (uint16_t)(1u << sel);
    else
        s6502_game_aot_bank_mask &= (uint16_t)~(1u << sel);
#endif
    if (paddr < 0x8000) {
        for (int i = 0; i < 16; i += 1) {
            sys.mem_r[sel * 16 + i] = sys.ram + paddr + i * 0x100;
            sys.mem_ir[sel * 16 + i] = ram_vread;
            sys.mem_iw[sel * 16 + i] = ram_vwrite;
        }
    } else if (paddr >= 0x200000 && paddr < 0x400000) {
        for (int i = 0; i < 16; i += 1) {
            uint32_t faddr = (paddr - 0x200000 + 0x8000) % 0x200000;
            uint32_t offset = faddr + (uint32_t)i * 0x100u;

            sys.mem_r[sel * 16 + i] =
                offset + 0x100u <= sys.flash_size
                    ? sys.flash + offset
                    : 0;
            sys.mem_ir[sel * 16 + i] = flash_vread;
            sys.mem_iw[sel * 16 + i] = flash_vwrite;
        }
    } else if (paddr >= 0x800000 && paddr < 0xa00000) {
        uint8_t *bank = sys.rom_8
            ? sys.rom_8 + (paddr - 0x800000)
            : 0;

        if (!sys.rom_8)
            bank = rom_cached_bank(
                sel, GAM4980_ROM_REGION_8, paddr - 0x800000
            );
        for (int i = 0; i < 16; i += 1) {
            sys.mem_r[sel * 16 + i] = bank ? bank + i * 0x100 : 0;
            sys.mem_ir[sel * 16 + i] = rom_8_vread;
            sys.mem_iw[sel * 16 + i] = invalid_write;
        }
    } else if (paddr >= 0xe00000 && paddr < 0x1000000) {
        uint8_t *bank = sys.rom_e
            ? sys.rom_e + (paddr - 0xe00000)
            : 0;

        if (!sys.rom_e)
            bank = rom_cached_bank(
                sel, GAM4980_ROM_REGION_E, paddr - 0xe00000
            );
        for (int i = 0; i < 16; i += 1) {
            sys.mem_r[sel * 16 + i] = bank ? bank + i * 0x100 : 0;
            sys.mem_ir[sel * 16 + i] = rom_e_vread;
            sys.mem_iw[sel * 16 + i] = invalid_write;
        }
    } else {
        for (int i = 0; i < 16; i += 1) {
            sys.mem_r[sel * 16 + i] = 0;
            sys.mem_ir[sel * 16 + i] = invalid_read;
            sys.mem_iw[sel * 16 + i] = invalid_write;
        }
    }
}

static uint8_t mem_readx(uint16_t addr)
{
    uint8_t page = addr >> 8;

    if (sys.mem_r[page])
        return sys.mem_r[page][addr & 0xff];
    return sys.mem_ir[page](addr);
}

static uint8_t mem_read(uint16_t addr)
{
    uint8_t page = addr >> 8;

    if (sys.mem_r[page])
        return sys.mem_r[page][addr & 0xff];
    else
        return sys.mem_ir[page](addr);
}

static uint16_t mem_read16(uint16_t addr)
{
    return mem_read(addr) | (mem_read(addr + 1) << 8);
}

static uint16_t mem_readx16(uint16_t addr)
{
    return mem_readx(addr) | (mem_readx(addr + 1) << 8);
}

static uint16_t mem_read16_wrapped(uint16_t addr)
{
    return mem_read(addr) | (mem_read((addr + 1) & 0xff) << 8);
}

static void mem_write(uint16_t addr, uint8_t val)
{
    return sys.mem_iw[addr >> 8](addr, val);
}

enum _key {
    KEY_ON_OFF     = 0x00,      /* 开关 */
    KEY_HOME_MENU  = 0x01,      /* 目录 */
    KEY_EC_SJ      = 0x02,      /* 双解 */
    KEY_EC_SW      = 0x03,      /* 十万 (4988: 现代) */
    KEY_CE         = 0x04,      /* 汉英 */
    KEY_DLG        = 0x05,      /* 对话 */
    KEY_DOWNLOAD   = 0x06,      /* 下载 */
    KEY_SPK        = 0x07,      /* 发音 */
    KEY_1          = 0x08,
    KEY_2          = 0x09,
    KEY_3          = 0x0a,
    KEY_4          = 0x0b,
    KEY_5          = 0x0c,
    KEY_6          = 0x0d,
    KEY_7          = 0x0e,
    KEY_8          = 0x0f,
    KEY_9          = 0x30,
    KEY_0          = 0x31,
    KEY_Q          = 0x10,
    KEY_W          = 0x11,
    KEY_E          = 0x12,
    KEY_R          = 0x13,
    KEY_T          = 0x14,
    KEY_Y          = 0x15,
    KEY_U          = 0x16,
    KEY_I          = 0x17,
    KEY_O          = 0x32,
    KEY_P          = 0x33,
    KEY_SPACE      = 0x36,      /* 空格 */
    KEY_A          = 0x18,
    KEY_S          = 0x19,
    KEY_D          = 0x1a,
    KEY_F          = 0x1b,
    KEY_G          = 0x1c,
    KEY_H          = 0x1d,
    KEY_J          = 0x1e,
    KEY_K          = 0x1f,
    KEY_L          = 0x34,
    KEY_INPUT      = 0x20,      /* 输入法 */
    KEY_CAPS       = KEY_INPUT,
    KEY_Z          = 0x21,
    KEY_X          = 0x22,
    KEY_C          = 0x23,
    KEY_V          = 0x24,
    KEY_B          = 0x25,
    KEY_N          = 0x26,
    KEY_M          = 0x27,
    KEY_ZY         = 0x28,      /* 中英 */
    KEY_SHIFT      = KEY_ZY,
    KEY_HELP       = 0x29,      /* 帮助 */
    KEY_SEARCH     = 0x2a,      /* 查找 */
    KEY_INSERT     = 0x2b,      /* 插入 */
    KEY_MODIFY     = 0x2c,      /* 修改 */
    KEY_DEL        = 0x2d,      /* 删除 */
    KEY_SHIFT_4988 = 0x2d,
    KEY_EXIT       = 0x2e,      /* 跳出 */
    KEY_ENTER      = 0x2f,      /* 输入 */
    KEY_UP         = 0x35,
    KEY_DOWN       = 0x38,
    KEY_LEFT       = 0x37,
    KEY_RIGHT      = 0x39,
    KEY_PGUP       = 0x3a,
    KEY_PGDN       = 0x3b,
};


static void sys_keydown(uint8_t key)
{
    sys.ram[_SYSCON] &= 0xf7;
    sys.ram[_KEYCODE] = key | 0x80;
    sys.ram[_ISR] |= 0x80;
    if (sys.ram[_IER] & 0x80) {
        sys.ram[_KeyBuffTop] = 0x0;
        sys.ram[_KeyBuffBottom] = 0xf;
        sys.ram[_KeyBuffer + 0x0f] = key & 0x3f;
        sys.ram[_KEYCODE] = 0x00;
    }
}

int gam4980_init(const gam4980_buffers_t *buffers)
{
    uint32_t blocks = 0;

    if (!buffers || !buffers->ram || !buffers->flash ||
        ((!buffers->rom_8 || !buffers->rom_e) && !buffers->rom_read))
        return -1;

    gam4980_memset(&sys, 0, sizeof(sys));
#ifdef GAM4980_RUNTIME_PERFORMANCE_LOG
    gam4980_memset(performance_samples, 0, sizeof(performance_samples));
    performance_guest_cycles = 0;
    performance_scheduled_cycles = 0;
    performance_halted_cycles = 0;
    performance_timer_ticks = 0;
    performance_exec_calls = 0;
    performance_step_frames = 0;
    performance_lcd_write_calls = 0;
    performance_lcd_changed_writes = 0;
    performance_render_calls = 0;
    performance_dirty_render_calls = 0;
    performance_changed_render_calls = 0;
    performance_sample_count = 0;
    performance_sample_dropped = 0;
#endif
#ifdef GAM4980_ENABLE_AOT
    gam4980_memset(
        s6502_aot_validation, 0, sizeof(s6502_aot_validation)
    );
#ifdef GAM4980_AOT_DIAGNOSTICS
    gam4980_memset(s6502_aot_block_hits, 0, sizeof(s6502_aot_block_hits));
    s6502_aot_instruction_hits = 0;
    gam4980_memset(s6502_aot_bank2, 0xff, sizeof(s6502_aot_bank2));
    gam4980_memset(
        s6502_aot_bank2_varies, 0, sizeof(s6502_aot_bank2_varies)
    );
#endif
#ifdef GAM4980_ENABLE_FIRMWARE_HLE
    s6502_firmware_hle_validation = 0u;
    s6502_firmware_hle_glyph_validation = 0u;
    s6502_firmware_hle_bitmap_validation = 0u;
    s6502_firmware_hle_bitmap_region_validation = 0u;
    s6502_firmware_hle_shift_region_validation = 0u;
    s6502_firmware_hle_picture_head_validation = 0u;
    s6502_firmware_hle_graphics_address_validation = 0u;
    s6502_firmware_hle_hline_validation = 0u;
    s6502_firmware_hle_part_picture_validation = 0u;
    s6502_firmware_hle_pixel_tail_validation = 0u;
#ifdef GAM4980_ENABLE_AGGRESSIVE_REGION_HLE
    s6502_hle_shift_cache.valid = 0u;
#endif
    s6502_firmware_hle_fill_validation = 0u;
    s6502_firmware_hle_wide_glyph_validation = 0u;
    s6502_firmware_hle_multiply_validation = 0u;
    s6502_firmware_hle_compare_validation = 0u;
    s6502_firmware_hle_bank_switch_validation = 0u;
    s6502_firmware_hle_c_runtime_validation = 0u;
    s6502_firmware_hle_compare_long_validation = 0u;
    s6502_firmware_hle_indirect_call_validation = 0u;
    s6502_game_hle_object_flow_firmware_validation = 0u;
    s6502_firmware_hle_hits = 0u;
    s6502_firmware_hle_guest_cycles = 0u;
    gam4980_memset(
        s6502_resource_span_cache, 0, sizeof(s6502_resource_span_cache)
    );
    s6502_resource_span_cache_hits = 0u;
    s6502_resource_span_cache_misses = 0u;
    gam4980_memset(
        s6502_firmware_hle_attempts, 0,
        sizeof(s6502_firmware_hle_attempts)
    );
    gam4980_memset(
        s6502_firmware_hle_path_hits, 0,
        sizeof(s6502_firmware_hle_path_hits)
    );
    gam4980_memset(
        s6502_firmware_hle_condition_rejects, 0,
        sizeof(s6502_firmware_hle_condition_rejects)
    );
    gam4980_memset(
        s6502_firmware_hle_budget_rejects, 0,
        sizeof(s6502_firmware_hle_budget_rejects)
    );
    gam4980_memset(
        s6502_firmware_hle_batch_groups, 0,
        sizeof(s6502_firmware_hle_batch_groups)
    );
    gam4980_memset(
        s6502_firmware_hle_batch_iterations, 0,
        sizeof(s6502_firmware_hle_batch_iterations)
    );
    gam4980_memset(
        s6502_firmware_hle_batch_max, 0,
        sizeof(s6502_firmware_hle_batch_max)
    );
    gam4980_memset(
        s6502_firmware_hle_direct_groups, 0,
        sizeof(s6502_firmware_hle_direct_groups)
    );
    gam4980_memset(
        s6502_firmware_hle_direct_iterations, 0,
        sizeof(s6502_firmware_hle_direct_iterations)
    );
    gam4980_memset(
        s6502_firmware_hle_path_guest_cycles, 0,
        sizeof(s6502_firmware_hle_path_guest_cycles)
    );
#endif
#ifdef GAM4980_ENABLE_GAME_LOAD_AOT
    s6502_game_aot_entry_count = 0;
    s6502_game_aot_bank_mask = 0;
    s6502_game_aot_enabled = 0;
    s6502_game_aot_physical_end = 0;
    s6502_game_aot_storage_end = 0;
#ifdef GAM4980_ENABLE_FIRMWARE_HLE
    s6502_game_hle_counter_count = 0u;
    s6502_game_hle_bitmap_count = 0u;
    s6502_game_hle_scan_count = 0u;
    s6502_game_hle_record_scan_count = 0u;
    s6502_game_hle_table_chain_count = 0u;
    s6502_game_hle_object_flow_count = 0u;
#endif
#ifdef GAM4980_AOT_DIAGNOSTICS
    gam4980_memset(
        s6502_game_aot_entry_hits, 0, sizeof(s6502_game_aot_entry_hits)
    );
#endif
#endif
#endif
    sys.ram = buffers->ram;
#ifdef GAM4980_ENABLE_FIRMWARE_HLE
    s6502_firmware_hle_banks = sys.bk_tab;
    s6502_firmware_hle_mem_pages = sys.mem_r;
#endif
#ifdef GAM4980_ENABLE_GAME_LOAD_AOT
    s6502_game_aot_banks = sys.bk_tab;
#endif
    s6502_stack_ram = buffers->ram;
    sys.flash = buffers->flash;
    sys.flash_size = buffers->flash_size;
    if (!sys.flash_size || sys.flash_size > GAM4980_FLASH_SIZE)
        sys.flash_size = GAM4980_FLASH_SIZE;
    sys.rom_8 = buffers->rom_8;
    sys.rom_e = buffers->rom_e;
    sys.rom_read = buffers->rom_read;
    sys.rom_context = buffers->rom_context;
    fb = buffers->framebuffer;
    gam4980_memset(sys.ram, 0x00, GAM4980_RAM_SIZE);
    gam4980_memset(sys.flash, 0xff, sys.flash_size);
    if (fb)
        gam4980_memset(fb, 0x00, LCD_STRIDE * LCD_HEIGHT * sizeof(*fb));
    gam4980_memset(lcd_frame, 0x00, sizeof(lcd_frame));
    shutdown_requested = 0;
    shutdown_pc = 0;
    step_cycles = 0;
    step_ticked = 0;
    step_cycle_fraction = 0;
    rtc_frames = 0;
    gam4980_memset(timer_ticks, 0, sizeof(timer_ticks));
    lcd_frame_valid = 0;
    lcd_dirty = 1;
    save_dirty = 0;
    gam4980_set_lcd_theme(GAM4980_LCD_THEME_OFF);
    sys.flash_cmd = 0;
    sys.flash_cycles = 0;
    sys.ram[_INCR] = 0x0f;

    rom_direct_valid = 0;
    rom_cache_clock = 0;
    gam4980_memset(rom_bank_valid, 0, sizeof(rom_bank_valid));
    gam4980_memset(rom_slot_line, 0xff, sizeof(rom_slot_line));
    if (!mem_init())
        return -4;
    sys.cpu.pc = 0x350;
    sys.cpu.ac = 0;
    sys.cpu.ix = 0;
    sys.cpu.iy = 0;
    sys.cpu.sp = 0xff;
    sys.cpu.status = 0x04;

    while (sys.ram[_MTCT] != 0xfe && blocks < 8192u) {
        (void)s6502_exec(&sys.cpu, 0x1000);
        ++blocks;
    }
    if (sys.ram[_MTCT] != 0xfe)
        return -2;

    sys.bk_sys_d = sys.bk_tab[0xd];
    if (sys.bk_sys_d != 0x0ea8 && sys.bk_sys_d != 0x0e88)
        return -3;
    return 1;
}

u8 *gam4980_game_storage(void)
{
    return sys.flash ? sys.flash + 0x15000 : 0;
}

void gam4980_set_load_progress_callback(
    gam4980_load_progress_fn callback, void *context
)
{
    core_load_progress_callback = callback;
    core_load_progress_context = context;
}

#ifdef GAM4980_ENABLE_GAME_LOAD_AOT
void gam4980_set_game_load_aot_enabled(int enabled)
{
    s6502_game_aot_requested = enabled != 0;
    if (!s6502_game_aot_requested) {
        s6502_game_aot_enabled = 0;
        s6502_game_aot_bank_mask = 0;
    }
}

void gam4980_set_game_aot_metrics_enabled(int enabled)
{
    s6502_game_aot_metrics_enabled = enabled != 0;
}

void gam4980_set_game_aot_semantic_mask(u32 mask)
{
    s6502_game_aot_semantic_mask = mask;
}

void gam4980_set_game_aot_entry_limit(u32 limit)
{
    s6502_game_aot_entry_limit = limit > S6502_GAME_AOT_MAX_ENTRIES
        ? S6502_GAME_AOT_MAX_ENTRIES : (uint16_t)limit;
}

void gam4980_set_game_aot_direct_links(int enabled)
{
    s6502_game_aot_direct_links_enabled = enabled != 0;
}
#endif

int gam4980_load_game_header(const u8 *gam, u32 size)
{
    if (!gam || size < GAM4980_GAME_HEADER_SIZE ||
        size > GAM4980_GAME_MAX_SIZE || !sys.flash ||
        sys.flash_size < 0x15000u || size > sys.flash_size - 0x15000u)
        return -1;

    uint16_t start = gam[0x40] | (gam[0x41] << 8);
    uint32_t data = gam[0x42] | gam[0x43] << 8 | gam[0x44] << 16 | gam[0x45] << 24;
    uint8_t sys_hdr[16] = {
        0xc0, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x10, 0x00, 0x2f,
    };
    uint8_t gam_hdr[16] = {
        0xd0, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00,
        size & 0xff, (size >> 8) & 0xff, (size >> 16) & 0xff,
        0x3d,
    };

    // Setup file headers.
    uint8_t *flash = sys.flash + 0x8000;
    gam4980_memcpy(gam_hdr + 2, gam + 6, 0x0a);
    gam4980_memcpy(flash, sys_hdr, 16);
    gam4980_memcpy(flash+16, gam_hdr, 16);
    /* The native front end streams game bytes into flash+0xd000. */
    gam4980_memset(flash+0x1000, 0x01, 0x100);
    for (int i = 0; i < 0x0c; i += 1) {
        flash[0x1000 + i] = 0x04;
    }

    if (sys.bk_sys_d == 0x0ea8) { /* A4980 */
        gam4980_memset(flash+0x7000, 0x01, 0x100);
        // Last 32 KiB for save file.
        flash[0x70f8] = 0x02;
        flash[0x70f9] = 0x02;
        flash[0x70fa] = 0x02;
        flash[0x70fb] = 0x02;
        flash[0x70fc] = 0x02;
        flash[0x70fd] = 0x02;
        flash[0x70fe] = 0x03;
        flash[0x70ff] = 0x02;
    } else if (sys.bk_sys_d == 0x0e88) { /* A4988 */
        gam4980_memset(flash+0x8000, 0x01, 0x100);
        // Last 32 KiB for save file.
        flash[0x80f8] = 0x02;
        flash[0x80f9] = 0x02;
        flash[0x80fa] = 0x02;
        flash[0x80fb] = 0x02;
        flash[0x80fc] = 0x02;
        flash[0x80fd] = 0x02;
        flash[0x80fe] = 0x03;
        flash[0x80ff] = 0x02;
    } else {
        return -2;
    }
    // Setup banks for the game.
    sys.bk_tab[0x5] = 0x20d;
    sys.bk_tab[0x6] = sys.bk_tab[0x05] + 1;
    sys.bk_tab[0x7] = sys.bk_tab[0x05] + 2;
    sys.bk_tab[0x8] = sys.bk_tab[0x05] + 3;
    sys.bk_tab[0x9] = 0x20d + (data >> 12);
    sys.bk_tab[0xa] = sys.bk_tab[0x09] + 1;
    sys.bk_tab[0xb] = sys.bk_tab[0x09] + 2;
    sys.bk_tab[0xc] = sys.bk_tab[0x09] + 3;
    for (int i = 0x05; i <= 0x0c; i += 1)
        mem_bs(i);
#ifdef GAM4980_ENABLE_GAME_LOAD_AOT
    s6502_game_aot_prepare(sys.flash + 0x15000u, size);
#endif
    mem_write(0x2029, 0x0d);
    mem_write(0x202a, 0x02);
    // Push game return address, 0x0260=BRK.
    s6502_push(0x02);
    s6502_push(0x60);
    // Start the game.
    sys.cpu.pc = start;
    return 1;
}

static void sys_timer(uint32_t n)
{
    for (int i = 0; i < 4; i += 1) {
        if (sys.ram[_STCON] & (1 << i)) {
            timer_ticks[i] += n;
            if (timer_ticks[i] >= 0x100) {
                timer_ticks[i] = sys.ram[_ST1LD + i];
                if (sys.ram[_TIER] & (1 << i)) {
                    sys.ram[_TISR] |= (1 << i);
                    sys.ram[_SYSCON] &= 0xf7;
                }
            }
        }
    }

    if (sys.ram[_STCTCON] & 0x10) {
        timer_ticks[4] += n;
        if (timer_ticks[4] >= 0x1000) {
            timer_ticks[4] = sys.ram[_CTLD];
            if (sys.ram[_IER] & 0x02) {
                sys.ram[_ISR] |= 0x02;
                sys.ram[_SYSCON] &= 0xf7;
            }
        }
    }

    if (sys.ram[_TIER] & 0x20u) {
        uint32_t melody = sys.ram[_MTCT] + n;

        sys.ram[_MTCT] = (uint8_t)melody;
        if (melody >= 0x100u) {
            sys.ram[_TISR] |= 0x20u;
            sys.ram[_SYSCON] &= 0xf7u;
        }
    }
 }

static uint32_t sys_ticks_until_timer_event(uint32_t maximum)
{
    uint32_t result = maximum;

    for (int i = 0; i < 4; ++i) {
        if (sys.ram[_STCON] & (1u << i)) {
            uint32_t remaining = 0x100u - timer_ticks[i];
            if (remaining < result)
                result = remaining;
        }
    }
    if (sys.ram[_STCTCON] & 0x10u) {
        uint32_t remaining = 0x1000u - timer_ticks[4];
        if (remaining < result)
            result = remaining;
    }
    if ((sys.ram[_TIER] & 0x20u) && !(sys.ram[_TISR] & 0x20u)) {
        uint32_t remaining = 0x100u - sys.ram[_MTCT];
        if (remaining < result)
            result = remaining;
    }
    return result ? result : 1u;
}

static uint32_t sys_cycles_until_timer_event(
    uint32_t maximum_cycles, uint32_t timer_cycle_size
)
{
    uint32_t phase;
    uint32_t maximum_ticks;
    uint32_t event_ticks;
    uint32_t event_cycles;

    if (!maximum_cycles || !timer_cycle_size)
        return maximum_cycles;
    phase = step_ticked % timer_cycle_size;
    maximum_ticks = (phase + maximum_cycles) / timer_cycle_size;
    if (!maximum_ticks)
        return maximum_cycles;
    event_ticks = sys_ticks_until_timer_event(maximum_ticks);
    if (event_ticks >= maximum_ticks)
        return maximum_cycles;
    event_cycles = event_ticks * timer_cycle_size - phase;
    return event_cycles ? event_cycles : 1u;
}

static void sys_rtc()
{
    if ((sys.ram[_STCTCON] & 0x40) == 0x00)
        return;

    if (sys.ram[_RTCSEC]++ == 59) {
        sys.ram[_RTCSEC] = 0;
        if (sys.ram[_RTCMIN]++ == 59) {
            sys.ram[_RTCMIN] = 0;
            if (sys.ram[_RTCHR]++ == 23) {
                sys.ram[_RTCHR] = 0;
                if (sys.ram[_RTCDAYL]++ == 0xff) {
                    if (sys.ram[_RTCDAYH]++ == 1) {
                        sys.ram[_RTCDAYH] = 0;
                    }
                }
            }
        }
    }
    if ((sys.ram[_STCTCON] & 0x20) == 0x00)
        return;
    if ((sys.ram[_RTCMIN] == sys.ram[_ALMMIN]) &&
        (sys.ram[_RTCHR] == sys.ram[_ALMHR]) &&
        (sys.ram[_RTCDAYL] == sys.ram[_ALMDAYL]) &&
        (sys.ram[_RTCDAYH] == sys.ram[_ALMDAYH])) {
        sys.ram[_ISR] |= 0x01;
    }
}


static void sys_isr()
{
    uint8_t idx = 0;
    if (sys.cpu.status & 0x04)
        return;
    if ((sys.ram[_ISR] & 0x80) && (sys.ram[_IER] & 0x80)) {
        idx = 0x02; // PI
        sys.ram[_ISR] &= 0x7f;
        // Handled by 'sys_keydown'.
        return;
    } else if ((sys.ram[_ISR] & 0x01) && (sys.ram[_IER] & 0x01)) {
        idx = 0x13; // ALM
    } else if ((sys.ram[_ISR] & 0x02) && (sys.ram[_IER] & 0x02)) {
        idx = 0x12; // CT
    } else if ((sys.ram[_TISR] & 0x20) && (sys.ram[_TIER] & 0x20)) {
        idx = 0x11; // MT
    } else if ((sys.ram[_TISR] & 0x80) && (sys.ram[_TIER] & 0x80)) {
        idx = 0x10; // GTH
    } else if ((sys.ram[_TISR] & 0x40) && (sys.ram[_TIER] & 0x40)) {
        idx = 0x0f; // GTL
    }  else if ((sys.ram[_TISR] & 0x01) && (sys.ram[_TIER] & 0x01)) {
        idx = 0x03; // ST1
        sys.ram[_TISR] &= 0xfe;
        sys.ram[0x2018] += 1;
        if (sys.ram[0x2018] >= sys.ram[0x2019]) {
            sys.ram[0x201e] |= 0x01;
            sys.ram[0x2018] = 0;
        }
        return;
    } else if ((sys.ram[_TISR] & 0x02) && (sys.ram[_TIER] & 0x02)) {
        idx = 0x04; // ST2
    } else if ((sys.ram[_TISR] & 0x04) && (sys.ram[_TIER] & 0x04)) {
        idx = 0x05; // ST3
    } else if ((sys.ram[_TISR] & 0x08) && (sys.ram[_TIER] & 0x08)) {
        idx = 0x06; // ST4
    } else {
        return;
    }

    s6502_push(sys.cpu.pc >> 8);
    s6502_push(sys.cpu.pc & 0xff);
    s6502_push(sys.cpu.status);
    sys.cpu.status |= 0x04;
    sys.cpu.pc = 0x0300 + idx * 4;
}

static void sys_step()
{
    const uint32_t tstep = 400;
    uint32_t frame_cycles = 66666u;

    /* Keep the upstream 4 MHz / 60 Hz cadence without cumulative rounding. */
    step_cycle_fraction += 40u;
    if (step_cycle_fraction >= 60u) {
        step_cycle_fraction -= 60u;
        ++frame_cycles;
    }
    step_cycles += frame_cycles;
#ifdef GAM4980_RUNTIME_PERFORMANCE_LOG
    if (s6502_performance_debug)
        performance_scheduled_cycles += frame_cycles;
#endif
    while (step_ticked + tstep < step_cycles) {
        if (sys_halt_p()) {
            uint32_t ticks = (step_cycles - step_ticked - 1u) / tstep;
            ticks = sys_ticks_until_timer_event(ticks);
            step_ticked += ticks * tstep;
            sys_timer(ticks);
#ifdef GAM4980_RUNTIME_PERFORMANCE_LOG
            if (s6502_performance_debug) {
                performance_halted_cycles += (uint64_t)ticks * tstep;
                performance_timer_ticks += ticks;
            }
#endif
        } else {
            uint32_t p = step_ticked / tstep;
            uint32_t exec_slice = sys_cycles_until_timer_event(
                step_cycles - step_ticked - tstep, tstep
            );
            uint32_t executed;

            sys_isr();
            executed = s6502_exec(&sys.cpu, exec_slice);
            step_ticked += executed;
#ifdef GAM4980_RUNTIME_PERFORMANCE_LOG
            if (s6502_performance_debug) {
                ++performance_exec_calls;
                performance_guest_cycles += executed;
                if ((performance_exec_calls &
                     (GAM4980_PERFORMANCE_PC_SAMPLE_STRIDE - 1u)) == 0u)
                    performance_sample_pc();
            }
#endif
            uint32_t q = step_ticked / tstep;
            sys_timer(q - p);
#ifdef GAM4980_RUNTIME_PERFORMANCE_LOG
            if (s6502_performance_debug)
                performance_timer_ticks += q - p;
#endif
        }
    }
    step_cycles -= step_ticked;
    step_ticked %= tstep;
}


static inline void unpack8(int y, int x, uint8_t p8)
{
    uint32_t *destination = (uint32_t *)(fb + y * LCD_STRIDE + x * 8);
    const uint32_t *high = lcd_nibble_lut[p8 >> 4];
    const uint32_t *low = lcd_nibble_lut[p8 & 0x0fu];

    destination[0] = high[0];
    destination[1] = high[1];
    destination[2] = low[0];
    destination[3] = low[1];
}

static inline int capture8(int y, int x, uint8_t p8)
{
    uint8_t *destination = lcd_frame + y * LCD_PACKED_STRIDE + x;
    int changed = !lcd_frame_valid || *destination != p8;

    *destination = p8;
    if (changed)
        lcd_changed_rows[(uint32_t)y >> 5] |=
            1u << ((uint32_t)y & 31u);
    return changed;
}


void gam4980_step_frame(void)
{
#ifdef GAM4980_RUNTIME_PERFORMANCE_LOG
    if (s6502_performance_debug)
        ++performance_step_frames;
#endif
    sys_step();
    if (++rtc_frames >= 60u) {
        rtc_frames = 0;
        sys_rtc();
    }
}

int gam4980_render_frame(void)
{
    int changed = 0;

#ifdef GAM4980_RUNTIME_PERFORMANCE_LOG
    if (s6502_performance_debug)
        ++performance_render_calls;
#endif
    if (!lcd_dirty)
        return 0;
#ifdef GAM4980_RUNTIME_PERFORMANCE_LOG
    if (s6502_performance_debug)
        ++performance_dirty_render_calls;
#endif
    gam4980_memset(lcd_changed_rows, 0, sizeof(lcd_changed_rows));

    // Draw the screen.
    uint8_t *v = sys.ram + 0x400;
    sys.ram[0x400] = sys.ram[0x1000];

    for (int j = 65; j >= -30; j -= 1) {
        for (int i = 1; i < 20; i += 1) {
            changed |= capture8(j >= 0 ? j : (j * -1 + 65), i, *v++);
        }
        v += 13;
    }
    v = sys.ram + 0x413;
    for (int j = 64; j >= -30; j -= 1) {
        changed |= capture8(j >= 0 ? j : (j * -1 + 65), 0, *v++);
        v += 31;
    }
    changed |= capture8(65, 0, sys.ram[0x0ff3]);
    lcd_frame_valid = 1;
    lcd_dirty = 0;
#ifdef GAM4980_RUNTIME_PERFORMANCE_LOG
    if (s6502_performance_debug && changed)
        ++performance_changed_render_calls;
#endif
    return changed;
}

void gam4980_run_frame(void)
{
    gam4980_step_frame();
    (void)gam4980_render_frame();
    (void)gam4980_expand_frame(lcd_frame);
}

int gam4980_cpu_halted(void)
{
    return sys_halt_p() ? 1 : 0;

}


void gam4980_key_down(u8 key)
{
    sys_keydown(key);
}

const u8 *gam4980_packed_frame(void)
{
    return lcd_frame;
}

u32 gam4980_changed_row_mask(u32 word)
{
    return word < 3u ? lcd_changed_rows[word] : 0u;
}

const u16 *gam4980_expand_frame(const u8 *packed_frame)
{
    int y;

    if (!packed_frame || !fb)
        return 0;
    for (y = 0; y < LCD_HEIGHT; ++y) {
        int x;

        for (x = 0; x < LCD_PACKED_STRIDE; ++x)
            unpack8(y, x, packed_frame[y * LCD_PACKED_STRIDE + x]);
    }
    return fb;
}

const u16 *gam4980_framebuffer(void)
{
    if (lcd_frame_valid)
        (void)gam4980_expand_frame(lcd_frame);
    return fb;
}

u8 *gam4980_save_data(void)
{
    return sys.flash;
}

int gam4980_save_dirty(void)
{
    return save_dirty;
}

void gam4980_save_mark_clean(void)
{
    save_dirty = 0;
}

int gam4980_shutdown_requested(void)
{
    return shutdown_requested;
}


u16 gam4980_shutdown_pc(void)
{
    return shutdown_pc;
}

#ifdef GAM4980_STATE_DIAGNOSTICS
static u64 state_hash_bytes(u64 hash, const u8 *data, u32 size)
{
    while (size-- != 0u) {
        hash ^= *data++;
        hash *= 1099511628211ull;
    }
    return hash;
}

u64 gam4980_state_hash(void)
{
    u64 hash = 1469598103934665603ull;

    hash = state_hash_bytes(hash, (const u8 *)&sys.cpu.pc, sizeof(sys.cpu.pc));
    hash = state_hash_bytes(hash, &sys.cpu.ac, sizeof(sys.cpu.ac));
    hash = state_hash_bytes(hash, &sys.cpu.ix, sizeof(sys.cpu.ix));
    hash = state_hash_bytes(hash, &sys.cpu.iy, sizeof(sys.cpu.iy));
    hash = state_hash_bytes(hash, &sys.cpu.sp, sizeof(sys.cpu.sp));
    hash = state_hash_bytes(hash, &sys.cpu.status, sizeof(sys.cpu.status));
    hash = state_hash_bytes(hash, sys.ram, GAM4980_RAM_SIZE);
    hash = state_hash_bytes(hash, sys.flash, sys.flash_size);
    hash = state_hash_bytes(
        hash, (const u8 *)sys.bk_tab, sizeof(sys.bk_tab)
    );
    hash = state_hash_bytes(hash, &sys.bk_sel, sizeof(sys.bk_sel));
    hash = state_hash_bytes(hash, (const u8 *)&sys.bk_sys_d, sizeof(sys.bk_sys_d));
    hash = state_hash_bytes(hash, &sys.flash_cmd, sizeof(sys.flash_cmd));
    hash = state_hash_bytes(hash, &sys.flash_cycles, sizeof(sys.flash_cycles));
    hash = state_hash_bytes(hash, (const u8 *)&step_cycles, sizeof(step_cycles));
    hash = state_hash_bytes(hash, (const u8 *)&step_ticked, sizeof(step_ticked));
    hash = state_hash_bytes(
        hash, (const u8 *)&step_cycle_fraction, sizeof(step_cycle_fraction)
    );
    hash = state_hash_bytes(hash, (const u8 *)&rtc_frames, sizeof(rtc_frames));
    hash = state_hash_bytes(hash, (const u8 *)timer_ticks, sizeof(timer_ticks));
    hash = state_hash_bytes(hash, lcd_frame, sizeof(lcd_frame));
    hash = state_hash_bytes(
        hash, (const u8 *)&shutdown_requested, sizeof(shutdown_requested)
    );
    hash = state_hash_bytes(hash, (const u8 *)&shutdown_pc, sizeof(shutdown_pc));
    return hash;
}

u64 gam4980_state_cpu_hash(void)
{
    u64 hash = 1469598103934665603ull;

    hash = state_hash_bytes(hash, (const u8 *)&sys.cpu.pc, sizeof(sys.cpu.pc));
    hash = state_hash_bytes(hash, &sys.cpu.ac, sizeof(sys.cpu.ac));
    hash = state_hash_bytes(hash, &sys.cpu.ix, sizeof(sys.cpu.ix));
    hash = state_hash_bytes(hash, &sys.cpu.iy, sizeof(sys.cpu.iy));
    hash = state_hash_bytes(hash, &sys.cpu.sp, sizeof(sys.cpu.sp));
    hash = state_hash_bytes(hash, &sys.cpu.status, sizeof(sys.cpu.status));
    return hash;
}

u64 gam4980_state_ram_hash(void)
{
    return state_hash_bytes(
        1469598103934665603ull, sys.ram, GAM4980_RAM_SIZE
    );
}

u64 gam4980_state_timing_hash(void)
{
    u64 hash = 1469598103934665603ull;

    hash = state_hash_bytes(hash, (const u8 *)&step_cycles, sizeof(step_cycles));
    hash = state_hash_bytes(hash, (const u8 *)&step_ticked, sizeof(step_ticked));
    hash = state_hash_bytes(
        hash, (const u8 *)&step_cycle_fraction, sizeof(step_cycle_fraction)
    );
    hash = state_hash_bytes(hash, (const u8 *)&rtc_frames, sizeof(rtc_frames));
    hash = state_hash_bytes(hash, (const u8 *)timer_ticks, sizeof(timer_ticks));
    return hash;
}

#endif

void gam4980_deinit(void)
{
#ifdef GAM4980_ENABLE_PROFILING
    instruction_profile_callback = 0;
    instruction_profile_context = 0;
#endif
    gam4980_memset(&sys, 0, sizeof(sys));
    s6502_stack_ram = 0;
    s6502_page3 = 0;
    fb = 0;
    shutdown_requested = 0;
    shutdown_pc = 0;
    step_cycles = 0;
    step_ticked = 0;
    step_cycle_fraction = 0;
    rtc_frames = 0;
    gam4980_memset(timer_ticks, 0, sizeof(timer_ticks));
    gam4980_memset(lcd_frame, 0, sizeof(lcd_frame));
    gam4980_memset(lcd_changed_rows, 0, sizeof(lcd_changed_rows));
    lcd_frame_valid = 0;
    lcd_dirty = 0;
    save_dirty = 0;
}
