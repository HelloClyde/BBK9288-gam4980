"""Leaf S1C33 entries sharing the IRAM guest-register ABI (no C bridge).

R4..R11 are A/X/Y/SP/PC/P/cycles/instructions. R0/R1/R14/R15 preserved;
R2/R3/R12/R13 scratch. R12=1 accepted, 0 unchanged guest state.
Only guarded, nonbanking, callback-free runtime contracts belong here.
"""
import json

ENTRIES = {0xd586, 0xd596, 0xd5a6, 0xd5b6, 0xdde4, 0xddee, 0xdda7}


def source(symbol, pc, *, atomic=False):
    if pc not in ENTRIES:
        return ''
    label = symbol + '_register'
    body = f'''
.section .text.{symbol},"ax",@progbits
.p2align 2
.globl {label}
.type {label},@function
{label}:
ld.w %r12, %r9
and %r12, 8
cmp %r12, 0
jrne 9f
'''
    if pc == 0xdde4:
        body += '''ld.w %r3, 12
cmp %r4, 0
jreq 1f
add %r3, 1
1:
'''
    elif pc == 0xddee:
        body += '''ext 32
ld.ub %r12, [%r14]
ext 33
ld.ub %r13, [%r14]
or %r12, %r13
ld.w %r3, 25
cmp %r12, 0
jrne 1f
add %r3, 2
1:
'''
    elif pc == 0xdda7:
        body += 'ld.w %r3, 30\n'
    else:
        body += 'ld.w %r3, 31\n'
    if atomic:
        body += 'ld.w %r3, 6\n'
    body += '''ext 24
ld.w %r13, [%r0]
cmp %r10, %r13
jrugt 9f
sub %r13, %r10
cmp %r3, %r13
jrugt 9f
'''
    if pc == 0xdde4:
        body += '''cmp %r4, 0
ld.w %r4, 0
jrne 2f
ld.w %r4, 1
2:
ld.w %r12, %r4
'''
    elif pc == 0xddee:
        body += '''cmp %r12, 0
ld.w %r13, 0
jrne 2f
ld.w %r13, 1
2:
ext 32
ld.b [%r14], %r13
ld.w %r4, 0
ext 33
ld.b [%r14], %r4
ld.w %r12, 0
'''
    elif pc == 0xdda7:
        body += '''ext 32
ld.ub %r12, [%r14]
ext 33
ld.ub %r4, [%r14]
ext 35
ld.ub %r13, [%r14]
ext 36
ld.ub %r5, [%r14]
ext 32
ld.b [%r14], %r13
ext 33
ld.b [%r14], %r5
ext 35
ld.b [%r14], %r12
ext 36
ld.b [%r14], %r4
ld.w %r12, %r5
'''
    else:
        add, dest = {0xd586:(16,32),0xd596:(8,32),0xd5a6:(32,35),0xd5b6:(24,35)}[pc]
        body += f'''ld.w %r13, %r14
ext 256
add %r13, %r13
add %r13, %r7
ld.b [%r13], %r4
ext 42
ld.ub %r13, [%r14]
ext 43
ld.ub %r2, [%r14]
ld.w %r12, %r2
sll %r12, 8
or %r13, %r12
ext {add}
add %r13, %r13
ext {dest}
ld.b [%r14], %r13
ld.w %r12, %r13
srl %r12, 8
ext {dest+1}
ld.b [%r14], %r12
xor %r2, %r12
and %r2, %r12
ext 128
and %r2, %r2
srl %r2, 1
ext 60
and %r9, %r9
or %r9, %r2
srl %r13, 8
srl %r13, 8
or %r9, %r13
ld.w %r12, %r4
'''
    # Publish conventional P before returning; no latent NZ escapes to C.
    body += '''ext 125
and %r9, %r9
cmp %r12, 0
jrne 3f
or %r9, 2
3:
ext 128
and %r12, %r12
or %r9, %r12
add %r7, 1
ld.ub %r7, %r7
ld.w %r13, %r14
ext 256
add %r13, %r13
add %r13, %r7
ld.ub %r8, [%r13]
add %r7, 1
ld.ub %r7, %r7
ld.w %r13, %r14
ext 256
add %r13, %r13
add %r13, %r7
ld.ub %r12, [%r13]
sll %r12, 8
or %r8, %r12
add %r8, 1
ld.uh %r8, %r8
add %r10, %r3
ld.w %r12, 1
ret
9:
ld.w %r12, 0
ret
'''
    body += f'.size {label}, .-{label}\n'
    return '\n__asm__(' + json.dumps(body) + ');\n'
