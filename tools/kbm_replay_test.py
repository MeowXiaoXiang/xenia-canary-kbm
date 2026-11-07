"""Synthetic tests for the read-only KBM trace checker."""

import unittest
import tempfile
from pathlib import Path

from kbm_replay import analyze, check_event_buckets, uses_radial_mapper


class PhysicalReplayTests(unittest.TestCase):
    def report(self, directory, schema="3"):
        path = Path(directory) / "report.csv"
        path.write_text(
            f"# schema={schema}, mapper=physical_linear_radial, estimator=poll_bucket_actual_time\n"
            "# dpi=800, full_stick_speed_cm_s=10, deadzone_compensation=false, minimum_response=0.3, invert_y=false\n"
            "# dropped_events=0, dropped_samples=0\n"
            "elapsed_ms,interval_ms,input_active,raw_delta_x,raw_delta_y,velocity_x,velocity_y,thumb_x,thumb_y,reset_sample,stale_sample,event_sequence\n"
            "10,10,true,10,0,1000,0,10404,0,false,false,2\n"
            "10,0,true,0,0,0,0,0,0,false,false,3\n"
            "20,10,true,1,0,100,0,1040,0,false,false,3\n"
            "30,10,true,0,0,0,0,0,0,false,false,3\n",
            encoding="utf-8")
        path.with_suffix(".events.csv").write_text(
            "# schema=3\n"
            "kind,sequence,delta_x,delta_y,previous_poll_offset_ms\n"
            "2,1,0,0,0\n0,2,10,0,0\n0,3,1,0,0\n", encoding="utf-8")
        return path

    def test_physical_replay_and_nonadvancing_bucket(self):
        with tempfile.TemporaryDirectory() as directory:
            result = analyze(self.report(directory))
            self.assertEqual(result["event_bucket_check"]["bucket_mismatches"], 0)
            self.assertEqual(result["replay_velocity_error_cps_p99"], 0)
            self.assertLess(result["replay_output_error_percentage_points_max"], .003)

    def test_unknown_schema_and_missing_metadata_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = self.report(directory, schema="99")
            with self.assertRaises(ValueError):
                analyze(path)
            path = self.report(directory)
            path.write_text(path.read_text().replace("dpi=800, ", ""))
            with self.assertRaises(KeyError):
                analyze(path)

    def test_unknown_mapper_and_mismatched_sidecar_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = self.report(directory)
            path.write_text(path.read_text().replace("physical_linear_radial", "mystery"))
            with self.assertRaises(ValueError):
                analyze(path)
            path = self.report(directory)
            sidecar = path.with_suffix(".events.csv")
            sidecar.write_text(sidecar.read_text().replace("schema=3", "schema=2"))
            with self.assertRaises(ValueError):
                analyze(path)

    def test_truncated_capture_cannot_claim_complete_event_replay(self):
        with tempfile.TemporaryDirectory() as directory:
            path = self.report(directory)
            path.write_text(path.read_text().replace("dropped_events=0", "dropped_events=1"))
            self.assertFalse(analyze(path)["event_bucket_check"]["available"])

    def test_schema_two_keeps_its_recorded_ema_model(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "legacy.csv"
            path.write_text(
                "# schema=2, mapper=radial, sensitivity=10, full_scale_velocity=24000, response_curve=0.8, smoothing_time_ms=8, deadzone_compensation=false, minimum_response=0.3\n"
                "elapsed_ms,interval_ms,input_active,raw_delta_x,raw_delta_y,filtered_velocity_x,filtered_velocity_y,reset_sample,stale_sample\n"
                "0,0,true,0,0,0,0,true,false\n8,8,true,0,0,0,0,false,false\n")
            result = analyze(path)
            self.assertEqual(result["schema"], "2")
            self.assertEqual(result["replay_velocity_error_cps_p99"], 0)


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
