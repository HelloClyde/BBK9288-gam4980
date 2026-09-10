"""Atomic policy for audited, bounded complete NAT function contracts.

Never enlarge a context budget or pretend a partial entry returned to its caller.
Keep the original computation, memory order, ABI/flags, and address guards;
remove only the old instruction-cost model before compiling native code.
Unrecognized source forms fail closed at build time.
"""
import re

FAMILIES=frozenset(('runtime','memory','string','strchr','strcmp','multiply',
                    'divide','compare','conversion','float','float_add'))

def eligible(name,entry):
    # D572 is an indirect jump adapter, not a completed function return.
    return name in FAMILIES and not (name=='runtime' and entry==0xd572)

def lower(source,name,entry):
    if not eligible(name,entry):return source
    source=source.replace('cost>c->cycle_budget-c->cycles','0')
    source=source.replace('c->cycles>c->cycle_budget','0')
    source=source.replace('(c->cycle_budget-c->cycles)/147u','65535u')
    if 'cycle_budget' in source:
        raise ValueError(f'{name}/{entry:04x}: unaudited budget expression')
    source,n=re.subn(r'c->cycles\s*\+=\s*cost\s*;', 'c->cycles+=6u;',source)
    if not n:raise ValueError('missing atomic cycle commit')
    source=source.replace('FW_RECORD(c,cost)','FW_RECORD(c,6u)')
    source,n=re.subn(r'\breturn\s+cost\s*;', 'return 6u;',source)
    if not n:raise ValueError('missing atomic return')
    signature=r'(uint32_t\s+firmware_native_\w+\s*\(s6502_iram_asm_context_t\s*\*c\)\s*\{)'
    source,n=re.subn(signature,lambda m:m[0]+'''
    /* Atomic complete-function contract; no internal guest deadline. */
    if(c->cycles>c->cycle_budget || c->cycle_budget-c->cycles<6u)return 0u;
''',source)
    if n!=1:raise ValueError('missing unique atomic entry')
    return source
