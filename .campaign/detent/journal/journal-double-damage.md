# journal-double-damage: journal

State: waiting on jobs 261120-094322-5b34a1 261120-094325-3140b2 261120-094327-db2bf3 261120-094329-72ecca 261120-094331-af0f45
Quest: .campaign/detent/quests/journal-double-damage.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Tests at journal:920a4f5 (on 1222e3b), fix at journal:cca2427 (turn_log.h recovery scan,
CHANGELOG). Branch pushed. Locally with the fix: turn_log 60 checks, 0 failures; turn_log_power
312 corrupted blocks, 295 doubly damaged boundaries, 2040 crash points, 0 failures.

## Next

1. Read jobs: 5b34a1 (A1 fails without), 3140b2 (A2 fails without), db2bf3 (A1), 72ecca (A2),
   af0f45 (suite, A3); check CI on quest/detent/journal-double-damage; then "ready for review".

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | <pending> |
| A2 | <pending> |
| A3 | <pending> |

## Result

<pending>

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
