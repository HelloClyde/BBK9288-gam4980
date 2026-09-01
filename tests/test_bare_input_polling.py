import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class BareInputPollingContractTest(unittest.TestCase):
    def test_core_splits_long_cpu_slices_only_when_callback_is_active(self):
        core = (ROOT / "src" / "gam4980_core.c").read_text(encoding="utf-8")

        self.assertIn("core_runtime_poll_callback", core)
        self.assertIn("exec_slice > core_runtime_poll_max_guest_cycles", core)
        self.assertIn("exec_slice = core_runtime_poll_max_guest_cycles", core)
        self.assertGreaterEqual(
            core.count("core_runtime_poll_callback(core_runtime_poll_context)"),
            2,
        )

    def test_bare_frontend_polls_inside_guest_frame_and_logs_scan_gap(self):
        frontend = (ROOT / "src" / "gam4980_9288.c").read_text(
            encoding="utf-8"
        )

        self.assertIn("#define BARE_INPUT_POLL_GUEST_CYCLES 8192u", frontend)
        self.assertIn(
            "poll_bare_input, &input, BARE_INPUT_POLL_GUEST_CYCLES", frontend
        )
        self.assertIn("gam4980_set_runtime_poll_callback(0, 0, 0u);", frontend)
        self.assertIn('"bare_key_scan_max_gap_ticks"', frontend)
        self.assertIn('"bare_key_scan_gap_over_8"', frontend)


if __name__ == "__main__":
    unittest.main()
