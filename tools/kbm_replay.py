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


def analyze(path):
    metadata, rows = load_report(path)
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
        _, events = load_report(sidecar)
        if int(metadata.get("dropped_events", "0")) or int(metadata.get("dropped_samples", "0")):
            event_check = {"available": False, "reason": "truncated recording"}
        else:
            event_check = check_event_buckets(rows, events)
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
