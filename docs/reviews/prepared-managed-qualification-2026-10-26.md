# Prepared managed reader qualification

The preserved 5M corpus passes real old-head preparation, unchanged-head restart and fresh SDK/CLI reads at Detent:0e862f0. With the publisher running, 200 fresh SDK processes return their first verified integer below 1 ms. This qualifies the measured resident-data case, not an unconditional deadline or the full release.

| Profile | Samples | Median | p99 | Maximum | Below 1 ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Publisher running | 200 | 0.715 ms | 0.835 ms | 0.864 ms | 200 |
| Publisher stopped | 200 | 0.806 ms | 0.992 ms | 1.035 ms | 198 |

Both profiles record zero major faults. Complete CLI commands verify the same 40 keys per profile and take about 8.32 ms median, including process launch, shutdown and controller overhead. SDK timing starts before Client construction and ends after the first checked value; it includes runtime initialization and the public managed-source factory, while excluding executable loading.

Publisher launch through observed prepared readiness takes 117.609 seconds for the old head and 108.307 seconds after restart. Startup still validates the full dataset. Equal-cursor preparation preserves revision 39064 and all 1,441 existing pack names. Restart leaves the selected descriptor unchanged. Both publisher instances drain and exit successfully. These are local persistent-state checks with deliberately unavailable upstream service, not network distribution or sustained fleet measurements.

The [raw record](../measurements/prepared-managed-5m-20261026.json) includes all 400 samples, expected keys, source/binary hashes, capacity settings and measurement limits. Each timed process follows a separate warmup process for its key. Live and stopped profiles run sequentially; their difference is not a counterbalanced comparison. Publisher readiness is sampled every 50 ms. The corpus is the saved reduced-topology dataset described in [capacity qualification](../CAPACITY_QUALIFICATION.md); this run does not repeat its exhaustive application oracle or reseed it.

The [exact runner snapshot](../measurements/prepared-managed-5m-20261026-runner.py.txt) expects the recorded repository-relative scratch layout and macOS APFS clone support (`cp -cR`). Copy it to `.scratch/fast-client-build/activation-qualification/qualify_managed.py`, provide the preserved agent state/configuration under `.scratch/capacity-trials/5000000-restore-r12/agent-0`, and use a fresh output path. Build the recorded source first. Run it through `quiet exclusive`; the original corpus is cloned rather than modified. The runner passes Ruff and strict Pyrefly with zero diagnostics.

The earlier attempt r1 is controller-interrupted and supplies no accepted measurement. Attempt r2 fails the readiness oracle: uncertain selected replacement is mistaken for an empty store, suppressing the later data-ready notification. The state-policy correction has native and sanitizer regressions, and r3 passes with that correction. Both unsuccessful artifacts remain preserved locally.

Remaining gates include final hosted qualification of the composed source, root-owned alias/ACL/acquisition-race fixtures and the broader release work in [the release plan](../RELEASE_PLAN.md). Cold-storage latency, boolean rollout timing and arbitrary scheduling delays are outside this record.
