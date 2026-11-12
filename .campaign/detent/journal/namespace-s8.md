# namespace-s8: journal

State: working
Quest: .campaign/detent/quests/namespace-s8.md (the lead's; never edit it)

## Doing

Mirror store, content-agnostic (D7 "its own files follow D6's commit and recovery, with the
upstream's layout in place of the rules"): `include/detent_relay/mirror.hh`,
`src/detent_relay/mirror.cc`. Files are named and digested by the upstream's layout; the relay
fetches the ones it lacks by name, verifies each digest, links them from `.tmp-` names, syncs them
as one D11 group, publishes its own table `mirror` (`.tmp-`, fsync, rename, directory sync), then
collects files the durable table no longer reaches. Answers are planned from the table (ring /
blocks / full copy by R against the cut), read from disk, digest-checked on read.

## Next

1. Mirror store + table codec + adopt + startup + collector + answer plan.
2. `tests/detent_relay/test_relay_mirror.cc` (A2, A4, chain part of A1) over a scripted upstream
   that counts bytes fetched.
3. `tests/detent_relay/test_relay_mirror_crash.cc` (A3), the T7 harness over completion's sync seam.
4. Durability review.

## Blocked on

1. **A1 needs S5.2 (namespace-blocks).** The quest consumes `namespace-blocks`, which
   namespace-s5-2 provides; it is `planned`, behind namespace-measure. No voter writes a seal
   file, block, base or `layout` on `main`, so "byte-identical to the voters'" has nothing to
   compare against, and the layout's wire (fetch the upstream's layout, a file by name) is not
   served. The brief only names namespace-s6. Options: (a) S8 builds the mirror against an
   abstract upstream layout now (names, digests, sizes, ranges, cut, opaque policy), proves A2-A4
   and the relay-chain half of A1 over a scripted upstream, and the voter half of A1 plus the
   wiring into `service.cc` land after S5.2 as a follow-up task; (b) hold S8 until S5.2 lands.
   Recommendation (a), which I am doing: the mirror's durability (A3) does not depend on the
   block format, only on names and digests. Lead decides whether S8 needs S5.2 before landing.
2. `tests/CMakeLists.txt` needs `add_subdirectory(detent_relay)` for the file set's
   `tests/detent_relay/**`; that one line is outside the file set. See Rulings.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | <no evidence yet> |
| A2 | <no evidence yet> |
| A3 | <no evidence yet> |
| A4 | <no evidence yet> |

## Result

<pending>

## Rulings I made

1. The mirror is content-agnostic: it never parses a block, base or section; the answer plan names
   files and the revision filter, and S5.2's codec trims and merges. So nothing here fixes
   S5.2's formats.
