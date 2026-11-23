#!/usr/bin/env python3
"""Names a hung test: a detached monitor samples and kills any suite process older than a threshold.

ctest waits for a test's output pipe to close, so a daemon that outlives its harness hangs the run
with no name printed. Killing the stale process lets ctest finish that test and report it failed;
the monitor's log (stacks, open files, the process table) lands where CI uploads failure artifacts.
Every poll also logs the machine's load, memory, swap, TCP states and busiest processes, and past
DEADLINE_S the monitor ends the suite, so a starved runner's step still ends and uploads the log.
"""

from __future__ import annotations

import os
import re
import signal
import subprocess
import sys
import time

THRESHOLD_S = 300
POLL_S = 15
DEADLINE_S = 1200  # passing macOS CI suites take 6 to 15 min; the step's cap is 50


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


def output(command: list[str]) -> str:
	try:
		return subprocess.run(command, capture_output=True, text=True, timeout=30).stdout
	except (OSError, subprocess.TimeoutExpired) as error:
		return f"({error})"


def resources(log, suite_count: int) -> None:
	load = " ".join(f"{value:.1f}" for value in os.getloadavg())
	vm = dict(re.findall(r"^(Pages free|Pages active|Pages wired down|Pageouts|Swapouts):\s+(\d+)", output(["vm_stat"]), re.M))
	swap = output(["sysctl", "-n", "vm.swapusage"]).strip()
	tcp: dict[str, int] = {}
	for line in output(["netstat", "-an", "-p", "tcp"]).splitlines():
		fields = line.split()
		if fields and fields[0].startswith("tcp"):
			tcp[fields[-1]] = tcp.get(fields[-1], 0) + 1
	log.write(f"\n--- {time.strftime('%H:%M:%S')} load {load}, suite processes {suite_count}, vm {vm}, swap {swap}, tcp {tcp}\n")
	rows = output(["ps", "-axwwo", "pid=,%cpu=,rss=,state=,etime=,command="]).splitlines()
	parsed = [row.split(None, 5) for row in rows]
	parsed = [row for row in parsed if len(row) == 6]
	for key, name in ((1, "cpu"), (2, "rss")):
		log.write(f"top {name}:\n")
		for row in sorted(parsed, key=lambda row: float(row[key]), reverse=True)[:6]:
			log.write(f"  {row[0]:>7} {row[1]:>5}% {int(row[2]) // 1024:>6} MiB {row[3]:<4} {row[4]:>9} {row[5][:160]}\n")
	log.flush()


def end_suite(ctest: int, suite: list[tuple[int, int, int, str]], log) -> None:
	log.write(f"\n=== {time.strftime('%H:%M:%S')} past {DEADLINE_S} s: ending the suite ===\n")
	for pid, ppid, age, command in suite:
		log.write(f"{pid:>7} {ppid:>7} {age:>6}s {command}\n")
	for pid, _, _, command in suite[:8]:
		log.write(f"\n--- pid {pid}: {command}\n")
		run(log, ["sample", str(pid), "1"] if sys.platform == "darwin" else ["gdb", "-p", str(pid), "-batch", "-ex", "thread apply all bt"])
	for pid in [row[0] for row in suite] + [ctest]:
		try:
			os.kill(pid, signal.SIGKILL)
		except OSError:
			pass
	log.write("killed the suite and ctest\n")
	log.flush()


def monitor(ctest: int, roots: list[str], log) -> None:
	started = time.monotonic()
	me = os.getpid()
	while True:
		try:
			os.kill(ctest, 0)
		except OSError:
			log.write("ctest exited\n")
			return
		table = processes()
		suite = [row for row in table if row[0] not in (me, ctest) and any(root in row[3] for root in roots)]
		resources(log, len(suite))
		if time.monotonic() - started >= DEADLINE_S:
			end_suite(ctest, suite, log)
			return
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
