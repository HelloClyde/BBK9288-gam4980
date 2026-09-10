from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


class C6502RuntimeLiftTests(unittest.TestCase):
    def test_caller_lifts_reject_an_exhausted_cycle_budget(self) -> None:
        source = (ROOT / "src" / "s6502_game_load_aot.h").read_text(
            encoding="utf-8"
        )
        guards = re.findall(
            r"if \(GAM4980_EXPERIMENTAL_C6502_CALLER_LIFTS &&\s*"
            r"executed < cycles && !DECIMAL_p &&.*?"
            r"(?:53u|61u) <= cycles - executed\)",
            source,
            flags=re.S,
        )
        self.assertEqual(len(guards), 2)
        self.assertIn(
            "#define GAM4980_EXPERIMENTAL_C6502_CALLER_LIFTS 0",
            source,
        )

    def test_native_module_lift_uses_additive_budget_check(self) -> None:
        source = (ROOT / "tools" / "pack_native_module.py").read_text(
            encoding="utf-8"
        )
        self.assertIn(
            'context->cycles + executed + "\n        f"{call_cycles}u > '
            'context->cycle_budget',
            source,
        )


if __name__ == "__main__":
    unittest.main()
