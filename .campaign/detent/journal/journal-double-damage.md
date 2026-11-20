# journal-double-damage: journal

State: working
Quest: .campaign/detent/quests/journal-double-damage.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fix written in the code worktree (include/journal/turn_log.h, recovery scan only); tests committed
first at journal:920a4f5 (on 1222e3b, fail without the fix). Running the local suite.

## Next

1. Commit the fix; jobs: A1 `--fails-without` at journal:920a4f5, A1/A2 at the fix head, suite.
2. Push the branch; CI green; "ready for review".

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
