"""A relay's parked wait never outlives a source that stops serving (relay-mirror-idle-wake F1).

Three detent-authority voters. CASE `hung`: a relay reads [follower, leader, other] with
`timeout_ms` 1000; at rest, the voter holding its parked next is stopped (SIGSTOP) and the quorum
publishes. From the publish, the relay serves the leader's heads within the timeout and one sync,
2.5 s, where a wait parked for its whole deadline held it stale for about 27 s. CASE `cut-off`: a reader parks a next
at a follower, and the two other voters stop; knowing no live leader, the follower answers it
503 `not_ready` as its history routes do, where it held the next to its 30 s deadline.

    test_relay_hung.py DETENT-AUTHORITY DETENT-RELAY RELAY-PROBE CERTIFICATES hung|cut-off
"""

from __future__ import annotations

import http.client
import json
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_relay_service as t  # noqa: E402

TIMEOUT_S = 1.0
STALE_S = 2.5  # the timeout and one sync


def heads(run: t.Run, port: int, token: str) -> dict[str, list[object]] | None:
    result = subprocess.run(
        [
            str(run.prober),
            str(run.certificates / "ca.pem"),
            str(port),
            token,
            t.CONTEXT["cluster"],
            "--heads",
            *t.NAMES,
        ],
        capture_output=True,
        text=True,
        timeout=30,
        check=False,
    )
    return json.loads(result.stdout) if result.returncode == 0 else None


def held_voter(run: t.Run) -> int:
    """The voter the relay's parked next is at: its one established connection to a voter."""
    pid = run.children["relay-a"].pid
    for _ in range(100):
        listing = subprocess.run(
            [
                "lsof",
                "-a",
                "-p",
                str(pid),
                "-iTCP",
                "-sTCP:ESTABLISHED",
                "-n",
                "-P",
                "-Fn",
            ],
            capture_output=True,
            text=True,
            check=False,
        ).stdout
        held = {
            index
            for index, port in enumerate(run.http)
            for line in listing.splitlines()
            if line.startswith("n") and line.endswith(f"->127.0.0.1:{port}")
        }
        if len(held) == 1:
            return held.pop()
        time.sleep(0.05)
    raise AssertionError("the relay holds no single connection to a voter")


def hung(run: t.Run) -> None:
    leader = run.leader()
    follower, other = (leader + 1) % 3, (leader + 2) % 3
    run.upstreams = {
        "a": ([run.http[follower], run.http[leader], run.http[other]], t.MIRROR)
    }
    run.mirror_options["a"] = {"timeout_ms": int(TIMEOUT_S * 1000)}
    run.start_relay("a")
    run.relay_ready("a")
    run.churn(6, 1)
    run.converged(
        {
            "authority": (run.http[leader], t.MIRROR),
            "a": (run.relay_ports["a"], t.READER),
        }
    )
    time.sleep(1)  # at rest: the relay's wait is parked
    stopped = held_voter(run)
    run.pause(stopped)
    run.churn(3, 2)  # a seal's worth, which the two running voters commit
    published = time.monotonic()
    target = heads(run, run.http[run.leader()], t.MIRROR)
    stale = None
    while time.monotonic() - published < 60:
        if heads(run, run.relay_ports["a"], t.READER) == target:
            stale = time.monotonic() - published
            break
        time.sleep(0.05)
    run.resume(stopped)
    run.log(
        f"hung: voter {stopped} stopped holding the relay's wait; the relay served the heads after {stale} s"
    )
    t.check(
        stale is not None and stale <= STALE_S,
        f"the relay stayed stale {stale} s past a hung source",
    )


def cut_off(run: t.Run) -> None:
    leader = run.leader()
    follower = (leader + 1) % 3
    port = run.http[follower]
    time.sleep(2.5)  # past the churn's seal, which would answer the next
    at = heads(run, port, t.MIRROR)
    t.check(at is not None, "the follower answered no heads")
    names = [[name, *at[name]] for name in t.NAMES if name in at]
    status, body = run.call(
        port,
        "/v2/distribution/session/open",
        {
            "version": 2,
            "profile": 1,
            "coding": "raw",
            "cluster": t.CONTEXT["cluster"],
            "namespaces": names,
        },
        t.MIRROR,
    )
    t.check(status == 200, f"session open answered {status}: {body!r}")
    session = json.loads(body)["session"]
    answered: list[tuple[int, bytes, float]] = []

    def park() -> None:
        connection = http.client.HTTPSConnection(
            "localhost", port, context=run.context, timeout=45
        )
        try:
            connection.request(
                "POST",
                "/v2/distribution/session/next",
                json.dumps(
                    {
                        "version": 2,
                        "session": session,
                        "cluster": t.CONTEXT["cluster"],
                        "namespaces": names,
                        "wait_ms": 30000,
                    }
                ),
                {
                    "Authorization": f"Bearer {t.MIRROR}",
                    "Content-Type": "application/json",
                    "Connection": "close",
                },
            )
            response = connection.getresponse()
            answered.append((response.status, response.read(), time.monotonic()))
        finally:
            connection.close()

    waiter = threading.Thread(target=park)
    waiter.start()
    time.sleep(1)
    t.check(
        not answered, f"the follower answered the next before the cut: {answered[:1]!r}"
    )
    others = [index for index in range(3) if index != follower]
    cut = time.monotonic()
    for index in others:
        run.pause(index)
    waiter.join()
    for index in others:
        run.resume(index)
    status, body, at_time = answered[0]
    run.log(
        f"cut-off: the follower answered its parked next {status} {body[:40]!r} after {at_time - cut:.2f} s"
    )
    t.check(
        status == 503 and b'"not_ready"' in body and at_time - cut < 25,
        f"the cut-off follower answered its parked next {status} {body[:80]!r} after {at_time - cut:.2f} s",
    )


def main() -> int:
    *paths, case = sys.argv[1:6]
    run = t.Run(*(Path(argument).resolve() for argument in paths))
    try:
        run.voters()
        run.leader()
        run.write("/v2/open", {"expected_generation": 0, "nonce": "6e" + "00" * 15})
        run.churn(12, 0)
        {"hung": hung, "cut-off": cut_off}[case](run)
        return 0
    except BaseException:
        for log in sorted(run.directory.glob("*.log")):
            print(
                f"--- {log.name}\n{log.read_text(errors='replace')[-3000:]}", flush=True
            )
        raise
    finally:
        for name in list(run.children):
            try:
                run.stop(name)
            except AssertionError:
                pass
        shutil.rmtree(run.directory, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
