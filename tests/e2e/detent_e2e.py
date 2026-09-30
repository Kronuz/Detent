#!/usr/bin/env python3
"""Real-process end-to-end harness for detentd's Raft cluster behavior.

Spawns actual `detentd` subprocesses (not in-process API calls), drives
them over real HTTP, and injects real OS signals (SIGKILL/SIGTERM) -- the
things the in-process C++ test suite structurally cannot do. See README.md
in this directory for why this exists and what each scenario covers.

Stdlib only, no third-party dependencies.
"""
from __future__ import annotations

import argparse
import glob
import json
import os
import random
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from dataclasses import dataclass, field


# --------------------------------------------------------------------------
# HTTP helpers (stdlib only)
# --------------------------------------------------------------------------

def _request(method: str, url: str, body: bytes | None = None, timeout: float = 5.0):
	req = urllib.request.Request(url, data=body, method=method)
	if body is not None:
		req.add_header("Content-Type", "application/json")
	try:
		with urllib.request.urlopen(req, timeout=timeout) as resp:
			return resp.status, resp.read(), dict(resp.headers)
	except urllib.error.HTTPError as e:
		return e.code, e.read(), dict(e.headers)


def http_get(base_url: str, path: str, timeout: float = 5.0):
	return _request("GET", base_url + path, timeout=timeout)


def http_put(base_url: str, path: str, body: dict, timeout: float = 5.0):
	return _request("PUT", base_url + path, json.dumps(body).encode(), timeout=timeout)


# --------------------------------------------------------------------------
# Process management: a real detentd subprocess, not an in-process object.
# --------------------------------------------------------------------------

def find_free_port() -> int:
	with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
		s.bind(("127.0.0.1", 0))
		return s.getsockname()[1]


@dataclass
class Node:
	binary: str
	port: int
	persist_path: str
	cluster_token: str
	mcast_port: int
	mcast_group: str
	peers: list[str]
	raft_timing: tuple[float, float, float, float] | None  # heartbeat, init, min, max
	proc: subprocess.Popen | None = field(default=None, init=False)

	@property
	def self_address(self) -> str:
		return f"127.0.0.1:{self.port}"

	@property
	def base_url(self) -> str:
		return f"http://127.0.0.1:{self.port}"

	def start(self) -> None:
		args = [
			self.binary, str(self.port), self.persist_path,
			"--cluster", self.cluster_token, str(self.mcast_port), self.mcast_group, "127.0.0.1",
			self.self_address, *self.peers,
		]
		if self.raft_timing is not None:
			args += ["--raft-timing", *[str(v) for v in self.raft_timing]]
		self.proc = subprocess.Popen(args, stdout=self._out(), stderr=self._out())

	def _out(self):
		if os.environ.get("E2E_DEBUG"):
			debug_dir = os.path.expanduser("~/.scratch/detentd-e2e-debug")
			os.makedirs(debug_dir, exist_ok=True)
			path = os.path.join(debug_dir, f"detentd-{self.port}.log")
			return open(path, "a")
		return subprocess.DEVNULL

	def kill(self, sig: int = signal.SIGKILL) -> None:
		if self.proc is not None and self.proc.poll() is None:
			self.proc.send_signal(sig)
			self.proc.wait(timeout=5)
		self.proc = None

	def is_running(self) -> bool:
		return self.proc is not None and self.proc.poll() is None

	def health(self) -> dict | None:
		if not self.is_running():
			return None
		try:
			status, body, _ = http_get(self.base_url, "/v1/health", timeout=1.0)
			return json.loads(body) if status == 200 else None
		except (urllib.error.URLError, socket.timeout, ConnectionError):
			return None

	def snapshot(self):
		if not self.is_running():
			return None
		try:
			status, body, _ = http_get(self.base_url, "/v1/snapshot", timeout=1.0)
			return (status, body)
		except (urllib.error.URLError, socket.timeout, ConnectionError):
			return None


@dataclass
class Cluster:
	nodes: list[Node]
	tmpdir: str

	def start_all(self) -> None:
		for n in self.nodes:
			n.start()

	def stop_all(self) -> None:
		for n in self.nodes:
			if n.is_running():
				n.kill(signal.SIGKILL)

	def live_nodes(self) -> list[Node]:
		return [n for n in self.nodes if n.is_running()]

	def wait_for_election(self, timeout: float) -> Node | None:
		deadline = time.monotonic() + timeout
		while time.monotonic() < deadline:
			leaders = [n for n in self.live_nodes() if (h := n.health()) and h.get("is_leader")]
			if len(leaders) == 1:
				return leaders[0]
			time.sleep(0.1)
		return None

	def wait_until(self, predicate, timeout: float) -> bool:
		deadline = time.monotonic() + timeout
		while time.monotonic() < deadline:
			if predicate():
				return True
			time.sleep(0.1)
		return predicate()


def make_cluster(binary: str, tmpdir: str, n: int, raft_timing=None, token_suffix: str = "") -> Cluster:
	ports = [find_free_port() for _ in range(n)]
	mcast_port = find_free_port()
	mcast_group = f"239.255.{random.randint(1, 250)}.{random.randint(1, 250)}"
	token = f"e2e-{token_suffix}-{mcast_port}"
	addrs = [f"127.0.0.1:{p}" for p in ports]
	nodes = []
	for i in range(n):
		peers = [a for j, a in enumerate(addrs) if j != i]
		nodes.append(Node(
			binary=binary, port=ports[i],
			persist_path=os.path.join(tmpdir, f"n{i}.json"),
			cluster_token=token, mcast_port=mcast_port, mcast_group=mcast_group,
			peers=peers, raft_timing=raft_timing,
		))
	return Cluster(nodes=nodes, tmpdir=tmpdir)


# --------------------------------------------------------------------------
# The two properties every scenario ultimately checks.
# --------------------------------------------------------------------------

class Harness:
	def __init__(self, verbose: bool = True):
		self.verbose = verbose

	def log(self, msg: str) -> None:
		if self.verbose:
			print(f"    {msg}")

	def current_version(self, cluster: Cluster, verbose: bool = True) -> int:
		"""Highest version any live node currently reports -- the source
		of truth for "what to write next", never a locally-tracked
		counter (which can drift out of sync with what actually
		committed, e.g. after a request that timed out client-side but
		still committed server-side)."""
		per_node = {n.self_address: n.health() for n in cluster.live_nodes()}
		if verbose and os.environ.get("E2E_DEBUG"):
			for addr, h in per_node.items():
				print(f"      [debug] health {addr}: {h}")
		versions = [h["current_version"] for h in per_node.values() if h]
		return max(versions, default=0)

	def assert_safety(self, cluster: Cluster, converge_timeout: float = 3.0) -> None:
		"""For any version number, every node that HAS that version must
		agree on its content -- a node merely lagging behind (different
		version, or no snapshot yet) is normal replication lag, not a
		fork. Waits briefly for lagging nodes to catch up first so a
		momentary lag right after a write doesn't look like one."""
		def versions():
			by_version: dict[int, set[bytes]] = {}
			for n in cluster.live_nodes():
				s = n.snapshot()
				if s is None or s[0] != 200:
					continue
				info = json.loads(s[1])
				by_version.setdefault(info["version"], set()).add(s[1])
			return by_version

		deadline = time.monotonic() + converge_timeout
		by_version = versions()
		while time.monotonic() < deadline and any(len(v) > 1 for v in by_version.values()):
			time.sleep(0.2)
			by_version = versions()
		forks = {v: bodies for v, bodies in by_version.items() if len(bodies) > 1}
		if forks:
			raise AssertionError(f"SAFETY VIOLATION: version(s) with disagreeing content: {forks}")

	def assert_eventually_writable(self, cluster: Cluster, version: int, timeout: float = 10.0) -> None:
		"""A write must eventually succeed against *some* live node. A
		request can legitimately time out (503) even though it already
		committed via quorum -- e.g. a just-restarted node forwards the
		write, quorum commits fast on the other two, but that node's own
		catch-up replay is too slow to locally confirm it before its own
		wait budget -- so a non-200 does NOT mean the version is free;
		re-checking the cluster's own reported version (not a locally
		retried attempt) is the only way to tell "already done" apart
		from "really still needed"."""
		body = {"schema_version": 1, "version": version,
		        "knobs": {"e2e:probe": {"type": "int", "default": version}}}
		deadline = time.monotonic() + timeout
		last_error = None
		while time.monotonic() < deadline:
			for n in cluster.live_nodes():
				try:
					status, resp_body, _ = http_put(n.base_url, "/v1/snapshot", body, timeout=12.0)
					if os.environ.get("E2E_DEBUG"):
						print(f"      [debug] PUT v{version} -> {n.self_address}: {status} {resp_body[:80]!r}")
					if status == 200:
						return
					last_error = f"{status}: {resp_body!r}"
				except (urllib.error.URLError, socket.timeout, ConnectionError) as e:
					if os.environ.get("E2E_DEBUG"):
						print(f"      [debug] PUT v{version} -> {n.self_address}: EXC {e}")
					last_error = str(e)
				if self.current_version(cluster, verbose=False) >= version:
					return  # committed elsewhere while we were waiting/retrying -- done
			time.sleep(0.2)
		if os.environ.get("E2E_DEBUG"):
			for n in cluster.live_nodes():
				print(f"      [debug] final health {n.self_address}: {n.health()}")
		raise AssertionError(f"write never succeeded within {timeout}s (last: {last_error})")


# --------------------------------------------------------------------------
# Scenarios
# --------------------------------------------------------------------------

def scenario_fresh_bootstrap_fast(cluster: Cluster, h: Harness) -> None:
	cluster.start_all()
	start = time.monotonic()
	leader = cluster.wait_for_election(timeout=5.0)
	elapsed = time.monotonic() - start
	assert leader is not None, "no leader elected within 5s (fast timing)"
	h.log(f"leader elected in {elapsed:.2f}s")
	h.assert_eventually_writable(cluster, version=1)
	h.assert_safety(cluster)


def scenario_fresh_bootstrap_production_timing(cluster: Cluster, h: Harness) -> None:
	# cluster.raft_timing is None here -> real defaults (up to 30s election).
	cluster.start_all()
	start = time.monotonic()
	leader = cluster.wait_for_election(timeout=40.0)
	elapsed = time.monotonic() - start
	assert leader is not None, "no leader elected within 40s (production timing)"
	# The bootstrap-latency bug made this ~90s (3x the 30s election_max);
	# a correct fresh bootstrap elects within one normal election cycle.
	assert elapsed < 35.0, f"took {elapsed:.1f}s -- looks like the 3x-silence bug is back"
	h.log(f"leader elected in {elapsed:.2f}s (production timing)")
	h.assert_eventually_writable(cluster, version=1, timeout=15.0)
	h.assert_safety(cluster)


def scenario_put_to_follower_forwards(cluster: Cluster, h: Harness) -> None:
	cluster.start_all()
	leader = cluster.wait_for_election(timeout=5.0)
	assert leader is not None
	follower = next(n for n in cluster.nodes if n.self_address != leader.self_address)
	status, body, headers = http_put(follower.base_url, "/v1/snapshot", {
		"schema_version": 1, "version": 1, "knobs": {"e2e:probe": {"type": "bool", "default": True}},
	})
	assert status == 200, f"PUT to follower failed: {status} {body!r}"
	assert headers.get("X-Detent-Leader") == leader.self_address
	h.log(f"follower forwarded to leader ({leader.self_address}), got 200")
	h.assert_safety(cluster)


def scenario_leader_failover(cluster: Cluster, h: Harness) -> None:
	cluster.start_all()
	leader = cluster.wait_for_election(timeout=5.0)
	assert leader is not None
	h.assert_eventually_writable(cluster, version=1)
	leader.kill(signal.SIGTERM)  # graceful, like a real rolling restart
	h.log(f"stopped leader {leader.self_address}")
	new_leader = cluster.wait_for_election(timeout=5.0)
	assert new_leader is not None, "no new leader elected after failover"
	assert new_leader.self_address != leader.self_address
	h.assert_eventually_writable(cluster, version=2)
	h.assert_safety(cluster)


def scenario_dead_follower_does_not_block_writes(cluster: Cluster, h: Harness) -> None:
	cluster.start_all()
	leader = cluster.wait_for_election(timeout=5.0)
	assert leader is not None
	h.assert_eventually_writable(cluster, version=1)
	follower = next(n for n in cluster.nodes if n.self_address != leader.self_address)
	follower.kill(signal.SIGKILL)  # ungraceful, no goodbye -- the exact liveness bug's trigger
	h.log(f"SIGKILL'd follower {follower.self_address}")
	for version in (2, 3, 4):
		h.assert_eventually_writable(cluster, version=version, timeout=15.0)
	h.log("writes kept succeeding after a follower vanished")
	h.assert_safety(cluster)


def scenario_kill_mid_request(cluster: Cluster, h: Harness) -> None:
	cluster.start_all()
	leader = cluster.wait_for_election(timeout=5.0)
	assert leader is not None
	follower = next(n for n in cluster.nodes if n.self_address != leader.self_address)
	body = json.dumps({"schema_version": 1, "version": 1,
	                    "knobs": {"e2e:probe": {"type": "bool", "default": True}}}).encode()
	req = urllib.request.Request(follower.base_url + "/v1/snapshot", data=body, method="PUT")

	result = {}

	def fire():
		try:
			with urllib.request.urlopen(req, timeout=3.0) as resp:
				result["status"] = resp.status
		except Exception as e:  # noqa: BLE001 - any failure here is a valid, expected outcome
			result["error"] = str(e)

	import threading
	t = threading.Thread(target=fire)
	t.start()
	time.sleep(0.02)  # give the request a moment to reach the server
	follower.kill(signal.SIGKILL)
	t.join(timeout=5.0)
	h.log(f"killed mid-request; client saw: {result}")
	# No false positive: either the client saw a real failure, or it saw a
	# genuine 200 -- never left hanging forever (t.join already bounds that).
	assert "status" not in result or result["status"] == 200
	h.assert_eventually_writable(cluster, version=2)
	h.assert_safety(cluster)


def scenario_restart_is_not_immediately_reelected(cluster: Cluster, h: Harness) -> None:
	cluster.start_all()
	leader = cluster.wait_for_election(timeout=5.0)
	assert leader is not None
	h.assert_eventually_writable(cluster, version=1)
	leader.kill(signal.SIGTERM)
	new_leader = cluster.wait_for_election(timeout=5.0)
	assert new_leader is not None
	leader.start()  # same config/port -- a real restart
	time.sleep(0.5)
	h2 = leader.health()
	assert h2 is not None and not h2.get("is_leader"), "restarted node was immediately re-electable"
	h.log("restarted node correctly not immediately leader")
	h.assert_eventually_writable(cluster, version=2)


def scenario_chaos(cluster: Cluster, h: Harness, rounds: int) -> None:
	cluster.start_all()
	leader = cluster.wait_for_election(timeout=5.0)
	assert leader is not None
	h.assert_eventually_writable(cluster, version=1)
	for round_num in range(rounds):
		live = cluster.live_nodes()
		# Never drop below a majority of the FULL membership.
		if len(live) > len(cluster.nodes) // 2 + 1 and random.random() < 0.5:
			victim = random.choice(live)
			sig = random.choice([signal.SIGKILL, signal.SIGTERM])
			victim.kill(sig)
			h.log(f"round {round_num}: killed {victim.self_address} ({sig.name})")
		else:
			dead = [n for n in cluster.nodes if not n.is_running()]
			if dead:
				victim = random.choice(dead)
				victim.start()
				h.log(f"round {round_num}: restarted {victim.self_address}")
		time.sleep(random.uniform(0.2, 0.8))
		if len(cluster.live_nodes()) > len(cluster.nodes) // 2:
			next_version = h.current_version(cluster) + 1
			if os.environ.get("E2E_DEBUG"):
				print(f"      [debug] round {round_num}: current_version={next_version - 1}, trying v{next_version}")
			h.assert_eventually_writable(cluster, version=next_version, timeout=15.0)
	h.assert_safety(cluster)
	h.log(f"chaos: {rounds} rounds, final safety check passed")


SCENARIOS = {
	"fresh_bootstrap_fast": (scenario_fresh_bootstrap_fast, {"fast"}),
	"fresh_bootstrap_production_timing": (scenario_fresh_bootstrap_production_timing, {"production"}),
	"put_to_follower_forwards": (scenario_put_to_follower_forwards, {"fast"}),
	"leader_failover": (scenario_leader_failover, {"fast"}),
	"dead_follower_does_not_block_writes": (scenario_dead_follower_does_not_block_writes, {"fast"}),
	"kill_mid_request": (scenario_kill_mid_request, {"fast"}),
	"restart_is_not_immediately_reelected": (scenario_restart_is_not_immediately_reelected, {"fast"}),
}

FAST_RAFT_TIMING = (0.1, 0.3, 0.5, 1.0)  # heartbeat, election_init, election_min, election_max


def find_binary() -> str | None:
	for pattern in ("build*/src/detentd/detentd", "build*/src/detentd"):
		matches = sorted(glob.glob(pattern))
		if matches:
			return matches[0]
	return None


def run_scenario(name: str, fn, tags: set[str], binary: str, h: Harness) -> bool:
	tmpdir = tempfile.mkdtemp(prefix="detent_e2e_")
	timing = None if "production" in tags else FAST_RAFT_TIMING
	cluster = make_cluster(binary, tmpdir, n=3, raft_timing=timing, token_suffix=name)
	print(f"--- {name} ---")
	try:
		if name == "chaos":
			fn(cluster, h, rounds=h.chaos_rounds)  # type: ignore[attr-defined]
		else:
			fn(cluster, h)
		print(f"    PASS")
		return True
	except AssertionError as e:
		print(f"    FAIL: {e}")
		return False
	finally:
		cluster.stop_all()
		shutil.rmtree(tmpdir, ignore_errors=True)


def main() -> int:
	ap = argparse.ArgumentParser(description=__doc__)
	ap.add_argument("--binary", default=None, help="path to the detentd binary")
	ap.add_argument("--scenario", default="all", help="scenario name, or 'all'")
	ap.add_argument("--production-timing", action="store_true",
	                 help="also run scenarios tagged 'production' (slow, real Raft timing)")
	ap.add_argument("--chaos-rounds", type=int, default=0,
	                 help="also run the chaos scenario for N rounds (0 = skip)")
	ap.add_argument("--quiet", action="store_true")
	args = ap.parse_args()

	binary = args.binary or find_binary()
	if not binary or not os.path.isfile(binary):
		print("error: detentd binary not found; build it first or pass --binary", file=sys.stderr)
		return 2

	h = Harness(verbose=not args.quiet)
	to_run = dict(SCENARIOS)
	if not args.production_timing:
		to_run = {k: v for k, v in to_run.items() if "production" not in v[1]}
	if args.scenario != "all":
		if args.scenario not in SCENARIOS:
			print(f"unknown scenario: {args.scenario}", file=sys.stderr)
			return 2
		to_run = {args.scenario: SCENARIOS[args.scenario]}

	results = {}
	for name, (fn, tags) in to_run.items():
		results[name] = run_scenario(name, fn, tags, binary, h)

	if args.chaos_rounds > 0:
		h.chaos_rounds = args.chaos_rounds  # type: ignore[attr-defined]
		results["chaos"] = run_scenario("chaos", scenario_chaos, {"chaos"}, binary, h)

	print()
	passed = sum(1 for v in results.values() if v)
	print(f"{passed}/{len(results)} scenarios passed")
	return 0 if passed == len(results) else 1


if __name__ == "__main__":
	sys.exit(main())
