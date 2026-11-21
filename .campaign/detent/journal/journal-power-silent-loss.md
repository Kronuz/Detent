# journal-power-silent-loss: journal

State: waiting on jobs 261120-201511-64af67
Quest: .campaign/detent/quests/journal-power-silent-loss.md (the lead's; never edit it)

## Doing

Fixes built (journal:e77d17b, journal:dbab658); 200-seed macOS row at dbab658 running; CI run
44865217682 on the pushed branch.

## Next

1. Read 261120-201511-64af67 (200 seeds, macOS); the FreeBSD VM row is Xapiand's driver
   (~/.scratch/c2-gate/vm-v5.sh freebsd journal, C2_JOURNAL=dbab658): the lead's call to run it.
2. CI green (A4); merge journal-idle-groups when it lands (both edit turn_log.h, different hunks).

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | Result below (diagnosis per seed; old-code replay of the five seeds) |
| A2 | 261120-201510-15f224 (all five seeds pass at dbab658); 261120-201511-64af67 (200 seeds, pending); FreeBSD VM: not run |
| A3 | 261120-201407-d594b9 (seed 91 @512 fails at 4b9166f, test only); 261120-201509-96e66d (seed 145 @2048 fails at e77d17b, tail fix only) |
| A4 | CI run 44865217682 (pending) |

msg-183: the five failing seeds (91, 140, 145, 199, 122; msg-184's 91 included) are diagnosed and
fixed below.
msg-184: seed 91 @512 was already in the set: same cause as 140 (cause 1).

## Result

Not a regression from journal-double-damage. 178030b's test added the 'both' and 'last' patterns
and five geometries; d90797c never ran them. The 178030b test built against d90797c's headers
(local, ~/.scratch/detent/journal-power-silent-loss) fails all five seeds plus many more
(seed 122 @256: 59 failures; 91 @512: 19; 140 @512: 12; 145 and 199 @2048: 6 each). 178030b fixed
most of them, and these two causes were left:

1. Damage read as a segment's unwritten end (seeds 91 and 140 @512 'both', 122 @256 'both').
   The pattern is a byte past block k's header plus one in block k+1's header. When k and k+1 are
   a segment's last written blocks, the merged gap is the segment's tail, and a tail costs nothing
   unless a manifest names its first block (k's manifest is in k+1's damaged header).
   - Seeds 91 and 140: the oldest segment, before the first kept turn. Nothing charged it, so
     `damaged` was false. Groups 2 (seed 91) and 1 and 6 (seed 140) were reported lost, but a group
     whose records were all there would have vanished from both maps.
   - Seed 122: segment 42's tail, before the incomplete acknowledged turn 57. missing_between
     charged segment 41's empty-manifest tail instead, so group 6 was kept with 2 of 3 records.
   - Fix (journal:e77d17b): a tail whose scan flagged damage (Item::damage) is a gap.
2. A damaged fragment length read as a tear (seeds 145 and 199 @2048 'last'). The low length byte
   was flipped: 29 became 71, so a fragment its sector held whole now claimed to cross into the
   next sector. damaged_fragment reads a crossing fragment as a possible tear, so the acknowledged
   last turn was dropped unreported. This cannot happen at block_bytes 512 or 256, where the
   sector end is the block end.
   - Fix (journal:dbab658): a crossing fragment is damage when its CRC verifies at a length that
     its sector holds whole. That costs at most 488 CRC trials, only for a failing fragment of this
     segment and epoch.

Neither cause is F2 or F3. Test: turn_log_power_test takes SEEDS FIRST_SEED BLOCK_BYTES
(journal:4b9166f). CHANGELOG has entries for both.

## Rulings I made

- Fix 2 is a probabilistic test. A real tear that verifies at some shorter length (about 488 x
  2^-32 per tear) is reported as damage and costs groups. It never loses data. I chose this over
  dropping the length bytes from the test's 'last' pattern, which would document the silent loss
  as a limit instead of fixing it.
- No turn-log recovery mechanism doc exists, so the rules are in turn_log.h comments and the
  CHANGELOG. mutants.py is outside the file set, so the mutants are --fails-without jobs at the
  pre-fix commits.
