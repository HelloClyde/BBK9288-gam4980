import tempfile
import unittest
from pathlib import Path

from tools.generate_c6502_spec import emit, parse_cfg, parse_map, parse_runtime_calls


class C6502SpecTest(unittest.TestCase):
    def test_cfg_map_and_runtime_parsers(self):
        regions, locates = parse_cfg(
            "range: psnake_program_group from e05000h to e08fffh\n"
            "locate: psnake_program_group at 30000h linked to e05000h\n"
        )
        self.assertEqual((regions[0].first, regions[0].last), (0xE05000, 0xE08FFF))
        self.assertEqual((locates[0].physical, locates[0].virtual), (0x30000, 0xE05000))
        self.assertEqual(parse_map("__oper1 00000020\n&SysPicture 0000E78E\n"),
                         {"__oper1": 0x20, "&SysPicture": 0xE78E})
        self.assertEqual(
            parse_runtime_calls(" 172  5027  20 F6 D2  jsr __banked_function_call\n"),
            {"__banked_function_call": 0xD2F6},
        )

    def test_emits_masked_compiler_templates(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cfg = root / "4980.cfg"
            map_file = root / "test.map"
            rt = root / "RT.LST"
            listing = root / "psnake.lst"
            cfg.write_text(
                "range: runtime_program_group from d000h to e534h\n"
                "locate: runtime_program_group at 9000h linked to d000h\n",
                encoding="ascii",
            )
            map_file.write_text(
                "__oper1 00000020\n__oper2 00000023\n"
                "__addr_reg 00000026\n&SysPicture 0000E78E\n",
                encoding="ascii",
            )
            rt.write_text(
                " 172  5027  20 F6 D2  jsr __banked_function_call\n",
                encoding="ascii",
            )
            listing.write_text(".c_start\n.bf_call\n", encoding="ascii")
            output = emit(cfg, map_file, rt, listing)
            self.assertIn("C6502_TEMPLATE_FAR_CALL", output)
            self.assertIn("C6502_ABI_MAP_OPER1 0x0020u", output)
            self.assertIn("C6502_ABI_OPER1 0x0020u", output)
            self.assertIn("C6502_SPEC_LISTING_FAR_CALLS 1u", output)


if __name__ == "__main__":
    unittest.main()
