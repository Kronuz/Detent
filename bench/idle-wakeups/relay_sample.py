#!/usr/bin/env python3
"""Runs a command (one relay test) and samples the machine's network and the relay processes it starts.

usage: relay_sample.py ROOT -- CMD...

Every 0.25 s: TCP sockets by state, bytes queued in socket buffers, the kernel's network memory in use
(`netstat -m`), and CPU time and resident memory of every process whose command line names ROOT. Prints
the peaks, the busiest processes by CPU share of their life, and a line per second of the TCP states.
Exits with CMD's status.
"""

from __future__ import annotations

import collections
import re
import subprocess
import sys
import time

INTERVAL_S = 0.25


def seconds(clock: str) -> float:
    days, _, clock = clock.rpartition("-")
    total = 0.0
    for part in clock.split(":"):
        total = total * 60 + float(part)
    return int(days or 0) * 86400 + total


def network() -> tuple[collections.Counter[str], int, int]:
    output = subprocess.run(
        ["netstat", "-an", "-p", "tcp"], capture_output=True, text=True, check=False
    ).stdout
    states: collections.Counter[str] = collections.Counter()
    queued = 0
    for line in output.splitlines():
        fields = line.split()
        if fields and fields[0].startswith("tcp"):
            states[fields[-1]] += 1
            queued += int(fields[1]) + int(fields[2])
    memory = subprocess.run(
        ["netstat", "-m"], capture_output=True, text=True, check=False
    ).stdout
    match = re.search(r"(\d+) KB allocated to network \(([\d.]+)% in use\)", memory)
    in_use = int(int(match[1]) * float(match[2]) / 100) if match else 0
    return states, queued, in_use


def processes(root: str) -> list[tuple[int, float, float, int, str]]:
    output = subprocess.run(
        ["ps", "-axwwo", "pid=,time=,etime=,rss=,command="],
        capture_output=True,
        text=True,
        check=False,
    ).stdout
    rows = []
    for line in output.splitlines():
        match = re.match(r"\s*(\d+)\s+(\S+)\s+(\S+)\s+(\d+)\s+(.*)", line)
        if match and root in match[5] and "relay_sample.py" not in match[5]:
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
    peak_queued = peak_network = peak_rss = 0
    busiest: dict[tuple[int, str], tuple[float, float, int]] = {}
    per_second: dict[int, collections.Counter[str]] = {}
    while child.poll() is None:
        states, queued, in_use = network()
        rows = processes(root)
        for state, count in states.items():
            peaks[state] = max(peaks[state], count)
        peak_queued = max(peak_queued, queued)
        peak_network = max(peak_network, in_use)
        peak_rss = max(peak_rss, sum(row[3] for row in rows))
        for pid, cpu, age, rss, name in rows:
            _, _, top = busiest.get((pid, name), (0.0, 0.0, 0))
            busiest[(pid, name)] = (cpu, age, max(top, rss))
        second = int(time.monotonic() - began)
        if second not in per_second:
            per_second[second] = states
        time.sleep(INTERVAL_S)
    print(f"relay sample: {time.monotonic() - began:.0f} s, exit {child.returncode}")
    print(
        "peak tcp sockets by state: "
        + ", ".join(f"{state} {count}" for state, count in peaks.most_common())
    )
    print(
        f"peak socket bytes queued {peak_queued}, peak network memory in use {peak_network} KB, "
        f"peak rss {peak_rss / 1024:.0f} MiB"
    )
    print("busiest (age >= 2 s): cpu s / age s, peak rss MiB, command")
    ranked = sorted(
        (
            (cpu / age, cpu, age, rss, name)
            for (_, name), (cpu, age, rss) in busiest.items()
            if age >= 2
        ),
        reverse=True,
    )
    for share, cpu, age, rss, name in ranked[:12]:
        print(
            f"  {share:5.2f}  {cpu:7.1f} / {age:5.0f}  {rss / 1024:6.0f}  {name[:140]}"
        )
    print("per second: ESTABLISHED TIME_WAIT SYN_SENT CLOSE_WAIT")
    for second, states in sorted(per_second.items()):
        print(
            f"  {second:4d} {states['ESTABLISHED']:6d} {states['TIME_WAIT']:6d} "
            f"{states['SYN_SENT']:5d} {states['CLOSE_WAIT']:5d}"
        )
    return child.returncode


if __name__ == "__main__":
    sys.exit(main())
