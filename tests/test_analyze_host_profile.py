import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('profile_analysis', Path(__file__).resolve().parents[1] / 'tools/analyze_host_profile.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class ProfileAnalysisTest(unittest.TestCase):
    def test_hle_groups_and_reconciliation(self):
        result = module.analyze('''[GAM4980 PERF LIGHT 1]
host_exclusive_hle_ticks=11
host_exclusive_core_ticks=9
hle_sample_id=7
hle_sample_attempts=3
hle_sample_completed_segments=9
hle_sample_exclusive_ticks=4
hle_sample_id=31
hle_sample_attempts=0
hle_sample_exclusive_ticks=7
nat_sample_pc=100
nat_sample_attempts=1
nat_sample_exclusive_ticks=2
[END]''')
        self.assertEqual(result['hle_attribution_delta_ticks'], 0)
        self.assertEqual(result['hle_functions'][0]['id'], 31)
        self.assertEqual(result['hle_functions'][0]['sampled_core_percent'], 35)
        self.assertEqual(result['functions'][0]['physical_pc'], 100)
        self.assertEqual(result['hle_functions'][1]['completed_segments'], 9)

    def test_last_complete_session_and_repeated_entries(self):
        result = module.analyze('''[GAM4980 PERF LIGHT 1]
game=old
[END]
[GAM4980 PERF LIGHT 1]
game=new
host_exclusive_core_ticks=30
host_exclusive_native_ticks=70
nat_sample_pc=100
nat_sample_attempts=1000
nat_sample_accepted=900
nat_sample_exclusive_ticks=10
nat_sample_pc=200
nat_sample_attempts=2
nat_sample_accepted=2
nat_sample_exclusive_ticks=60
[END]
[GAM4980 PERF LIGHT 1]
game=incomplete
''')
        self.assertEqual(result['game'], 'new')
        self.assertEqual(result['functions'][0]['physical_pc'], 200)
        self.assertEqual(result['functions'][0]['sampled_core_percent'], 60)
        self.assertEqual(result['sampled_total_ticks'], 100)

    def test_no_samples(self):
        self.assertEqual(module.analyze('[GAM4980 PERF LIGHT 1]\n[END]')['functions'], [])
        with self.assertRaises(ValueError):
            module.analyze('incomplete')

    def test_private_profile_version(self):
        result=module.analyze('''[GAM4980 PERF LIGHT 1]
nat_function_profile_c_bridge_only=0
native_function_profile_version=2
host_exclusive_iram_ticks=3
host_exclusive_native_ticks=7
nat_sample_pc=15302656
nat_sample_attempts=5
nat_sample_accepted=4
nat_sample_exclusive_ticks=7
nat_sample_private_attempts=3
nat_sample_private_accepted=2
[END]''')
        self.assertEqual(result['functions'][0]['private_attempts'],3)
        self.assertEqual(result['functions'][0]['sampled_core_percent'],70)
        self.assertIn('Includes sampled private',result['warning'])
