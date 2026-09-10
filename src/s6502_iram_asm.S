/*
 * Register-resident 65C02 interpreter for the S1C33 IRAM overlay.
 *
 * The only C ABI argument is a pointer in R6.  Every context member is a
 * 32-bit word so the assembly ABI is independent of C bit-field and narrow
 * type layout.  R15 is owned by the 9288 loader and is never touched here.
 *
 * This is the P0/P1 engine: it handles the measured top-84 opcode set.  Any
 * address with firmware side effects, an absent
 * mapped page, decimal ADC/SBC, or an opcode not listed in the dispatch table
 * leaves PC at the unexecuted instruction and returns SLOW to the complete C
 * interpreter.
 */

    .section ".iram.hot.exec_engine","ax",@progbits
    .p2align 2

    .equ CTX_PC,                  0
    .equ CTX_AC,                  4
    .equ CTX_IX,                  8
    .equ CTX_IY,                 12
    .equ CTX_SP,                 16
    .equ CTX_STATUS,             20
    .equ CTX_CYCLE_BUDGET,       24
    .equ CTX_PAGES,              28
    .equ CTX_PAGE_KIND,          32
    .equ CTX_RAM,                36
    .equ CTX_DIRTY,              40
    .equ CTX_DISPATCH_BITS,      44
    .equ CTX_CYCLES,             48
    .equ CTX_INSTRUCTIONS,       52
    .equ CTX_CONTROL_TRANSITIONS,56
    .equ CTX_EXIT_REASON,        60
    .equ CTX_CODE_PAGES,         64
    .equ CTX_SUPER_HITS,         68
    .equ CTX_NATIVE_ENTRY,       72
    .equ CTX_NATIVE_METRICS,     76
    .equ CTX_READ8,              80
    .equ CTX_WRITE8,             84
    .equ CTX_SAVED_FETCH,        100

    .macro SAVE_FETCH
    ext   CTX_SAVED_FETCH
    ld.w  [%r0], %r2
    .endm

    .equ PAGE_WRITE_DIRECT,      2
    .equ PAGE_FORCE_PB_ZERO,     8
    .equ PAGE_FORCE_APO_FF,      16
    .equ PAGE_FORCE_MASK,        24

    .equ EXIT_SLOW,              1
    .equ EXIT_DEADLINE,          2
    .equ EXIT_DISPATCH,          3

    .equ FLAG_N,                 0x80
    .equ FLAG_V,                 0x40
    .equ FLAG_U,                 0x20
    .equ FLAG_B,                 0x10
    .equ FLAG_D,                 0x08
    .equ FLAG_I,                 0x04
    .equ FLAG_Z,                 0x02
    .equ FLAG_C,                 0x01

    .macro SUPER_HIT offset
    ext   CTX_SUPER_HITS
    ld.w  %r13, [%r0]
    cmp   %r13, 0
    jreq  .Lsuper_hit_done_\@
    .if \offset
    ext   \offset
    add   %r13, %r13
    .endif
    ld.w  %r12, [%r13]
    add   %r12, 1
    ld.w  [%r13], %r12
.Lsuper_hit_done_\@:
    .endm

/*
 * Fetch one operand byte.  R2 is the mapped host instruction pointer.
 * R8 may temporarily carry bit 16 while finishing an instruction at 0xffff;
 * map/control/writeback boundaries truncate it before it becomes observable.
 */
    .macro FETCH8
    ld.ub %r12, [%r2]+
    add   %r8, 1
    .endm

/*
 * Fetch a little-endian 16-bit operand without issuing an unaligned S1C33
 * halfword load.  The measured true-device trace repeatedly executes an
 * absolute instruction at $xxfe, so map the second operand byte when the
 * operand starts at $xxff instead of returning zero to the C interpreter.
 * The common page-interior path remains unchanged.
 */
    .macro FETCH16 slow
    ld.w  %r12, %r8
    ext   255
    and   %r12, %r12
    ext   255
    cmp   %r12, %r12
    jreq  \slow
    ld.ub %r12, [%r2]+
    ld.ub %r13, [%r2]+
    sll   %r13, 8
    or    %r12, %r13
    add   %r8, 2
    .endm

/* The true-device zero-fallback leader is LDA absolute at $84fe.  Give that
 * handler the shared page bridge without expanding it into every colder
 * absolute opcode and exhausting the verified 5832-byte IRAM window. */
    .macro FETCH16_CROSS slow
    ld.w  %r12, %r8
    ext   255
    and   %r12, %r12
    ext   255
    cmp   %r12, %r12
    jrne  .Lfetch16_cross_in_page_\@
    call  .Lfetch16_cross
    cmp   %r13, 0
    jreq  \slow
    jp    .Lfetch16_cross_done_\@
.Lfetch16_cross_in_page_\@:
    ld.ub %r12, [%r2]+
    ld.ub %r13, [%r2]+
    sll   %r13, 8
    or    %r12, %r13
    add   %r8, 2
.Lfetch16_cross_done_\@:
    .endm

/*
 * Translate guest address R12 and read a byte back into R12.  Page zero is
 * special: 0..3 are DATA channels and 0x0c..0x0e are bank registers; both
 * ranges must retain the firmware slow path.  Other zero-page bytes are RAM.
 */
    /* Known zero-page operands do not need the generic high-page lookup.
     * Keep DATA $00..$03 and bank $0c..$0e on the exact slow path. */
    .macro READ_ZP_VALUE slow
    cmp   %r12, 14
    jrugt .Lread_zp_ram_\@
    cmp   %r12, 3
    jrule \slow
    cmp   %r12, 12
    jruge \slow
.Lread_zp_ram_\@:
    ld.w  %r13, %r14
    add   %r13, %r12
    ld.ub %r12, [%r13]
    .endm

    .macro MAP_READ_VALUE slow
    ld.w  %r13, %r12
    srl   %r13, 8
    cmp   %r13, 0
    jrne  .Lmap_read_page_\@
    cmp   %r12, 3
    jrule \slow
    cmp   %r12, 12
    jrult .Lmap_read_zp_ok_\@
    cmp   %r12, 14
    jrule \slow
.Lmap_read_zp_ok_\@:
    ld.w  %r13, %r14
    add   %r13, %r12
    ld.ub %r12, [%r13]
    jp    .Lmap_read_done_\@
.Lmap_read_page_\@:
    sll   %r13, 2
    add   %r13, %r1
    ld.w  %r13, [%r13]
    cmp   %r13, 0
    jreq  \slow
    ext   255
    and   %r12, %r12
    add   %r13, %r12
    ld.ub %r12, [%r13]
.Lmap_read_done_\@:
    .endm

/*
 * Translate guest address R12 for a byte store.  R3 holds the value.  R2 is
 * deliberately scratch here, so successful callers finish through the
 * remap/reload tail.  C marks only side-effect-free RAM pages WRITE_DIRECT.
 */
    .macro MAP_WRITE_POINTER slow
    ld.w  %r13, %r12
    srl   %r13, 8
    cmp   %r13, 0
    jrne  .Lmap_write_page_\@
    cmp   %r12, 5
    jrule \slow
    cmp   %r12, 12
    jrult .Lmap_write_zp_ok_\@
    cmp   %r12, 14
    jrule \slow
.Lmap_write_zp_ok_\@:
    ld.w  %r2, 0
    ld.w  %r13, %r14
    add   %r13, %r12
    jp    .Lmap_write_done_\@
.Lmap_write_page_\@:
    ext   CTX_PAGE_KIND
    ld.w  %r2, [%r0]
    add   %r2, %r13
    ld.ub %r2, [%r2]
    ld.w  %r13, %r2
    and   %r13, PAGE_WRITE_DIRECT
    cmp   %r13, 0
    jreq  \slow
    ld.w  %r13, %r12
    srl   %r13, 8
    sll   %r13, 2
    add   %r13, %r1
    ld.w  %r13, [%r13]
    cmp   %r13, 0
    jreq  \slow
    ext   255
    and   %r12, %r12
    add   %r13, %r12
.Lmap_write_done_\@:
    .endm

/* Store R3 through host pointer R13 and mark physical LCD RAM dirty.  Keep
 * the two post-write rules from ram_write() local so their otherwise-normal
 * $02xx/$20xx pages can stay on the direct native path. */
    .macro STORE_MAPPED_VALUE force_post=0
    ld.ub %r12, [%r13]
    cmp   %r12, %r3
    jreq  .Lstore_done_\@
    ld.b  [%r13], %r3
    .if \force_post
    call  .Lstore_post_rules
    .else
    and   %r2, PAGE_FORCE_MASK
    cmp   %r2, 0
    jreq  .Lstore_check_lcd_\@
    call  .Lstore_post_rules
    .endif
.Lstore_check_lcd_\@:
    ld.w  %r12, %r14
    ext   1024
    add   %r12, %r12
    cmp   %r13, %r12
    jrult .Lstore_done_\@
    ld.w  %r12, %r14
    ext   4096
    add   %r12, %r12
    cmp   %r13, %r12
    jrugt .Lstore_done_\@
    ext   CTX_DIRTY
    ld.w  %r2, [%r0]
    ld.w  %r12, 1
    ld.w  [%r2], %r12
.Lstore_done_\@:
    .endm

    .globl s6502_iram_exec_burst_asm
    .type s6502_iram_exec_burst_asm,@function
s6502_iram_exec_burst_asm:
    /* R0-R3 are the only callee-saved registers used by the hot loop. */
    pushn %r3
    ld.w  %r0, %r6

    ld.w  %r8, [%r0]
    ext   CTX_AC
    ld.w  %r4, [%r0]
    ext   CTX_IX
    ld.w  %r5, [%r0]
    ext   CTX_IY
    ld.w  %r6, [%r0]
    ext   CTX_SP
    ld.w  %r7, [%r0]
    ext   CTX_STATUS
    ld.w  %r9, [%r0]
    ext   CTX_PAGES
    ld.w  %r1, [%r0]
    ext   CTX_RAM
    ld.w  %r14, [%r0]

    ld.uh %r8, %r8
    ld.ub %r4, %r4
    ld.ub %r5, %r5
    ld.ub %r6, %r6
    ld.ub %r7, %r7
    ld.ub %r9, %r9
    ld.w  %r10, 0                 /* guest cycles */
    ld.w  %r11, 0                 /* executed instruction count */
    ld.w  %r12, 0
    ext   CTX_CONTROL_TRANSITIONS
    ld.w  [%r0], %r12

    ext   s6502_iram_dispatch_table@h
    ext   s6502_iram_dispatch_table@m
    ld.w  %r3, s6502_iram_dispatch_table@l
    jp    .Lmap_fetch

/* Map the page containing guest PC and rebuild the cached host PC. */
.Lmap_fetch:
    /* Centralize 16-bit guest-PC wrap before any page-table access. */
    ld.uh %r8, %r8
    ld.w  %r12, %r8
    srl   %r12, 8
    /* Page-zero opcode fetches can hit DATA/BK side effects: keep them slow. */
    cmp   %r12, 0
    jreq  .Lexit_slow0
    sll   %r12, 2
    ext   CTX_CODE_PAGES
    ld.w  %r2, [%r0]
    add   %r2, %r12
    ld.w  %r2, [%r2]
    cmp   %r2, 0
    jreq  .Lexit_slow0
    ld.ub %r12, %r8
    add   %r2, %r12
    /* Only an opcode at byte 255 needs the cross-page operand path. */
    ext   255
    cmp   %r12, %r12
    jreq  .Ldispatch_page_tail

/* One load, one compact table lookup, and an indirect jump per opcode. */
.Ldispatch_fetch:
    ld.ub %r12, [%r2]+
    add   %r8, 1
    sll   %r12, 1
    ld.w  %r13, %r3
    add   %r13, %r12
    ld.uh %r13, [%r13]
    jp    %r13

/*
 * Fetch an opcode at xxff, then map the following page before its handler
 * reads operands.  This is the only opcode-fetch case that crosses a page.
 */
.Ldispatch_page_tail:
    ld.ub %r12, [%r2]+
    add   %r8, 1
    sll   %r12, 1
    ld.w  %r13, %r3
    add   %r13, %r12
    ld.uh %r13, [%r13]
    ld.uh %r8, %r8
    ld.w  %r12, %r8
    srl   %r12, 8
    /* Page zero has DATA/BK side effects; replay the whole opcode in C. */
    cmp   %r12, 0
    jreq  .Lexit_slow1
    sll   %r12, 2
    ext   CTX_CODE_PAGES
    ld.w  %r2, [%r0]
    add   %r2, %r12
    ld.w  %r2, [%r2]
    cmp   %r2, 0
    jreq  .Lexit_slow1
    jp    %r13

/* ---------- Shared completion and state-writeback tails ---------- */

/* Defer architectural N/Z publication.  R9 keeps the real status byte in its
 * low byte and stores (result + 1) in the upper bits; zero means that no lazy
 * value exists.  Encoding result+1 distinguishes a real $ff result from the
 * inactive state without consuming another resident register. */
.Lset_nz_finish:
    ld.ub %r12, %r12
    ld.ub %r9, %r9
    add   %r12, 1
    sll   %r12, 8
    or    %r9, %r12

.Lfinish:
s6502_iram_count_patch_finish:
    add   %r11, 1
    ld.ub %r12, %r8
    cmp   %r12, 0
    jreq  .Lmap_fetch
    ext   255
    cmp   %r12, %r12
    jreq  .Ldispatch_page_tail
    /* Fuse the common completion with the next opcode dispatch. */
    ld.ub %r12, [%r2]+
    add   %r8, 1
    sll   %r12, 1
    ld.w  %r13, %r3
    add   %r13, %r12
    ld.uh %r13, [%r13]
    jp    %r13

/* R2 no longer contains host PC. */
.Lfinish_remap:
s6502_iram_count_patch_remap:
    add   %r11, 1
    jp    .Lmap_fetch

/* Same N/Z tail after R2/R3 were used as scratch. */
.Lset_nz_reload:
    ld.ub %r12, %r12
    ld.ub %r9, %r9
    add   %r12, 1
    sll   %r12, 8
    or    %r9, %r12
    jp    .Lfinish_reload

/* Materialize lazy N/Z only at an observing instruction or an engine exit.
 * R12/R13 are scratch by ABI; all other resident guest registers survive. */
.Lmaterialize_nz:
    ld.w  %r12, %r9
    srl   %r12, 8
    cmp   %r12, 0
    jreq  .Lmaterialize_nz_return
    sub   %r12, 1
    ld.ub %r12, %r12
    ld.ub %r9, %r9
    ext   125                       /* preserve V/B/D/I/U/C */
    and   %r9, %r9
    cmp   %r12, 0
    jreq  .Lmaterialize_nz_zero
    ld.w  %r13, %r12
    ext   128
    and   %r13, %r13
    or    %r9, %r13
    ret
.Lmaterialize_nz_zero:
    or    %r9, FLAG_Z
.Lmaterialize_nz_return:
    ret

/* R2 and R3 were scratch; restore the table base as well as host PC. */
.Lfinish_reload:
s6502_iram_count_patch_reload:
    add   %r11, 1
    ext   s6502_iram_dispatch_table@h
    ext   s6502_iram_dispatch_table@m
    ld.w  %r3, s6502_iram_dispatch_table@l
    jp    .Lmap_fetch

/* Selected data handlers saved the post-operand host PC before using R2/R3
 * as scratch. RAM writes cannot change the code mapping on this guarded
 * path; ordinary .Lfinish still handles xxff/page wrap before fetching. */
.Lset_nz_restore_fetch:
    ld.ub %r12, %r12
    ld.ub %r9, %r9
    add   %r12, 1
    sll   %r12, 8
    or    %r9, %r12
.Lrestore_fetch:
    ext   CTX_SAVED_FETCH
    ld.w  %r2, [%r0]
.Lreload_dispatch_finish:
    ext   s6502_iram_dispatch_table@h
    ext   s6502_iram_dispatch_table@m
    ld.w  %r3, s6502_iram_dispatch_table@l
    jp    .Lfinish

.Lcontrol_reload:
    ext   s6502_iram_dispatch_table@h
    ext   s6502_iram_dispatch_table@m
    ld.w  %r3, s6502_iram_dispatch_table@l
    /* R2 was scratch in the caller/native bridge.  Zero marks that the
     * target PC must be remapped instead of reusing the old host pointer. */
    ld.w  %r2, 0
    jp    .Lcontrol_complete

.Lcontrol_complete:
    /* Branch arithmetic may temporarily carry beyond the 16-bit guest PC. */
    ld.uh %r8, %r8
s6502_iram_count_patch_control:
    add   %r11, 1
    ext   CTX_CONTROL_TRANSITIONS
    ld.w  %r12, [%r0]
    add   %r12, 1
    ext   CTX_CONTROL_TRANSITIONS
    ld.w  [%r0], %r12

    ext   CTX_CYCLE_BUDGET
    ld.w  %r12, [%r0]
    cmp   %r10, %r12
    jruge .Lexit_deadline
    ext   512                       /* RAM[_SYSCON] */
    ld.ub %r12, [%r14]
    and   %r12, 8
    cmp   %r12, 0
    jrne  .Lexit_deadline

    ext   CTX_DISPATCH_BITS
    ld.w  %r12, [%r0]
    cmp   %r12, 0
    jreq  .Lcontrol_native_check
    add   %r12, %r8
    ld.ub %r12, [%r12]
    cmp   %r12, 0
    jrne  .Lexit_dispatch

.Lcontrol_native_check:
#if !defined(GAM4980_AUTHORED_FIRMWARE) && \
    (defined(GAM4980_DYNAMIC_NATIVE_ALL) || defined(GAM4980_STATIC_NATIVE_GAME))
    /* Full-GAM packages publish a page entry even for PCs which were not in
     * the old bounded game-template table.  Ask the module switch after each
     * real guest control transfer; an absent block returns zero atomically
     * and retains the complete interpreter fallback. */
    ext   CTX_NATIVE_ENTRY
    ld.w  %r12, [%r0]
    cmp   %r12, 0
    jreq  .Lmap_fetch
    ld.w  %r13, %r8
    srl   %r13, 8
    sll   %r13, 2
    add   %r12, %r13
    ld.w  %r12, [%r12]
    cmp   %r12, 0
    jrne  .Lexit_dispatch
    jp    .Lcontrol_continue
#else
    /* Authored firmware uses exact bank-qualified dispatch bits above.
     * Do not try a module at unrelated addresses on the same page. */
    jp    .Lcontrol_continue
#endif

.Lcontrol_continue:
    /* Conditional branches leave R2 pointing at the target whenever it is
     * still in the mapped host page.  JSR/JMP/RTS and cross-page branches
     * explicitly clear R2 and retain the complete remap path. */
    cmp   %r2, 0
    jreq  .Lmap_fetch
    ld.ub %r12, %r8
    cmp   %r12, 0
    jreq  .Lmap_fetch
    ext   255
    cmp   %r12, %r12
    jreq  .Ldispatch_page_tail
    jp    .Ldispatch_fetch

.Lexit_slow3:
    sub   %r8, 3
    jp    .Lexit_slow0
.Lexit_slow2:
    sub   %r8, 2
    jp    .Lexit_slow0
.Lexit_slow1:
    sub   %r8, 1
.Lexit_slow0:
    /* Do not make the C fallback execute past an already-reached deadline. */
    cmp   %r10, 0
    jreq  .Lexit_slow_write
    ext   CTX_CYCLE_BUDGET
    ld.w  %r12, [%r0]
    cmp   %r10, %r12
    jruge .Lexit_deadline
.Lexit_slow_write:
    ld.w  %r12, EXIT_SLOW
    jp    .Lwriteback
.Lexit_deadline:
    ld.w  %r12, EXIT_DEADLINE
    jp    .Lwriteback
.Lexit_dispatch:
#if defined(GAM4980_DYNAMIC_NATIVE_ALL) || \
    defined(GAM4980_STATIC_NATIVE_GAME)
    /* Pure native blocks live in a pageable arena in ordinary application
     * RAM.  CTX_NATIVE_ENTRY points at a 256-entry guest-page dispatch table;
     * an absent or evicted module leaves a zero entry and falls back to the
     * normal IRAM/C path.  Disk modules use the normal S1C33 C ABI, so publish
     * the register-resident guest state before the call and reload it after.
     * R0-R3 are callee-saved and the first C argument is passed in R6. */
    call  .Lmaterialize_nz
    /* Sparse exact-entry private ABI. Bank validation is one indexed halfword
     * read, not a C gateway; only loaded callback-free leaves are published. */
    ext   108
    ld.w  %r13, [%r0]
    cmp   %r13, 0
    jreq  .Lregister_miss
    ld.w  %r12, %r8
    ext   255
    and   %r12, %r12
    sll   %r12, 4
    add   %r13, %r12
    ld.w  %r12, [%r13]
    cmp   %r12, %r8
    jrne  .Lregister_miss
.Lregister_validate_bank:
    ext   112
    ld.w  %r2, [%r0]
    ld.w  %r12, %r8
    srl   %r12, 8
    srl   %r12, 4
    sll   %r12, 1
    add   %r2, %r12
    ld.uh %r2, [%r2]
    ext   4
    ld.w  %r12, [%r13]
    cmp   %r2, %r12
    jrne  .Lregister_miss
    /* Pin pageable code across callbacks. At a control boundary the saved
     * operand-fetch slot is dead; reuse it for the pin pointer. */
    ext   12
    ld.w  %r2, [%r13]
    cmp   %r2, 0
    jreq  .Lregister_pin_done
    ld.w  %r12, [%r2]
    add   %r12, 1
    ld.w  [%r2], %r12
    ld.w  %r12, 1
    ext   4
    ld.w  [%r2], %r12
.Lregister_pin_done:
    ext   8
    ld.w  %r13, [%r13]
    cmp   %r13, 0
    jreq  .Lregister_unpin_miss
    ext   CTX_SAVED_FETCH
    ld.w  [%r0], %r2
    call  %r13
    ext   CTX_SAVED_FETCH
    ld.w  %r2, [%r0]
    cmp   %r2, 0
    jreq  .Lregister_unpin_done
    ld.w  %r3, [%r2]
    sub   %r3, 1
    ld.w  [%r2], %r3
.Lregister_unpin_done:
    cmp   %r12, 0
    jreq  .Lregister_rejected
    ext   116
    ld.w  %r12, [%r0]
    add   %r12, 1
    ext   116
    ld.w  [%r0], %r12
    /* Resident native successor: stay in the register ABI instead of
     * reloading the opcode table and probing the general dispatch bitmap. */
    ext   CTX_CYCLE_BUDGET
    ld.w  %r12, [%r0]
    cmp   %r10, %r12
    jruge .Lcontrol_reload
    ext   512
    ld.ub %r12, [%r14]
    and   %r12, 8
    cmp   %r12, 0
    jrne  .Lcontrol_reload
    ext   108
    ld.w  %r13, [%r0]
    cmp   %r13, 0
    jreq  .Lcontrol_reload
    ld.w  %r12, %r8
    ext   255
    and   %r12, %r12
    sll   %r12, 4
    add   %r13, %r12
    ld.w  %r12, [%r13]
    cmp   %r12, %r8
    jrne  .Lcontrol_reload
s6502_iram_count_patch_native_chain:
    add   %r11, 1
    ext   CTX_CONTROL_TRANSITIONS
    ld.w  %r12, [%r0]
    add   %r12, 1
    ext   CTX_CONTROL_TRANSITIONS
    ld.w  [%r0], %r12
    jp    .Lregister_validate_bank
.Lregister_rejected:
    /* This exact native contract already declined the unchanged state.
     * Do not retry the same function through its C wrapper and fastchain. */
    ld.w  %r12, EXIT_SLOW
    jp    .Lwriteback
.Lregister_unpin_miss:
    cmp   %r2, 0
    jreq  .Lregister_miss
    ld.w  %r12, [%r2]
    sub   %r12, 1
    ld.w  [%r2], %r12
.Lregister_miss:
    ext   CTX_NATIVE_ENTRY
    ld.w  %r13, [%r0]
    cmp   %r13, 0
    jreq  .Lexit_dispatch_write
    ld.w  %r12, %r8
    srl   %r12, 8
    sll   %r12, 2
    add   %r13, %r12
    ld.w  %r13, [%r13]
    cmp   %r13, 0
    jreq  .Lexit_dispatch_write

    ld.w  [%r0], %r8
    ext   CTX_AC
    ld.w  [%r0], %r4
    ext   CTX_IX
    ld.w  [%r0], %r5
    ext   CTX_IY
    ld.w  [%r0], %r6
    ext   CTX_SP
    ld.w  [%r0], %r7
    ext   CTX_STATUS
    ld.w  [%r0], %r9
    ext   CTX_CYCLES
    ld.w  [%r0], %r10
    ext   CTX_INSTRUCTIONS
    ld.w  [%r0], %r11
    ld.w  %r6, %r0
    call  %r13
    ld.w  %r12, %r4

    ld.w  %r8, [%r0]
    ext   CTX_AC
    ld.w  %r4, [%r0]
    ext   CTX_IX
    ld.w  %r5, [%r0]
    ext   CTX_IY
    ld.w  %r6, [%r0]
    ext   CTX_SP
    ld.w  %r7, [%r0]
    ext   CTX_STATUS
    ld.w  %r9, [%r0]
    ext   CTX_CYCLES
    ld.w  %r10, [%r0]
    ext   CTX_INSTRUCTIONS
    ld.w  %r11, [%r0]
    ext   CTX_PAGES
    ld.w  %r1, [%r0]
    ext   CTX_RAM
    ld.w  %r14, [%r0]
    cmp   %r12, 0
    jreq  .Lexit_dispatch_write
    jp    .Lcontrol_reload
#endif
.Lexit_dispatch_write:
    ld.w  %r12, EXIT_DISPATCH

.Lwriteback:
    /* R12 carries the exit reason; preserve it while publishing lazy N/Z. */
    ld.w  %r2, %r12
    call  .Lmaterialize_nz
    ld.w  %r12, %r2
    ld.uh %r8, %r8
    ld.w  [%r0], %r8
    ext   CTX_AC
    ld.w  [%r0], %r4
    ext   CTX_IX
    ld.w  [%r0], %r5
    ext   CTX_IY
    ld.w  [%r0], %r6
    ext   CTX_SP
    ld.w  [%r0], %r7
    ext   CTX_STATUS
    ld.w  [%r0], %r9
    ext   CTX_CYCLES
    ld.w  [%r0], %r10
    ext   CTX_INSTRUCTIONS
    ld.w  [%r0], %r11
    ext   CTX_EXIT_REASON
    ld.w  [%r0], %r12

    ld.w  %r4, %r10
    cmp   %r12, EXIT_SLOW
    jreq  .Lreturn
    ld.w  %r13, 1
    sll   %r13, 8
    sll   %r13, 8
    sll   %r13, 8
    sll   %r13, 7
    or    %r4, %r13
.Lreturn:
    popn  %r3
    ret

/* Rare operand-page bridge shared by every absolute handler.  R8 remains
 * untouched until the next fetch page exists, preserving exact slow replay. */
.Lfetch16_cross:
    ld.w  %r13, %r8
    add   %r13, 1
    ld.uh %r13, %r13
    srl   %r13, 8
    cmp   %r13, 0
    jreq  .Lfetch16_cross_fail
    sll   %r13, 2
    ext   CTX_CODE_PAGES
    ld.w  %r12, [%r0]
    add   %r13, %r12
    ld.w  %r13, [%r13]
    cmp   %r13, 0
    jreq  .Lfetch16_cross_fail
    ld.ub %r12, [%r2]
    ld.ub %r2, [%r13]+
    sll   %r2, 8
    or    %r12, %r2
    ld.w  %r2, %r13
    add   %r8, 2
    ld.w  %r13, 1
    ret
.Lfetch16_cross_fail:
    ld.w  %r13, 0
    ret

/* Exact physical-address rules for the two otherwise-direct RAM pages.  The
 * page-kind bits make this call cold for every other store. */
.Lstore_post_rules:
    ld.w  %r12, %r13
    sub   %r12, %r14
    ld.w  %r2, %r12
    srl   %r2, 8
    cmp   %r2, 2
    jreq  .Lstore_post_pb
    ext   0x20
    cmp   %r2, %r2
    jrne  .Lstore_post_done
    ld.ub %r12, %r12
    ext   0x28
    cmp   %r12, %r12
    jrne  .Lstore_post_done
    ld.w  %r12, -1
    ld.b  [%r13], %r12
    ret
.Lstore_post_pb:
    ld.ub %r12, %r12
    ext   0x1b
    cmp   %r12, %r12
    jrne  .Lstore_post_done
    ld.w  %r12, 0
    ld.b  [%r13], %r12
    ret
.Lstore_post_done:
    ret

/* ---------- Hot opcode handlers ---------- */

#ifdef GAM4980_IRAM_V2
/* True-device traces showed that the three old shadow phrases paid for bank
 * copies and marker walks but produced no useful hits.  Keep opcode $02 on
 * the exact slow fallback in v2; the dedicated equivalence probe still builds
 * the legacy implementation without GAM4980_IRAM_V2. */
    .set .Lsuper, .Lslow_opcode
#else
/*
 * Shadow opcode $02 denotes one of a small set of C6502 compiler templates.
 * The load/bank-remap builder has already validated the complete phrase before
 * publishing this private marker.  Runtime only reads the original first-byte
 * discriminator and dynamic operands; it never repeats the full byte scan.
 * The shadow never replaces bytes visible to guest reads, and the builder never
 * emits a marker across a 256-byte page.  A real guest opcode $02 therefore
 * fails the original-byte discriminator and returns to the complete C core.
 */
.Lsuper:
    ld.w  %r12, %r8
    sub   %r12, 1
    ld.uh %r12, %r12
    ld.w  %r13, %r12
    srl   %r13, 8
    sll   %r13, 2
    add   %r13, %r1
    ld.w  %r13, [%r13]
    cmp   %r13, 0
    jreq  .Lexit_slow1
    ext   255
    and   %r12, %r12
    add   %r13, %r12
    ld.ub %r12, [%r13]+
    ext   0xa9
    cmp   %r12, %r12
    jreq  .Lsuper_imm16
    ext   0x08
    cmp   %r12, %r12
    jreq  .Lsuper_stack16
    ext   0x18
    cmp   %r12, %r12
    jreq  .Lsuper_add16
    jp    .Lexit_slow1

/* LDA #lo; STA zp; LDA #hi; STA zp+1.
 *
 * The shadow builder has already proved that both destination bytes are
 * adjacent and do not wrap at $ff.  This is a C6502-wide compiler phrase,
 * not only the OPER1/OPER2 ABI form, so runtime no longer rejects other
 * zero-page words here. */
.Lsuper_imm16:
    ld.ub %r3, [%r13]+
    add   %r13, 1
    ld.ub %r2, [%r13]+
    add   %r13, 1
    ld.ub %r4, [%r13]
    ld.w  %r13, %r14
    add   %r13, %r2
    ld.b  [%r13], %r3
    add   %r13, 1
    ld.b  [%r13], %r4
    ext   0x20
    cmp   %r2, %r2
    jrne  .Lsuper_imm16_oper2
    SUPER_HIT 0
    jp    .Lsuper_imm16_finish
.Lsuper_imm16_oper2:
    SUPER_HIT 4
.Lsuper_imm16_finish:
    add   %r8, 7
    add   %r10, 10
s6502_iram_count_patch_super_imm16:
    add   %r11, 3
    ld.w  %r12, %r4
    jp    .Lset_nz_reload

/* PHP; SEI; CLC/SEC; 16-bit immediate add/sub at $28; PLP. */
.Lsuper_stack16:
    add   %r13, 1
    ld.ub %r3, [%r13]
    cmp   %r3, 0x18
    jreq  .Lsuper_stack_op_ok
    ext   0x38
    cmp   %r3, %r3
    jrne  .Lexit_slow1
.Lsuper_stack_op_ok:
    ld.w  %r12, %r9
    and   %r12, FLAG_D
    cmp   %r12, 0
    jrne  .Lexit_slow1

    /* Dynamic immediates live at bytes 6 and 12 of the original sequence. */
    add   %r13, 4
    ld.ub %r2, [%r13]
    add   %r13, 6
    ld.ub %r12, [%r13]
    sll   %r12, 8
    or    %r2, %r12
    ld.w  %r13, %r14
    add   %r13, 0x28
    ld.ub %r12, [%r13]+
    ld.ub %r13, [%r13]
    sll   %r13, 8
    or    %r12, %r13
    cmp   %r3, 0x18
    jrne  .Lsuper_stack_sub
    add   %r12, %r2
    ld.w  %r2, %r12
    SUPER_HIT 8
    ld.w  %r12, %r2
    jp    .Lsuper_stack_store
.Lsuper_stack_sub:
    sub   %r12, %r2
    ld.w  %r2, %r12
    SUPER_HIT 12
    ld.w  %r12, %r2
.Lsuper_stack_store:
    ld.uh %r12, %r12
    ld.w  %r13, %r14
    add   %r13, 0x28
    ld.b  [%r13]+, %r12
    srl   %r12, 8
    ld.b  [%r13], %r12
    ld.ub %r4, %r12
    ld.w  %r13, %r14
    ext   256
    add   %r13, %r13
    add   %r13, %r7
    ld.w  %r12, %r9
    ext   48
    or    %r12, %r12
    ld.b  [%r13], %r12
    ld.w  %r9, %r12
    add   %r8, 15
    ext   27
    add   %r10, %r10
s6502_iram_count_patch_super_stack16:
    add   %r11, 9
    jp    .Lfinish_reload

/* CLC; LDA $20; ADC $23; STA $20; LDA $21; ADC $24; STA $21. */
.Lsuper_add16:
    ld.w  %r12, %r9
    and   %r12, FLAG_D
    cmp   %r12, 0
    jrne  .Lexit_slow1
    ext   254
    and   %r9, %r9
    ld.w  %r13, %r14
    add   %r13, 0x20
    ld.ub %r12, [%r13]
    ld.w  %r2, %r13
    add   %r2, 3
    ld.ub %r2, [%r2]
    add   %r12, %r2
    ext   255
    cmp   %r12, %r12
    jrule .Lsuper_add16_low_no_carry
    or    %r9, FLAG_C
.Lsuper_add16_low_no_carry:
    ld.b  [%r13], %r12
    SUPER_HIT 16
    ld.w  %r13, %r14
    add   %r13, 0x21
    ld.ub %r4, [%r13]
    add   %r13, 3
    ld.ub %r12, [%r13]
    add   %r8, 12
    ext   20
    add   %r10, %r10
s6502_iram_count_patch_super_add16:
    add   %r11, 6
    /* High-byte ADC with the low-byte carry; STA $21 follows it. */
    ld.w  %r2, %r12
    ld.w  %r13, %r9
    and   %r13, FLAG_C
    add   %r12, %r4
    add   %r12, %r13
    ext   60
    and   %r9, %r9
    ext   255
    cmp   %r12, %r12
    jrule .Lsuper_add16_no_carry
    or    %r9, FLAG_C
.Lsuper_add16_no_carry:
    ld.w  %r13, %r12
    ld.ub %r13, %r13
    ld.w  %r3, %r4
    xor   %r3, %r13
    xor   %r2, %r13
    and   %r3, %r2
    ext   128
    and   %r3, %r3
    srl   %r3, 1
    or    %r9, %r3
    ld.w  %r4, %r13
    ld.w  %r12, %r14
    add   %r12, 0x21
    ld.b  [%r12], %r4
    ld.w  %r12, %r4
    jp    .Lset_nz_reload
#endif

/* CLC / SEC */
.Lop18:
    /* CLC changes only C and therefore must retain any pending N/Z result. */
    call  .Lmaterialize_nz
    ext   254
    and   %r9, %r9
    add   %r10, 2
    jp    .Lfinish
.Lop38:
    or    %r9, FLAG_C
    add   %r10, 2
    jp    .Lfinish

/* LDA/LDX/LDY immediate */
.LopA9:
    FETCH8
    ld.w  %r4, %r12
    add   %r10, 2
    jp    .Lset_nz_finish
.LopA2:
    FETCH8
    ld.w  %r5, %r12
    add   %r10, 2
    jp    .Lset_nz_finish
.LopA0:
    FETCH8
    ld.w  %r6, %r12
    add   %r10, 2
    jp    .Lset_nz_finish

/* ORA/AND/EOR immediate. */
.Lop09:
    FETCH8
    or    %r4, %r12
    ld.ub %r4, %r4
    ld.w  %r12, %r4
    add   %r10, 2
    jp    .Lset_nz_finish
.Lop49:
    FETCH8
    xor   %r4, %r12
    ld.ub %r4, %r4
    ld.w  %r12, %r4
    add   %r10, 2
    jp    .Lset_nz_finish
.Lop29:
    FETCH8
    and   %r4, %r12
    ld.w  %r12, %r4
    add   %r10, 2
    jp    .Lset_nz_finish

/* LDA zero page. */
.LopA5:
    FETCH8
    READ_ZP_VALUE .Lexit_slow2
    ld.w  %r4, %r12
    add   %r10, 3
    jp    .Lset_nz_finish

/* ORA/AND zero page. */
.Lop05:
    FETCH8
    READ_ZP_VALUE .Lexit_slow2
    or    %r4, %r12
    ld.ub %r4, %r4
    ld.w  %r12, %r4
    add   %r10, 3
    jp    .Lset_nz_finish

.Lop25:
    FETCH8
    READ_ZP_VALUE .Lexit_slow2
    and   %r4, %r12
    ld.w  %r12, %r4
    add   %r10, 3
    jp    .Lset_nz_finish

/* STA/STX/STY zero page. */
.Lop84:
    FETCH8
    ld.w  %r3, %r6
    jp    .Lstore_zp
.Lop85:
    FETCH8
    ld.w  %r3, %r4
    jp    .Lstore_zp
.Lop86:
    FETCH8
    ld.w  %r3, %r5
.Lstore_zp:
    /* Compiler temporaries $20..$ff are unaliased RAM: no page lookup,
     * display dirty check, or fetch-pointer save/restore is necessary. */
    ext   32
    cmp   %r12, %r12
    jrult .Lstore_zp_special
    ld.w  %r13, %r14
    add   %r13, %r12
    ld.b  [%r13], %r3
    add   %r10, 3
    jp    .Lreload_dispatch_finish
.Lstore_zp_special:
    SAVE_FETCH
    MAP_WRITE_POINTER .Lexit_slow2
    STORE_MAPPED_VALUE
    add   %r10, 3
    jp    .Lrestore_fetch

/* LDX/LDY zero page. */
.LopA4:
    FETCH8
    READ_ZP_VALUE .Lexit_slow2
    ld.w  %r6, %r12
    add   %r10, 3
    jp    .Lset_nz_finish
.LopA6:
    FETCH8
    READ_ZP_VALUE .Lexit_slow2
    ld.w  %r5, %r12
    add   %r10, 3
    jp    .Lset_nz_finish

/* LDA/LDX/LDY and logical reads using absolute addressing. */
.Lop0D:
    FETCH16 .Lexit_slow1
    MAP_READ_VALUE .Lexit_slow3
    or    %r4, %r12
    ld.ub %r4, %r4
    ld.w  %r12, %r4
    add   %r10, 4
    jp    .Lset_nz_finish
.LopAD:
    FETCH16_CROSS .Lexit_slow1
    MAP_READ_VALUE .Lexit_slow3
    ld.w  %r4, %r12
    add   %r10, 4
    jp    .Lset_nz_finish
.Lop2D:
    FETCH16 .Lexit_slow1
    MAP_READ_VALUE .Lexit_slow3
    and   %r4, %r12
    ld.w  %r12, %r4
    add   %r10, 4
    jp    .Lset_nz_finish
.LopAE:
    FETCH16 .Lexit_slow1
    MAP_READ_VALUE .Lexit_slow3
    ld.w  %r5, %r12
    add   %r10, 4
    jp    .Lset_nz_finish
.LopAC:
    FETCH16 .Lexit_slow1
    MAP_READ_VALUE .Lexit_slow3
    ld.w  %r6, %r12
    add   %r10, 4
    jp    .Lset_nz_finish

/* LDA absolute,X/Y share indexed addressing and the read page penalty. */
.LopBD:
    FETCH16 .Lexit_slow1
    ld.w  %r3, %r5
    jp    .Llda_abs_indexed
.LopB9:
    FETCH16 .Lexit_slow1
    ld.w  %r3, %r6
.Llda_abs_indexed:
    ld.w  %r13, %r12               /* base */
    add   %r12, %r3
    ld.uh %r12, %r12               /* effective */
    xor   %r13, %r12
    srl   %r13, 8
    ld.w  %r3, 0
    cmp   %r13, 0
    jreq  .Llda_abs_no_cross
    ld.w  %r3, 1
.Llda_abs_no_cross:
    MAP_READ_VALUE .Lexit_slow3
    ld.w  %r4, %r12
    add   %r10, 4
    add   %r10, %r3
    /* The read mapper does not clobber R2: retain the host fetch pointer. */
    ext   s6502_iram_dispatch_table@h
    ext   s6502_iram_dispatch_table@m
    ld.w  %r3, s6502_iram_dispatch_table@l
    jp    .Lset_nz_finish

/* ADC/SBC absolute,X share address resolution and the read page penalty.
 * Decimal arithmetic stays in the complete C engine.  R3 selects ADC/SBC
 * and survives the direct mapped read. */
.Lop7D:
    ld.w  %r3, 1                    /* ADC */
    jp    .Labs_x_arithmetic
.LopFD:
    ld.w  %r3, 2                    /* SBC */
.Labs_x_arithmetic:
    ld.w  %r12, %r9
    and   %r12, FLAG_D
    cmp   %r12, 0
    jrne  .Lexit_slow1
    FETCH16 .Lexit_slow1
    SAVE_FETCH
    ld.w  %r13, %r12               /* base */
    add   %r12, %r5
    ld.uh %r12, %r12               /* effective */
    xor   %r13, %r12
    srl   %r13, 8
    ld.w  %r2, 0
    cmp   %r13, 0
    jreq  .Labs_x_arithmetic_no_cross
    ld.w  %r2, 1
.Labs_x_arithmetic_no_cross:
    MAP_READ_VALUE .Lexit_slow3
    add   %r10, 4
    add   %r10, %r2
    cmp   %r3, 2
    jrne  .Lbinary_add_saved
    ext   255
    xor   %r12, %r12
    jp    .Lbinary_add_saved

.Lop8D:
    FETCH16 .Lexit_slow1
    ld.w  %r3, %r4
.Lstore_abs:
    SAVE_FETCH
    MAP_WRITE_POINTER .Lexit_slow3
    STORE_MAPPED_VALUE
    add   %r10, 4
    jp    .Lrestore_fetch
.Lop8E:
    FETCH16 .Lexit_slow1
    ld.w  %r3, %r5
    jp    .Lstore_abs

/* STA absolute,X.  The indexed store has no page-cross cycle penalty. */
.Lop9D:
    FETCH16 .Lexit_slow1
    SAVE_FETCH
    add   %r12, %r5
    ld.uh %r12, %r12
    ld.w  %r3, %r4
    MAP_WRITE_POINTER .Lexit_slow3
    STORE_MAPPED_VALUE
    add   %r10, 5
    jp    .Lrestore_fetch

/* LDA/ADC/SBC/STA (zero page),Y share pointer validation and indexing. */
.LopB1:
    FETCH8
    ld.w  %r3, 0                    /* LDA */
    jp    .Lindy_resolve
.Lop71:
    ld.w  %r12, %r9
    and   %r12, FLAG_D
    cmp   %r12, 0
    jrne  .Lexit_slow1
    FETCH8
    ld.w  %r3, 1                    /* ADC */
    jp    .Lindy_resolve
.LopF1:
    ld.w  %r12, %r9
    and   %r12, FLAG_D
    cmp   %r12, 0
    jrne  .Lexit_slow1
    FETCH8
    ld.w  %r3, 2                    /* SBC */
    jp    .Lindy_resolve
.Lop91:
    FETCH8
    ld.w  %r3, 3                    /* STA */

.Lindy_resolve:
    SAVE_FETCH
    cmp   %r12, 3
    jrule .Lexit_slow2
    ext   255
    cmp   %r12, %r12
    jreq  .Lexit_slow2
    /* Both bytes of the zero-page pointer must avoid BK 0x0c..0x0e. */
    cmp   %r12, 11
    jrult .Lindy_zp_safe
    cmp   %r12, 14
    jrule .Lexit_slow2
.Lindy_zp_safe:
    ld.w  %r13, %r14
    add   %r13, %r12
    ld.ub %r12, [%r13]+
    ld.ub %r13, [%r13]
    sll   %r13, 8
    or    %r12, %r13               /* base */
    ld.w  %r13, %r12
    add   %r12, %r6
    ld.uh %r12, %r12               /* effective */
    cmp   %r3, 3
    jreq  .Lindy_store
    xor   %r13, %r12
    srl   %r13, 8
    ld.w  %r2, 0                    /* page-cross penalty, committed on success */
    cmp   %r13, 0
    jreq  .Lindy_no_cross
    ld.w  %r2, 1
.Lindy_no_cross:
    MAP_READ_VALUE .Lexit_slow2
    add   %r10, 5
    add   %r10, %r2
    cmp   %r3, 0
    jrne  .Lindy_arithmetic
    ld.w  %r4, %r12
    jp    .Lset_nz_restore_fetch
.Lindy_arithmetic:
    cmp   %r3, 2
    jrne  .Lbinary_add_saved
    ext   255
    xor   %r12, %r12
    jp    .Lbinary_add_saved
.Lindy_store:
    ld.w  %r3, %r4
    MAP_WRITE_POINTER .Lexit_slow2
    STORE_MAPPED_VALUE
    add   %r10, 6
    jp    .Lrestore_fetch

/* JSR absolute, JMP absolute, RTS. */
.Lop20:
    FETCH16 .Lexit_slow1
    ld.w  %r3, %r8
    sub   %r3, 1
    ld.uh %r3, %r3
    ld.w  %r13, %r14
    ext   256
    add   %r13, %r13
    add   %r13, %r7
    ld.w  %r2, %r3
    srl   %r2, 8
    ld.b  [%r13], %r2
    sub   %r7, 1
    ld.ub %r7, %r7
    ld.w  %r13, %r14
    ext   256
    add   %r13, %r13
    add   %r13, %r7
    ld.b  [%r13], %r3
    sub   %r7, 1
    ld.ub %r7, %r7
    ld.w  %r8, %r12
    add   %r10, 6
    jp    .Lcontrol_reload

.Lop4C:
    FETCH16_CROSS .Lexit_slow1
    ld.w  %r8, %r12
    add   %r10, 3
    ld.w  %r2, 0
    jp    .Lcontrol_complete

.Lop60:
    add   %r7, 1
    ld.ub %r7, %r7
    ld.w  %r13, %r14
    ext   256
    add   %r13, %r13
    add   %r13, %r7
    ld.ub %r12, [%r13]
    add   %r7, 1
    ld.ub %r7, %r7
    ld.w  %r13, %r14
    ext   256
    add   %r13, %r13
    add   %r13, %r7
    ld.ub %r13, [%r13]
    sll   %r13, 8
    or    %r12, %r13
    add   %r12, 1
    ld.uh %r8, %r12
    add   %r10, 6
    ld.w  %r2, 0
    jp    .Lcontrol_complete

/* Conditional branches share the relative branch body. */
    /* Read lazy flags without publishing the entire status byte.  Calls and
     * host arithmetic clobber PSR, so persistent guest flags remain in R9. */
    .macro BRANCH_ZERO lazy_taken, real_taken
    ld.w  %r13, %r9
    srl   %r13, 8
    cmp   %r13, 0
    jreq  .Lzero_real_\@
    cmp   %r13, 1
    \lazy_taken .Lbranch_taken
    jp    .Lbranch_not_taken
.Lzero_real_\@:
    ld.w  %r13, %r9
    and   %r13, FLAG_Z
    cmp   %r13, 0
    \real_taken .Lbranch_taken
    jp    .Lbranch_not_taken
    .endm

    .macro BRANCH_NEGATIVE taken
    ld.w  %r13, %r9
    srl   %r13, 8
    cmp   %r13, 0
    jreq  .Lnegative_real_\@
    sub   %r13, 1
    jp    .Lnegative_test_\@
.Lnegative_real_\@:
    ld.w  %r13, %r9
.Lnegative_test_\@:
    ext   FLAG_N
    and   %r13, %r13
    cmp   %r13, 0
    \taken .Lbranch_taken
    jp    .Lbranch_not_taken
    .endm

.LopD0:
    BRANCH_ZERO jrne, jreq
.Lop90:
    ld.w  %r13, %r9
    and   %r13, FLAG_C
    cmp   %r13, 0
    jreq  .Lbranch_taken
    jp    .Lbranch_not_taken
.Lop50:
    ld.w  %r13, %r9
    ext   FLAG_V
    and   %r13, %r13
    cmp   %r13, 0
    jreq  .Lbranch_taken
    jp    .Lbranch_not_taken
.Lop30:
    BRANCH_NEGATIVE jrne
.LopF0:
    BRANCH_ZERO jreq, jrne
.Lbranch_not_taken:
    ld.ub %r12, [%r2]+
    add   %r8, 1
    add   %r10, 2
    jp    .Lcontrol_complete
.Lbranch_taken:
    ld.b  %r12, [%r2]+
    add   %r8, 1
    ld.w  %r13, %r8
    add   %r8, %r12
    add   %r10, 3
    xor   %r13, %r8
    srl   %r13, 8
    cmp   %r13, 0
    jrne  .Lbranch_cross_page
    /* R2 already points just past the relative operand.  The signed byte in
     * R12 applies to both guest PC and its mapped host pointer. */
    add   %r2, %r12
    jp    .Lcontrol_complete
.Lbranch_cross_page:
    add   %r10, 1
    ld.w  %r2, 0
    jp    .Lcontrol_complete

/* Accumulator shifts/rotates and register transfers/increments/decrements. */
.Lop0A:
    ld.w  %r12, %r4
    ld.w  %r13, %r12
    ext   128
    and   %r13, %r13
    srl   %r13, 7
    ext   254
    and   %r9, %r9
    or    %r9, %r13
    sll   %r12, 1
    ld.ub %r12, %r12
    ld.w  %r4, %r12
    add   %r10, 2
    jp    .Lset_nz_finish
.Lop2A:
    ld.w  %r12, %r4
    ld.w  %r13, %r9
    and   %r13, FLAG_C
    sll   %r12, 1
    or    %r12, %r13
    ld.w  %r13, %r12
    srl   %r13, 8
    and   %r13, 1
    ext   254
    and   %r9, %r9
    or    %r9, %r13
    ld.ub %r12, %r12
    ld.w  %r4, %r12
    add   %r10, 2
    jp    .Lset_nz_finish
.Lop4A:
    ld.w  %r12, %r4
    ld.w  %r13, %r12
    and   %r13, 1
    ext   254
    and   %r9, %r9
    or    %r9, %r13
    srl   %r12, 1
    ld.w  %r4, %r12
    add   %r10, 2
    jp    .Lset_nz_finish
.Lop6A:
    ld.w  %r12, %r9
    and   %r12, FLAG_C
    sll   %r12, 7
    ld.w  %r13, %r4
    and   %r13, 1
    ext   254
    and   %r9, %r9
    or    %r9, %r13
    srl   %r4, 1
    or    %r4, %r12
    ld.ub %r4, %r4
    ld.w  %r12, %r4
    add   %r10, 2
    jp    .Lset_nz_finish
.LopAA:
    ld.w  %r5, %r4
    ld.w  %r12, %r5
    add   %r10, 2
    jp    .Lset_nz_finish
.Lop8A:
    ld.w  %r4, %r5
    ld.w  %r12, %r4
    add   %r10, 2
    jp    .Lset_nz_finish
.LopA8:
    ld.w  %r6, %r4
    ld.w  %r12, %r6
    add   %r10, 2
    jp    .Lset_nz_finish
.Lop98:
    ld.w  %r4, %r6
    ld.w  %r12, %r4
    add   %r10, 2
    jp    .Lset_nz_finish
.LopC8:
    add   %r6, 1
    ld.ub %r6, %r6
    ld.w  %r12, %r6
    add   %r10, 2
    jp    .Lset_nz_finish
.Lop88:
    sub   %r6, 1
    ld.ub %r6, %r6
    ld.w  %r12, %r6
    add   %r10, 2
    jp    .Lset_nz_finish
.LopCA:
    sub   %r5, 1
    ld.ub %r5, %r5
    ld.w  %r12, %r5
    add   %r10, 2
    jp    .Lset_nz_finish
.LopE8:
    add   %r5, 1
    ld.ub %r5, %r5
    ld.w  %r12, %r5
    add   %r10, 2
    jp    .Lset_nz_finish

/* PHA/PHP/PLA/PLP use the directly mapped guest stack page. */
.Lop48:
    ld.w  %r12, %r14
    ext   256
    add   %r12, %r12
    add   %r12, %r7
    ld.b  [%r12], %r4
    sub   %r7, 1
    ld.ub %r7, %r7
    add   %r10, 3
    jp    .Lfinish
.Lop08:
    /* PHP exposes the complete architectural status byte on the guest stack. */
    call  .Lmaterialize_nz
    ld.w  %r13, %r9
    ext   48
    or    %r13, %r13
    ld.w  %r12, %r14
    ext   256
    add   %r12, %r12
    add   %r12, %r7
    ld.b  [%r12], %r13
    sub   %r7, 1
    ld.ub %r7, %r7
    add   %r10, 3
    jp    .Lfinish
.Lop68:
    add   %r7, 1
    ld.ub %r7, %r7
    ld.w  %r12, %r14
    ext   256
    add   %r12, %r12
    add   %r12, %r7
    ld.ub %r4, [%r12]
    ld.w  %r12, %r4
    add   %r10, 4
    jp    .Lset_nz_finish
.Lop28:
    add   %r7, 1
    ld.ub %r7, %r7
    ld.w  %r12, %r14
    ext   256
    add   %r12, %r12
    add   %r12, %r7
    ld.ub %r9, [%r12]
    ext   48
    or    %r9, %r9
    add   %r10, 4
    jp    .Lfinish

/* Binary ADC/SBC.  Decimal mode is deliberately delegated to slow C. */
.Lop69:
    ld.w  %r12, %r9
    and   %r12, FLAG_D
    cmp   %r12, 0
    jrne  .Lexit_slow1
    FETCH8
    add   %r10, 2
    jp    .Lbinary_add
.LopE9:
    ld.w  %r12, %r9
    and   %r12, FLAG_D
    cmp   %r12, 0
    jrne  .Lexit_slow1
    FETCH8
    ext   255
    xor   %r12, %r12
    add   %r10, 2
    jp    .Lbinary_add
.Lop6D:
    ld.w  %r12, %r9
    and   %r12, FLAG_D
    cmp   %r12, 0
    jrne  .Lexit_slow1
    FETCH16 .Lexit_slow1
    MAP_READ_VALUE .Lexit_slow3
    add   %r10, 4
    jp    .Lbinary_add
.LopED:
    ld.w  %r12, %r9
    and   %r12, FLAG_D
    cmp   %r12, 0
    jrne  .Lexit_slow1
    FETCH16 .Lexit_slow1
    MAP_READ_VALUE .Lexit_slow3
    ext   255
    xor   %r12, %r12
    add   %r10, 4
    jp    .Lbinary_add
.Lop65:
    ld.w  %r12, %r9
    and   %r12, FLAG_D
    cmp   %r12, 0
    jrne  .Lexit_slow1
    FETCH8
    READ_ZP_VALUE .Lexit_slow2
    add   %r10, 3
    jp    .Lbinary_add
.LopE5:
    ld.w  %r12, %r9
    and   %r12, FLAG_D
    cmp   %r12, 0
    jrne  .Lexit_slow1
    FETCH8
    READ_ZP_VALUE .Lexit_slow2
    ext   255
    xor   %r12, %r12
    add   %r10, 3

/* Operand (or complemented SBC operand) is in R12. */
.Lbinary_add:
    SAVE_FETCH
.Lbinary_add_saved:
    ld.w  %r2, %r12               /* original arithmetic operand */
    ld.w  %r13, %r9
    and   %r13, FLAG_C
    add   %r12, %r4
    add   %r12, %r13              /* full 9-bit result */
    ext   60                       /* clear N/V/Z/C */
    and   %r9, %r9

    ext   255
    cmp   %r12, %r12
    jrule .Ladd_no_carry
    or    %r9, FLAG_C
.Ladd_no_carry:
    ld.w  %r13, %r12
    ld.ub %r13, %r13
    ld.w  %r3, %r4
    xor   %r3, %r13
    xor   %r2, %r13
    and   %r3, %r2
    ext   128
    and   %r3, %r3
    srl   %r3, 1
    or    %r9, %r3
    ld.w  %r4, %r13
    ld.w  %r12, %r4
    jp    .Lset_nz_restore_fetch

/* CMP/CPX/CPY share one unsigned subtract flag tail. */
.LopC0:
    FETCH8
    add   %r10, 2
    ld.w  %r13, %r6
    jp    .Lcompare_value
.LopC9:
    FETCH8
    add   %r10, 2
    ld.w  %r13, %r4
    jp    .Lcompare_value
.LopCD:
    FETCH16 .Lexit_slow1
    MAP_READ_VALUE .Lexit_slow3
    add   %r10, 4
    ld.w  %r13, %r4
    jp    .Lcompare_value
.LopC5:
    FETCH8
    READ_ZP_VALUE .Lexit_slow2
    add   %r10, 3
    ld.w  %r13, %r4
    jp    .Lcompare_value
.LopE0:
    FETCH8
    add   %r10, 2
    ld.w  %r13, %r5
    jp    .Lcompare_value
.LopC4:
    FETCH8
    READ_ZP_VALUE .Lexit_slow2
    add   %r10, 3
    ld.w  %r13, %r6
.Lcompare_value:
    ext   254                      /* clear carry; N/Z tail follows */
    and   %r9, %r9
    cmp   %r13, %r12
    jrult .Lcmp_no_carry
    or    %r9, FLAG_C
.Lcmp_no_carry:
    sub   %r13, %r12
    ld.w  %r12, %r13
    jp    .Lset_nz_finish

/* ASL/ROL/LSR/ROR/DEC/INC read-modify-write family.  R3 packs op:cycles. */
.Lop06:
    FETCH8
    ld.w  %r3, 5                    /* ASL, 5 cycles */
    jp    .Lrmw_zp_map
.Lop0E:
    FETCH16 .Lexit_slow1
    ld.w  %r3, 6                    /* ASL, 6 cycles */
    jp    .Lrmw_abs_map
.Lop2E:
    FETCH16 .Lexit_slow1
    ext   4
    ld.w  %r3, 6                    /* ROL, 6 cycles */
    jp    .Lrmw_abs_map
.LopCE:
    FETCH16 .Lexit_slow1
    ext   8
    ld.w  %r3, 6                    /* DEC, 6 cycles */
    jp    .Lrmw_abs_map
.Lop4E:
    FETCH16 .Lexit_slow1
    ext   12
    ld.w  %r3, 6                    /* LSR, 6 cycles */
    jp    .Lrmw_abs_map
.Lop6E:
    FETCH16 .Lexit_slow1
    ext   16
    ld.w  %r3, 6                    /* ROR, 6 cycles */
    jp    .Lrmw_abs_map
.LopE6:
    FETCH8
    ext   20
    ld.w  %r3, 5                    /* INC, 5 cycles */
    jp    .Lrmw_zp_map
.LopEE:
    FETCH16 .Lexit_slow1
    ext   20
    ld.w  %r3, 6                    /* INC, 6 cycles */
    jp    .Lrmw_abs_map
.Lop26:
    FETCH8
    ext   4
    ld.w  %r3, 5                    /* ROL, 5 cycles */
    jp    .Lrmw_zp_map

#ifdef GAM4980_IRAM_STATIC_COMPACT
    /* The static native bridge consumes a small fixed part of the 5.8 KiB
     * overlay.  These uncommon indexed RMW forms remain exact through the C
     * fallback, while linked game banks execute them natively. */
    .set .Lop1E, .Lslow_opcode
    .set .Lop3E, .Lslow_opcode
    .set .LopDE, .Lslow_opcode
#else
.Lop1E:
    FETCH16 .Lexit_slow1
    ld.w  %r3, 6                    /* ASL abs,X, 6 + page cross */
    jp    .Lrmw_abs_x_cross
.Lop3E:
    FETCH16 .Lexit_slow1
    ext   4
    ld.w  %r3, 6                    /* ROL abs,X, 6 + page cross */
.Lrmw_abs_x_cross:
    SAVE_FETCH
    ld.w  %r13, %r12               /* base */
    add   %r12, %r5
    ld.uh %r12, %r12
    ld.w  %r2, %r13
    xor   %r2, %r12
    srl   %r2, 8
    cmp   %r2, 0
    jreq  .Lrmw_abs_x_map
    add   %r3, 1
    jp    .Lrmw_abs_x_map
.LopDE:
    FETCH16 .Lexit_slow1
    ext   8
    ld.w  %r3, 7                    /* DEC abs,X, fixed 7 cycles */
    SAVE_FETCH
    add   %r12, %r5
    ld.uh %r12, %r12
.Lrmw_abs_x_map:
    jp    .Lrmw_abs_map_saved
#endif

.Lrmw_zp_map:
    SAVE_FETCH
    MAP_WRITE_POINTER .Lexit_slow2
    jp    .Lrmw_mapped
.Lrmw_abs_map:
    SAVE_FETCH
.Lrmw_abs_map_saved:
    MAP_WRITE_POINTER .Lexit_slow3

.Lrmw_mapped:
    ld.w  %r2, %r3
    ext   255
    and   %r2, %r2
    add   %r10, %r2
    srl   %r3, 8
    cmp   %r3, 0
    jreq  .Lrmw_asl
    cmp   %r3, 1
    jreq  .Lrmw_rol
    cmp   %r3, 2
    jreq  .Lrmw_dec
    cmp   %r3, 3
    jreq  .Lrmw_lsr
    cmp   %r3, 4
    jreq  .Lrmw_ror
    jp    .Lrmw_inc

/* ROL memory. */
.Lrmw_rol:
    ld.ub %r12, [%r13]
    ld.w  %r3, %r9
    and   %r3, FLAG_C              /* old carry */
    ld.w  %r2, %r12
    ext   128
    and   %r2, %r2
    srl   %r2, 7                   /* new carry */
    ext   254
    and   %r9, %r9
    or    %r9, %r2
    sll   %r12, 1
    or    %r12, %r3
    ld.ub %r12, %r12
    ld.w  %r3, %r12
    STORE_MAPPED_VALUE 1
    ld.w  %r12, %r3
    jp    .Lset_nz_restore_fetch

/* ASL memory. */
.Lrmw_asl:
    ld.ub %r12, [%r13]
    ld.w  %r2, %r12
    ext   128
    and   %r2, %r2
    srl   %r2, 7
    ext   254
    and   %r9, %r9
    or    %r9, %r2
    sll   %r12, 1
    ld.ub %r12, %r12
    ld.w  %r3, %r12
    STORE_MAPPED_VALUE 1
    ld.w  %r12, %r3
    jp    .Lset_nz_restore_fetch

/* LSR memory. */
.Lrmw_lsr:
    ld.ub %r12, [%r13]
    ld.w  %r2, %r12
    and   %r2, 1
    ext   254
    and   %r9, %r9
    or    %r9, %r2
    srl   %r12, 1
    ld.w  %r3, %r12
    STORE_MAPPED_VALUE 1
    ld.w  %r12, %r3
    jp    .Lset_nz_restore_fetch

/* ROR memory. */
.Lrmw_ror:
    ld.ub %r12, [%r13]
    ld.w  %r3, %r9
    and   %r3, FLAG_C
    sll   %r3, 7                    /* old carry enters bit 7 */
    ld.w  %r2, %r12
    and   %r2, 1                   /* bit 0 becomes carry */
    ext   254
    and   %r9, %r9
    or    %r9, %r2
    srl   %r12, 1
    or    %r12, %r3
    ld.ub %r12, %r12
    ld.w  %r3, %r12
    STORE_MAPPED_VALUE 1
    ld.w  %r12, %r3
    jp    .Lset_nz_restore_fetch

/* DEC memory does not alter carry. */
.Lrmw_dec:
    ld.ub %r12, [%r13]
    sub   %r12, 1
    ld.ub %r12, %r12
    ld.w  %r3, %r12
    STORE_MAPPED_VALUE 1
    ld.w  %r12, %r3
    jp    .Lset_nz_restore_fetch

/* INC memory does not alter carry. */
.Lrmw_inc:
    ld.ub %r12, [%r13]
    add   %r12, 1
    ld.ub %r12, %r12
    ld.w  %r3, %r12
    STORE_MAPPED_VALUE 1
    ld.w  %r12, %r3
    jp    .Lset_nz_restore_fetch

/* Cheap second-tier neighbours. */
.Lop78:
    or    %r9, FLAG_I
    add   %r10, 2
    jp    .Lfinish
.LopEA:
    add   %r10, 2
    jp    .Lfinish
.LopB0:
    ld.w  %r13, %r9
    and   %r13, FLAG_C
    cmp   %r13, 0
    jrne  .Lbranch_taken
    jp    .Lbranch_not_taken
.Lop10:
    BRANCH_NEGATIVE jreq

/* Every unsupported table entry lands here immediately after opcode fetch. */
.Lslow_opcode:
    jp    .Lexit_slow1

.Ls6502_iram_exec_burst_asm_end:
    .size s6502_iram_exec_burst_asm, \
        .Ls6502_iram_exec_burst_asm_end-s6502_iram_exec_burst_asm

/*
 * LLVM deliberately rejects a relocatable `.short symbol` for S1C33.  All
 * handlers and the entry symbol are in this section, however, so their
 * difference is resolved by MC without a relocation.  S1C33's MC backend
 * also does not apply resolved 16-bit data fixups, so each pair is packed in
 * one supported 32-bit data word.  The linker contract fixes this engine's
 * VMA at 0x800; adding it produces the two absolute 16-bit IRAM handler
 * addresses consumed separately by ld.uh in the dispatch paths.
 */
    .macro DISPATCH16 a, b, c, d
    .long ((\a - s6502_iram_exec_burst_asm + 0x800) & 0xffff) | \
        ((\b - s6502_iram_exec_burst_asm + 0x800) << 16)
    .long ((\c - s6502_iram_exec_burst_asm + 0x800) & 0xffff) | \
        ((\d - s6502_iram_exec_burst_asm + 0x800) << 16)
    .endm

    .p2align 2
    .globl s6502_iram_dispatch_table
    .type s6502_iram_dispatch_table,@object
s6502_iram_dispatch_table:
    /* 00 */ DISPATCH16 .Lslow_opcode, .Lslow_opcode, .Lsuper, .Lslow_opcode
    /* 04 */ DISPATCH16 .Lslow_opcode, .Lop05, .Lop06, .Lslow_opcode
    /* 08 */ DISPATCH16 .Lop08, .Lop09, .Lop0A, .Lslow_opcode
    /* 0c */ DISPATCH16 .Lslow_opcode, .Lop0D, .Lop0E, .Lslow_opcode
    /* 10 */ DISPATCH16 .Lop10, .Lslow_opcode, .Lslow_opcode, .Lslow_opcode
    /* 14 */ DISPATCH16 .Lslow_opcode, .Lslow_opcode, .Lslow_opcode, .Lslow_opcode
    /* 18 */ DISPATCH16 .Lop18, .Lslow_opcode, .Lslow_opcode, .Lslow_opcode
    /* 1c */ DISPATCH16 .Lslow_opcode, .Lslow_opcode, .Lop1E, .Lslow_opcode
    /* 20 */ DISPATCH16 .Lop20, .Lslow_opcode, .Lslow_opcode, .Lslow_opcode
    /* 24 */ DISPATCH16 .Lslow_opcode, .Lop25, .Lop26, .Lslow_opcode
    /* 28 */ DISPATCH16 .Lop28, .Lop29, .Lop2A, .Lslow_opcode
    /* 2c */ DISPATCH16 .Lslow_opcode, .Lop2D, .Lop2E, .Lslow_opcode
    /* 30 */ DISPATCH16 .Lop30, .Lslow_opcode, .Lslow_opcode, .Lslow_opcode
    /* 34 */ DISPATCH16 .Lslow_opcode, .Lslow_opcode, .Lslow_opcode, .Lslow_opcode
    /* 38 */ DISPATCH16 .Lop38, .Lslow_opcode, .Lslow_opcode, .Lslow_opcode
    /* 3c */ DISPATCH16 .Lslow_opcode, .Lslow_opcode, .Lop3E, .Lslow_opcode
    /* 40 */ DISPATCH16 .Lslow_opcode, .Lslow_opcode, .Lslow_opcode, .Lslow_opcode
    /* 44 */ DISPATCH16 .Lslow_opcode, .Lslow_opcode, .Lslow_opcode, .Lslow_opcode
    /* 48 */ DISPATCH16 .Lop48, .Lop49, .Lop4A, .Lslow_opcode
    /* 4c */ DISPATCH16 .Lop4C, .Lslow_opcode, .Lop4E, .Lslow_opcode
    /* 50 */ DISPATCH16 .Lop50, .Lslow_opcode, .Lslow_opcode, .Lslow_opcode
    /* 54 */ DISPATCH16 .Lslow_opcode, .Lslow_opcode, .Lslow_opcode, .Lslow_opcode
    /* 58 */ DISPATCH16 .Lslow_opcode, .Lslow_opcode, .Lslow_opcode, .Lslow_opcode
    /* 5c */ DISPATCH16 .Lslow_opcode, .Lslow_opcode, .Lslow_opcode, .Lslow_opcode
    /* 60 */ DISPATCH16 .Lop60, .Lslow_opcode, .Lslow_opcode, .Lslow_opcode
    /* 64 */ DISPATCH16 .Lslow_opcode, .Lop65, .Lslow_opcode, .Lslow_opcode
    /* 68 */ DISPATCH16 .Lop68, .Lop69, .Lop6A, .Lslow_opcode
    /* 6c */ DISPATCH16 .Lslow_opcode, .Lop6D, .Lop6E, .Lslow_opcode
    /* 70 */ DISPATCH16 .Lslow_opcode, .Lop71, .Lslow_opcode, .Lslow_opcode
    /* 74 */ DISPATCH16 .Lslow_opcode, .Lslow_opcode, .Lslow_opcode, .Lslow_opcode
    /* 78 */ DISPATCH16 .Lop78, .Lslow_opcode, .Lslow_opcode, .Lslow_opcode
    /* 7c */ DISPATCH16 .Lslow_opcode, .Lop7D, .Lslow_opcode, .Lslow_opcode
    /* 80 */ DISPATCH16 .Lslow_opcode, .Lslow_opcode, .Lslow_opcode, .Lslow_opcode
    /* 84 */ DISPATCH16 .Lop84, .Lop85, .Lop86, .Lslow_opcode
    /* 88 */ DISPATCH16 .Lop88, .Lslow_opcode, .Lop8A, .Lslow_opcode
    /* 8c */ DISPATCH16 .Lslow_opcode, .Lop8D, .Lop8E, .Lslow_opcode
    /* 90 */ DISPATCH16 .Lop90, .Lop91, .Lslow_opcode, .Lslow_opcode
    /* 94 */ DISPATCH16 .Lslow_opcode, .Lslow_opcode, .Lslow_opcode, .Lslow_opcode
    /* 98 */ DISPATCH16 .Lop98, .Lslow_opcode, .Lslow_opcode, .Lslow_opcode
    /* 9c */ DISPATCH16 .Lslow_opcode, .Lop9D, .Lslow_opcode, .Lslow_opcode
    /* a0 */ DISPATCH16 .LopA0, .Lslow_opcode, .LopA2, .Lslow_opcode
    /* a4 */ DISPATCH16 .LopA4, .LopA5, .LopA6, .Lslow_opcode
    /* a8 */ DISPATCH16 .LopA8, .LopA9, .LopAA, .Lslow_opcode
    /* ac */ DISPATCH16 .LopAC, .LopAD, .LopAE, .Lslow_opcode
    /* b0 */ DISPATCH16 .LopB0, .LopB1, .Lslow_opcode, .Lslow_opcode
    /* b4 */ DISPATCH16 .Lslow_opcode, .Lslow_opcode, .Lslow_opcode, .Lslow_opcode
    /* b8 */ DISPATCH16 .Lslow_opcode, .LopB9, .Lslow_opcode, .Lslow_opcode
    /* bc */ DISPATCH16 .Lslow_opcode, .LopBD, .Lslow_opcode, .Lslow_opcode
    /* c0 */ DISPATCH16 .LopC0, .Lslow_opcode, .Lslow_opcode, .Lslow_opcode
    /* c4 */ DISPATCH16 .LopC4, .LopC5, .Lslow_opcode, .Lslow_opcode
    /* c8 */ DISPATCH16 .LopC8, .LopC9, .LopCA, .Lslow_opcode
    /* cc */ DISPATCH16 .Lslow_opcode, .LopCD, .LopCE, .Lslow_opcode
    /* d0 */ DISPATCH16 .LopD0, .Lslow_opcode, .Lslow_opcode, .Lslow_opcode
    /* d4 */ DISPATCH16 .Lslow_opcode, .Lslow_opcode, .Lslow_opcode, .Lslow_opcode
    /* d8 */ DISPATCH16 .Lslow_opcode, .Lslow_opcode, .Lslow_opcode, .Lslow_opcode
    /* dc */ DISPATCH16 .Lslow_opcode, .Lslow_opcode, .LopDE, .Lslow_opcode
    /* e0 */ DISPATCH16 .LopE0, .Lslow_opcode, .Lslow_opcode, .Lslow_opcode
    /* e4 */ DISPATCH16 .Lslow_opcode, .LopE5, .LopE6, .Lslow_opcode
    /* e8 */ DISPATCH16 .LopE8, .LopE9, .LopEA, .Lslow_opcode
    /* ec */ DISPATCH16 .Lslow_opcode, .LopED, .LopEE, .Lslow_opcode
    /* f0 */ DISPATCH16 .LopF0, .LopF1, .Lslow_opcode, .Lslow_opcode
    /* f4 */ DISPATCH16 .Lslow_opcode, .Lslow_opcode, .Lslow_opcode, .Lslow_opcode
    /* f8 */ DISPATCH16 .Lslow_opcode, .Lslow_opcode, .Lslow_opcode, .Lslow_opcode
    /* fc */ DISPATCH16 .Lslow_opcode, .LopFD, .Lslow_opcode, .Lslow_opcode
s6502_iram_dispatch_table_end:
    .size s6502_iram_dispatch_table, \
        s6502_iram_dispatch_table_end-s6502_iram_dispatch_table

/*
 * Byte offsets of the instruction-count additions.  The installer
 * replaces these aligned 16-bit instructions with S1C33 NOPs when diagnostics
 * are disabled.  V2 has no legacy shadow phrases, so it carries only the four
 * common patch sites and avoids their dead code and table entries.
 */
    .p2align 2
    .globl s6502_iram_count_patch_offsets
    .type s6502_iram_count_patch_offsets,@object
s6502_iram_count_patch_offsets:
    .long s6502_iram_count_patch_finish - s6502_iram_exec_burst_asm
    .long s6502_iram_count_patch_remap - s6502_iram_exec_burst_asm
    .long s6502_iram_count_patch_reload - s6502_iram_exec_burst_asm
    .long s6502_iram_count_patch_control - s6502_iram_exec_burst_asm
#if defined(GAM4980_DYNAMIC_NATIVE_ALL) || defined(GAM4980_STATIC_NATIVE_GAME)
    .long s6502_iram_count_patch_native_chain - s6502_iram_exec_burst_asm
#endif
#ifndef GAM4980_IRAM_V2
    .long s6502_iram_count_patch_super_imm16 - s6502_iram_exec_burst_asm
    .long s6502_iram_count_patch_super_stack16 - s6502_iram_exec_burst_asm
    .long s6502_iram_count_patch_super_add16 - s6502_iram_exec_burst_asm
#endif
    .size s6502_iram_count_patch_offsets, .-s6502_iram_count_patch_offsets

#if defined(GAM4980_ENABLE_AOT) || defined(GAM4980_NATIVE_PROFILE_TEST)
    /* External RAM only; selected by the sampled dispatch table. The normal
     * IRAM call site and non-sampled private ABI path are unchanged. */
    .section .text.gam4980_native_profile_trampoline,"ax",@progbits
    .p2align 2
    .globl gam4980_native_profile_trampoline
    .type gam4980_native_profile_trampoline,@function
gam4980_native_profile_trampoline:
    sub %sp, 12
    pushn %r15
    ld.w %r6, %r0
    ld.w %r7, %r8
    ld.w %r8, %sp
    add %r8, 32
    add %r8, 32
    call gam4980_native_profile_begin
    popn %r15
    ld.w %r13, %sp
    ld.w %r13, [%r13]
    call %r13
    pushn %r15
    ld.w %r6, %sp
    add %r6, 32
    add %r6, 32
    ld.w %r7, %r12
    call gam4980_native_profile_end
    popn %r15
    add %sp, 12
    ret
    .size gam4980_native_profile_trampoline, .-gam4980_native_profile_trampoline
#endif
    .section ".note.GNU-stack","",@progbits
