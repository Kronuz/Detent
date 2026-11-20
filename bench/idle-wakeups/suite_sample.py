#!/usr/bin/env python3
"""Runs a command (the suite) and samples what could starve a small CI runner while it runs.

usage: suite_sample.py ROOT -- CMD...

Every 2 s: TCP sockets by state (TIME_WAIT holds an ephemeral port), and every process whose command
line names ROOT, with its CPU time, age and resident memory. Prints the peaks and the processes that
spent the largest share of their life on CPU. Exits with CMD's status.
"""

from __future__ import annotations

import collections
import re
import subprocess
import sys
import time

INTERVAL_S = 2.0


def seconds(clock: str) -> float:
    days, _, clock = clock.rpartition("-")
    parts = [float(part) for part in clock.split(":")]
    total = 0.0
    for part in parts:
        total = total * 60 + part
    return int(days or 0) * 86400 + total


def tcp_states() -> collections.Counter[str]:
    output = subprocess.run(
        ["netstat", "-an", "-p", "tcp"], capture_output=True, text=True, check=False
    ).stdout
    states: collections.Counter[str] = collections.Counter()
    for line in output.splitlines():
        fields = line.split()
        if fields and fields[0].startswith("tcp"):
            states[fields[-1]] += 1
    return states


def suite_processes(root: str) -> list[tuple[int, float, float, int, str]]:
    output = subprocess.run(
        ["ps", "-axwwo", "pid=,time=,etime=,rss=,command="],
        capture_output=True,
        text=True,
        check=False,
    ).stdout
    rows = []
    for line in output.splitlines():
        match = re.match(r"\s*(\d+)\s+(\S+)\s+(\S+)\s+(\d+)\s+(.*)", line)
        if match and root in match[5] and "suite_sample.py" not in match[5]:
            rows.append(
                (
                    int(match[1]),
                    seconds(match[2]),
                    seconds(match[3]),
                    int(match[4]),
                    match[5],
                )
            )
    return rows


def main() -> int:
    root, separator, *command = sys.argv[1:]
    if separator != "--" or not command:
        sys.exit(__doc__)
    child = subprocess.Popen(command)
    began = time.monotonic()
    peaks: collections.Counter[str] = collections.Counter()
    peak_rss = peak_count = 0
    # When TIME_WAIT peaked, and the suite's programs then.
    waits_at: tuple[float, list[str]] = (0.0, [])
    # (pid, command) -> (cpu, age) at its last sample.
    busiest: dict[tuple[int, str], tuple[float, float]] = {}
    while child.poll() is None:
        states = tcp_states()
        rows = suite_processes(root)
        if states["TIME_WAIT"] > peaks["TIME_WAIT"]:
            waits_at = (
                time.monotonic() - began,
                sorted({row[4].split()[0].rsplit("/", 1)[-1] for row in rows}),
            )
        for state, count in states.items():
            peaks[state] = max(peaks[state], count)
        peak_count = max(peak_count, len(rows))
        peak_rss = max(peak_rss, sum(row[3] for row in rows))
        for pid, cpu, age, _, name in rows:
            busiest[(pid, name)] = (cpu, age)
        time.sleep(INTERVAL_S)
    print(f"suite sample: {time.monotonic() - began:.0f} s, exit {child.returncode}")
    print(
        "peak tcp sockets by state: "
        + ", ".join(f"{state} {count}" for state, count in peaks.most_common())
    )
    print(f"TIME_WAIT peaked at {waits_at[0]:.0f} s, running: {' '.join(waits_at[1])}")
    print(
        f"peak suite processes {peak_count}, peak suite rss {peak_rss / 1024:.0f} MiB"
    )
    print("largest CPU share of life (age >= 10 s): cpu s / age s, command")
    ranked = sorted(
        (
            (cpu / age, cpu, age, name)
            for (_, name), (cpu, age) in busiest.items()
            if age >= 10
        ),
        reverse=True,
    )
    for share, cpu, age, name in ranked[:15]:
        print(f"  {share:5.2f}  {cpu:8.1f} / {age:6.0f}  {name[:160]}")
    return child.returncode


if __name__ == "__main__":
    sys.exit(main())
