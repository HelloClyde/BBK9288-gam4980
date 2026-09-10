import sys
from pathlib import Path
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from audit_firmware_native_coverage import inventory, resolve_public_entries

class FirmwareRegistryTest(unittest.TestCase):
    def test_no_inferred_coverage(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'test.map'
            path.write_text('&SysPicture 0000E700\n&SysGetKey 0000E703\n')
            result=inventory(path,'X(0xeb8351u, 0x5351u, S6502_NATIVE_NO_HOOK)')
            self.assertEqual(result['sdk_public_symbols'],2)
            self.assertFalse(result['resource_only_ready'])
            self.assertEqual(result['entries'][0]['status'],'no_native_handler')
            self.assertTrue(all(x['status']=='contract_and_rom_binding_pending'
                                for x in result['public_apis']))

    def test_real_registry_shared(self):
        source=(ROOT/'src/gam4980_core.c').read_text(encoding='utf-8')
        self.assertIn('S6502_FIRMWARE_NATIVE_ENTRIES(S6502_NATIVE_CASE)',source)
        self.assertIn('S6502_FIRMWARE_NATIVE_ENTRIES(S6502_NATIVE_ADDRESS)',source)

    def test_direct_public_stubs_are_not_omitted(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'test.map'
            path.write_text('&SysPicture 0000E700\n_SysMemcpy 0000E901\n_SysMemcmp 0000E904\n_SysGetVer 000878A3\n')
            result=inventory(path,'')
            self.assertEqual(result['sdk_public_symbols'],3)
            self.assertEqual({x['name'] for x in result['public_apis']},
                             {'SysPicture','SysMemcpy','SysMemcmp'})

    def test_duplicate_is_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'test.map'; path.write_text('')
            with self.assertRaises(ValueError):
                inventory(path,'X(0xeb8351u, 0x5351u, HOOK)\n'*2)

    def test_runtime_span_is_not_function_coverage(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'test.map'
            path.write_text('___mult_long 0000D201\n__unknown_inner 0000D202\n')
            result=inventory(path,'')['runtime']
            self.assertEqual(result['symbols'],2)
            self.assertEqual(result['guarded_symbols'],1)
            self.assertFalse(result['complete'])
            self.assertEqual(result['entries'][1]['status'],'not_authored')

    def test_unknown_rom_binding_is_rejected(self):
        with self.assertRaises(ValueError):
            resolve_public_entries({'public_apis':[]}, bytes(0x200000))

    def test_real_rom_public_bindings_when_available(self):
        rom=ROOT/'build/emulator-c6502-engine-stage/gam4980/E.BIN'
        if not rom.exists():self.skipTest('local reference ROM unavailable')
        rows=[dict(name=n,sdk_table_address=a) for n,a in
              [('strlen',0xe71e),('SysMemcpy',0xe901),('SysMemcmp',0xe904),
               ('SysWriteCom',0xe90d),('SysReadCom',0xe910)]]
        resolve_public_entries({'public_apis':rows},rom.read_bytes())
        self.assertEqual(rows[0]['target_physical_pc'],0xeb13dd)
        self.assertEqual(rows[1]['target_physical_pc'],0xeaa5bd)
        self.assertEqual(rows[2]['target_physical_pc'],0xeaa68a)
        self.assertTrue(all(r['binding_kind']=='direct_jmp' for r in rows[1:]))

if __name__=='__main__': unittest.main()
