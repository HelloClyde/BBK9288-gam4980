import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src" / "gam4980_9288.c"


class PreGameClearContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE.read_text(encoding="utf-8")

    def test_clear_updates_sdk_and_direct_framebuffer_paths(self):
        match = re.search(
            r"static void clear_screen\(void\)\s*\{(?P<body>.*?)\n\}",
            self.source,
            re.S,
        )
        self.assertIsNotNone(match)
        body = match.group("body")
        self.assertIn("memset(g_screen_frame, 0xff", body)
        self.assertIn("submit_screen_frame();", body)
        self.assertIn("commit_loading_frame_direct();", body)
        self.assertLess(
            body.index("submit_screen_frame();"),
            body.index("commit_loading_frame_direct();"),
        )

    def test_clear_occurs_after_starting_page_and_before_guest_loop(self):
        start = self.source.index(
            "draw_loading_stage(k_loading_start, 9u, 1u, 1u);"
        )
        clear = self.source.index("clear_screen();", start)
        run = self.source.index("run_emulator_window()", clear)
        self.assertLess(start, clear)
        self.assertLess(clear, run)


if __name__ == "__main__":
    unittest.main()
