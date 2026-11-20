# dense-distribution-build review: correctness (with the security questions, D135's one review)

Commit: Detent:a0a435b49d7cfd443537340ae0346b706e6915bc (src, include, tests and CMake identical to Detent:427bdd30)
Verdict: CHANGES
Date: 2026-11-20

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- Suite `261120-113956-1e73f5` green at Detent:427bdd30 (same code as the head): the receiver's hermetic
  refusals, test_pages_coordinator (100K cold boot with 0 build hashes, packs equal to the records
  build, a pages delta, wrong-root full copy and delta falling back to records with equal reads),
  test_relay_pages (full copy byte for byte, D6 choice, window miss keeps records, off-thread build),
  test_system_relay_pages (real relay, records agent and pages agent at equal roots).
- `261120-114005-1bacdc`: forced root mismatch through a real relay is refused, rebuilt from records, same root.
- `261120-114010-d77302`: the build-on-mirror-thread mutant fails test_relay_pages.
- DD1 fixtures `261120-053017-1bd05e` (100K model root and packs byte for byte, model counters).
- CI (A2) and TSan are CI's; not green yet at this head (queued), so concurrency under TSan is not
  established by any result I could read.

## What I checked myself

Scripts in `.scratch/detent/review-dense-distribution-build-correctness/` (home checkout); each runs at
the quest commit as a job.

- `foreign_namespace_probe.{cc,sh}` (job `261120-120143-1b3c7e`, exit 3 = finding): a source serves
  namespace `big` the honest, correctly rooted transfer of namespace `bad` under big's cursor. The agent
  publishes it ("received 1, refused 0"), reads of big differ from the server, and the next records
  delta of big fails 599 times in 30 s with "delta namespace outside recovered scope": stuck, no repair.
- `relay_answer_lifetime_probe.{cc,sh}` (job `261120-120448-4c6dcb`, exit 134): ASan
  heap-use-after-free in the relay's pages answer (F2).
- `dd1_mutants.sh` with DD1's seven check mutants against the 100K model fixtures (job
  `261120-120156-0e5cc8`, `--fails-without`, passed): 7 of 7 killed. Under the `canonical` (D5 packing)
  mutant the suite's hermetic test passes; only the fixture case kills it (F7).
- `d136_mutants.sh` (job `261120-120600-2910d1`, `--fails-without`, passed; question 5): with the
  leader's idle seal disabled, test_system_managed_relay fails (agent 0 never converges; note that
  test_system_relay_pages still passes without it); with a damaged entry's recovery failure no longer
  failing the entry, test_namespace_coordinator fails. Both D136 fixes are load-bearing and tested.
- `transfer_fuzz.sh` (job `261120-121316-a7d94a`, exit 0): the libFuzzer build with brew LLVM fails in
  the compressors dependency (libc++ header search; jobs `261120-120145-7a35c5`, `261120-120645-7a28c5`
  too), so it fell back to the fuzz target's replay main under Apple clang ASan+UBSan: 200,000
  deterministic inputs, 4 receives each (raw stream as a full copy and as a delta; damaged and
  truncated honest transfers, checked byte for byte against the honest result when accepted), nothing
  found. This is not DD1's coverage-guided CPU-hour (F11).
- Code read: `receive_transfer` (paged_transfer.cc:258-388), every length and count against its limit
  (below), the Pages frame grammar (session_frames.cc), FrameInput's admission, the coordinator's
  pages paths and failure handling, the relay's build, window and answer paths, MirrorService's builder
  thread, D136's seal entry and failed-entry path.

Decoder bounds, from the code (security question 1): a stream page needs 12 readable bytes before its
magic and length are read (paged_transfer.cc:275); length in [79, 65,536] and within the rest of the
stream (:278); page count and total bytes against `limits.pages` and `limits.bytes` before any
allocation (:280), segments against `limits.segments` (:289); `decode_page_header` requires the
stated length to equal the slice and validates externals against it before `content_at` or
`placement_at` index the page (paged_layout.cc:218-231); node decoding is bounds checked
(paged_layout.cc:236-268). The walk cannot loop: an external child must precede its parent and is used
once (:87-92), an in-page slot must be the next preorder offset (:184-186), and recursion depth is
bounded by the 202-byte key path (:155, :161). Base resolution consumes at least one path byte per
step (:103-140). The order check cannot read past `order` (its last entry is always the root, so a short
order mismatches first). The frame bounds chunks by `limits.bytes` (session_frames.cc chunk count and
running total) and FrameInput admits every byte through the storage account. A wrong root, any D4
refusal or a short stream leaves nothing written: segments live in memory until publication. Nothing
I found writes outside its target or allocates past its limits; F1 is what the receiver accepts.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | defect | The pages receiver never binds a transfer to the frame's namespace. `receive_image` and `receive_delta` take no name, and `check_leaves` checks revisions only, so a source (hostile, or a relay that mixes up namespaces) can publish another namespace's keys, several namespace leaves or none under a namespace's cursor with a root that verifies. The records decoder refuses the same keys ("keys of the frame's namespace"). The namespace is then stuck: every records delta fails ("delta namespace outside recovered scope") and nothing asks a repair. | Job `261120-120143-1b3c7e`; paged_transfer.cc:27-47, :396-425; coordinator.cc:388-413, :454-492 | Pass the frame's name to the receiver: every definition leaf's key in that namespace and exactly one namespace leaf, its name; refuse as `TransferRefusal::Leaf` (records fallback). Same for a delta's new pages. Test: the probe's case in test_pages_coordinator (full copy and delta), refused, then records, reads equal. |
| F2 | yes | defect | Relay use after free. `answer()` keeps a raw `BuiltCopy*` and a span of its `transfer()` for a full-copy pages frame (namespace_relay.cc:370-373); a later namespace's `copy()` or `delta()` in the same answer calls `build()`, whose `collect()` (:432) can land the first namespace's newer build, replace `held.head` and let the window evict the borrowed build (page_window.cc:121-124). The encoder then reads freed memory (session_frames.cc:272). Needs a session following two namespaces with pages, a build landing mid-answer, and a rewrite that shrinks the namespace. | Job `261120-120448-4c6dcb`: ASan heap-use-after-free, READ of 65,536 B in `SessionFramesEncoder::Impl::chunks`, freed in `~BuiltCopy` from `collect()` via `build()` via `copy()` | Hold a `shared_ptr<const BuiltCopy>` for every borrowed copy until the body is encoded (or collect only between answers). Test: the probe's deterministic runner case in test_relay_pages (CI's ASan job catches a regression; also check the frame's bytes equal the build it names). |
| F3 | yes | gap | The quest's outcome says "a fetch resumes from its offset" (D8, D9's first row; DD3's done list). Nothing resumes: a lost connection mid-transfer restarts the response, and no test covers it. No ruling records dropping it. | quest file, Outcome bullet 3; no resume path in src/detent_delivery or src/detent_relay; journal Next 4e lists "a resumed fetch" as not done | Build D8's resume, or the owner amends the outcome (a 5M copy is 25.7 MB to refetch). |
| F4 | yes | gap | The relay deviates from D7 without the measurement and report the quest file asks for first. It runs a full in-memory build at every head a pages reader needs (page_copy.cc:75-111; namespace_relay.cc:402-433), not dense-pages D7 after its cold build (D7.2); DD2's relocation (paged_canonical_copy.cc) has no caller outside its test (D7.4). The window keeps 4 full builds, each with its packs and its whole wire transfer in memory (namespace_relay.cc:16, page_copy.cc:98-109); its byte bound counts only diff bytes (page_window.cc:115-121), so by the design's 5M figures (315.8 MB packs + 315.4 MB transfer per build) one 5M namespace can hold about 2.5 GB plus the build in flight. Delta transfers are recomputed per session answer, not cached by name (D2). | Code as cited; relay-pages journal "Rulings I made" | Owner's call, with a 5M relay RSS and CPU measurement: accept it as measured, or build D7.2/D7.4 (incremental build, DD2 relocation, shared window pages, cached transfers). |
| F5 | no | gap | D1's records-path root check is not built: Entries, Blocks and FullCopy frames carry no target root, so D6's "what a warm receiver relies on" item 2 (divergence becomes a refusal) does not hold; divergence stays silent, as on main. | session_frames.cc (root only for kind 5) | Owner's call: a root in records frames, or record the limit in the design. |
| F6 | no | gap | D9's "one other source" before records is not built: `receive_delta` is called with `refused_sources = 1` and a refused full copy goes straight to records. Recorded as a ruling in the agent-pages journal. | coordinator.cc:481, :603-607 | Owner's call; keep as is or build it. |
| F7 | no | gap | A1 asks the suite to refuse each D4 case; the "tree packed against D5" case is only in the model fixtures, outside the suite. The suite's hermetic test passes under the `canonical` mutant; the fixture case kills it. | Job `261120-120156-0e5cc8` ("hermetic test under canonical: ... pages sent") | Add a hermetic non-canonical-packing case to test_paged_transfer, or check the fixtures into the suite. |
| F8 | no | nit | A cold reader's next answer waits for a full relay build at the head whenever the records answer is at least a third of the root page (about 1.4 KB), even when the change is scattered and records will be chosen anyway; it converges (a head move answers records), but each change after a pages copy or pages delta costs a full build's latency. | namespace_relay.cc:487-507 | Optional: answer records without waiting below D6 rule 1's bound measured on the window's diff, not the root page. |
| F9 | no | nit | D136's `damaged()` treats every non-pressure recovery failure as damage, including transient I/O errors and GenerationUnavailable (which is then fatal anyway), so a transient error refetches a full copy. | coordinator.cc:305-330 | Optional: narrow to the store's damage and verification errors. |
| F10 | no | nit | A repair or D136-failed entry is refilled from the relay's newest build, which may be older than the position the agent had published; a repair over the base is refused until a build at or above it lands, and a failed (unselected) entry publishes the older position. | namespace_relay.cc:472-484; disk_store.cc:401-415 | Owner's call: accept, or ask the relay for a build at or above the agent's last position. |

| F11 | no | gap | DD1's "one CPU-hour of fuzzing finds nothing" has not run anywhere: no fuzz job exists for this quest or its tasks, and the libFuzzer target does not build with brew LLVM 23 on this machine (a dependency's header search). My 200,000-input sanitizer replay found nothing. | Jobs `261120-120645-7a28c5`, `261120-121316-a7d94a` | Fix the fuzz build (or run it in CI's Linux clang), then one CPU-hour on the receiver; seed it with F1's foreign-namespace case once fixed. |

F1 attacker: a relay (or anything on the path the agent trusts as its source). Limit 1 already trusts a
relay's content within a namespace; F1 lets it break the namespace's shape, which the records path
refuses, and it leaves the namespace unrecoverable without a manual reset.

## Not run

- CI and TSan at this head (A2): queued when I wrote this; the off-thread build race (question 4) rests on
  code reading (the builder gets copies of layout, ring and file bytes; results cross under a mutex) and
  the mutant job above, not on a TSan result.
- The 5M rounds (DD1's counters at 5M, DD3's 25.7 MB as sent, the cold-boot measurement): the lead's,
  after landing.
- DD2's A3 mutant (seal-order sort): DD2 has no production caller (F4).
- Fuzzing of the W6 Pages frame grammar itself (only the receiver's target was fuzzed).
