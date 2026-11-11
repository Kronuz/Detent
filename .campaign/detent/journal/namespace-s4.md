# namespace-s4: journal

State: waiting on jobs 261111-112151-b80250 261111-112200-5c3e22 261111-112206-82f33e 261111-112211-a15d6d 261111-112217-538846 261111-112224-51a2f5 261111-112229-231ca1 (pins 261111-112151-95bc87 green; F1 jobs done)
Quest: .campaign/detent/quests/namespace-s4.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Head c0d8f081: review CHANGES (namespace-s4-correctness at 96854bda) folded in. Jobs in State: F1 tear test 77b7ad,
F1 revert b25713 (fails without), reviewer probe counter_tear 588788 (for `accept --confirm F1=`), native b80250,
A2 strict 5c3e22 (after native), A1 mutants 82f33e a15d6d 538846, A3 mutant 51a2f5, A3 test 231ca1.

## Next

1. When the jobs finish: paste `campaign brief`'s Evidence table below, fill the A2 numbers into the Result and
   `docs/status/namespace-s4.md`, set State to "ready for review".
2. CI: the lead pushes head (contributors do not push). Run 43054127674 (0f071900) failed only on Linux legs at
   the apt/ccache step ("Temporary failure resolving azure.archive.ubuntu.com"), before configure; macOS passed.

## Blocked on

- File set: F5's fix edits `tests/detent_local/test_state_equivalence.cc`, which is not in the set; please add it
  (the lead asked for F5).

## Review findings (namespace-s4-correctness, CHANGES at 96854bda)

- F1 (fixed, e8983741; test 74883768): build and checking hashes now have disjoint counters, each only growing;
  `HashStats` holds `checked` and `built`, and `hashed()` sums them, so `checked_bytes()` and snapshot
  differences cannot wrap. Test `test_delivery_hash_stats_tear` (writer on `count_build_hash`, reader asserts
  `checked_bytes() == 0` and monotone reads): 77b7ad passed; revert `mutants/s4-f1-shared-counters.patch`:
  b25713 killed it (torn at read 225); reviewer probe rerun 261111-112147-588788: `reads=27837791 torn=0
  worst_checked_bytes=0` (was 3,822,927 torn at 96854bda). For `accept --confirm F1=261111-112147-588788`.
- F2 (follow-up): `PackScrub` and `PagedSegmentRead` collection proofs hash packs uncounted; outside A2's window.
  Follow-up: count them as checked Pack, or narrow `CoordinatorStatus::hash`'s comment.
- F3 (fixed, dbd4b8d5): `hash_once_report.sh` fails on any relay-delta exit but 124, which it names as the known
  hang (relay-delta-shutdown-hang), and checks hashed against built bytes from the printed line. Registering
  `relay-delta` in ctest waits for that quest.
- F4 (fixed, dbd4b8d5): heap pressure can no longer fail the trusted open after a pack install (local scan:
  available <= 1340 B fails at `.live`'s admission with 0 installs, >= 1344 B succeeds; nothing allocates after
  the take). The restored case grows the delta pack after publication, so its mapping's file check refuses it
  after `take_segment`; fence, replay refusal, ledger refunds and restart are checked as before (two runs).
- F5 (fixed, dbd4b8d5): the pack corruption flips the first page's magic (a pack's first page is at offset 0);
  refused as "invalid paged local image" by the walk, where the old last-byte flip relied on the digest.
- F6 (fixed): CHANGELOG gives both totals (698,201 instead of 2,094,603) and "re-hashes 0 bytes instead of
  1,396,402"; it also names `checked_hash_counts()`.

## Earlier findings

- Native 261111-080602-b970f1 failed `test_delivery_hash_once_damage` at line 184. Reproduced with the check split:
  261111-091328-380510 at 98c0abdd, 4 of 160 runs failed (8 at once), all at `scrub.damaged_packs >= 1`. Cause: the
  gauge reports one scrub pass and resets to 0 when the scrub moves to the repaired root (coordinator.cc scrub
  loop; `test_repair.cc` expects 0 after a repair), so reading it after the repair races the scrub tick. A test
  bug, not code: 4452eee0 checks `repair.requested` and `failures`, which only grow. After the fix: 184ad3.
- The relay-delta shutdown hang is pre-existing: probe at main 6cc26585, 261111-080405-ffc37e, exit 124; at the
  quest code ff7a1e20, 261111-080407-249b55, exit 124 (both print "delta published; closing" and do not drain).
  `relay-delta` stays out of ctest (CMake registers agent, relay, agent-delta, restart, damage); the lead opens the
  follow-up. The earlier pair 080104-4bc749 / 080106-08759e never ran the probe.
- Mutants 261111-080611-bdb97c, 080613-d97e81, 080616-194a7b, 080618-672220 gave no verdict: `hash_mutant.sh` wrote
  its build log into a build directory the job checkout lacked. 98c0abdd adds `tests/detent_delivery/hash_build.sh`,
  which configures a missing build directory as native does; the mutant and report runners use it.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| <A1> | <from `campaign brief`> |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

- The damage test asserts scrub detection through `repair.requested >= 1 && failures >= 1`: with no page read in
  that phase, the scrub's report is the only requester; the A3 mutant (scrub-no-compare) still kills it by timeout.
- Evidence jobs run unchained; only A2 (strict, capped at 10 minutes) keeps `--after` native, for the built slot.
