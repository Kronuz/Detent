# CI flake triage

Snapshot: `Detent:a900571a` (`main`), native x86_64 macOS 15.8.1, Apple clang 17, Release with test assertions enabled (`tests/CMakeLists.txt:2`, `-UNDEBUG`). This branch changes only this report. Status is partial: the DNS port-reservation race is forced; the other five causes require stronger evidence. Passing repeats do not close a flake.

## Evidence and proposed next probes

| Test | Observed signature and evidence | Cause classification | Proposed fix/probe |
|---|---|---|---|
| `test_system_managed_relay` | Task's original report: macos-x86_64, okt-followups, 2026-11-12. Branch run 43310873806 and 43310874197 currently report success; `--attempt 1 --log-failed` returned no failed-job log. | Unresolved; cannot attribute to a wider timeout from the summary alone. | Preserve relay/agent/authority logs, selected-table positions, reconnect reason and pending retirement at failure. Force upstream disconnect after session-next begins and before it settles, then compare relay and agent frontiers. |
| `test_system_authority_writer_failover` | [Original attempt 1](https://github.com/Kronuz/Detent/actions/runs/43355759936/attempts/1), linux-x86_64-clang: TLS `ConnectionResetError: [Errno 104] Connection reset by peer`, then `writers.py:79` → `authority.py:309`, `RuntimeError: authority did not elect an available leader`. Test failed after 20.10 s. | Reset is expected around a leader kill; failure to discover an available leader within the fixture's 15 s window is the terminal symptom. Original log alone does not prove a product election failure or generator starvation. | Record each voter's role/term/fenced/ready and health-request admission latency through the kill. Force the kill after an in-flight writer crosses admission; distinguish a dead/discovering leader from a healthy leader whose probe never obtains generator/transport capacity. Keep transaction retry identity unchanged. |
| `test_authority_service` | [Original attempt 6](https://github.com/Kronuz/Detent/actions/runs/43346413800/attempts/6), macos-x86_64: abort after 10.30 s, `libc++abi: terminating due to uncaught exception of type http::RequestRejected: {"error":"admission_full"}`. | Product admission refusal remains a product finding until the calling test and outstanding ownership are identified. Catching a broader exception would hide the defect. | Capture throw-site backtrace and request/body/per-producer counters at `service.cc:724,729,752`. Hold original response IO while releasing handler ownership to force the lifetime boundary. Check `Writer::admission` is expired only after actual retirement and original IO settle. |
| `test_authority_dns_voters_names` | Forced minimal reproduction below: `configure()` returns ports, competitor binds node 2's peer port, voter bind fails with errno 48 `Address already in use`. Original job named by quest: 261112-230929-531cef. | Proven fixture TOCTOU: `test_dns_voters.py:34-37` reserves ports, `:73-74` closes every reservation, `:126` later spawns voters. A free port is not a reservation after close. | Pass inherited bound listeners to daemons if that interface exists; otherwise isolate this fixture's allocation with a cross-process reservation/launch protocol and retry only confirmed startup `EADDRINUSE` before election/workload starts. A lock only coordinates cooperating test allocators, so it is not a complete guarantee against arbitrary local processes. |
| `test_local_paged_publication_artifacts` | Quest reports 4/400 failures under 40 concurrent copies at `Detent:8d07ecad`, 0/60 serial: `invalid paged artifact operation accepted`. At the current snapshot the same exception text serves three independent rejection checks (`test_paged_publication_artifacts.cc:69,70,99`). | Likely pressure/retirement interleaving, not yet forced. `PagedDeltaPlanBuilder::take()` resets `work_` (`paged_delta_plan.cc:108`); its queued `Work::retire_step()` releases source/overlay/leaves on the same account. The test snapshots `used()`, admits the remaining capacity, then assumes admission stays saturated. A later retirement can open capacity between those steps. This is a source-based hypothesis, not an attribution. | Give the three rejection checks distinct diagnostic names first. Log used/reserved bytes before saturation and immediately before populate; force queued builder retirement to run between them. Prefer an isolated pressure account or explicit quiescence of construction-only retirement over sleeping or increasing pressure heuristically. Preserve the assertion that `populate()` cannot partially publish artifacts on refusal. |
| `test_authority_snapshot_catchup` | Quest job 261112-235321-9e6f06 reports a failed response read at `test_multivoter.py:304`. At the current snapshot the corresponding unguarded read is at `:297`, outside the retry guard used for the later catchup read (`:338`). | Unguarded transient health read is a fixture failure surface; the reason the original connection failed is still unresolved. | Force a socket close at the health-read boundary and preserve voter stderr/status. After proving the voter remains live/healthy, use the existing bounded health-probe helper; retain the committed/checkpoint boundary and exact post-install data assertions. Never classify a dead/fenced voter as a harmless read retry. |

## Forced DNS reproduction

The probe calls the real fixture's `configure`, then deterministically schedules another listener between reservation release and daemon bind. It does not change a test or product file:

```python
from pathlib import Path
import socket
from test_dns_voters import configure

ports, _ = configure(certificates, directory, ["localhost"] * 3)
with socket.socket() as competitor, socket.socket() as voter:
    competitor.bind(("127.0.0.1", ports[1]))
    competitor.listen()
    voter.bind(("127.0.0.1", ports[1]))  # EADDRINUSE, node 2's configured peer port
```

Run with `tests/detent_authority` on `PYTHONPATH`, a real TLS fixture, and an empty task scratch directory under `quiet light`. The actual command was `quiet light codex-flakes-port-repro 1 'Force competing bind after DNS fixture drops reservations' -- /usr/local/bin/python3 .scratch/free-port-repro.py`. Raw result in `.scratch/free-port-repro.log`:

```text
configured node-2 port=49602 stolen after configure(): errno=48 Address already in use
```

This forces the port-ownership defect; it does not claim that the real daemon was launched in this minimal probe. The original daemon failure is recorded in the quest's job evidence.

## Build and repeat commands

The first configure tried to reuse the main checkout's old `_deps/*-src` trees. That failed because the stale reactor did not define `reactor::tls`; adding `OPENSSL_ROOT_DIR` could not fix a missing CMake target. Those attempts provide no current-main test evidence. The successful configure uses the task worktree's pinned dependencies with no source overrides:

```sh
quiet check
quiet light codex-detent-flakes-build 30 'Build six flake targets' -- sh -c 'cmake -S . -B .scratch/build -DCMAKE_BUILD_TYPE=Release -DDETENT_BUILD_RELAY=ON -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_EXAMPLES=OFF -DOPENSSL_ROOT_DIR=/usr/local/opt/openssl@3 -DCMAKE_CXX_COMPILER_LAUNCHER=ccache && cmake --build .scratch/build -j4 --target detentd detent-agent detent-relay detent_system_local_oracle test_authority_service test_local_paged_publication_artifacts'
quiet light codex-detent-flakes-repeat 20 'Six flakes, ten repeats under shared-host load' -- ctest --test-dir .scratch/build -j4 --output-on-failure --repeat until-fail:10 -R '^(test_system_managed_relay|test_system_authority_writer_failover|test_authority_service|test_authority_dns_voters_names|test_local_paged_publication_artifacts|test_authority_snapshot_catchup)$'
```

The shared host's x7-gate light builds/test/index-creation holds provide background load; the repeat command itself uses at most four CTest jobs. The TLS fixture is included automatically by CTest's fixture dependency. The one-CPU-hour/parser fuzz budget belongs to C4 and is not mixed into these tests.

## Repeat results

Pending the isolated pinned build and repeat job. No six-cause or 200-repeat acceptance claim is made. The task's read-only boundary leaves all proposed source/test changes for the owning quest.

Raw evidence lives in this worktree's `.scratch/`: `build.log`, `free-port-repro.log`, `admission-ci.log`, `writer-ci-attempt1.log`, `relay-ci-attempt1.log` and `relay-ci-second.log`. Original CI evidence can be fetched with `gh run view RUN --attempt ATTEMPT --repo Kronuz/Detent --log-failed`; use run 43355759936/attempt 1 and run 43346413800/attempt 6.
