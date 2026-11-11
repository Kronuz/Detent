"""A3 timing (docs/perf/one-knob-type.md): main against the one-knob tree on one corpus size.

Six interleaved pairs (D51); the first arm alternates pair by pair. Each run takes a fresh APFS
clone of its arm's prepared store, reads it into the page cache, then times:
  steady  one process: STEADY_ROUNDS timed passes over STEADY_QUERIES sampled reads after a warm pass
  first   FIRST fresh processes, each after a warmup process: Client construction to the first
          value of the steady run's probe key
Each arm's run statistic is the median (first) or the median pass (steady). The verdict per metric:
paired differences (one-knob minus main), each arm's spread (median absolute difference of
consecutive run statistics), noise floor (the larger spread); NO WORSE iff the median paired
difference is at most the noise floor.
"""

from __future__ import annotations

import argparse
import itertools
import json
import shutil
import statistics
import subprocess
from pathlib import Path
from typing import Any

PAIRS, FIRST, STEADY_QUERIES, STEADY_ROUNDS = 6, 60, 200_000, 7


def run_json(command: list[str]) -> dict[str, Any]:
    return json.loads(
        subprocess.run(command, check=True, capture_output=True, text=True).stdout
    )


def one_run(
    prepared: Path, scratch: Path, arm: str, size: str, corpus: Path
) -> dict[str, Any]:
    store = scratch / f"{arm}-{size}"
    shutil.rmtree(store, ignore_errors=True)
    subprocess.run(
        ["cp", "-c", "-R", str(prepared / arm / f"store-{size}"), str(store)],
        check=True,
    )
    for path in store.iterdir():
        path.read_bytes()
    binary = str(prepared / arm / "one_knob_read")
    steady = run_json(
        [
            binary,
            "steady",
            str(store),
            str(corpus),
            str(STEADY_QUERIES),
            str(STEADY_ROUNDS),
        ]
    )
    probe = [binary, "first", str(store), steady["probe"], str(steady["probe_boolean"])]
    first = []
    for _ in range(FIRST):
        run_json(probe)
        first.append(run_json(probe)["first_ns"])
    shutil.rmtree(store)
    return {
        "first_median_ns": statistics.median(first),
        "first_ns": first,
        "steady_median_ns": statistics.median(steady["ns_per_read"]),
        "steady": steady,
    }


def verdict(runs: list[dict[str, Any]], metric: str, arm: str) -> dict[str, Any]:
    values = {
        name: [r[metric] for r in runs if r["arm"] == name] for name in ("main", arm)
    }
    diffs = [b - a for a, b in zip(values["main"], values[arm])]
    spread = {
        arm: statistics.median(abs(b - a) for a, b in itertools.pairwise(v))
        for arm, v in values.items()
    }
    noise = max(spread.values())
    return {
        "metric": metric,
        "run_values": values,
        "paired_differences": diffs,
        "median_difference": statistics.median(diffs),
        "spread": spread,
        "noise_floor": noise,
        "no_worse": statistics.median(diffs) <= noise,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prepared", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--size", choices=("100k", "5m"), required=True)
    parser.add_argument("--corpus-main", type=Path, required=True)
    parser.add_argument("--arm", required=True)
    parser.add_argument("--corpus-arm", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    scratch = args.output / "clones"
    scratch.mkdir(exist_ok=True)
    corpora = {"main": args.corpus_main, args.arm: args.corpus_arm}
    arms = ("main", args.arm)
    runs: list[dict[str, Any]] = []
    for pair in range(PAIRS):
        for arm in arms if pair % 2 == 0 else reversed(arms):
            result = one_run(args.prepared, scratch, arm, args.size, corpora[arm])
            runs.append({"pair": pair, "arm": arm, **result})
            print(
                json.dumps(
                    {
                        k: v
                        for k, v in runs[-1].items()
                        if k not in ("first_ns", "steady")
                    }
                ),
                flush=True,
            )
    shutil.rmtree(scratch)
    verdicts = [
        verdict(runs, "first_median_ns", args.arm),
        verdict(runs, "steady_median_ns", args.arm),
    ]
    for v in verdicts:
        print(
            f"A3 verdict {args.size} {v['metric']}: main {v['run_values']['main']}, one-knob "
            f"{v['run_values'][args.arm]}; paired {v['paired_differences']}, median "
            f"{v['median_difference']:+.1f}; noise floor {v['noise_floor']:.1f}; "
            f"{'NO WORSE' if v['no_worse'] else 'WORSE'}"
        )
    (args.output / "result.json").write_text(
        json.dumps({"size": args.size, "runs": runs, "verdicts": verdicts}, indent=1)
    )


if __name__ == "__main__":
    main()
