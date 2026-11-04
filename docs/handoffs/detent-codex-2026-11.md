> Status: CURRENT (2026-11-04) Handoff of the Detent campaign to a new lead, for about four days. Self-contained: no conversation history is needed.

# Detent handoff, 2026-11-04

You lead Detent until the owner takes it back. Five branches are in flight, an unattended script is producing the measurements two of them still owe, and one design is ready for its preliminary review. Land the branches in the order below, then build the dense-page format.

## Rules that apply to every step

1. **Repository and identity.** [Kronuz/Detent](https://github.com/Kronuz/Detent); the git remote is `github` (`origin` is stale). Commit as `Germán Méndez Bravo <german.mb@gmail.com>`, push as the `Kronuz` gh account. No AI or tool attribution anywhere: no `Co-authored-by`, no session trailer, no "Generated with" line.
2. **Shared host.** Run `quiet check` before any build, test or timing. Builds and tests go under `quiet light NAME MINUTES "what" -- cmd` with at most 4 jobs; timings under `quiet strict`, and published numbers under `quiet exclusive`. Scratch goes in `.scratch/`, never `/tmp`. In backgrounded commands use `command ls` (the `ls` alias hangs).
3. **No AWS** of any kind: no `lease`, no AWS calls. Use the local VMs (Linux ext4 on SSH port 18772, FreeBSD on 18774) under `QUIET_VM=<pid> quiet strict`, or hosted CI.
4. **CI.** Branch names other than `main` do not trigger CI: dispatch with `gh workflow run ci.yml --ref BRANCH`, and watch with `gh run watch ID --interval 60`. Done means every job green on the tree that lands, after merging current `main` into it. The macOS arm64 runner sometimes never starts; rerun that job.
5. **Landing.** Merge into `main` (`git merge --no-ff`); never force-push a shared branch. Before merging a branch "as docs", run `git diff --stat main...BRANCH` and refuse any code file in it.
6. **Ledgers.** Each workstream has `docs/status/NAME.md` with numbered done criteria. Tick one only with evidence; never weaken or remove one. The ledger's "Next" list is the exact work left.
7. **Reviews.** Durability, concurrency, formats and wire changes get one independent review before landing (the architect for durability and formats). Fixes to named findings are confirmed by rerunning the reviewer's probes and a revert check, not by another full review.
8. **Shared libraries** live in `~/code/Kronuz/cpp-libraries` (completion and the others under `~/code/Kronuz/`). Moving a pin needs that library's CI green first. A peer session (`xapiand-74`) also edits them; coordinate through it before changing a shared API.

## State on 2026-11-04

`main` is `Detent:ea81f1d`, CI green at `Detent:32aa0c8` (later commits are docs). Worktrees are under `.claude/worktrees/`; each named below still holds its builds and untracked `.scratch/` helpers.

| Branch | Head | State | Ledger |
| --- | --- | --- | --- |
| `reader/s6` | `Detent:8768732` | Built, CI run 41921276663; `test_reader_switch` flakes under load | `docs/status/reader-s5-s6.md` |
| `legacy/wave2-h` | `Detent:aac6cb6` | Built on top of S6, CI run 41926685309 | `docs/status/legacy-wave2.md` |
| `namespace/s3b` | `Detent:d4692ba` | CI green; architect APPROVE WITH CONDITIONS; fix round not started | `docs/status/namespace-s3b.md` |
| `namespace/s1-s2` | `Detent:76bb6cf` | S1 done; S2 written (WIP), evidence owed | `docs/status/namespace-s1-s2.md` |
| `design/dense-pages` | `Detent:57bc17e` | Merged to `main` (`Detent:ea81f1d`); awaiting preliminary architect review | `docs/status/dense-pages.md` |

Worktrees: S6 `agent-a1fbf8344cbf48ecb` (builds in `.scratch/build`, `build-asan`, `build-tsan`); H `agent-a6c13d9faa82afd4d` (helpers copied to `~/code/Kronuz/Detent/.scratch/wave2-h-tools/`); S1/S2 `agent-af2c08dd24001ec58` (`build-rel`; the "before" tree is `~/code/Kronuz/Detent-wt/s2-before` at `Detent:7a4425a`); S3b's is gone, so make a new one from the branch.

### The unattended runs

`~/code/Kronuz/Detent/.scratch/overnight/run.sh` started 2026-11-04 14:51 under `nohup`. It runs, each step under its own `quiet` hold:

1. `test_reader_switch` from S6, 12 times alone and 8 rounds of four at once.
2. The S1/S2 build, its seven ledger tests and the whole `ctest`.
3. S1/S2 ASan+UBSan and TSan builds of the power-cut, disk-store and artifacts tests.
4. Cold-bootstrap timings, before (`s2-before`) against after (S1/S2): three interleaved pairs at 100K (`quiet strict`) and two at 5M (`quiet exclusive`), with the restart phase.
5. The ext4 VM probe at 1,445 files.

Read `.scratch/overnight/SUMMARY` first: one line per step with its exit code, then `logs/` and `coldboot/`. The cold-bootstrap harness (`agent-af2c08dd24001ec58/.scratch/cold-bootstrap-5m/coldboot.py`) had never run before; if the 100K lines fail, fix the harness and rerun step 4 only. The flush counts are the agent log lines "publication synced N files in N groups"; expected about 5,775 per publication at 5M before and about 25 after.

### The run queue

Long CPU runs cost no agent credits, so a run an agent actually needs goes in the queue rather than being watched by the agent. `.scratch/overnight/queue.sh` (under `nohup`, shared by every session on this host, Xapiand's included) runs `.scratch/overnight/queue/*.sh` one at a time, oldest first, after moving each to `done/`; it writes `done/NAME.log`, `done/NAME.rc` and a line in `SUMMARY`.

1. Write a job as a self-contained bash script named `PROJECT-what.sh`, with absolute paths and its own `quiet light`, `strict` or `exclusive` hold around each command.
2. Copy it into `queue/` only when finished (the runner may start it the moment it appears).
3. Queue only runs whose result someone will read; a run that loops forever blocks everything behind it, so bound every loop.
4. `touch queue/STOP` ends the runner after its current job; restart it with `nohup .scratch/overnight/queue.sh </dev/null >.scratch/overnight/queue-driver.log 2>&1 &`.

## Landing order

### 1. Reader S6

1. Read the soak results. Reproduce the `refresh status 5: invalid local generation descriptor` failure (`tests/detent_reader/test_reader_switch.cc:237`), find whether it is the test or the reader, fix it with a test that fails without the fix.
2. Finish the ledger's "Next (S6)": sanitizer runs, mutant group B, the `reader_bench` smoke run.
3. Merge `main`, dispatch CI, land when every job is green.

### 2. Legacy H

It deletes `detent::Client` and its tools, and replaces pybind11 and cxx with a C extension and a Rust FFI crate over `libdetent_reader`.

1. After S6 lands, merge `main` into `legacy/wave2-h`. Before running targeted tests, `touch` the two mutated source files the ledger names (stale mutant binaries are in `.scratch/build` and `bindings/rust/target`).
2. Dispatch CI. Two fixes there are still unproven: `.ccache/` is now ignored (the `git status` 64 KiB failure in `test_system_*`), and the bindings job moved to macos-15 with `-fexperimental-library` (`std::jthread`).
3. Tell the owner the two calls listed under "Owner questions", then land on green.

### 3. Namespace S3b, damaged-pack repair

Review: `docs/reviews/namespace-s3b-architect-2026-11-04.md`. Fix round, on `namespace/s3b` after merging `main`:

1. **L1 (blocks landing):** a collection requested while a reader still leases the damaged generation removes nothing and is never retried. Retry it when the lease goes away, with a test that holds a reader across the repair.
2. **AR1:** a verified page rewritten underneath a `MAP_PRIVATE` mapping can throw from the read path (`paged_read_state.cc:86`, `:96`, `:101`, `:103`; `borrowed_definition.cc:9`; `view.cc:390`). It fired in 59 of 150 trials and predates S3b. Fix per Q1 below.
3. **Q2:** recover every snapshot plan cold, not only a repair (`disk_store.cc:1599`, `:1809`). Update `test_testing_trusted_restart t5`.
4. **C3:** retry the scrub once, off the coordinator thread. **C4:** the docs the review lists.
5. Confirm each fix by rerunning the reviewer's probe and a revert check; CI green on every job; land.

### 4. Namespace S1/S2, grouped syncs

1. Use the overnight results for ledger Next 1, 2, 4, 6 and 7. Fix red tests; three were updated for the new primitive counts and not yet confirmed.
2. Do Next 3 (the three T7 mutants and revert checks) by hand.
3. Reconfigure `build-rel` without `FETCHCONTENT_SOURCE_DIR_COMPLETION` (the pin is `completion:560c288`), dispatch CI.
4. Write `docs/perf/sync-groups.md` with the before/after table, and `docs/mechanisms/sync-groups.md`; add the ext4 result to completion's `docs/PLATFORM_FINDINGS.md`.
5. Architect review (a durability change), its fix round, land. S1/S2 and S3b may land in either order; the later one merges the other.

### 5. Dense pages

Design: `docs/design/dense-pages.md` (DTPAGE03 and DTROOT03 replace DTPAGE02 and DTROOT02, no legacy reader; old stores are refused and refetched). Projected by `docs/design/dense-pages-model.py` from the 5M trial store, not measured:

| At 5M | Dense, 4 KiB | Today |
| --- | --- | --- |
| Pages | 162,367 | 7,000,007 |
| Bytes per record | 63.1 | 295.8 |
| Packs, zstd-1 | 31.7 MB | 530 MB |
| SHA-256 input at a full build | 0.62 GB | 3.77 GB |
| Pages per lookup, median/p99 | 5/6 | 12/12 |
| Delta bytes, 10K uniform keys | 42.8 MB | 25.5 MB |

1. Launch one architect, preliminary review, at max effort (a format). Fold its findings into the design; no second design review unless it adds a new mechanism.
2. Build S1 to S5 on one branch, in the design's "Build plan" (each step names files, criteria and the mutants its tests must kill): S1 codec and packer; S2 full build and validation; S3 reading (after S6 lands; both touch `resolve` and `lookup`); S4 deltas and relocation; S5 refusal of old stores and removal of DTPAGE02.
3. S3b's byte-flip tests must pass on the new format before the series lands.
4. Gate: measure the model's rows on `main` under `quiet exclusive` at 100K and 5M, with the model's projection beside each measurement. Decoder-safety review (security reviewer) and the architect's landing review, then land.

## Owner questions, with the default to follow

Act on the default unless the owner rules otherwise; note each in its ledger.

1. **S3b Q1:** check page headers against the page size on every read and map any exception from a verified page to `SourceFailure`. Default: yes.
2. **S3b Q2:** recover every snapshot cold. Default: yes (0.74 to 1.07 s at 100K, and the warm path walks the whole prior anyway).
3. **Dense Q1:** keep the pack checksum pass at build. Default: keep (the architect may overrule).
4. **Dense Q2:** a full copy ships records for S1 to S5; shipping files is a separate later design. Default: records.
5. **Legacy H:** `detent get` stays on `GenerationReader` and loses `--defaults` until the reader core gains generation-identity and capacity open options (reader S7); `CMAKE_POSITION_INDEPENDENT_CODE` is on for the whole build. Default: accept both.
6. **Homebrew tap:** pinned at `Detent:323b971`, far behind `main`. Bump only on the owner's word.
7. **`detentd --daemonize`** reports ready about 2 s before a leader exists. Default: make ready wait for a leader; a small fix with a test.

## Follow-ups, not blocking

1. Flakes 3 to 5 and 8 in `docs/status/http-coroutines.md`; flake 8 is the TSan failure of `test_local_paged_publication_artifacts`.
2. T9 and T4(7) measurements; the period-64 wire run.
3. Legacy step I (flat-v1 removal).
4. Pin wave (per the Xapiand lead, 2026-11-04, no Detent API change): strata main has cold-tier items 5 and 5b and manifest-log steps 1 and 2; journal main has the framing (`journal:5bbeedc`). Take them in one pin commit after the branches above land, CI green.
5. H's leftovers: Python on a free-threaded build, fork in the Rust tests, the unused `atomic-shared-ptr` link.

## Housekeeping

Delete each worktree and its build directories once its branch lands (check `git status` in it first; free disk is reported in every status note, 178 GiB today). Keep the owner's sidebar current with `report`, and `report notify` on a landing, a blocker or a decision.
