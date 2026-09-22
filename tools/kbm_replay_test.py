"""Synthetic tests for the read-only KBM trace checker."""

import unittest

from kbm_replay import check_event_buckets, uses_radial_mapper


class EventBucketTests(unittest.TestCase):
    def test_pending_initial_motion_and_reset(self):
        events = [
            dict(kind="2", sequence="5", delta_x="3", delta_y="-1"),
            dict(kind="0", sequence="6", delta_x="2", delta_y="1"),
            dict(kind="1", sequence="7", delta_x="0", delta_y="0"),
            dict(kind="0", sequence="8", delta_x="1", delta_y="2"),
        ]
        rows = [
            dict(event_sequence="6", interval_ms="0", raw_delta_x="0", raw_delta_y="0"),
            dict(event_sequence="6", interval_ms="0.1", raw_delta_x="5", raw_delta_y="0"),
            dict(event_sequence="8", interval_ms="0.2", raw_delta_x="1", raw_delta_y="2"),
        ]
        result = check_event_buckets(rows, events)
        self.assertEqual(result["bucket_mismatches"], 0)
        self.assertEqual(result["checked_polls"], 3)
        rows[2]["raw_delta_x"] = "4"
        self.assertEqual(check_event_buckets(rows, events)["bucket_mismatches"], 1)

    def test_new_reports_always_use_the_radial_mapper(self):
        self.assertTrue(uses_radial_mapper({"mapper": "radial"}))
        with self.assertRaises(ValueError):
            uses_radial_mapper({"mapper": "axis"})

    def test_legacy_reports_keep_their_recorded_mapper(self):
        self.assertTrue(uses_radial_mapper({"radial": "true"}))
        self.assertFalse(uses_radial_mapper({"radial": "false"}))

    def test_missing_event_is_not_success(self):
        events = [dict(kind="2", sequence="5", delta_x="0", delta_y="0")]
        rows = [dict(event_sequence="6", interval_ms="1", raw_delta_x="0", raw_delta_y="0")]
        self.assertFalse(check_event_buckets(rows, events)["available"])


if __name__ == "__main__":
    unittest.main()
