import sys
import ast
import re
import struct
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from build_firmware_native import (pack_authored, FIRMWARE_FLAG, FUNCTION_FLAG,
    GRAPHICS_FLAG, TEXT_FLAG, AUTHORED_FUNCTION_FLAGS, MAX_MANIFEST_BYTES,
    authored_entries, specialize_source)
from pack_native_module import NativeModule, fnv1a


class AuthoredFirmwareTest(unittest.TestCase):
    def test_compact_guards_deduplicate_without_changing_links(self):
        first = self.module()
        second = self.module()
        second.blocks = [(8, (0xea85a0, 0xd5a0, 0xa85a0, 8, 0, 0)), *first.blocks]
        old = pack_authored([first, second], bytes(0x200000))
        new = pack_authored([first, second], bytes(0x200000), compact=True)
        h = struct.unpack_from('<16I', new)
        self.assertEqual(h[1], 5)
        self.assertEqual(h[7], 3)
        self.assertEqual(h[9], 2)
        refs = struct.unpack_from('<3I', new, h[8])
        self.assertEqual(refs[0], refs[2])
        self.assertNotEqual(refs[0], refs[1])
        self.assertEqual(struct.unpack_from('<4I', new, h[10]),
                         (0xffffffff, 16, 0xea8596, fnv1a(bytes(16))))
        oh = struct.unpack_from('<16I', old)
        self.assertEqual(new[h[12]:h[12]+32], old[oh[12]:oh[12]+32])
    def module(self):
        return NativeModule(13,0xea8,b'\x00'*16,
            [(16,(0xea8596,0xd596,0xa8596,16,0,0))],flags=AUTHORED_FUNCTION_FLAGS)

    def test_distinct_format_and_full_function_hash(self):
        rom=bytes(range(256))*8192
        package=pack_authored([self.module()],rom)
        header=struct.unpack_from('<16I',package)
        self.assertEqual(header[1:4],(4,4,64))
        self.assertEqual(struct.unpack_from('<4I',package,header[8]),
            (0,16,0xea8596,fnv1a(rom[0xa8596:0xa85a6])))

    def test_no_old_aot_or_empty_package(self):
        module=self.module();module.flags=0
        with self.assertRaises(ValueError):pack_authored([module],bytes(0x200000))
        with self.assertRaises(ValueError):pack_authored([],bytes(0x200000))
        module.flags=FIRMWARE_FLAG
        with self.assertRaises(ValueError):pack_authored([module],bytes(0x200000))

    def test_graphics_and_text_require_abi5(self):
        for flags in (AUTHORED_FUNCTION_FLAGS | GRAPHICS_FLAG,
                      AUTHORED_FUNCTION_FLAGS | GRAPHICS_FLAG | TEXT_FLAG):
            module = self.module(); module.flags = flags
            package = pack_authored([module], bytes(0x200000))
            header = struct.unpack_from('<16I', package)
            self.assertEqual(header[1:4], (4, 5, 64))
            self.assertEqual(struct.unpack_from('<I', package, header[6] + 4)[0], flags | 1)
            self.assertEqual(header[9], 0, 'authored functions must remain relocation-free')
            with self.assertRaises(ValueError):
                pack_authored([module], bytes(0x200000), abi_version=4)
        module.flags = AUTHORED_FUNCTION_FLAGS | TEXT_FLAG
        with self.assertRaises(ValueError):
            pack_authored([module], bytes(0x200000))
        module.flags = AUTHORED_FUNCTION_FLAGS | 0x100
        with self.assertRaises(ValueError):
            pack_authored([module], bytes(0x200000))
        with self.assertRaises(ValueError):
            pack_authored([self.module()], bytes(0x200000), abi_version=7)
        self.assertEqual(struct.unpack_from('<I',
            pack_authored([self.module()], bytes(0x200000), abi_version=6), 8)[0], 6)
        self.assertEqual(struct.unpack_from('<I',
            pack_authored([self.module()], bytes(0x200000), abi_version=5), 8)[0], 5)

    def test_manifest_and_function_limits(self):
        module = self.module()
        module.blocks *= 4096
        with self.assertRaisesRegex(ValueError, 'manifest.*exceeds'):
            pack_authored([module], bytes(0x200000))
        with self.assertRaisesRegex(ValueError, 'too many authored functions'):
            pack_authored([self.module()] * 193, bytes(0x200000))

    def test_one_exact_link_and_guard_only_dependencies(self):
        module=self.module()
        module.blocks.append((32,(0xea9520,0xe520,0xa9520,32,0,0)))
        rom=bytes(0x200000)
        package=pack_authored([module],rom)
        header=struct.unpack_from('<16I',package)
        descriptor=struct.unpack_from('<14I',package,header[6])
        self.assertEqual(header[7],2)
        self.assertEqual(header[11],1)
        self.assertEqual(descriptor[1],1|FIRMWARE_FLAG|FUNCTION_FLAG)
        self.assertEqual(descriptor[6:8],(0,2))
        self.assertEqual(descriptor[10:12],(0,1))
        self.assertEqual(descriptor[13],0xea8596)
        self.assertEqual(struct.unpack_from('<4I',package,header[12]),
                         (0,0xd596,(13<<16)|0xea8,0))

    def test_duplicate_callable_entries_rejected(self):
        with self.assertRaises(ValueError):
            pack_authored([self.module(),self.module()],bytes(0x200000))

    def test_specialization_preserves_stores_and_local_pc(self):
        source='''uint32_t firmware_native_test(s6502_iram_asm_context_t *c) {
          uint16_t pc=c->pc;
          if (pc==0xd000u) {pc=0xd020u;}
          if (c->pc==0xd020u) return 0;
          c->pc=pc+1; return 12;
        }'''
        specialized,symbol=specialize_source(source,'test',0xd000)
        self.assertEqual(symbol,'firmware_native_test_d000')
        self.assertIn('uint16_t pc=0xd000u;',specialized)
        self.assertIn('if (0xd000u==0xd020u)',specialized)
        self.assertIn('pc=0xd020u;',specialized)
        self.assertIn('c->pc=pc+1;',specialized)
        self.assertIn('if (c->pc != 0xd000u) return 0u;',specialized)
        self.assertNotIn('c->pc=0xd000u',specialized)

    def test_specialization_rejects_non_entry_reads(self):
        source='''uint32_t firmware_native_test(s6502_iram_asm_context_t *c) {
          c->pc=0xd020u; return c->pc==0xd000u;
        }'''
        # A read in a return expression is still after the context store.
        with self.assertRaises(ValueError):specialize_source(source,'test',0xd000)

    def test_specialization_does_not_rename_graphics_abi_headers(self):
        source = '''#include "firmware_native_graphics_abi.h"
#include "firmware_native_graphics_io.h"
__attribute__((section(".text.firmware_native_graphics")))
uint32_t firmware_native_graphics(s6502_iram_asm_context_t *c) {
    firmware_native_graphics_services_t *services;
    if(c->pc != 0x682du) return 0; return 1;
}'''
        specialized, symbol = specialize_source(source, 'graphics', 0x682d)
        self.assertIn('#include "firmware_native_graphics_abi.h"', specialized)
        self.assertIn('#include "firmware_native_graphics_io.h"', specialized)
        self.assertIn('firmware_native_graphics_services_t *services', specialized)
        self.assertIn(f'".text.{symbol}"', specialized)
        self.assertIn(f'uint32_t {symbol}(', specialized)

    def test_entries_not_every_byte_in_a_fingerprint(self):
        self.assertEqual(authored_entries('if(pc==0xd000u || pc==0xd032u){}',
            [(0xea8000,0xd000,0xa8)]),
            [(0xea8000,0xd000,0xa8),(0xea8032,0xd032,0x76)])

    def test_invalid_length_rejected(self):
        module=self.module();module.blocks[0]=(2,module.blocks[0][1])
        with self.assertRaises(ValueError):pack_authored([module],bytes(0x200000))

    def test_publish_does_not_index_aot_with_function_length(self):
        source=(ROOT/'src/gam4980_core.c').read_text(encoding='utf-8')
        start=source.index('uint32_t virtual_pc = (slot << 12)')
        end=source.index('block = &s6502_aot_blocks[match->aot_block_id]',start)
        self.assertIn('continue;',source[start:end])

    def test_every_authored_entry_is_fingerprinted_and_dispatched(self):
        tree=ast.parse((ROOT/'tools/build_firmware_native.py').read_text(encoding='utf-8'))
        loop=next(n for n in ast.walk(tree) if isinstance(n,ast.For)
                  and isinstance(n.target,ast.Tuple)
                  and all(isinstance(x,ast.Name) for x in n.target.elts)
                  and [x.id for x in n.target.elts]==['name','bindings'])
        modules=ast.literal_eval(loop.iter)
        core=(ROOT/'src/gam4980_core.c').read_text(encoding='utf-8')
        registry=(ROOT/'src/s6502_firmware_native_registry.h').read_text(encoding='utf-8')
        published={(int(p,16),int(v,16)) for p,v in re.findall(
            r'(?:\{|X\()0x([0-9a-f]+)u,\s*0x([0-9a-f]+)u',core+registry)}
        seen=set()
        for name,bindings in modules:
            source=(ROOT/f'src/firmware_native_{name}.c').read_text(encoding='utf-8')
            entries={int(v,16) for v in re.findall(r'\bpc\s*(?:==|!=)\s*0x([0-9a-f]+)u',source)}
            self.assertTrue(entries,name)
            for entry in entries:
                if name == 'graphics' and entry == 0x682d:
                    continue  # Retained legacy fixture, not packaged alongside atomic owner.
                with self.subTest(module=name,entry=hex(entry)):
                    candidates=[(p+entry-v,entry) for p,v,size in bindings if v<=entry<v+size]
                    self.assertEqual(len(candidates),1,'missing/overlapping fingerprint')
                    # Every FUNCTION descriptor publishes its exact primary
                    # link, even when its family's guard begins another page.
                    self.assertIn(candidates[0],published,'missing IRAM dispatch entry')
                    self.assertNotIn(candidates[0],seen,'entry belongs to two modules')
                    seen.add(candidates[0])

    def test_complete_graphics_manifest_fits_and_keeps_exact_guards(self):
        tree = ast.parse((ROOT/'tools/build_firmware_native.py').read_text(encoding='utf-8'))
        loop = next(n for n in ast.walk(tree) if isinstance(n, ast.For)
                    and isinstance(n.target, ast.Tuple)
                    and all(isinstance(x, ast.Name) for x in n.target.elts)
                    and [x.id for x in n.target.elts] == ['name', 'bindings'])
        modules, family_counts = [], {}
        for name, bindings in ast.literal_eval(loop.iter):
            original = (ROOT/f'src/firmware_native_{name}.c').read_text(encoding='utf-8')
            entries = authored_entries(original, bindings)
            if name == 'graphics': entries = [e for e in entries if e[1] != 0x682d]
            family_counts[name] = len(entries)
            for physical, entry, length in entries:
                specialized, _ = specialize_source(original, name, entry)
                self.assertIn(f'if (c->pc != 0x{entry:04x}u) return 0u;', specialized)
                records = [(length, (physical, entry, physical-0xe00000, length, 0, 0))]
                for p, v, size in bindings:
                    record = (size, (p, v, p-0xe00000, size, 0, 0))
                    if record not in records: records.append(record)
                flags = AUTHORED_FUNCTION_FLAGS
                if name in ('graphics', 'text', 'public_graphics'): flags |= GRAPHICS_FLAG
                if name == 'text': flags |= TEXT_FLAG
                modules.append(NativeModule(entry >> 12, physical >> 12,
                    bytes(16), records, flags=flags, module_key=physical))
        package = pack_authored(modules, bytes(0x200000))
        header = struct.unpack_from('<16I', package)
        self.assertEqual(header[1:4], (4, 5, 64))
        self.assertEqual(family_counts['graphics'], 10)
        self.assertEqual(family_counts['public_graphics'], 4)
        self.assertEqual(family_counts['text'], 2)
        self.assertEqual(header[5], 145)
        self.assertLessEqual(header[13], MAX_MANIFEST_BYTES)
        self.assertEqual(header[9], 0)
        self.assertEqual(header[11], 145)
        self.assertEqual(header[15], fnv1a(package[64:header[13]]))
        print(f'145-function ABI5 manifest: {header[13]} / {MAX_MANIFEST_BYTES} bytes')

    def test_pic_gate_remains_strict_and_temporary_directory_is_configurable(self):
        source = (ROOT/'tools/build_firmware_native.py').read_text(encoding='utf-8')
        self.assertIn("if 'There are no relocations' not in relocations:", source)
        self.assertIn("raise ValueError(f'{symbol}: non-PIC function: {relocations}')", source)
        self.assertIn('dir=work_dir', source)
        self.assertIn('TMP=directory, TEMP=directory', source)

    def test_shared_page_routes_before_first_entry_wins(self):
        source=(ROOT/'src/gam4980_core.c').read_text(encoding='utf-8')
        start=source.index('if (!page_has_match || !page_matches)')
        end=source.index('if (native_module_page_entries[page])',start)
        self.assertIn('native_module_firmware_entry',source[start:end])
        start=source.index('static uint32_t native_module_firmware_entry_once(s6502_iram_asm_context_t *context)\n{')
        end=source.index('static uint32_t native_module_fault_entry(',start)
        body=source[start:end]
        self.assertIn('physical-match->physical_pc>=match->aot_block_id',body)
        self.assertIn('native_module_load(index)',body)

if __name__=='__main__':unittest.main()
