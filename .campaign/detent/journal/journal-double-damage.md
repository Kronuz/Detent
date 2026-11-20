# journal-double-damage: journal

State: waiting on jobs 261120-103833-515d78 261120-103834-355174 261120-103836-0bd8e4 261120-103837-8062a8 261120-103839-e6f93e 261120-103840-a4ab03 261120-103840-0765f6
Quest: .campaign/detent/quests/journal-double-damage.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Durability review CHANGES (journal:cca2427) folded in at journal:178030b: F1, F2, F4, F5 fixed,
F3 recorded (R8). Mutant journal:27ecb12 = 178030b's tests with cca2427's turn_log.h.

## Next

1. Read jobs: 515d78 (A1 fails at mutant), 355174 (A2 fails at mutant), 0bd8e4 (A1), 8062a8
   (A2), e6f93e (suite), a4ab03 and 0765f6 (reviewer probes at 178030b); CI; "ready for review".

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | at journal:178030b: 261120-103836-0bd8e4; fails without: 261120-103833-515d78 (mutant journal:27ecb12); at journal:cca2427: 261120-094327-db2bf3, fails at 920a4f5 (1222e3b + tests): 261120-094322-5b34a1 |
| A2 | at journal:178030b: 261120-103837-8062a8; fails without: 261120-103834-355174; earlier 261120-094329-72ecca, 261120-094325-3140b2 |
| A3 | suite 261120-103839-e6f93e at journal:178030b; CI on the branch |
| review | durability F1-F5: probes rerun at 178030b: 261120-103840-a4ab03, 261120-103840-0765f6 |

## Result

journal:cca2427 changes only the recovery scan in include/journal/turn_log.h (`damaged_header`,
`Item::damage`, the newest epoch's tail check, unnamed damage costs every group); CHANGELOG
2026-11-20. Tests: turn_log_test `double_damage` (the probe) and `double_damage_copy` (a
forwarded copy that masks a loss); turn_log_power_test `double_damage`, 295 doubly damaged block
boundaries over 8 seeds, each reported and no kept group missing a record, alongside the
existing 2040 crash points with no false damage. Leaves open: R2's multi-sector header limit,
and R3's broader cost (a copy flag in the format would narrow it). ARCHITECTURE.md and
docs/mechanisms are outside the file set; their text is still accurate.

## Review findings (durability, journal:cca2427)

- F1 fixed: damage past the last kept turn also costs every group with a record in the turns
  dropped there (turn_log.h, recover). Tests: turn_log_test `damaged_header_last_turn`;
  turn_log_power_test `Damage::header` at every boundary. Reviewer probe, local at 178030b:
  "lost while reported" 0 at 512/4096/2048/768/256 (was 8, 0, 1, 2, 41); 1 at 1000 (F3).
- F2 fixed where the sector holds evidence: a failed header scans its whole sector for a fragment
  of its segment. Silent at 512 and 2048: 0 (was 21, 9); 4096: 3 (was 5); 256: 6 (was 61);
  768: 4 (was 12). The rest: the first fragment is the sector's only one (R7).
- F3 recorded, not fixed: R8.
- F4 fixed for fragments a sector holds whole: `damaged_fragment`. Last-block payload byte silent
  at 512: 1 of 8 (was 6), 2048: 4 (was 7), 256: 3 (was 5); 4096, 768, 1000 unchanged (R7).
  turn_log_power_test `Damage::last` asserts it for whole-sector fragments.
- F5 fixed: turn_log_power_test runs crash points at 512, 4096, 2048, 1000, 768, 256 (9624
  points, 0 failures locally) and damage patterns at the sector-aligned four.

## Rulings I made

- R1. Cause, two holes, both reached by double damage (block k past its header, block k+1's
  header, which carries k's manifest):
  1. Past the newest epoch's last kept turn, the damaged region reads as a torn tail: nothing
     after it shows the hidden turns were acknowledged (the probe).
  2. Before later turns, damage with no manifest cost only groups "that wrote nothing after it",
     and a forwarded copy of an older record after the damage counts as such a write
     (test `double_damage_copy`; also fails on 1222e3b).
- R2. Header damage is recognized without a format change: a header that fails while a fragment
  of its own segment starts in the header's 512-byte sector. A crash leaves each sector old or
  new and no earlier use of a recycled file holds this segment's fragments, so that sector is
  this use's and its header verified. Stale or zero headers never qualify, so power loss alone
  never reads as damage. Limit: a header past its first sector (more than ~26 manifest entries)
  whose damage lies beyond that sector still reads as possibly torn.
- R3. Damage that no manifest names now costs every group (was: groups with no record after it).
  A record cannot be told from a forwarded copy without a format change, so "wrote after" proves
  nothing. Costs more groups on that rare damage, never a silent loss. A copy flag in the record
  format would let recovery keep groups with a later original write: the lead's call if wanted.
- R4. The power-loss test passes `TurnLogGroups::open` for the double-damage pass, per the
  TurnRecovery contract (a damaged log's open groups it holds nothing of are lost).
- R5. torn_tail cut 20 bytes inside a sector, which the declared crash model (framing.h: a sector
  persists old or new) never leaves; it now loses the turn's last sector whole. A byte-granular
  cut reads as damage now: reported, never silent.
- R6. Condition 10 (recycling and forwarding) is checked at the 512-byte geometry only: 60 turns
  do not fill the larger geometries' segments.
- R7. Limit: a failed fragment or header whose sector holds no bytes bound to its segment (its
  only fragment there damaged in its segment or epoch fields, or a fragment crossing a sector)
  reads as a tear. Telling more needs per-sector checksums: a format change.
- R8 (F3). Limit: with block_bytes not a multiple or divisor of 512, a header shares its sector
  with the block before and the rule sees few headers (1000: 5 silent of 170 double damages).
  Options for the owner: require block_bytes to be a multiple or divisor of 512 (valid_block_bytes,
  a public contract change), or keep the documented limit. Recommendation: require it; every
  geometry in use (32 KiB default, 512 in tests) already is.
