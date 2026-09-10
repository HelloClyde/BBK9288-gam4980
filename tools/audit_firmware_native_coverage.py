"""Inventory SDK symbols separately from verified native replacement entries.

No inferred coverage: a compiler bridge case or a registered suffix is not a
verified, complete public API implementation. ROM code/data classification
must be established before resource-only mode can be enabled.
"""
import argparse
import ast
import json
from pathlib import Path
import re
from recompile_c6502_frontend import firmware_table_symbols

ROOT = Path(__file__).resolve().parents[1]


def resolve_public_entries(report, rom):
    """Resolve ROM table bytes, without treating an address as a verified ABI."""
    import hashlib
    if hashlib.sha256(rom).hexdigest() != '9d13aa4593d97b790afc37d73da8be985e7a3aa7f3dcfe6b91c798671067aa5e':
        raise ValueError('unsupported firmware for public table mapping')
    for row in report['public_apis']:
        table = row['sdk_table_address']
        offset = 0xa8000 + table - 0xd000
        a,b,bank = rom[offset:offset+3]
        if row['name'] in {'SysMemcpy','SysMemcmp','SysReadCom','SysWriteCom'} and a == 0x4c:
            target = b | bank << 8
            physical = 0xea8000 + target - 0xd000 if 0xd000 <= target <= 0xffff else None
            kind = 'direct_jmp'
        else:
            target = a | b << 8
            physical = ((0xea0 + bank*4 + (target>>12)-5)<<12) | (target&0xfff) if 0x5000 <= target < 0x9000 else None
            kind = 'bank_descriptor'
        row.update(rom_table_bytes=rom[offset:offset+3].hex(),
                   target_virtual_pc=target,target_physical_pc=physical,
                   binding_kind=kind if physical is not None else 'unresolved')
    return report


def authored_runtime_inventory(sdk_map):
    """Show actual authored entries; never equate a fingerprint span to coverage."""
    tree=ast.parse((ROOT/'tools/build_firmware_native.py').read_text(encoding='utf-8'))
    loop=next(n for n in ast.walk(tree) if isinstance(n,ast.For)
              and isinstance(n.target,ast.Tuple)
              and all(isinstance(x,ast.Name) for x in n.target.elts)
              and [x.id for x in n.target.elts]==['name','bindings'])
    owners={}
    for name,bindings in ast.literal_eval(loop.iter):
        source=(ROOT/f'src/firmware_native_{name}.c').read_text(encoding='utf-8')
        for value in re.findall(r'\bpc\s*(?:==|!=)\s*0x([0-9a-f]+)u',source):
            entry=int(value,16)
            matches=[p+entry-v for p,v,size in bindings if v<=entry<v+size]
            if len(matches)!=1:
                raise ValueError(f'{name}: entry {entry:04x} has no unique binding')
            owners[entry]=name
    symbols={}
    for name,value in re.findall(r'^(__\w+)\s+([0-9A-Fa-f]{8})\s*$',
                                sdk_map.read_text(errors='replace'),re.MULTILINE):
        address=int(value,16)
        if 0xd000<=address<0xe535:symbols[name]=address
    rows=[dict(name=name,virtual_pc=address,module=owners.get(address),
               status='guarded_authored_entry' if address in owners else 'not_authored')
          for name,address in sorted(symbols.items(),key=lambda x:(x[1],x[0]))]
    return dict(symbols=len(rows),guarded_symbols=sum(x['module'] is not None for x in rows),
                complete=False,entries=rows,
                note='Entry guards and budget fallback still require firmware execution; aliases are counted as symbols, not distinct functions.')

def inventory(sdk_map, registry):
    symbols = firmware_table_symbols(sdk_map)
    # A few public APIs are direct JMP stubs (_SysMemcpy, _SysMemcmp,
    # bank helpers, serial I/O), not ampersand-prefixed far-call entries.
    for name,address in re.findall(r'^(_Sys\w+)\s+([0-9A-Fa-f]{8})\s*$',
                                   sdk_map.read_text(errors='replace'),re.MULTILINE):
        address=int(address,16)
        if 0xe700<=address<0xf200:
            symbols.setdefault(address,name[1:])
    entries = [dict(physical_pc=int(p,16), virtual_pc=int(v,16), hook=h,
                    status='no_native_handler' if h=='S6502_NATIVE_NO_HOOK'
                           else 'guarded_native_entry')
               for p,v,h in re.findall(r'X\(0x([0-9a-f]+)u, 0x([0-9a-f]+)u, (\w+)\)',registry)]
    if len({(e['physical_pc'],e['virtual_pc']) for e in entries}) != len(entries):
        raise ValueError('duplicate firmware entry')
    return dict(resource_only_ready=False,
                sdk_public_symbols=len(symbols),
                registered_entries=len(entries),
                registered_entries_are_not_function_count=True,
                entries=entries,
                public_apis=[dict(name=name, sdk_table_address=addr,
                                 status='contract_and_rom_binding_pending')
                             for addr,name in sorted(symbols.items())],
                runtime=authored_runtime_inventory(sdk_map),
                blockers=['public ABI/ROM binding audit incomplete',
                          'remaining firmware code still executes',
                          'ROM resource/code ranges not fully classified'])

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk-map',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--rom',type=Path)
    a=p.parse_args()
    report=inventory(a.sdk_map,(ROOT/'src/s6502_firmware_native_registry.h').read_text())
    if a.rom:resolve_public_entries(report,a.rom.read_bytes())
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(f"SDK APIs={report['sdk_public_symbols']}; registry entries={report['registered_entries']}; resource-only=False")
