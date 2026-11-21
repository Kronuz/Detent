# journal-power-silent-loss: journal

State: waiting on jobs 261121-021347-4f85dd 261121-021358-5dfddc 261121-021401-adac93, CI at 3e36940
Quest: .campaign/detent/quests/journal-power-silent-loss.md (the lead's; never edit it)

## Doing

msg-192: test/turn_log_power_test.cc drew a record's size and kind inside one call's argument
list, so Clang and GCC ran different workloads from one seed. journal:3e36940 draws them into
named locals, size first as Clang did. Clang's workloads are unchanged: the five seeds hit the same
segments, blocks and checks at the new mutants. The fix is test-only. The 200-seed rows on macOS,
FreeBSD and Linux at 3e36940 are running.

## Next

1. Merge journal-idle-groups when it lands; both edit turn_log.h, in different hunks.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | `docs/mechanisms/turn-log-damage.md` (journal:0465f24): the rules and the seed that pins each; per-seed diagnosis and old-code replay in Result below |
| A2 | 261121-021345-4841ea (the five seeds pass at 3e36940); 261121-021347-4f85dd (macOS, 200 seeds), 261121-021358-5dfddc (FreeBSD VM, 200), 261121-021401-adac93 (Linux VM, GCC, 200): pending |
| A3 | 261121-021345-4841ea (the five seeds pass at 3e36940); fails without: 261121-021331-c7d5d8, 261121-021333-c493df, 261121-021336-c45179 (seeds 91 and 140 @512, 122 @256 at mutant 3793195, tail fix reverted); 261121-021339-6e1ee1, 261121-021342-6b2ffb (seeds 145 and 199 @2048 at mutant 59c9300, length trial removed). Mutants are kept on the local branches mutant/journal-power-silent-loss/{tail,length} |
| A4 | `ci:macos-arm64` |

msg-183: the five failing seeds (91, 140, 145, 199, 122; msg-184's 91 included) are diagnosed and
fixed below.
msg-192: fixed at journal:3e36940 (test-only); see Doing.
msg-184: seed 91 @512 was already in the set: same cause as 140 (cause 1).

## Result

CI run 44871736665 is green at journal:0465f24, every job including macos-arm64 (A4).


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
(journal:4b9166f). CHANGELOG has entries for both. test/turn_log_power_vm.sh (journal:b1bf19a) runs the row in a local VM.

## Rulings I made

- Fix 2 is a probabilistic test. A real tear that verifies at some shorter length (about 488 x
  2^-32 per tear) is reported as damage and costs groups. It never loses data. I chose this over
  dropping the length bytes from the test's 'last' pattern, which would document the silent loss
  as a limit instead of fixing it.
- The rules are in docs/mechanisms/turn-log-damage.md, turn_log.h comments and the CHANGELOG.
  mutants.py is outside the file set, so the mutants are --fails-without jobs at the
  pre-fix commits.
