"""C-to-private-register entry lowering for the ABI-6 firmware package.

The compiler must inline and scalar-replace the local context. Merely wrapping
the original out-of-line C entry is explicitly forbidden by the generated code.
Callbacks still use the normal C ABI; the compiler preserves live guest values
over those callbacks. This is a register *boundary* ABI, not fixed allocation
of every guest register throughout the function body.
"""
import re
import json

FIELDS = ('ac', 'ix', 'iy', 'sp', 'pc', 'status', 'cycles', 'instructions')
CONTEXT_FIELDS = (
    'pc', 'ac', 'ix', 'iy', 'sp', 'status', 'cycle_budget', 'pages',
    'page_kind', 'ram', 'dirty', 'dispatch_bits', 'cycles', 'instructions',
    'control_transitions', 'exit_reason', 'code_pages', 'super_hits',
    'native_shared_entry', 'native_shared_metrics', 'read8', 'write8',
    'native_epoch', 'lcd_write_calls', 'lcd_changed_writes', 'saved_fetch',
    'graphics', 'register_entries', 'register_banks', 'register_calls',
)


def bridge_source(symbol, payload_path, register_offset, *, external_target=None,
                  record_metrics=False):
    """C ABI adapter and one existing private body in a single PIC section.

    R6 is the C argument, R4 the return. Return guest cycles, not the private
    hand-written leaves' boolean acceptance value. No absolute code pointer.
    """
    registers = dict(zip(FIELDS, range(4, 12)))
    lines = [f'.section .text.{symbol},"ax",@progbits', '.p2align 2',
             f'.globl {symbol}', f'.type {symbol},@function', f'{symbol}:',
             'pushn %r3', 'ld.w %r0, %r6']
    for field, register in registers.items():
        offset = CONTEXT_FIELDS.index(field) * 4
        if offset: lines.append(f'ext {offset}')
        lines.append(f'ld.w %r{register}, [%r0]')
    lines += ['ext 36', 'ld.w %r14, [%r0]',
              'call .Lprivate_entry', 'cmp %r12, 0', 'jreq .Lreject',
              'ext 48', 'ld.w %r12, [%r0]',
              'ld.w %r13, %r10', 'sub %r13, %r12']
    for field, register in registers.items():
        offset = CONTEXT_FIELDS.index(field) * 4
        if offset: lines.append(f'ext {offset}')
        lines.append(f'ld.w [%r0], %r{register}')
    if record_metrics:
        # The seven hand-written private leaves deliberately omit FW_RECORD.
        # Preserve its original C-entry diagnostic contract in the adapter.
        lines += ['ext 76', 'ld.w %r12, [%r0]', 'cmp %r12, 0',
                  'jreq .Lmetrics_done', 'ext 36', 'ld.w %r2, [%r12]',
                  'cmp %r2, 0', 'jreq .Lmetrics_done',
                  'ld.w %r2, [%r12]', 'add %r2, 1', 'ld.w [%r12], %r2',
                  'ext 4', 'ld.w %r2, [%r12]', 'add %r2, 1',
                  'ext 4', 'ld.w [%r12], %r2',
                  'ext 8', 'ld.w %r2, [%r12]', 'add %r2, %r13',
                  'ext 8', 'ld.w [%r12], %r2',
                  'ext 24', 'ld.w %r2, [%r12]', 'cmp %r2, 0',
                  'jrne .Lmetrics_done', 'ld.w %r2, 1',
                  'ext 24', 'ld.w [%r12], %r2', '.Lmetrics_done:']
    lines += ['ld.w %r4, %r13', 'popn %r3', 'ret', '.Lreject:',
              'ld.w %r4, 0', 'popn %r3', 'ret',
              f'.size {symbol}, .-{symbol}']
    if external_target:
        lines += [f'.set .Lprivate_entry, {external_target}']
    else:
        lines += ['.p2align 2', '.Lpayload:',
                  f'.incbin {json.dumps(str(payload_path).replace(chr(92), "/"))}',
                  f'.set .Lprivate_entry, .Lpayload+{register_offset}',
                  f'.globl {symbol}_register',
                  f'.set {symbol}_register, .Lprivate_entry']
    return '\n'.join(lines)+'\n'


def lower(source, symbol):
    implementation = symbol + '_body'
    # Retain the verified C entry as differential reference and safe fallback.
    body = re.sub(r'\b' + re.escape(symbol) + r'\b', implementation, source)
    body = re.sub(r'used\s*,\s*noinline\s*,', 'always_inline,', body)
    body = body.replace('uint32_t ' + implementation + '(', 'static inline uint32_t ' + implementation + '(')
    if 'noinline' in body:
        raise ValueError('unhandled noinline entry')
    names = [f'g{i}' for i in range(8)]
    declarations = '\n'.join(f'    register uint32_t {n} __asm__("r{i+4}");' for i,n in enumerate(names))
    outputs = ', '.join(f'"=r"({n})' for n in names + ['origin', 'saved_ram'])
    inputs = ', '.join(f'"r"({n})' for n in names + ['accepted', 'saved_ram'])
    imports = '\n'.join(f'    local.{field} = {name};' for field,name in zip(FIELDS,names))
    exports = '\n'.join(f'    {name} = local.{field};' for field,name in zip(FIELDS,names))
    # Do not copy stale guest state only to overwrite it from registers. Keep
    # every other field initialized; included graphics helpers may observe it.
    environment = '\n'.join(f'    local.{field} = origin->{field};'
                            for field in CONTEXT_FIELDS if field not in FIELDS)
    # Text has an escaping context and different allocator pressure: explicit
    # field copies increased its two largest target bodies by 824 bytes in
    # the audited build. Keep its previous lowering until measured otherwise.
    if symbol.startswith('firmware_native_text_'):
        environment = '    __builtin_memcpy_inline(&local, origin, sizeof(local));'
    return body + f'''
__attribute__((used,noinline,section(".text.{symbol}_body")))
void {symbol}_register(void)
{{
{declarations}
    register s6502_iram_asm_context_t *origin __asm__("r0");
    register uint32_t saved_ram __asm__("r14");
    register uint32_t accepted __asm__("r12");
    s6502_iram_asm_context_t local;
    __asm__ volatile("" : {outputs});
{environment}
{imports}
    accepted = {implementation}(&local);
{exports}
    __asm__ volatile("" : : {inputs} : "memory");
}}
'''
