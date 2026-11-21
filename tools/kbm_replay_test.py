"""Synthetic tests for the read-only KBM trace checker."""

import unittest
import tempfile
import math
from pathlib import Path

from kbm_replay import (analyze, check_event_buckets, processing_parameters,
                        uses_radial_mapper)
from kbm_compare_modes import compare_trace, simulate, synthetic_schedule


class PhysicalReplayTests(unittest.TestCase):
    def report(self, directory, schema="3"):
        path = Path(directory) / "report.csv"
        path.write_text(
            f"# schema={schema}, mapper=physical_linear_radial, estimator=poll_bucket_actual_time\n"
            "# dpi=800, full_stick_speed_cm_s=10, deadzone_compensation=false, minimum_response=0.3, invert_y=false\n"
            "# dropped_events=0, dropped_samples=0\n"
            "# samples=4, events=3\n"
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

    def schema_four_report(self, directory, mode, velocities):
        path = Path(directory) / "report.csv"
        data = (
            f"# schema=4, processing_mode={mode}, fixed_window_ms=8, ema_tau_ms=8\n"
            "# mapper=physical_linear_radial, dpi=800, full_stick_speed_cm_s=10\n"
            "# deadzone_compensation=false, minimum_response=0.3, invert_y=false\n"
            "# samples=6, events=2, dropped_samples=0, dropped_events=0\n"
            "elapsed_ms,interval_ms,input_active,raw_delta_x,raw_delta_y,velocity_x,"
            "velocity_y,thumb_x,thumb_y,reset_sample,stale_sample,event_sequence\n"
            "1,1,true,0,0,0,0,0,0,true,false,1\n"
        )
        threshold = 800 * 10 / 2.54
        for (t, dt, dx), velocity in zip(((9, 8, 8), (14, 5, 0), (17, 3, 0),
                                           (21, 4, 0)), velocities):
            thumb = round(velocity / threshold * 32767)
            data += f"{t},{dt},true,{dx},0,{velocity},0,{thumb},0,false,false,2\n"
        data += "59,38,true,0,0,0,0,0,0,false,false,2\n"
        path.write_text(data, encoding="utf-8")
        path.with_suffix(".events.csv").write_text(
            "# schema=4\n"
            "kind,sequence,event_elapsed_us,delta_x,delta_y,velocity_x,velocity_y,"
            "previous_poll_offset_ms,reset_pending\n"
            "2,1,0,0,0,0,0,0,true\n0,2,8000,8,0,0,0,0,false\n",
            encoding="utf-8")
        return path

    def test_physical_replay_and_nonadvancing_bucket(self):
        with tempfile.TemporaryDirectory() as directory:
            result = analyze(self.report(directory))
            self.assertEqual(result["event_bucket_check"]["bucket_mismatches"], 0)
            self.assertEqual(result["replay_velocity_error_cps_p99"], 0)
            self.assertLess(result["replay_output_error_percentage_points_max"], .003)

    def test_schema_four_accepts_new_processing_limits(self):
        metadata = {"processing_mode": "ema", "fixed_window_ms": "24",
                    "ema_tau_ms": "32"}
        self.assertEqual(processing_parameters(metadata), ("ema", 24, 32))
        for key, invalid in (("fixed_window_ms", "24.5"), ("ema_tau_ms", "32.5")):
            with self.subTest(key=key), self.assertRaises(ValueError):
                processing_parameters({**metadata, key: invalid})

    def test_schema_four_reconstructs_each_processor(self):
        alpha = -math.expm1(-1)
        # The event is still in the fixed window at 14 ms and expires at 16 ms.
        # EMA retains motion in the later empty buckets until its quiet gate.
        velocities = {
            "direct": (1000, 0, 0, 0),
            "fixed_window": (1000, 1000, 0, 0),
            "ema": (1000 * alpha, 1000 * alpha * math.exp(-.625),
                    1000 * alpha * math.exp(-1), 1000 * alpha * math.exp(-1.5)),
        }
        for mode, speeds in velocities.items():
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as directory:
                result = analyze(self.schema_four_report(directory, mode, speeds))
                self.assertEqual(result["event_bucket_check"]["bucket_mismatches"], 0)
                self.assertAlmostEqual(result["replay_velocity_error_cps_p99"], 0)
                self.assertLess(result["replay_output_error_percentage_points_max"], .003)

    def test_schema_four_comparison_rejects_inconsistent_event_timing(self):
        with tempfile.TemporaryDirectory() as directory:
            path = self.schema_four_report(directory, "fixed_window", (1000, 1000, 0, 0))
            self.assertIn("recorded", compare_trace(path)["schedules"])

            # The first bucket still consumes eight counts, but at 14 ms the
            # modified event is now outside the 8 ms window.
            events = path.with_suffix(".events.csv")
            text = events.read_text(encoding="utf-8")
            events.write_text(text.replace(",8000,", ",2000,"), encoding="utf-8")

            report = analyze(path)
            self.assertTrue(report["event_bucket_check"]["available"])
            self.assertEqual(report["event_bucket_check"]["bucket_mismatches"], 0)
            self.assertGreater(report["replay_output_error_percentage_points_max"], 30)
            with self.assertRaisesRegex(ValueError, "stick replay differs"):
                compare_trace(path)

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

    def test_missing_poll_rows_cannot_claim_complete_event_replay(self):
        with tempfile.TemporaryDirectory() as directory:
            path = self.report(directory)
            lines = path.read_text(encoding="utf-8").splitlines()
            path.write_text("\n".join(lines[:-1]) + "\n", encoding="utf-8")
            self.assertFalse(analyze(path)["event_bucket_check"]["available"])

    def test_event_count_metadata_and_negative_counts_are_checked(self):
        with tempfile.TemporaryDirectory() as directory:
            path = self.report(directory)
            text = path.read_text(encoding="utf-8")
            path.write_text(text.replace("events=3", "events=4"), encoding="utf-8")
            self.assertFalse(analyze(path)["event_bucket_check"]["available"])
            path.write_text(text.replace("dropped_samples=0", "dropped_samples=-1"), encoding="utf-8")
            with self.assertRaises(ValueError):
                analyze(path)

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

    def test_schema_two_rejects_a_schema_three_sidecar(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "legacy.csv"
            path.write_text(
                "# schema=2, mapper=radial, sensitivity=10, full_scale_velocity=24000, response_curve=0.8, smoothing_time_ms=8, deadzone_compensation=false, minimum_response=0.3\n"
                "elapsed_ms,interval_ms,input_active,raw_delta_x,raw_delta_y,filtered_velocity_x,filtered_velocity_y,reset_sample,stale_sample\n",
                encoding="utf-8")
            path.with_suffix(".events.csv").write_text("# schema=3\nkind,sequence\n", encoding="utf-8")
            with self.assertRaises(ValueError):
                analyze(path)


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

    def test_unknown_event_kind_is_not_a_valid_empty_bucket(self):
        events = [dict(kind="2", sequence="5", delta_x="0", delta_y="0"),
                  dict(kind="99", sequence="6", delta_x="0", delta_y="0")]
        rows = [dict(event_sequence="6", interval_ms="1", raw_delta_x="0", raw_delta_y="0")]
        self.assertFalse(check_event_buckets(rows, events)["available"])

    def test_trailing_events_are_checked_even_without_another_poll(self):
        events = [dict(kind="2", sequence="5", delta_x="0", delta_y="0"),
                  dict(kind="0", sequence="7", delta_x="1", delta_y="0")]
        rows = [dict(event_sequence="5", interval_ms="1", raw_delta_x="0", raw_delta_y="0")]
        self.assertFalse(check_event_buckets(rows, events)["available"])
        events[1]["sequence"] = "6"
        self.assertTrue(check_event_buckets(rows, events)["available"])


class CounterfactualScheduleTests(unittest.TestCase):
    def test_duplicate_poll_keeps_motion_and_stall_clears_all_estimators(self):
        # A same-time poll must leave the event for the next advancing query.
        events = [(4_000_000, 1, 8, 0), (4_000_000, 2, 8, 0),
                  (131_000_000, 3, 8, 0)]
        polls = [(4_000_000, 1), (4_000_000, 2), (8_000_000, 2),
                 (132_000_000, 3), (136_000_000, 3)]
        for mode in ("direct", "fixed_window", "ema"):
            with self.subTest(mode=mode):
                rows = list(simulate(events, polls, (mode, 8, 8), 4000, .3))
                self.assertGreater(rows[0][1][0], 0)
                self.assertEqual(rows[1][1], (0, 0))
                self.assertGreater(rows[2][1][0], 0)
                self.assertEqual(rows[3][1], (0, 0))
                self.assertEqual(rows[4][1], (0, 0))

    def test_generated_stall_stays_in_trace_duration(self):
        self.assertEqual(list(synthetic_schedule(144_000_000, (8,), stall_every=2)),
                         [(8_000_000, None), (136_000_000, None), (144_000_000, None)])


if __name__ == "__main__":
    unittest.main()
