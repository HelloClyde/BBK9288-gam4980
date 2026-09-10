import importlib.util
from pathlib import Path
import sys
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from native_register_codegen import lower, bridge_source, FIELDS, CONTEXT_FIELDS
from build_firmware_native import specialize_source


class RegisterCodegenTest(unittest.TestCase):
    def test_adapter_uses_relative_call_and_cycle_delta(self):
        code = bridge_source('test_entry', Path('body.bin'), 24)
        self.assertIn('call .Lprivate_entry', code)
        self.assertIn('sub %r13, %r12', code)
        self.assertIn('.set .Lprivate_entry, .Lpayload+24', code)
        self.assertIn('pushn %r3', code)
        for field, register in zip(FIELDS, range(4, 12)):
            self.assertIn(f'ld.w [%r0], %r{register}', code)
        self.assertNotIn('FW_RECORD', code)

    def test_inlined_private_entry_and_register_contract(self):
        source, symbol = specialize_source((ROOT/'src/firmware_native_runtime.c').read_text(), 'runtime', 0xd596)
        code = lower(source, symbol)
        self.assertIn('static inline uint32_t '+symbol+'_body', code)
        self.assertIn('always_inline', code)
        self.assertNotIn('__builtin_memcpy_inline', code)
        for index, field in enumerate(FIELDS):
            self.assertIn(f'local.{field} = g{index};', code)
            self.assertIn(f'g{index} = local.{field};', code)
        self.assertNotIn('origin->ac =', code)
        self.assertNotIn('origin->pc =', code)

    def test_every_context_field_initialized_exactly_once(self):
        import re
        header = (ROOT/'src/s6502_iram_exec_abi.h').read_text()
        struct = header.split('typedef struct s6502_iram_asm_context {')[1].split('} s6502_iram_asm_context_t')[0]
        self.assertEqual(tuple(re.findall(r'uint32_t\s+(\w+)\s*;', struct)), CONTEXT_FIELDS)
        source, symbol = specialize_source((ROOT/'src/firmware_native_graphics.c').read_text(), 'graphics', 0x5c5d)
        code = lower(source, symbol).split('void '+symbol+'_register(void)')[1]
        for field in CONTEXT_FIELDS:
            self.assertEqual(code.count('local.'+field+' ='), 1)
            if field in FIELDS:
                self.assertNotIn('origin->'+field+';', code)

    def test_spaced_entry_attributes(self):
        source, symbol = specialize_source((ROOT/'src/firmware_native_bank.c').read_text(), 'bank', 0xf457)
        self.assertIn('always_inline', lower(source, symbol))

    def test_text_retains_audited_lowering(self):
        source, symbol = specialize_source((ROOT/'src/firmware_native_text.c').read_text(), 'text', 0x650f)
        self.assertIn('__builtin_memcpy_inline', lower(source, symbol))
