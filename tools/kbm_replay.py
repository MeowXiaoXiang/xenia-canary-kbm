#!/usr/bin/env python3
"""Read-only replay of KBM poll reports; never modifies player settings.

Usage: python tools/kbm_replay.py path/to/kbm-input-report-TIMESTAMP.csv
Legacy reports use rounded/floored intervals. Their reconstructed actual-time
results are estimates, not raw-event replay or measured camera motion.
"""

import argparse
import csv
import json
import math
from collections import deque
from pathlib import Path


def percentile(values, fraction):
    if not values:
        return None
    values = sorted(values)
    index = (len(values) - 1) * fraction
    lower = int(index)
    return values[lower] + (values[min(lower + 1, len(values) - 1)] - values[lower]) * (index - lower)


def load_report(path):
    metadata = {}
    with path.open(encoding="utf-8-sig", newline="") as source:
        lines = []
        for line in source:
            if line.startswith("#"):
                for item in line[1:].strip().split(","):
                    if "=" in item:
                        key, value = item.strip().split("=", 1)
                        metadata[key] = value
            else:
                lines.append(line)
    return metadata, list(csv.DictReader(lines))


def filter_delta(delta, dt, tau, previous):
    if dt <= 0:
        return previous
    if tau <= 0:
        return delta / dt
    ratio = dt / tau
    return previous * math.exp(-ratio) + delta / tau * (-math.expm1(-ratio) / ratio)


def axis_output(value, threshold, curve, minimum):
    if value == 0:
        return 0.0
    magnitude = min(abs(value) / threshold, 1.0) ** curve
    return math.copysign(minimum + (1 - minimum) * magnitude, value)


def uses_radial_mapper(metadata):
    """Select the fixed v2 mapper while retaining old report analysis."""
    mapper = metadata.get("mapper")
    if mapper is not None:
        if mapper != "radial":
            raise ValueError(f"unsupported KBM mapper: {mapper}")
        return True
    return metadata.get("radial", "false") == "true"


def check_event_buckets(rows, events):
    """Check recorded arrival order against poll buckets, including resets.

    Sequence numbers avoid guessing which side of a rounded timestamp an event
    belongs to. A missing/truncated sidecar must not masquerade as a valid replay.
    """
    if not events or events[0].get("kind") != "2":
        return {"available": False, "reason": "missing initial state"}
    # Motion may arrive after the last poll and before manual Stop. Validate
    # that tail too, even though it cannot contribute to a recorded bucket.
    expected_sequence = int(events[0]["sequence"])
    for event in events[1:]:
        expected_sequence += 1
        if int(event["sequence"]) != expected_sequence:
            return {"available": False, "reason": "event sequence gap"}
        if event["kind"] not in ("0", "1"):
            return {"available": False, "reason": "unknown event kind"}
    pending = [int(events[0]["delta_x"]), int(events[0]["delta_y"])]
    cursor = 1
    mismatches = 0
    checked = 0
    last_sequence = int(events[0]["sequence"])
    for row in rows:
        sequence = int(row["event_sequence"])
        # Disabled Raw Input does not consume a bucket or carry a sequence.
        if sequence == 0 and float(row["interval_ms"]) == 0:
            continue
        while cursor < len(events) and int(events[cursor]["sequence"]) <= sequence:
            event = events[cursor]
            current_sequence = int(event["sequence"])
            if current_sequence != last_sequence + 1:
                return {"available": False, "reason": "event sequence gap"}
            last_sequence = current_sequence
            if event["kind"] == "1":
                pending = [0, 0]
            elif event["kind"] == "0":
                pending[0] += int(event["delta_x"])
                pending[1] += int(event["delta_y"])
            cursor += 1
        if sequence != last_sequence:
            return {"available": False, "reason": "missing events or unordered polls"}
        recorded = [int(row["raw_delta_x"]), int(row["raw_delta_y"])]
        consumes = float(row["interval_ms"]) > 0
        mismatches += recorded != (pending if consumes else [0, 0])
        checked += 1
        if consumes:
            pending = [0, 0]
    return {"available": True, "checked_polls": checked, "bucket_mismatches": mismatches}


def check_recorded_events(metadata, rows, events):
    if int(metadata.get("dropped_events", "0")) or int(metadata.get("dropped_samples", "0")):
        return {"available": False, "reason": "truncated recording"}
    # A file can be truncated after publication without changing its metadata.
    for key, records in (("samples", rows), ("events", events)):
        if key in metadata and int(metadata[key]) != len(records):
            return {"available": False, "reason": "record count mismatch"}
    return check_event_buckets(rows, events)


class PhysicalEstimator:
    """Reconstruct a processor from ordered host events and query intervals."""

    def __init__(self, mode, window_ms, tau_ms, threshold, invert_y):
        self.mode = mode
        self.width_ns = round(window_ms * 1_000_000)
        self.tau = tau_ms / 1000
        self.threshold = threshold
        self.y_sign = 1 if invert_y else -1
        self.clear()

    def clear(self):
        self.events = deque()
        self.x = self.y = 0
        self.last_dropped = None
        self.last_motion = None
        self.ema = (0.0, 0.0)

    def _pop(self):
        t, x, y = self.events.popleft()
        self.x -= x
        self.y -= y
        return t

    def _expire(self, now):
        cutoff = now - self.width_ns
        while self.events and self.events[0][0] <= cutoff:
            self._pop()
        if self.last_dropped is not None and self.last_dropped <= cutoff:
            self.last_dropped = None

    def event(self, time_ns, x, y):
        if self.mode == "fixed_window":
            self._expire(time_ns)
            if x or y:
                if len(self.events) == 1024:
                    self.last_dropped = self._pop()
                self.events.append((time_ns, x, y))
                self.x += x
                self.y += y
        elif self.mode == "ema" and (x or y):
            self.last_motion = time_ns

    def sample(self, time_ns, dt, dx, dy, valid):
        if dt <= 0:
            return (0.0, 0.0)
        if not valid:
            self.clear()
            return (0.0, 0.0)
        if self.mode == "fixed_window":
            self._expire(time_ns)
            if self.last_dropped is not None:
                return (0.0, 0.0)
            dt = self.width_ns / 1e9
            dx, dy = self.x, self.y
        elif self.mode == "ema":
            self.ema = tuple(filter_delta(d, dt, self.tau, prev)
                             for d, prev in zip((dx, dy), self.ema))
            quiet = time_ns - self.last_motion if self.last_motion is not None else math.inf
            if quiet >= 50_000_000 or (quiet >= 12_000_000 and
                                      math.hypot(*self.ema) <= 0.02 * self.threshold):
                self.ema = (0.0, 0.0)
            return (self.ema[0], self.ema[1] * self.y_sign)
        return (dx / dt, dy / dt * self.y_sign)


def map_physical(velocity, threshold, minimum):
    magnitude = math.hypot(*velocity)
    if magnitude == 0:
        return (0.0, 0.0)
    radius = minimum + (1 - minimum) * min(magnitude / threshold, 1.0)
    return tuple(v / magnitude * radius for v in velocity)


def processing_parameters(metadata):
    mode = metadata["processing_mode"]
    window = float(metadata["fixed_window_ms"])
    tau = float(metadata["ema_tau_ms"])
    if (mode not in ("direct", "fixed_window", "ema") or
            not (4 <= window <= 24 and 2 <= tau <= 32) or
            not (window * 2).is_integer() or not (tau * 2).is_integer()):
        raise ValueError("invalid KBM processing mode or duration")
    return mode, window, tau


def recorded_estimates(rows, events, estimator):
    cursor = 1
    for row in rows:
        sequence = int(row["event_sequence"])
        dt = float(row["interval_ms"]) / 1000
        while cursor < len(events) and int(events[cursor]["sequence"]) <= sequence:
            event = events[cursor]
            cursor += 1
            if event["kind"] == "1":
                estimator.clear()
            else:
                estimator.event(round(float(event["event_elapsed_us"]) * 1000),
                                int(event["delta_x"]), int(event["delta_y"]))
        valid = (row["input_active"] == "true" and
                 row["reset_sample"] == row["stale_sample"] == "false")
        yield estimator.sample(round(float(row["elapsed_ms"]) * 1e6), dt,
                               int(row["raw_delta_x"]), int(row["raw_delta_y"]), valid)


def analyze_modes(path, metadata, rows):
    if metadata["mapper"] != "physical_linear_radial":
        raise ValueError("unsupported physical KBM mapper")
    mode, window, tau = processing_parameters(metadata)
    dpi = float(metadata["dpi"])
    speed = float(metadata["full_stick_speed_cm_s"])
    minimum = float(metadata["minimum_response"])
    if not (100 <= dpi <= 32000 and 1 <= speed <= 100 and 0 <= minimum <= .5):
        raise ValueError("invalid physical KBM parameters")
    if any(metadata[k] not in ("true", "false") for k in ("deadzone_compensation", "invert_y")):
        raise ValueError("invalid KBM mapping flag")
    if metadata["deadzone_compensation"] == "false":
        minimum = 0.0
    threshold = dpi * speed / 2.54
    for key in ("samples", "events", "dropped_samples", "dropped_events"):
        if int(metadata[key]) < 0:
            raise ValueError(f"invalid {key}")
    result = {"report": path.name, "schema": "4", "processing_mode": mode,
              "fixed_window_ms": window, "ema_tau_ms": tau,
              "analyzed_samples": len(rows)}
    sidecar = path.with_suffix(".events.csv")
    if not sidecar.exists():
        result["event_bucket_check"] = {"available": False, "reason": "missing sidecar"}
        return result
    event_metadata, events = load_report(sidecar)
    if event_metadata.get("schema") != "4":
        raise ValueError("mismatched event sidecar schema")
    check = check_recorded_events(metadata, rows, events)
    result["event_bucket_check"] = check
    if not check["available"] or check["bucket_mismatches"]:
        return result
    initial = events[0]
    if (initial["reset_pending"] != "true" or
            any(float(initial[k]) for k in ("delta_x", "delta_y", "velocity_x",
                                             "velocity_y", "previous_poll_offset_ms"))):
        raise ValueError("schema 4 requires a clean initial processor state")
    estimator = PhysicalEstimator(mode, window, tau, threshold, metadata["invert_y"] == "true")
    velocity_errors, output_errors = [], []
    for row, velocity in zip(rows, recorded_estimates(rows, events, estimator)):
        velocity_errors.append(math.hypot(
            velocity[0] - float(row["velocity_x"]), velocity[1] - float(row["velocity_y"])))
        expected = map_physical(velocity, threshold, minimum)
        output_errors.append(100 * math.hypot(
            expected[0] - int(row["thumb_x"]) / 32767,
            expected[1] - int(row["thumb_y"]) / 32767))
    result["replay_velocity_error_cps_p99"] = percentile(velocity_errors, .99)
    result["replay_output_error_percentage_points_max"] = max(output_errors, default=0)
    return result


def analyze_physical(path, metadata, rows):
    if metadata.get("mapper") != "physical_linear_radial" or metadata.get("estimator") != "poll_bucket_actual_time":
        raise ValueError("unsupported physical KBM mapper or estimator")
    for key in ("samples", "events", "dropped_samples", "dropped_events"):
        if int(metadata[key]) < 0:
            raise ValueError(f"invalid {key}")
    dpi = float(metadata["dpi"])
    speed = float(metadata["full_stick_speed_cm_s"])
    minimum = float(metadata["minimum_response"])
    if not (100 <= dpi <= 32000 and 1 <= speed <= 100 and 0 <= minimum <= .5):
        raise ValueError("invalid physical KBM parameters")
    for key in ("deadzone_compensation", "invert_y"):
        if metadata[key] not in ("true", "false"):
            raise ValueError(f"invalid {key}")
    if metadata["deadzone_compensation"] == "false":
        minimum = 0.0
    threshold = dpi * speed / 2.54
    invert = metadata["invert_y"] == "true"
    sidecar = path.with_suffix(".events.csv")
    event_check = {"available": False, "reason": "missing sidecar"}
    previous = None
    if sidecar.exists():
        event_metadata, events = load_report(sidecar)
        if event_metadata.get("schema") != "3":
            raise ValueError("mismatched event sidecar schema")
        event_check = check_recorded_events(metadata, rows, events)
        if events and events[0]["kind"] == "2":
            previous = float(events[0]["previous_poll_offset_ms"]) / 1000
    errors, outputs, intervals, differences = [], [], [], []
    short_empty = short_count = 0
    for row in rows:
        now = float(row["elapsed_ms"]) / 1000
        dt = float(row["interval_ms"]) / 1000
        actual_dt = dt if previous is None else now - previous
        if dt > 0:
            previous = now
        raw = [int(row["raw_delta_x"]), int(row["raw_delta_y"])*(1 if invert else -1)]
        active = row["input_active"] == "true"
        reset = row["reset_sample"] == "true" or row["stale_sample"] == "true" or not active
        velocity = [v/dt for v in raw] if dt > 0 and not reset else [0.0, 0.0]
        errors.append(math.hypot(velocity[0]-float(row["velocity_x"]), velocity[1]-float(row["velocity_y"])))
        def mapped(values):
            magnitude = math.hypot(*values)
            if magnitude == 0:
                return [0.0, 0.0]
            radius = minimum + (1-minimum)*min(magnitude/threshold, 1.0)
            return [v/magnitude*radius for v in values]
        expected = mapped(velocity)
        actual = mapped([v/actual_dt for v in raw]) if actual_dt > 0 and not reset else [0.0,0.0]
        outputs.append(100*math.hypot(expected[0]-int(row["thumb_x"])/32767, expected[1]-int(row["thumb_y"])/32767))
        differences.append(100*math.hypot(expected[0]-actual[0], expected[1]-actual[1]))
        if active and not reset and dt > 0:
            intervals.append(dt*1000)
            if dt < .001:
                short_count += 1
                short_empty += raw == [0, 0]
    return {
        "report": path.name, "schema": "3", "mapper": metadata["mapper"],
        "effective_full_stick_axis_speed_cps": threshold,
        "analyzed_samples": len(rows), "event_bucket_check": event_check,
        "sub_ms_samples": short_count, "sub_ms_empty_samples": short_empty,
        "actual_interval_ms_p50": percentile(intervals,.5),
        "replay_velocity_error_cps_p99": percentile(errors,.99),
        "replay_output_error_percentage_points_max": max(outputs,default=0),
        "actual_time_output_difference_percentage_points_p99": percentile(differences,.99),
        "limitations": "Physical poll-bucket replay; not camera response or gameplay frame time measurement.",
    }


def analyze(path):
    metadata, rows = load_report(path)
    if metadata.get("schema") == "4":
        return analyze_modes(path, metadata, rows)
    if metadata.get("schema") == "3":
        return analyze_physical(path, metadata, rows)
    if metadata.get("schema") not in (None, "1", "2"):
        raise ValueError("unsupported KBM report schema")
    threshold = float(metadata["full_scale_velocity"]) / float(metadata["sensitivity"])
    curve = float(metadata["response_curve"])
    tau = float(metadata["smoothing_time_ms"]) / 1000
    minimum = float(metadata["minimum_response"]) if metadata["deadzone_compensation"] == "true" else 0.0
    invert = metadata.get("invert_y", "false") == "true"
    radial = uses_radial_mapper(metadata)
    def mapped(state):
        if not radial:
            return [axis_output(v, threshold, curve, minimum) for v in state]
        speed = math.hypot(*state)
        if speed == 0:
            return [0.0, 0.0]
        radius = minimum + (1 - minimum) * min(speed / threshold, 1.0) ** curve
        return [v / speed * radius for v in state]
    stored_state = [0.0, 0.0]
    actual_state = [0.0, 0.0]
    previous = None
    errors, differences, intervals = [], [], []
    short_empty = short_count = 0
    initialized = False
    sidecar = path.with_suffix(".events.csv")
    event_check = {"available": False, "reason": "legacy or missing sidecar"}
    if metadata.get("schema") == "2" and sidecar.exists():
        event_metadata, events = load_report(sidecar)
        if event_metadata.get("schema") != "2":
            raise ValueError("mismatched event sidecar schema")
        event_check = check_recorded_events(metadata, rows, events)
        if events and events[0]["kind"] == "2":
            initial = events[0]
            stored_state = [float(initial["velocity_x"]), float(initial["velocity_y"])]
            actual_state = stored_state.copy()
            previous = float(initial["previous_poll_offset_ms"]) / 1000
            initialized = True
    for row in rows:
        now = float(row["elapsed_ms"]) / 1000
        dt = float(row["interval_ms"]) / 1000
        actual_dt = dt if previous is None else now - previous
        previous = now
        delta = [int(row["raw_delta_x"]), int(row["raw_delta_y"]) * (1 if invert else -1)]
        active = row["input_active"] == "true"
        reset = row["reset_sample"] == "true" or row["stale_sample"] == "true" or not active
        if reset:
            stored_state = [0.0, 0.0]
            actual_state = [0.0, 0.0]
            initialized = True
        else:
            for axis in range(2):
                stored_state[axis] = filter_delta(delta[axis], dt, tau, stored_state[axis])
                actual_state[axis] = filter_delta(delta[axis], actual_dt, tau, actual_state[axis])
        if not active or reset or not initialized:
            continue
        intervals.append(actual_dt * 1000)
        if 0 < actual_dt < .001:
            short_count += 1
            short_empty += delta == [0, 0]
        errors.append(math.hypot(stored_state[0] - float(row["filtered_velocity_x"]),
                                 stored_state[1] - float(row["filtered_velocity_y"])))
        first, second = mapped(stored_state), mapped(actual_state)
        differences.append(100 * math.hypot(*[first[i] - second[i] for i in range(2)]))
    return {
        "report": path.name,
        "schema": metadata.get("schema", "legacy"),
        "effective_full_stick_axis_speed_cps": threshold,
        "analyzed_samples": len(intervals),
        "event_bucket_check": event_check,
        "sub_ms_samples": short_count,
        "sub_ms_empty_samples": short_empty,
        "actual_interval_ms_p50": percentile(intervals, .5),
        "replay_velocity_error_cps_p99": percentile(errors, .99),
        "actual_time_output_difference_percentage_points_p95": percentile(differences, .95),
        "actual_time_output_difference_percentage_points_p99": percentile(differences, .99),
        "limitations": "Poll-bucket replay before the anti-deadzone stop gate; not camera response measurement.",
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reports", nargs="+", type=Path)
    args = parser.parse_args()
    for report in args.reports:
        print(json.dumps(analyze(report), indent=2))
