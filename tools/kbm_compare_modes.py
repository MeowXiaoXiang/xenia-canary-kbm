#!/usr/bin/env python3
"""Compare KBM mouse processors on a continuous section of a recorded trace."""

import argparse
import json
import math
from bisect import bisect_right
from pathlib import Path

from kbm_replay import (PhysicalEstimator, analyze, load_report, map_physical,
                        percentile)


MODE_PARAMETERS = [
    ("direct", 8, 8),
    *(("fixed_window", w, 4) for w in (4, 8, 12, 16, 24)),
    *(("ema", 16, tau) for tau in (2, 4, 6, 8, 12)),
]

# Rounding two signed stick axes contributes at most about 0.00216 points.
SCHEMA_FOUR_STICK_ERROR_LIMIT_PP = 0.003


def mode_label(mode, window, tau):
    return mode if mode == "direct" else f"{mode}_{window if mode == 'fixed_window' else tau}ms"


def continuous_section(rows):
    sections = [[]]
    for row in rows:
        if (row["input_active"] == "true" and
                row["reset_sample"] == row["stale_sample"] == "false"):
            sections[-1].append(row)
        elif sections[-1]:
            sections.append([])
    sections = [s for s in sections if len(s) >= 2]
    if not sections:
        raise ValueError("no continuous active input section")
    return max(sections, key=lambda s: float(s[-1]["elapsed_ms"]) - float(s[0]["elapsed_ms"]))


def recorded_schedule(section, origin_ns):
    return [(round(float(row["elapsed_ms"]) * 1e6) - origin_ns, int(row["event_sequence"]))
            for row in section[1:]]


def synthetic_schedule(end_ns, steps_ms, stall_every=None):
    now = 0
    index = 0
    while True:
        delay = steps_ms[index % len(steps_ms)]
        index += 1
        if stall_every and index % stall_every == 0:
            delay = 128
        now += round(delay * 1e6)
        if now > end_ns:
            return
        yield now, None


def simulate(events, schedule, parameters, threshold, minimum, invert_y=False):
    """Run a clean active segment. A 100 ms poll timeout clears all history."""
    estimator = PhysicalEstimator(*parameters, threshold, invert_y)
    cursor = 0
    previous = 0
    for now, sequence in schedule:
        dx = dy = 0
        # A same-time query preserves its pending events for the next poll.
        if now <= previous:
            yield now, (0.0, 0.0), (0.0, 0.0), False
            continue
        while cursor < len(events):
            t, seq, x, y = events[cursor]
            visible = seq <= sequence if sequence is not None else t <= now
            if not visible:
                break
            cursor += 1
            estimator.event(t, x, y)
            dx += x
            dy += y
        dt = (now - previous) / 1e9
        previous = now
        valid = dt <= 0.1
        velocity = estimator.sample(now, dt, dx, dy, valid)
        yield now, velocity, map_physical(velocity, threshold, minimum), valid


class MotionHistory:
    def __init__(self, events):
        self.times = []
        self.x = [0]
        self.y = [0]
        self.distance = [0.0]
        for t, _, x, y in events:
            if x or y:
                self.times.append(t)
                self.x.append(self.x[-1] + x)
                self.y.append(self.y[-1] + y)
                self.distance.append(self.distance[-1] + math.hypot(x, y))

    def steady_low_speed(self, now, dpi):
        a = bisect_right(self.times, now - 32_000_000)
        b = bisect_right(self.times, now)
        net = math.hypot(self.x[b] - self.x[a], self.y[b] - self.y[a])
        speed = net * 2.54 / (dpi * .032)
        # This independent reference selects roughly straight, slow movement.
        return .25 <= speed <= 1.5 and net >= .8 * (self.distance[b] - self.distance[a])


def summarize_trace(samples, history, dpi, threshold, reference):
    usable = [(i, t, v, out) for i, (t, v, out, valid) in enumerate(samples)
              if valid and t >= 50_000_000 and reference[i][3]]
    moving = sum(bool(x or y) for _, _, _, (x, y) in usable)
    saturated = sum(math.hypot(*v) >= threshold for _, _, v, _ in usable)
    low = [(i, out) for i, t, _, out in usable if history.steady_low_speed(t, dpi)]
    steps = [100 * math.dist(previous, current)
             for (a, previous), (b, current) in zip(low, low[1:]) if b == a + 1]
    differences = [100 * math.dist(out, reference[i][2]) for i, _, _, out in usable]
    return {
        "usable_polls": len(usable),
        "low_speed_polls": len(low),
        "low_speed_zero_pct": 100 * sum(out == (0, 0) for _, out in low) / len(low) if low else None,
        "low_speed_step_pp_p95": percentile(steps, .95),
        "moving_polls": moving,
        "full_stick_pct_of_moving": 100 * saturated / moving if moving else None,
        "difference_from_direct_pp_p95": percentile(differences, .95),
    }


def compare_trace(path):
    meta, rows = load_report(path)
    if meta.get("schema") not in ("3", "4"):
        raise ValueError("comparison requires a physical schema 3 or 4 report")
    replay = analyze(path)
    check = replay["event_bucket_check"]
    if not check["available"] or check["bucket_mismatches"]:
        raise ValueError(f"comparison requires a complete event trace: {check}")
    if meta["schema"] == "4":
        error = replay["replay_output_error_percentage_points_max"]
        if not 0.0 <= error <= SCHEMA_FOUR_STICK_ERROR_LIMIT_PP:
            raise ValueError(
                f"{path.name}: stick replay differs from the recorded output "
                f"by up to {error:.6g} percentage points "
                f"(limit {SCHEMA_FOUR_STICK_ERROR_LIMIT_PP:g})"
            )
    _, events = load_report(path.with_suffix(".events.csv"))
    section = continuous_section(rows)
    origin = round(float(section[0]["elapsed_ms"]) * 1e6)
    schedule = recorded_schedule(section, origin)
    end = schedule[-1][0]
    first_seq = int(section[0]["event_sequence"])
    last_seq = schedule[-1][1]
    trace = []
    for event in events:
        seq = int(event["sequence"])
        if first_seq < seq <= last_seq:
            if event["kind"] != "0":
                raise ValueError("unexpected reset inside continuous input section")
            trace.append((round(float(event["event_elapsed_us"]) * 1000) - origin,
                          seq, int(event["delta_x"]), int(event["delta_y"])))
    dpi = float(meta["dpi"])
    speed = float(meta["full_stick_speed_cm_s"])
    threshold = dpi * speed / 2.54
    minimum = float(meta["minimum_response"]) if meta["deadzone_compensation"] == "true" else 0
    invert_y = meta["invert_y"] == "true"
    history = MotionHistory(trace)
    schedules = {"recorded": schedule}
    for name, steps in (("4ms", (4,)), ("8ms", (8,)), ("16ms", (16,)),
                        ("jitter", (2, 14, 4, 12)), ("burst", (.25, .25, 15.5)),
                        ("stall", (8,))):
        schedules[name] = list(synthetic_schedule(end, steps, 100 if name == "stall" else None))

    results = {}
    for schedule_name, times in schedules.items():
        run = [list(simulate(trace, times, p, threshold, minimum, invert_y)) for p in MODE_PARAMETERS]
        results[schedule_name] = {
            mode_label(*p): summarize_trace(samples, history, dpi, threshold, run[0])
            for p, samples in zip(MODE_PARAMETERS, run)
        }
    return {"report": path.name, "dpi": dpi, "full_stick_speed_cm_s": speed,
            "minimum_response": minimum, "active_section_seconds": end / 1e9,
            "event_bucket_check": check, "schedules": results}


def controlled_motion():
    # Ground truth: 1 cm/s until 240 ms, then 8 cm/s starting at 320 ms,
    # instant reversal at 440 ms, and rest at 560 ms. Quantize physical
    # positions to whole counts at the device's report rate.
    results = []
    for dpi in (800, 1600, 3200):
        for report_ms in (1, 8):
            events = []
            position = 0.0
            previous_counts = 0
            for t in range(report_ms, 641, report_ms):
                midpoint = t - report_ms / 2
                speed = (1 if midpoint < 240 else 8 if 320 <= midpoint < 440
                         else -8 if 440 <= midpoint < 560 else 0)
                position += speed * report_ms / 1000
                counts = round(position * dpi / 2.54)
                if counts != previous_counts:
                    events.append((t * 1_000_000, len(events) + 1, counts - previous_counts, 0))
                previous_counts = counts
            for phase_ms in (0, 2, 4, 6):
                schedule = [(t * 1_000_000, None) for t in range(8 + phase_ms, 641, 8)]
                for parameters in MODE_PARAMETERS:
                    samples = list(simulate(events, schedule, parameters, dpi * 4 / 2.54, .3))
                    slow = [out[0] for t, _, out, _ in samples if 80 <= t / 1e6 < 240]

                    def first_delay(start_ms, end_ms, predicate):
                        return next((t / 1e6 - start_ms for t, velocity, out, _ in samples
                                     if start_ms <= t / 1e6 < end_ms
                                     and predicate(velocity, out)), None)

                    last_motion_index = max(
                        (i for i, (_, _, out, _) in enumerate(samples) if out[0] != 0),
                        default=-1)
                    stop_delay = None
                    if 0 <= last_motion_index < len(samples) - 1:
                        stop_delay = max(0, samples[last_motion_index + 1][0] / 1e6 - 560)
                    results.append({
                        "dpi": dpi, "report_ms": report_ms, "poll_phase_ms": phase_ms,
                        "mode": mode_label(*parameters),
                        "low_speed_zero_pct": 100 * slow.count(0) / len(slow),
                        "low_speed_span_pp": 100 * (max(slow) - min(slow)),
                        "full_stick_delay_ms": first_delay(
                            320, 440, lambda velocity, _: velocity[0] >= dpi * 4 / 2.54),
                        "reverse_delay_ms": first_delay(440, 560, lambda _, out: out[0] < 0),
                        "stop_delay_ms": stop_delay,
                    })
    return results


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reports", nargs="*", type=Path)
    args = parser.parse_args()
    print(json.dumps({"traces": [compare_trace(p) for p in args.reports],
                      "controlled_motion": controlled_motion()}, indent=2))
