#!/usr/bin/env python3
"""Offline estimator experiment; no emulator, device or config access.

Constant 1000 counts/s, 8ms filter/window, one-second warm-up. Compare device
report rate, consumer query rate/phase and simulated batched host delivery.
Results measure an estimator, not in-game turning or subjective input quality.
"""

import json
import math
from collections import deque


def run_case(device_hz, query_hz, phase, batch_ms=0):
    event_period = round(1_000_000_000 / device_hz)
    query_period = round(1_000_000_000 / query_hz)
    tau_seconds = 0.008
    window_ns = 8_000_000
    batch_ns = round(batch_ms * 1_000_000)
    delta = 1000 * event_period / 1_000_000_000
    events = []
    for timestamp in range(event_period, 3_000_000_001, event_period):
        delivered = ((timestamp + batch_ns - 1) // batch_ns) * batch_ns if batch_ns else timestamp
        events.append((delivered, delta))

    cursor = 0
    state = 0.0
    last_event = 0
    history = deque()
    impulse_values = []
    window_values = []
    first_query = 1_000_000_000 + round(query_period * phase)
    for now in range(first_query, 3_000_000_000, query_period):
        while cursor < len(events) and events[cursor][0] <= now:
            timestamp, movement = events[cursor]
            state = state * math.exp(-(timestamp - last_event) / 1e9 / tau_seconds) + movement / tau_seconds
            last_event = timestamp
            history.append((timestamp, movement))
            cursor += 1
        while history and history[0][0] <= now - window_ns:
            history.popleft()
        impulse_values.append(state * math.exp(-(now - last_event) / 1e9 / tau_seconds))
        window_values.append(sum(movement for _, movement in history) / tau_seconds)

    def summarize(values):
        return {
            "mean_gain_error_percent": round((sum(values) / len(values) / 1000 - 1) * 100, 4),
            "min_gain": round(min(values) / 1000, 6),
            "max_gain": round(max(values) / 1000, 6),
            "query_count": len(values),
        }

    return {
        "device_hz": device_hz,
        "query_hz": query_hz,
        "query_phase": phase,
        "host_batch_ms": batch_ms,
        "event_exponential": summarize(impulse_values),
        "fixed_window": summarize(window_values),
    }


def report():
    cases = [run_case(device, query, phase, batch)
             for device in (125, 500, 1000)
             for query in (60, 125, 1000)
             for phase in (0.0, 0.25, 0.5, 0.75)
             for batch in (0, 3, 4)]
    return {
        "description": __doc__,
        "worst_absolute_mean_gain_error_percent": {
            estimator: max(abs(case[estimator]["mean_gain_error_percent"]) for case in cases)
            for estimator in ("event_exponential", "fixed_window")
        },
        "cases": cases,
    }


if __name__ == "__main__":
    print(json.dumps(report(), indent=2))
