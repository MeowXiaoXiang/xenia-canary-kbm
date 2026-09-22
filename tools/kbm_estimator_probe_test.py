"""Analytic checks for the offline phase/rate experiment."""

import math
import unittest

from kbm_estimator_probe import run_case


class ProbeTests(unittest.TestCase):
    def test_phase_locked_125hz_matches_impulse_peak(self):
        result = run_case(125, 125, 0)
        expected = 1 / (1 - math.exp(-1))
        self.assertAlmostEqual(result["event_exponential"]["min_gain"], expected, places=5)
        self.assertAlmostEqual(result["event_exponential"]["max_gain"], expected, places=5)
        self.assertEqual(result["fixed_window"]["mean_gain_error_percent"], 0)

    def test_quarter_phase_changes_sampled_gain(self):
        result = run_case(125, 125, .25)
        expected = math.exp(-.25) / (1 - math.exp(-1))
        self.assertAlmostEqual(result["event_exponential"]["min_gain"], expected, places=5)
        self.assertEqual(result["fixed_window"]["mean_gain_error_percent"], 0)

    def test_batching_is_visible_at_1000hz(self):
        direct = run_case(1000, 1000, 0)
        batched = run_case(1000, 1000, 0, 4)
        self.assertGreater(batched["event_exponential"]["max_gain"],
                           direct["event_exponential"]["max_gain"])
        self.assertLess(batched["event_exponential"]["min_gain"],
                        direct["event_exponential"]["min_gain"])


if __name__ == "__main__":
    unittest.main()
