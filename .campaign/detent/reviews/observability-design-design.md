# observability-design review: design

Commit: 6139e4c4b07959669baa0d8d3d11208b3714f40b
Verdict: CHANGES
Date: 2026-11-09

APPROVE: lands as is, at this commit's code. CHANGES: lands after each blocking finding is fixed
and confirmed by a job at the fixed commit. REJECT: the approach is wrong; the quest is re-planned.
An APPROVE lists no blocking finding; a CHANGES lists at least one.

The shape is right. Sampled per-knob counting into a per-handle table, with arms rather than
values and a hook for exactness, answers D37. It targets the Reader API only, and the 8-thread data
back choosing sampled over exact counting. Two things block, and both are text fixes in the
document:

1. **Q7 understates what sampling loses for "which knobs are unused" (OD-1).** Knobs read once or
   a few times per process, such as at startup, are invisible on a host. The fleet path that would
   reveal them is deferred. The claim that the hook "already offers" exactness contradicts the
   document's own rejection of counting in the application only.
2. **OB1's cost gate is undefined at one thread (OD-2).** "Within D2's `knob_sampled` cost" has
   no number and no method that can resolve a single thread. The bench's single-thread spread is
   220 to 290 ns.

## Evidence I relied on

- **Confirm `261109-120858-28cd50`** (exclusive, `Detent:d1ce742`, ref `s6=b1d281c`, exit 0) and
  its `a1/out/confirm.{md,jsonl}`. The bench source and driver are unchanged from `d1ce742` to
  `6139e4c`. Only the document and journal changed, and `6f718fa..6139e4c` touches only the
  journal. So this record stands for the reviewed commit.
- **Screen `261109-120857-3fb43d`** at `d1ce742`, as the document cites it.
- **Reruns at `6f718fa`:** build `d3567b` and smoke `7b2e64` passed. Screen `bd1c57` and confirm
  `bc28dd` are still queued behind six exclusive namespace-s1-s2 jobs. I did not wait for them.
  A7 stays pending until they finish.

## What I checked myself

- **Paired analysis of the confirm output:**
  `.scratch/review-observability-design-design/paired.py <confirm.jsonl>` (offline; rerun it on
  `bc28dd` when that finishes). It computes per-repetition deltas against `none` from the same
  repetition. At 8 threads the `knob_sampled` paired medians are +2.8, +6.4, +6.7 and -4.4 ns. For
  `knob_rows64` they are +23 to +59 ns, and every repetition is above +15 ns. Sampled and exact
  are cleanly separated, which supports the pick.
  - The document's A/A band is the largest gap between `none` and `none2`, which always run
    back-to-back. That understates the noise for an unpaired median delta: `none` itself spans
    95 to 122 ns across repetitions at 8 threads and 221 to 287 ns at 1. "Inside every A/A band"
    reads stronger than it is. The supportable claim is "a paired median of 7 ns or less at
    8 threads".
- **Mechanism versus measurement.** The per-lookup work is one load, a decrement and a store on a
  stripe word. One lookup in 64 adds a hash of about 49 bytes, a probe and a relaxed add.
  Analytically that is 1 to 2 ns, which agrees with the 8-thread medians.
  - The bench never shares a countdown word: every 8-thread case has `stripes` 8 (jsonl).
  - The scattered set with a fixed reload samples only 16 of 1,024 names, so the probe stays in
    cache. The document says this, and leaves the random reload for OB1 to measure.
- **Citations at S6 `b1d281c`.** I checked `core.cc:649-699` (the `detent_eval` and `detent_get`
  bodies and their fallback writes) and `evaluation.hh:14-39` (the switch, rollout and default
  order). The arm set matches the branches, including a group rollout with no hash, which answers
  the default (`no_hash`).
- **Reader API only (question 3).** The design itself has no dependency on `Client`, defaults
  files or overrides. The switch rules are published definition data, which R2 does not remove
  (reader.md D6). Only the bench's setup touches `parse_defaults`. At S6 the test
  `GenerationWriter::publish_json` also calls it (`src/detent_testing/generation_writer.cc:326`),
  so OB1 should use `publish(Snapshot)` (note OD-9).
- **The arithmetic.**
  - (63/64)^64 = 0.36 and (63/64)^292 = 0.010.
  - 1,000 × 10 / 64 = 156.
  - The relative standard error is √(63/n).
  - The collision odds are 4,096² / 2^65 ≈ 4.5 × 10⁻¹³.

  All four are correct. paired.py also prints two numbers the document lacks:
  - The miss odds for knobs read once per process: 0.98 for one process, 0.85 for 10, 0.21 for
    100.
  - The standard error of a 50/50 arm split: 15.8, 4.0 and 1.3 percentage points at 640, 10,000
    and 100,000 reads.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| OD-1 | yes | gap | Q7 and Limit 2 understate what sampling loses for the owner's "which knobs are used". (1) A knob read once per process, at startup or on a reload, is missed with probability 0.98 per process and 0.85 across 10 processes on a host. A zero in the host document is not evidence of non-use, and the document never says so. (2) D7 item 6 says fleet aggregation is what reveals such knobs, but it is Q4/OB6, deferred until after namespace S4-S10. Until then dead-knob detection is unreliable for exactly the knobs most likely to be dead. (3) Q7 rejects exact counting as "exactness the hook already offers". The hook gives exactness only to applications that wire it, which is the adoption cost the document uses to reject counting in the application only. (4) Ramp verification gets the error of a count, not of an arm split: a 50/50 split is ±4 points at 10,000 reads and ±16 at 640, so a 1% ramp shows about 1.5 samples per 10,000 reads. | paired.py output; `observability.md` Q7, Limit 2, D3 item 6, D7 item 6, "Count in the application only" | State all four in Q7 and Limits. Then give the owner the real choices, without making any of them blocking: (a) sampled, with dead-knob answers waiting for OB6; (b) bring OB6, or a cheap host-level "seen" export, ahead for the dead-knob use; (c) evaluate an exact "ever read" bit keyed by definition position with test-before-set, which costs a load after the first read. Whether to measure (c) is the owner's call. |
| OD-2 | yes | gap | OB1's cost criterion, "T1's rows within D2's `knob_sampled` cost", has no threshold and no method that resolves one thread. The confirm's single-thread `none` moves 221 to 287 ns between repetitions and looks bimodal, from about 650 to about 1,180 ns. One thread per process is the common shape for Python under the GIL. OB1 is high risk and would have to invent its own gate. | confirm jsonl, 1-thread rows; paired.py "spread of none"; `observability.md` Build plan OB1, D2 item 4 | Write OB1's gate: counting on and off in one binary, alternated per batch or per small block on the same thread, paired deltas at 1 and 8 threads, and a numeric ceiling (for example a paired median of at most 10 ns at 8 threads and at most 5 ns at 1). It runs as an exclusive job. Reword D2 item 1 to the paired claim. |
| OD-3 | no | defect | The usage page cannot be read by the agent on Linux as specified. The app creates `usage/<pid>-<handle>` with mode 0640 in a 1733 directory. Without a setgid bit, Linux gives the file the creator's group, so the agent (`publisher_uid`, not that group) cannot `pread` it. macOS inherits the directory's group, so the macOS-only soak cannot catch this. | `observability.md` D7 items 1-2 | Make the directory 3733 (setgid plus sticky), group the agent's, and keep files 0640. Add to OB4's hostile cases a pre-created `usage/<pid>-<handle>` (the `O_EXCL` failure falls back to a private table) and a Linux readability test. |
| OD-4 | no | gap | Only `missing` and `wrong_type` have per-knob cells. Lookups with a valid name that return the caller's fallback for `DETENT_NOT_OPENED`, `DETENT_UNAVAILABLE`, `DETENT_NO_MEMORY` or `DETENT_SOURCE_FAILURE` have no stated cell. Yet A1 asks whether each knob fell back, and `samples[8]` is ABI. | `observability.md` D1 row 2, D3 cells, D4; `core.cc:655-656` (NOT_OPENED returns before `lookup`) | Map each fallback status to a cell, or add a `fallback_other` cell, before Q1 freezes the struct. |
| OD-5 | no | gap | The counts are not keyed by revision, but the exporter derives each arm's value "at the process's revision". A `published` delta that straddles a publish which changed an integer's value or a bool's default is attributed entirely to the new value. The host document mixes processes at different revisions. | D3 item 3, D4, D7 item 5 | State the limit: attribution goes to the revision at snapshot time, and the error is bounded to the scan window that straddles a publish. Report per-revision rows when processes differ. |
| OD-6 | no | gap | OB1 is missing details it needs. (1) Where the per-stripe countdown and xorshift state live: on D5's per-slot pin lines, which rotate with the ring, or in a separate handle array. (2) How they are seeded at open and in a fork child. A zeroed countdown samples every stripe's first lookup, which biases toward startup knobs. (3) No cost case where threads share a stripe. | D2, D5 of reader.md; jsonl `stripes` 8 in every 8-thread case | Name the location and seeding in D2, and add a case with 64 or more threads to OB1's cost criterion. |
| OD-7 | no | gap | OB4 has no criterion for what it exports: the shape of the JSON usage document, the 65,536-knob cap with `truncated`, and the Prometheus textfile. D9 says no exporter for `kronuz::metrics` is proposed, yet Q3 (a) needs something that renders the agent's `Counter` values as Prometheus text. | D7 item 5, D9, Q3, OB4 row | Say where the textfile writer lives: in the agent, or reusable in Kronuz/metrics, since the authority and relay could share it. Add criteria for the document and the textfile to OB4. |
| OD-8 | no | nit | D2 item 7 says the deltas "do not depend on" the 3.3× slower S6 baseline. At about 900 ns per lookup, out-of-order overlap can hide added work that a 268 ns lookup would expose. | D2 item 7 | Say the deltas are measured at the S6 baseline and that OB1 re-establishes them in-core. |
| OD-9 | no | nit | "`parse_defaults` goes with `Client`" is doubtful: at S6 the test generation writer's `publish_json` calls it for the authority's JSON format. | `generation_writer.hh` (S6) `publish_json`; `generation_writer.cc:326` | Have OB1 build test generations with `publish(Snapshot)`, and drop the claim. |

On question 2: the design answers D37. It covers knobs used (D3), resolved values and ramp arms
(D4), defaults taken (D1), freshness (D5), the hook (D6) and export (D7). Kronuz/metrics was
considered first: both of its counters were measured on the hot path and rejected there, at +46 to
+66 ns, and it is used in the agent (D9). OD-1 and OD-7 qualify this.

On question 5: OB2, OB3 and OB5 are buildable as written. OB1 needs OD-2, OD-4 and OD-6, OB4 needs
OD-3 and OD-7, and OB6 is correctly deferred.

## Not run

- No new bench job: the queue holds six exclusive jobs ahead of any probe. The paired single-thread
  resolution OD-2 asks for is OB1's to build.
- The reruns at `6f718fa` (`bd1c57`, `bc28dd`) had not run when I wrote this. Rerun `paired.py` on
  `bc28dd` once it finishes.
- No Linux check of OD-3 or of Limit 5's writeback faults; both are reasoned from the
  documentation, not run.
- I did not verify the agent, relay and authority citations in "What a lookup can tell today"
  beyond the core and evaluator.
