"""Build hand-authored firmware modules, not generated instruction AOT.

This is a development package until the complete firmware contract set passes
validation. No game files are read or embedded.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile

from pack_native_module import NativeModule, align, extract_section, fnv1a
import native_register_abi
import native_atomic_contracts

ROOT = Path(__file__).resolve().parents[1]
FIRMWARE_FLAG = 0x10
FUNCTION_FLAG = 0x20
GRAPHICS_FLAG = 0x40
TEXT_FLAG = 0x80
AUTHORED_FUNCTION_FLAGS = FIRMWARE_FLAG | FUNCTION_FLAG
MAX_MANIFEST_BYTES = 65536
MAX_FUNCTIONS = 192


def authored_entries(source, bindings):
    """Explicit guarded entry PCs only; a fingerprint span is not a function."""
    source = re.sub(r'/\*.*?\*/|//[^\n]*', '', source, flags=re.S)
    entries = sorted({int(value, 16) for value in re.findall(
        r'\bpc\s*(?:==|!=)\s*0x([0-9a-f]+)u', source)})
    if not entries:
        raise ValueError('authored source has no explicit entry guards')
    result = []
    for entry in entries:
        owners = [(physical + entry - virtual, entry,
                   virtual + size - entry)
                  for physical, virtual, size in bindings
                  if virtual <= entry < virtual + size]
        if len(owners) != 1:
            raise ValueError(f'entry {entry:04x} has no unique fingerprint binding')
        result.append(owners[0])
    return result


def specialize_source(source, name, entry):
    """Constant-fold entry selection, never alter the guest PC to select it.

    Local `pc` variables remain mutable (bank-call continuations use them).
    Context PC stores also remain untouched. The current handwritten sources
    only read the context PC before they commit an accepted operation; refuse
    a future read-after-store pattern rather than incorrectly specializing it.
    An explicit original-PC guard protects accidental wrong-entry calls.
    """
    token = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|'
                       r"'(?:\\.|[^'\\])*'|\bc\s*->\s*pc\b", re.S)
    stores = []
    reads = []
    for match in token.finditer(source):
        if not re.fullmatch(r'c\s*->\s*pc', match.group()):
            continue
        tail = source[match.end():]
        assignment = re.match(r'\s*=(?!=)', tail)
        if assignment:
            stores.append(match.start())
        elif re.match(r'\s*(?:\+\+|--|[-+*/%&|^]=|<<=|>>=)', tail):
            raise ValueError('context PC update cannot be entry-specialized')
        else:
            if any(not re.search(r'\breturn\b[^;]*;', source[pos:match.start()])
                   for pos in stores):
                raise ValueError('context PC read after a store is not an entry read')
            reads.append((match.start(), match.end()))
    if not reads:
        raise ValueError('authored source does not read its entry PC')
    for start, end in reversed(reads):
        source = source[:start] + f'0x{entry:04x}u' + source[end:]
    symbol = f'firmware_native_{name}_{entry:04x}'
    # Rename the exact entry identifier and its exact .text section name,
    # not include paths such as firmware_native_graphics_io.h or private
    # structure names. Each specialized function has its own object already.
    source = re.sub(r'\b' + re.escape(f'firmware_native_{name}') + r'\b',
                    symbol, source)
    signature = re.compile(r'(uint32_t\s+' + re.escape(symbol) +
                           r'\s*\(s6502_iram_asm_context_t\s*\*c\)\s*\{)')
    source, count = signature.subn(
        lambda match: match.group() +
        f'\n    if (c->pc != 0x{entry:04x}u) return 0u;\n', source)
    if count != 1:
        raise ValueError(f'{name}: cannot locate authored entry function')
    return source, symbol


def pack_authored(modules, data, abi_version=None, compact=False):
    if not modules:
        raise ValueError('empty firmware package')
    if len(modules) > MAX_FUNCTIONS:
        raise ValueError('too many authored functions for the runtime loader')
    needs_graphics_abi = any(module.flags & (GRAPHICS_FLAG | TEXT_FLAG)
                             for module in modules)
    if abi_version is None:
        abi_version = 5 if needs_graphics_abi else 4
    if abi_version not in (4, 5, 6) or (needs_graphics_abi and abi_version < 5):
        raise ValueError('graphics/text functions require ABI 5; supported ABIs are 4 and 5')
    seen = set()
    for module in modules:
        if (module.flags & ~0x100) not in (AUTHORED_FUNCTION_FLAGS,
                AUTHORED_FUNCTION_FLAGS | GRAPHICS_FLAG,
                AUTHORED_FUNCTION_FLAGS | GRAPHICS_FLAG | TEXT_FLAG) or not module.blocks:
            raise ValueError('authored package must not contain old AOT modules')
        if not module.code or not 0 <= module.entry_offset < len(module.code):
            raise ValueError('invalid authored function code')
        if module.flags & 0x100 and (not hasattr(module, 'register_offset') or
                not 0 <= module.register_offset < len(module.code) or module.register_offset & 1):
            raise ValueError('invalid register ABI entry')
        for index, (length, record) in enumerate(module.blocks):
            physical, virtual, offset, size, _, _ = record
            if (length != size or not 0 < size <= 0x1000 or
                    offset != physical-0xe00000 or offset < 0 or
                    offset+size > len(data) or not 0 <= virtual <= 0xffff):
                raise ValueError('invalid firmware function binding')
            if index == 0:
                if (virtual >> 12 != module.mapping_slot or
                        physical >> 12 != module.physical_bank or
                        physical in seen):
                    raise ValueError('invalid or duplicate exact function entry')
                seen.add(physical)

    # Record format remains 4. ABI 5 appends graphics services to the context;
    # legacy arithmetic/string-only packages retain ABI 4 compatibility.
    # FUNCTION changes the existing records' contract:
    # match[0] is the only callable entry; additional matches are guard-only
    # family fingerprints. The sole link carries the exact PC, not its page.
    # No source-family helper code is loaded merely to validate a fingerprint.
    count = len(modules)
    matches = sum(len(module.blocks) for module in modules)
    module_offset = 64
    match_offset = module_offset + 56 * count
    guards = {}
    if compact:
        for module in modules:
            for length, record in module.blocks:
                physical, _, offset, size, _, _ = record
                key = (length, physical, fnv1a(data[offset:offset+size]))
                if key not in guards:
                    guards[key] = len(guards)
        if len(guards) > 512:
            raise ValueError('too many shared firmware guards')
    reloc_offset = match_offset + (4 if compact else 16) * matches
    link_offset = reloc_offset + (16 * len(guards) if compact else 0)
    payload_offset = align(link_offset + 16 * count, 16)
    if payload_offset > MAX_MANIFEST_BYTES:
        raise ValueError(f'authored manifest {payload_offset} exceeds runtime limit '
                         f'{MAX_MANIFEST_BYTES}')
    cursor = payload_offset
    offsets = []
    for module in modules:
        cursor = align(cursor, 16)
        offsets.append(cursor)
        cursor += len(module.code)
    package = bytearray(cursor)
    match_cursor = 0
    for index, module in enumerate(modules):
        mapping = (module.mapping_slot << 16) | module.physical_bank
        physical, virtual = module.blocks[0][1][:2]
        struct.pack_into('<14I', package, module_offset + index * 56,
            mapping, 1 | module.flags, offsets[index], len(module.code),
            fnv1a(module.code), module.entry_offset, match_cursor,
            len(module.blocks), 0, 0, index, 1,
            getattr(module, 'register_offset', mapping), physical)
        for length, record in module.blocks:
            guard_physical, _, offset, size, _, _ = record
            digest = fnv1a(data[offset:offset+size])
            if compact:
                struct.pack_into('<I', package, match_offset + 4 * match_cursor,
                    guards[length, guard_physical, digest])
            else:
                struct.pack_into('<4I', package, match_offset + 16 * match_cursor,
                    index, length, guard_physical, digest)
            match_cursor += 1
        struct.pack_into('<4I', package, link_offset + index * 16,
            index, virtual, mapping, module.entry_offset)
        package[offsets[index]:offsets[index]+len(module.code)] = module.code
    for (length, physical, digest), index in guards.items():
        struct.pack_into('<4I', package, reloc_offset + 16 * index,
            0xffffffff, length, physical, digest)
    struct.pack_into('<16I', package, 0,
        0x54414e47, 5 if compact else 4, abi_version, 64, len(package), count, module_offset,
        matches, match_offset, len(guards), reloc_offset, count, link_offset,
        payload_offset, len(package)-payload_offset,
        fnv1a(package[64:payload_offset]))
    return bytes(package)


def build(toolchain, rom, output, *, work_dir=None, include_graphics=True, register_audit_dir=None, compiled_registers=False):
    data = rom.read_bytes()
    if len(data) != 0x200000:
        raise ValueError('E.BIN must be exactly 2 MiB')
    if hashlib.sha256(data).hexdigest() != (
        '9d13aa4593d97b790afc37d73da8be985e7a3aa7f3dcfe6b91c798671067aa5e'
    ):
        raise ValueError('unsupported E.BIN: handwritten contracts are version bound')
    modules = []
    report = []
    # Keep all intermediates on the output/work volume, not the system TEMP
    # volume: full function builds contain many specialized source objects.
    work_dir = Path(work_dir) if work_dir else output.parent
    work_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='9288-firmware-', dir=work_dir) as directory:
        temporary = Path(directory)
        compiler_env = dict(os.environ, TMP=directory, TEMP=directory)
        # Entire function group is fingerprinted, not just its first opcode.
        for name, bindings in [
            ('bank', [(0xeaa457, 0xf457, 0x9d), (0xeaa52a, 0xf52a, 0x93),
                      (0xea82f6,0xd2f6,0x1d),(0xea8313,0xd313,7)]),
            ('switch', [(0xea8b5c,0xdb5c,0x85)]),
            ('string', [(0xeb13dd,0x63dd,0x8c)]),
            ('strchr', [(0xeb0fc4,0x5fc4,0x8b)]),
            ('strcmp', [(0xeb104f,0x604f,0xe3)]),
            ('memory', [(0xeaa5bd, 0xf5bd, 0xcd), (0xeaa68a, 0xf68a, 0x67)]),
            ('multiply', [(0xea8184, 0xd184, 0x7d), (0xea8201, 0xd201, 0x9c), (0xea865b, 0xd65b, 0x54), (0xea86af, 0xd6af, 0x46),
                          (0xea8cef, 0xdcef, 0x08), (0xea8cf7, 0xdcf7, 0x22),
                          (0xea86f5, 0xd6f5, 0x5d)]),
            ('divide', [(0xea8000, 0xd000, 0xa8), (0xea80a8, 0xd0a8, 0xdc), (0xea839b, 0xd39b, 0x54), (0xea83ef, 0xd3ef, 0x46),
                        (0xea8c7b, 0xdc7b, 0x08), (0xea8cab, 0xdcab, 0x1d),
                        (0xea85c6, 0xd5c6, 0x3e), (0xea8c83, 0xdc83, 0x28),
                        (0xea8cc8, 0xdcc8, 0x27), (0xea8435, 0xd435, 0x63),
                        (0xea8604, 0xd604, 0x57), (0xea8d8f, 0xdd8f, 0x18)]),
            ('compare', [(0xea8340, 0xd340, 0x5b)]),
            ('float', [(0xea9039, 0xe039, 0xc7), (0xea951c, 0xe51c, 4),
                       (0xea9282, 0xe282, 0x9b), (0xea9524, 0xe524, 4),
                       (0xea931d, 0xe31d, 0x9a), (0xea9528, 0xe528, 4),
                       (0xea93b7, 0xe3b7, 0x160), (0xea952c, 0xe52c, 4),
                       (0xea9100, 0xe100, 0x182), (0xea9520, 0xe520, 4),
                       (0xea831a, 0xd31a, 0x26)]),
            ('conversion', [(0xea84e1,0xd4e1,0x25),(0xea8557,0xd557,0x1b),
                            (0xea8835,0xd835,0x25),(0xea888a,0xd88a,0x1b),
                            (0xea8d4f,0xdd4f,3),(0xea8d8c,0xdd8c,3),
                            (0xea8498,0xd498,6),(0xea84c0,0xd4c0,6),
                            (0xea8506,0xd506,6),(0xea8534,0xd534,6),
                            (0xea8d19,0xdd19,6),(0xea8d32,0xdd32,6),
                            (0xea8d52,0xdd52,6),(0xea8d6f,0xdd6f,6),
                            (0xea881d,0xd81d,8),(0xea8825,0xd825,0x10),
                            (0xea886c,0xd86c,0xc),(0xea8878,0xd878,0x12)]),
            ('runtime', [(0xea82ca, 0xd2ca, 0x2c),
                         (0xea8752, 0xd752, 0x2e),
                         (0xea849e, 0xd49e, 0x0b),
                         (0xea84a9, 0xd4a9, 0x17),
                         (0xea84c6, 0xd4c6, 0x1b),
                         (0xea8519, 0xd519, 0x1b),
                         (0xea853a, 0xd53a, 0x1d),
                         (0xea8d1f, 0xdd1f, 0x13),
                         (0xea8d38, 0xdd38, 0x17),
                         (0xea8d58, 0xdd58, 0x17),
                         (0xea8d75, 0xdd75, 0x17),
                         (0xea850c, 0xd50c, 0x0d),
                         (0xea885a, 0xd85a, 0x12),
                         (0xea88a5, 0xd8a5, 0x18),
                         (0xea829d, 0xd29d, 0x2d),
                         (0xea88bd, 0xd8bd, 0x2c),
                         (0xea8b2f, 0xdb2f, 0x2d),
                         (0xea8db8, 0xddb8, 0x2c),
                         (0xea8a09, 0xda09, 0x11),
                         (0xea88e9, 0xd8e9, 0x22),
                         (0xea890b, 0xd90b, 0x34),
                         (0xea893f, 0xd93f, 0xa0),
                         (0xea89df, 0xd9df, 0x2a),
                         (0xea8a1a, 0xda1a, 0x23),
                         (0xea8a3d, 0xda3d, 0x6d),
                         (0xea8c0e, 0xdc0e, 0x6d),
                         (0xea8bf2, 0xdbf2, 0x1c),
                         (0xea8be1, 0xdbe1, 0x11),
                         (0xea8da7, 0xdda7, 0x11),
                         (0xea8de4, 0xdde4, 0x1e),
                         (0xea8e02, 0xde02, 0x27),
                         (0xea8aaa, 0xdaaa, 0x6f),
                         (0xea8b19, 0xdb19, 0x16),
                         (0xea8572, 0xd572, 0x14),
                         (0xea8586, 0xd586, 0x40),
                         (0xea87a6, 0xd7a6, 0x0e),
                         (0xea8780, 0xd780, 0x26),
                         (0xea87b4, 0xd7b4, 0x2d),
                         (0xea87f1, 0xd7f1, 0x2c),
                         (0xea87e1, 0xd7e1, 0x10)]),
            ('float_add', [(0xea8e29, 0xde29, 0x210),
                           (0xea9517, 0xe517, 5), (0xea9530, 0xe530, 5)]),
            ('graphics', [(0xeb582d, 0x682d, 0x103),
                          (0xeb5988, 0x6988, 0x2ee),
                          (0xeb5646, 0x6646, 0xb7),
                          (0xeb8c5d, 0x5c5d, 0x1a5),
                          (0xeb776b, 0x876b, 0x11d),
                          (0xeb7039, 0x8039, 0x55),
                          (0xeb8351, 0x5351, 0x8c),
                          (0xeb8801, 0x5801, 0x8c),
                          (0xeb759e, 0x859e, 0xbc)]),
            ('text', [(0xeb550b, 0x650b, 0x1f2),
                      (0xeb5086, 0x6086, 0x13f)]),
            ('public_graphics', [(0xeb582d,0x682d,0x103),
                                 (0xeb8000,0x5000,0x351),
                                 (0xeb53d7,0x63d7,0x277),
                                 (0xeb4c57,0x5c57,0x433)]),
        ]:
            if not include_graphics and name in ('graphics', 'text', 'public_graphics'):
                continue
            original = (ROOT/'src'/f'firmware_native_{name}.c').read_text(
                encoding='utf-8')
            for physical, entry, length in authored_entries(original, bindings):
                if name=='graphics' and entry==0x682d:
                    continue  # Public atomic owner; internal continuations remain pageable.
                source_text, symbol = specialize_source(original, name, entry)
                source_text = native_atomic_contracts.lower(source_text,name,entry)
                if register_audit_dir or compiled_registers:
                    from native_register_codegen import lower
                    audit = Path(register_audit_dir) if register_audit_dir else temporary / 'private'
                    audit.mkdir(parents=True, exist_ok=True)
                    private_source = audit / (symbol + '.c')
                    private_obj = audit / (symbol + '.o')
                    private_source.write_text(lower(source_text, symbol), encoding='utf-8')
                    subprocess.run([str(toolchain/'clang.exe'), '--target=s1c33-none-elf', '-O2',
                        '-ffreestanding', '-fno-builtin', '-fno-jump-tables', '-fomit-frame-pointer',
                        '-I', str(ROOT/'src'), '-c', str(private_source), '-o', str(private_obj)],
                        check=True, env=compiler_env)
                    private_reloc = subprocess.check_output([str(toolchain/'llvm-readelf.exe'), '-r',
                        str(private_obj)], text=True)
                    if 'There are no relocations' not in private_reloc:
                        raise ValueError(f'{symbol}: private entry not PIC: {private_reloc}')
                register_source = native_register_abi.source(symbol, entry,
                    atomic=native_atomic_contracts.eligible(name,entry)) if name == 'runtime' else ''
                source_text += register_source
                source = temporary / (symbol + '.c')
                source.write_text(source_text, encoding='utf-8')
                obj = temporary / (symbol + '.o')
                subprocess.run([
                    str(toolchain/'clang.exe'), '--target=s1c33-none-elf', '-O2',
                    '-ffreestanding', '-fno-builtin', '-fno-jump-tables',
                    '-fomit-frame-pointer', '-I', str(ROOT/'src'), '-c',
                    str(source), '-o', str(obj),
                ], check=True, env=compiler_env)
                relocations = subprocess.check_output([
                    str(toolchain/'llvm-readelf.exe'), '-r', str(obj)], text=True)
                if 'There are no relocations' not in relocations:
                    raise ValueError(f'{symbol}: non-PIC function: {relocations}')
                code = extract_section(str(toolchain/'llvm-objcopy.exe'), obj,
                    f'.text.{symbol}', temporary/(symbol+'.bin'))
                symbols = subprocess.check_output([
                    str(toolchain/'llvm-readelf.exe'), '--symbols', '--wide', str(obj)], text=True)
                entry_symbol = re.search(r'^\s*\d+:\s*([0-9a-fA-F]+)\s+\d+\s+FUNC\s+GLOBAL\s+\S+\s+\S+\s+'
                                         + re.escape(symbol) + r'\s*$', symbols, re.M)
                if not entry_symbol:
                    raise ValueError(f'{symbol}: missing callable symbol')
                entry_offset = int(entry_symbol.group(1), 16)
                if entry_offset >= len(code):
                    raise ValueError(f'{symbol}: entry outside payload')
                records = [(length, (physical, entry, physical-0xe00000,
                                     length, 0, 0))]
                # The callable match is exact. The remaining records preserve
                # the original family's ROM guards, including alias bodies
                # in another physical bank. They never become callable links.
                for p, v, size in bindings:
                    record = (size, (p, v, p-0xe00000, size, 0, 0))
                    if record not in records:
                        records.append(record)
                modules.append(NativeModule(entry >> 12, physical >> 12,
                    code, records, entry_offset=entry_offset, flags=AUTHORED_FUNCTION_FLAGS |
                        (GRAPHICS_FLAG if name in ('graphics','public_graphics') else
                         GRAPHICS_FLAG | TEXT_FLAG if name == 'text' else 0),
                    module_key=physical))
                if register_source:
                    register_symbol = re.search(r'^\s*\d+:\s*([0-9a-fA-F]+)\s+\d+\s+FUNC\s+GLOBAL\s+\S+\s+\S+\s+'
                        + re.escape(symbol + '_register') + r'\s*$', symbols, re.M)
                    if not register_symbol:
                        raise ValueError('missing register entry: ' + symbol)
                    modules[-1].flags |= 0x100
                    modules[-1].register_offset = int(register_symbol.group(1), 16)
                elif compiled_registers:
                    private_code = extract_section(str(toolchain/'llvm-objcopy.exe'), private_obj,
                        f'.text.{symbol}_body', temporary/(symbol+'_private.bin'))
                    private_symbols = subprocess.check_output([str(toolchain/'llvm-readelf.exe'),
                        '--symbols', '--wide', str(private_obj)], text=True)
                    private_entry = re.search(r'^\s*\d+:\s*([0-9a-fA-F]+)\s+\d+\s+FUNC\s+GLOBAL\s+\S+\s+\S+\s+'
                        + re.escape(symbol + '_register') + r'\s*$', private_symbols, re.M)
                    if not private_entry: raise ValueError('private entry missing: ' + symbol)
                    padding = (-len(code)) & 3
                    modules[-1].register_offset = len(code) + padding + int(private_entry.group(1), 16)
                    modules[-1].code = code + bytes(padding) + private_code
                    modules[-1].flags |= 0x100
                if compiled_registers:
                    # Keep one implementation, not C body + private body.
                    # Existing hand-written leaves remain hand-written.
                    from native_register_codegen import bridge_source
                    if register_source:
                        size_match = re.search(r'^\s*\d+:\s*[0-9a-fA-F]+\s+(\d+)\s+FUNC\s+GLOBAL\s+\S+\s+\S+\s+'
                            + re.escape(symbol + '_register') + r'\s*$', symbols, re.M)
                        if not size_match or not int(size_match.group(1)):
                            raise ValueError('missing private leaf extent: ' + symbol)
                        start = modules[-1].register_offset
                        body_code = code[start:start+int(size_match.group(1))]
                        body_entry = 0
                    else:
                        body_code = private_code
                        body_entry = int(private_entry.group(1), 16)
                    payload_file = temporary / (symbol + '_shared_payload.bin')
                    payload_file.write_bytes(body_code)
                    adapter_source = temporary / (symbol + '_adapter.S')
                    adapter_obj = temporary / (symbol + '_adapter.o')
                    adapter_source.write_text(bridge_source(symbol, payload_file.resolve(), body_entry,
                        record_metrics=bool(register_source)), encoding='utf-8')
                    subprocess.run([str(toolchain/'clang.exe'), '--target=s1c33-none-elf',
                        '-c', str(adapter_source), '-o', str(adapter_obj)], check=True, env=compiler_env)
                    adapter_reloc = subprocess.check_output([str(toolchain/'llvm-readelf.exe'), '-r', str(adapter_obj)], text=True)
                    if 'There are no relocations' not in adapter_reloc:
                        raise ValueError('non-PIC shared adapter: '+adapter_reloc)
                    adapter_symbols = subprocess.check_output([str(toolchain/'llvm-readelf.exe'), '--symbols', '--wide', str(adapter_obj)], text=True)
                    adapter_entry = re.search(r'^\s*\d+:\s*([0-9a-fA-F]+)\s+\d+\s+\S+\s+GLOBAL\s+\S+\s+\S+\s+'
                        + re.escape(symbol + '_register') + r'\s*$', adapter_symbols, re.M)
                    if not adapter_entry: raise ValueError('missing shared entry: '+symbol)
                    modules[-1].code = extract_section(str(toolchain/'llvm-objcopy.exe'), adapter_obj,
                        f'.text.{symbol}', temporary/(symbol+'_shared.bin'))
                    entry_offset = modules[-1].entry_offset = 0
                    modules[-1].register_offset = int(adapter_entry.group(1),16)
                report.append(dict(family=name, virtual_pc=entry,
                    physical_pc=physical, code_bytes=len(modules[-1].code),
                    entry_offset=entry_offset,
                    guard_records=len(records), native_dependencies=[],
                    relocation_count=0,
                    shared_body=bool(compiled_registers),
                    register_entry_offset=getattr(modules[-1], 'register_offset', None)))
    package = pack_authored(modules, data, compact=True, abi_version=6 if compiled_registers else None)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(package)
    output.with_suffix('.functions.json').write_text(json.dumps(dict(
        format=5, abi=struct.unpack_from('<I', package, 8)[0],
        function_count=len(modules),
        graphics_function_count=sum(row['family'] == 'graphics' for row in report),
        text_function_count=sum(row['family'] == 'text' for row in report),
        code_bytes=sum(len(module.code) for module in modules),
        manifest_bytes=struct.unpack_from('<I', package, 52)[0],
        file_bytes=len(package), resource_only_ready=False,
        function_entries=report), indent=2), encoding='utf-8')
    print(f'Authored firmware development package: {len(modules)} functions, '
          f'{len(package)} bytes; NOT resource-only ready')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--toolchain', type=Path, required=True)
    parser.add_argument('--rom', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--work-dir', type=Path,
                        help='temporary source/object directory (defaults to output directory)')
    parser.add_argument('--runtime-only', action='store_true',
                        help='build the legacy ABI 4 arithmetic/string/runtime package')
    parser.add_argument('--register-audit-dir', type=Path)
    parser.add_argument('--compiled-registers', action='store_true')
    args = parser.parse_args()
    build(args.toolchain.resolve(), args.rom, args.output,
          work_dir=args.work_dir, include_graphics=not args.runtime_only,
          register_audit_dir=args.register_audit_dir, compiled_registers=args.compiled_registers)
