> Status: CURRENT (2026-11-03) code review of `phase/retained-state` at `Detent:052a170` (five commits on `Detent:d4abd4a`).

# Review: retained-state envelope and replacement-pool funding

Verdict: **APPROVE TO LAND**. No blocking findings. Three non-blocking notes become follow-ups.

## What was run

- Fresh build of the branch (macOS x86_64, Homebrew clang 23.1.2, RelWithDebInfo `-O1 -g` asserts on, `.scratch/b`), `test_authority_node` and `test_authority_service` under `quiet light`: both pass (7.7 s and 9.9 s).
- Probe `.scratch/probe_funding.cc` (built against the branch's `libdetent_authority_node.a`; binary `.scratch/probe_funding`), see CR-probe results below.
- Source reads: `node.cc`, `node.hh`, `service.cc`, the consensus library's `retained_state.h`, `checkpoint_format.h`, `worker.h`, `retirement.h`, `transport/profile.h` (from the fetched `_deps`), the builder's mutants and logs.
- Not re-run: the seven mutants (read `.scratch/mutants.py` in the builder's worktree; each edit targets the behavior its name claims), ASan/UBSan, TSan.

## Probe results by risk

### 1. Funding arithmetic

- `validate_replacement_funding` (`src/detent_authority/node.cc:42-63`) uses `__builtin_add_overflow` for both additions and the encoded bound from `checkpoint_detail::encoded_bound({log_entries, log_bytes})`, the same function the Worker uses for its checkpoint cap (`consensus/worker.h:152`, `checkpoint.h:38,69`). It is the encoded size (21 bytes per entry plus the 616-byte fixed part), not a raw byte count. The test asserts the suffix is 68,491,764 for 4 MiB / 128 entries and the default requirement is 336,931,316.
- Probe, all five retained fields at the library ceilings (suffix 17,356,036,596): pool = image 0 + suffix + 4096 starts; one byte less refuses with "short by 1 byte". With pool = UINT64_MAX and image = UINT64_MAX - suffix - 4096 it passes; image + 1 is refused (wrap path); image = UINT64_MAX is refused. Defaults: 336,931,316 passes, one less refuses. Each field at SIZE_MAX is refused by the ceiling check before the sum is formed (so `encoded_bound`, which throws `length_error` on overflow, is never reached with an unvalidated value). Ceilings with the default pool refuse and name a 17 GB shortfall.
- Result: no wrap at any configurable value. The probe's one "FAIL" was my expectation that the wrap path says "short by 1 byte"; it says "more than 0 bytes". See CR1.

### 2. Replicated profile

- `grep` of `state.cc`, `state.hh`, `peers.hh` finds no use of `scheduling` or the retained policy. `semantic_profile`, the `DTABAT02` header (`BatchDecoder`, `state.cc:1092-1103`, binds `write_limits(application)`) and `peer_configuration` (`peers.hh:10-18`, `profile_fingerprint(semantic_profile, configuration, consensus)`) carry the application limits and the operating consensus limits only. The retained policy is reached only by the Worker/Core/Recovery constructors and by the historical-command cap (`node.cc:386`).
- Divergence: the retained values only change what a node accepts as historical data, never how it applies it. Because validation forces every retained field to be at least its operating limit, and the operating limits are in the fingerprint (so identical across admitted peers), all admitted history (including what Detent itself can ever write) fits every valid envelope. A voter with a smaller envelope than the stored history fails closed at recovery (CR-3 below); it does not commit or replay differently. No finding.
- Rolling change: the fingerprint hashes the operating `consensus_limits` (voters, command, log bytes/entries, control, rpc, reads, uncommitted). A rolling change of `consensus_limits` already makes peers refuse each other; that is existing behavior, intended ("All voters must use the same operating profile", `AUTHORITY_TRANSPORT.md`). A rolling change of `retained_state` does not alter the fingerprint, so peers keep admitting each other; docs say so ("voters need not configure the same values") and that is the intended design. No finding.

### 3. Recovery

- `lowered-envelope-application` (`tests/detent_authority/test_node.cc`, `lowered == 5`) writes a store with more entries than the envelope, snapshots every file size, constructs the Node and calls `recover()`, then asserts it threw and the sizes map is equal. I ran it: the refusal is the library's `storage entry count exceeds bound`, so the throw is the envelope check and not an unrelated failure. The same test with the default envelope (`lowered-count-application`, operating `log_entries` 4, stored history larger) recovers, so it is the envelope that decides.
- The Node constructor does only option validation (`node.cc:73-82`, no I/O) before the Worker's `recover`; validation precedes any journal access. The check before truncation is the library's (`Recovery` reads and validates before `finish`). I did not find a Detent-side path that compacts or rewrites before the check. Only the entry-count dimension is exercised; the byte dimension goes through the same library `validate_policy` and is not separately tested (CR2).

### 4. Worker and parser use the configured envelope

- Grep of `src/`, `tests/`, examples and benchmarks for `ceilings`, `WorkerLimits`, `RetainedStatePolicy`, `Recovery(`, `decode_checkpoint`, `command_bytes`: the Worker is built with `options_.scheduling` (`node.cc:81`); the historical-group decoder gets `scheduling.retained.command_bytes` (`node.cc:386`); `ceilings` appears only in `validate_retained_state`. The service loads into `o.node.scheduling.retained` (`service.cc:255`) and `Service::run` copies `options_.node` whole (`service.cc:1405`), keeping it. `node.cc:169` hands the library's `RetirementBudget` the operating command bound as a per-tick byte budget; the budget always admits the first payload (`retirement.h:36`), so a larger retained payload still makes progress. No leftover use of the old ceiling defaults.

### 5. Design-doc match

- `RETAINED_STATE_ARCHITECTURE.md` "Detent integration", the mechanism note `authority-retained-state.md`, `AUTHORITY_TRANSPORT.md`, `CAPACITY_RUNTIME.md`, the website pages and the CHANGELOG agree with the code on: the five fields and defaults, the 1..ceiling and at-least-operating checks and their exact messages, the funding terms and message, local-only envelope, 336,931,316 default. The one design deviation (the old `maximum_size(consensus)` check dropped because the new one subsumes it, and the default is the operating defaults not the library ceilings) is recorded in the ledger and the architecture doc, and the code reasoning holds: validation guarantees retained >= operating, so `encoded_bound` of the retained suffix >= `maximum_size(consensus)`.
- Minor doc nit (CR3).

## Findings

- **CR1 (non-blocking)**. In the 64-bit wrap case the message says "short by more than 0 bytes" (`node.cc:55-58`), which reads as nonsense. It needs a pool/image beyond 2^64 - 17 GB, so no real configuration reaches it. Follow-up: say "the requirement exceeds 64 bits" and drop the shortfall.
- **CR2 (non-blocking)**. The explicit-envelope recovery refusal is tested only for the entry-count dimension (`test_node.cc`, `lowered == 5`). The byte and command-size dimensions depend on the library's shared validation and have no Detent test. A follow-up probe would reuse the same harness with `retained.log_bytes` below a stored suffix.
- **CR3 (non-blocking, docs)**. The CHANGELOG upgrade note says "Two kinds of configuration are refused" and lists three. Fix the count.

Also noted, not findings: the effective default for a configuration without the object drops from the library ceilings to the operating defaults (ledger "Design vs code" 3). The argument that every journal the configuration language can write fits holds, since `consensus_limits` only lowers and command/RPC bounds are not configurable. Library users embedding `Node` with larger operating limits are refused at construction with a message naming `retained_state.<field>` (covered by `retained_validation` and the CHANGELOG upgrade note).

## Criteria check

R1 through R6 are supported by evidence I could reproduce (tests pass, boundaries at plus and minus one byte, exact messages, the replicated-profile answer from `peers.hh` and `state.cc`). R6's sanitizer runs are the builder's and CI run 41735791062 (green per the lead); I did not repeat them.

## Conditions

None.

## Not run

Sanitizer builds (ASan/UBSan, TSan), the seven mutants, the remaining five authority tests (`state`, `node_completion`, `cluster`, `cluster_completion`, `process`) and other platforms: left to the builder's evidence and CI. No multi-voter run with differing retained envelopes (the argument above rests on reading the fingerprint and `Recovery`).

## Probes

- `.scratch/probe_funding.cc` (source), `.scratch/probe_funding` (binary), `.scratch/b` (build tree), `.scratch/build.log`, `.scratch/cfg.log`, `.scratch/cfg2.log`.
