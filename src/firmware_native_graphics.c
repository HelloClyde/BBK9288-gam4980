/* Pageable, position-independent copies of verified E.BIN graphics HLE.
 * Public API coverage is deliberately not inferred from internal entries.
 * The loader validates each exact physical PC and all ROM contract guards.
 */
#ifndef FIRMWARE_NATIVE_HOST_TEST
#include "s6502_iram_exec_abi.h"
#include "firmware_native_graphics_abi.h"
#define FG_POINTER unsigned long
#else
#define FG_POINTER uintptr_t
#endif
#include "firmware_native_metrics.h"
#include "firmware_native_graphics_io.h"
#define FW_MEMORY_POINTER FG_POINTER
#define FW_GRAPHICS_SECTION ".text.firmware_native_graphics"
#include "firmware_native_graphics_memory.h"

#define FG_INLINE static inline __attribute__((always_inline))
typedef struct fg_env {
    s6502_iram_asm_context_t *context;
    firmware_native_graphics_services_t *services;
    uint8_t *ram;
    uint8_t *page3;
    uint8_t **pages;
} fg_env_t;
typedef struct fg_direct_result {
    uint32_t cycles, hits;
    uint16_t pc, ea;
    uint8_t ac, ix, iy, dt, status;
} fg_direct_result_t;
typedef struct fg_region_result {
    uint32_t cycles, rows;
    uint16_t pc;
    uint8_t ac, ix, iy, sp, status;
} fg_region_result_t;

FG_INLINE uint8_t fg_read(fg_env_t *fg, uint16_t address)
{
    uint8_t *page = fg->pages[address >> 8];
    if (page) return page[address & 255u];
    return ((uint8_t (*)(uint16_t))(FG_POINTER)fg->context->read8)(address);
}

FG_INLINE void fg_write(fg_env_t *fg, uint16_t address, uint8_t value)
{
    firmware_native_graphics_services_t *services = fg->services;
    uint32_t physical = fw_graphics_write(fg->context, services, address, value);
    /* The callback commits the real memory operation first and resolves all
     * guest bank/page-3 aliases. Only canonical guest RAM is mirrored here.
     * Read the live display pointer AFTER that callback: SDK service may have
     * changed ownership, so never cache a framebuffer pointer across it. */
    if (physical > 0x1000u || physical <= 0x0400u) return;
    if (services->framebuffer) fw_graphics_stage(fg->context, services, physical);
}

#include "firmware_native_graphics_algorithms.h"
#include "firmware_native_blit.h"

__attribute__((used, noinline, section(".text.firmware_native_graphics")))
uint32_t firmware_native_graphics(s6502_iram_asm_context_t *c)
{
    fg_env_t env;
    fg_env_t *fg = &env;
    fg_direct_result_t direct;
    fg_region_result_t region;
    firmware_native_graphics_services_t *services;
    fw_graphics_batch_t batch;
    uint32_t pc = c->pc, budget, result_cycles;
    int accepted = 0, is_region = 0;
    if (!c->graphics || !c->ram || !c->pages || !c->read8 ||
        c->cycles > c->cycle_budget) return 0u;
    services = (firmware_native_graphics_services_t *)(FG_POINTER)c->graphics;
    if (services->version != FW_GRAPHICS_SERVICE_VERSION ||
        !services->page3 || !services->write8_resolved) return 0u;
    if ((c->status & 8u) && pc != 0x859eu) return 0u;
    env.context = c;
    env.services = services;
    env.ram = (uint8_t *)(FG_POINTER)c->ram;
    env.page3 = (uint8_t *)(FG_POINTER)services->page3;
    env.pages = (uint8_t **)(FG_POINTER)c->pages;
    budget = c->cycle_budget - c->cycles;
    batch.y = 96u; batch.rows = 0u;
    services->batch = (FG_POINTER)&batch;
    if (pc == 0x682du)
        accepted = fg_alg_picture_head(fg,c->ac,c->iy,c->sp,c->status,budget,&direct);
    else if (pc == 0x690fu)
        accepted = fg_alg_picture_resume(fg,c->ac,c->ix,c->iy,c->sp,c->status,budget,&direct);
    else if (pc == 0x876bu)
        accepted = fg_alg_graphics_address(fg,c->ix,c->iy,c->sp,c->status,budget,&direct);
    else if (pc == 0x8039u) {
        if (c->iy != 0u) { services->batch = 0u; return 0u; }
        accepted = fg_alg_hline_middle(fg,c->ac,c->ix,c->iy,c->sp,c->status,budget,&direct);
    } else if (pc == 0x5351u || pc == 0x5801u)
        accepted = fg_alg_part_picture_row(fg,pc == 0x5801u,c->ac,c->iy,c->sp,c->status,budget,&direct);
    else if (pc == 0x859eu)
        accepted = fg_alg_pixel_tail(fg,c->sp,c->status,budget,&direct);
    else if (pc == 0x6988u) {
        is_region = 1;
        accepted = fg_fast_shift(fg,c->sp,c->status,budget,&region);
        if (!accepted) accepted = fg_alg_shift_region(fg,c->sp,c->status,budget,&region);
    } else if (pc == 0x5c5du) {
        is_region = 1;
        accepted = fg_fast_bitmap(fg,c->sp,c->status,budget,&region);
        if (!accepted) accepted = fg_alg_bitmap_region(fg,c->sp,c->status,budget,&region);
    } else if (pc == 0x6b1au || pc == 0x6ba4u) {
        is_region = 1;
        accepted = fg_alg_picture_tail(fg,pc == 0x6b1au,c->sp,c->status,budget,&region);
    }
    fw_graphics_flush(c, services);
    services->batch = 0u;
    if (accepted <= 0) return 0u;
    if (services->picture_state) {
        uint32_t *state=(uint32_t *)(FG_POINTER)services->picture_state;
        if (pc == 0x682du) {
            *state=1u | ((!env.ram[0x2081u] && !env.ram[0x2082u] &&
                env.ram[0x2083u]>=158u && env.ram[0x2084u]>=95u) ? 2u : 0u);
        } else if ((pc == 0x6b1au || pc == 0x6ba4u || pc == 0x5c5du) &&
                   !env.ram[0x20dau]) {
            uint32_t complete=*state;
            *state=0u;
            if ((complete&3u)==3u && services->picture_complete)
                ((void (*)(FG_POINTER))(FG_POINTER)services->picture_complete)(c->ram);
        }
    }
    if (is_region) {
        c->pc=region.pc;c->ac=region.ac;c->ix=region.ix;c->iy=region.iy;
        c->sp=region.sp;c->status=region.status;result_cycles=region.cycles;
    } else {
        c->pc=direct.pc;c->ac=direct.ac;c->ix=direct.ix;c->iy=direct.iy;
        c->sp=direct.dt;c->status=direct.status;result_cycles=direct.cycles;
    }
    c->cycles += result_cycles;
    FW_RECORD(c, result_cycles);
    return result_cycles;
}

#undef FG_INLINE
