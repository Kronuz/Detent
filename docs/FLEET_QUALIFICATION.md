# Native fleet qualification

**Status:** Tooling has Architect source approval. The reduced native matrix passes 11/11 in 178.65 seconds; large-fleet capacity qualification remains open.

`detent_fleet_load` drives real TLS subscription sessions through the production client and decoder. Each probe retains a session, cursor and payload digest. It has no durable store. A separate cohort of managed agents publishes durable images, and an SDK oracle verifies every definition after writer reconciliation.

The runner owns three authority processes, two relays, the durable cohort and one native probe process. `--active` bounds simultaneous probe exchanges independently of the session population; `--workers` selects fixed event-loop threads. A run with 10,000 sessions and 128 active exchanges does not establish 10,000 simultaneously parked connections. Cold bootstrap and checkpoint reconciliation occur outside the measured writer interval. The parent brackets that interval with acknowledged native measurement commands. Per-probe measured offer counts and maximum silence exclude bootstrap and checkpoint offers; process CPU is sampled at those boundaries. Lifetime offer counts and exchange histograms remain separate.

The [smoke evidence](../benchmarks/fleet-tooling-qualification.json) records raw, zstd, warm, mixed, partial-launch and cancellation cases. Each successful case reconciles all 16 probes and independently checks 256 SDK definitions. These runs overlapped independent builds and do not calibrate sustainable capacity.

## Running

Configure authority, relay and benchmark support, build `detent_fleet_load`, the daemons and both system oracle targets, and generate the authority TLS fixture. The CTest cases `test_system_fleet_raw`, `test_system_fleet_zstd`, `test_system_fleet_warm` and `test_system_fleet_mixed` run a reduced integration workload.

For larger runs, use `tests/detent_system/run_fleet.py --help` and select a scenario from `tests/detent_system/presets/`. Supply absolute paths to the authority, agent, relay, native driver, SDK oracle, build and certificate directory. The output directory must not exist. Raise the descriptor limit to satisfy the reported preflight requirement.

Use `detent_system_local_oracle` for integer writers and `detent_system_mixed_oracle` with `--mixed`. `--hot-namespace` adds overlapping keys within one authorized namespace. Mixed workloads request 1, 10 or 1,000 logical edits, but each authority transaction remains bounded to 128 operations. A 1,000-edit workload therefore comprises several independently committed transactions.

Relay health reports `sessions` as retained table entries, including closed entries awaiting bounded collection. The runner checks the live population at readiness and its return to durable-cohort headroom after healthy probe cleanup. Best-effort Close admission stops after 20 seconds of shutdown; accepted Close requests still drain, and skipped Close requests are counted. Remote sessions skipped at that deadline can remain until expiry or relay shutdown.

Completed bootstrap probes park outside exchange admission while cold probes finish. An owner-local timer registration releases them after a monotonic completion latch, with wakeups batched in groups of 64. `peak_bootstrap_parked` and `premature_ready_polls` qualify this boundary. Reduced delayed/warm/cancelled bootstrap cases pass; the stopped 10k diagnostic reaps all accepted originals, while the corrected 10k rerun remains pending.

## HTTP generator admission

`--http-workers N` bounds Python blocking HTTP workers; its default is the producer count. Pre-submit permits use the smaller of those counts and remain held through authentic request completion, including cancellation. Producer calls carry an absolute admission deadline. A request refused before submission raises `GeneratorAdmissionRefused`; qualification fails without advancing its sequence or changing the state oracle. A prior attempt that may have executed therefore remains unresolved, rather than being mislabeled as unsent.

Actual worker concurrency and fixed queue/service histograms accompany writer counters. Queue time includes asynchronous admission waiting; HTTP observations include health queries, reads and retries. Queue time is recorded on entry and service time on completion, with initial in-flight counts retained. Generator CPU is recorded separately. A lower-worker diagnostic does not establish server capacity.

## Evidence and limits

The runner records source and dependency fingerprints, binary hashes, host resources, effective admission, bounded process output, health samples and full-corpus verification. `fleet-result.json` requires exact accepted/completed/opened/ready/checked counts at one quorum checkpoint. Every probe snapshot digest must match the fully validated reference at that exact cursor; the independent SDK cohort checks semantic state.

Memory admission reserves workload and OS headroom before launch and repeats after durable seeding. It refuses an oversized population rather than reducing it. The production byte ledger covers admitted client buffers and retained slots; the memory estimate separately reserves TLS and socket capacity. The preflight includes per-session retained configuration and queued Close copies, even when their count exceeds active network exchanges. RSS sampling is process residency, not a measurement of kernel socket memory or device durability.

The remaining acceptance gates are in [the fleet blueprint](FLEET_QUALIFICATION_ARCHITECTURE.md). Reduced smoke tests do not establish the 500 → 2,000 → 10,000 ladder, sustained 500k-key ingestion, physical failure independence or a complete durable-agent fleet.
