#!/usr/bin/env python3
"""Names a hung test: a detached monitor samples and kills any suite process older than a threshold.

ctest waits for a test's output pipe to close, so a daemon that outlives its harness hangs the run
with no name printed. Killing the stale process lets ctest finish that test and report it failed;
the monitor's log (stacks, open files, the process table) lands where CI uploads failure artifacts.
"""

from __future__ import annotations

import os
import re
import signal
import subprocess
import sys
import time

THRESHOLD_S = 90
POLL_S = 15
LIFETIME_S = 3600


def elapsed_seconds(etime: str) -> int:
	days, _, clock = etime.rpartition("-")
	parts = [int(part) for part in clock.split(":")]
	while len(parts) < 3:
		parts.insert(0, 0)
	return int(days or 0) * 86400 + parts[0] * 3600 + parts[1] * 60 + parts[2]


def processes() -> list[tuple[int, int, int, str]]:
	output = subprocess.run(["ps", "-axwwo", "pid=,ppid=,etime=,command="], capture_output=True, text=True).stdout
	rows = []
	for line in output.splitlines():
		match = re.match(r"\s*(\d+)\s+(\d+)\s+(\S+)\s+(.*)", line)
		if match:
			rows.append((int(match[1]), int(match[2]), elapsed_seconds(match[3]), match[4]))
	return rows


def run(log, command: list[str], timeout: int = 60) -> None:
	log.write(f"$ {' '.join(command)}\n")
	log.flush()
	try:
		subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=timeout)
	except (OSError, subprocess.TimeoutExpired) as error:
		log.write(f"  ({error})\n")
	log.flush()


def monitor(ctest: int, roots: list[str], log) -> None:
	started = time.monotonic()
	me = os.getpid()
	while time.monotonic() - started < LIFETIME_S:
		try:
			os.kill(ctest, 0)
		except OSError:
			log.write("ctest exited\n")
			return
		table = processes()
		suite = [row for row in table if row[0] not in (me, ctest) and any(root in row[3] for root in roots)]
		stale = [row for row in suite if row[2] >= THRESHOLD_S]
		if stale:
			log.write(f"\n=== {time.strftime('%H:%M:%S')} stale suite processes (>= {THRESHOLD_S} s) ===\n")
			for pid, ppid, age, command in suite:
				log.write(f"{pid:>7} {ppid:>7} {age:>6}s {command}\n")
			for pid, _, _, command in stale:
				log.write(f"\n--- pid {pid}: {command}\n")
				run(log, ["lsof", "-p", str(pid)])
				if sys.platform == "darwin":
					run(log, ["sample", str(pid), "2"])
				else:
					run(log, ["gdb", "-p", str(pid), "-batch", "-ex", "thread apply all bt"])
			for pid, _, _, _ in stale:
				try:
					os.kill(pid, signal.SIGKILL)
					log.write(f"killed {pid}\n")
				except OSError as error:
					log.write(f"kill {pid}: {error}\n")
			log.flush()
		time.sleep(POLL_S)


def main() -> int:
	log_path, roots = sys.argv[1], sys.argv[2:]
	os.makedirs(os.path.dirname(log_path), exist_ok=True)
	ctest = os.getppid()
	if os.fork():
		print(f"hang watchdog for ctest {ctest}: {log_path}")
		return 0
	os.setsid()
	if os.fork():
		os._exit(0)
	# Detached from ctest's pipes, so the monitor itself never holds the run open.
	with open(log_path, "a", buffering=1) as log:
		null = os.open(os.devnull, os.O_RDWR)
		for fd in (0, 1, 2):
			os.dup2(null if fd == 0 else log.fileno(), fd)
		log.write(f"monitor {os.getpid()} watching ctest {ctest}, roots {roots}\n")
		monitor(ctest, roots, log)
	os._exit(0)


if __name__ == "__main__":
	sys.exit(main())
