# consensus-inline-sync-loop: consensus: a journal sync on InlineCompletion never stalls its loop's links

Risk: normal (placement or a documented limit). The lead writes this quest; contributors read it
and never edit it. Its machine state is in `state.json`: `campaign status consensus-inline-sync-loop`.

## Outcome

Found by consensus-fetch-alongside-flake: groups and links are placed by the same slot hash
(`include/consensus/transport/host_transport.h` `loop_of_group`/`loop_of_node`; journal
`host.h:92-97`), so a group shares a loop with some peers' links by design. With the native
completion queue a journal sync runs on its worker or on AIO/io_uring, off the loop
(`bsd_completion.h:105-133`), and the links are unaffected; detentd uses it. With InlineCompletion
(the blocking entry, `DRIVER.md:153-155`) the sync runs on the loop, and a slow sync (6 s in that
quest's reproduction) stops the heartbeats of every link on it, which costs elections.

After this quest the blocking entry cannot stall links with a sync: either a group whose journal
completes inline is never placed on a loop that carries links, or the blocking entry runs syncs off
the loop, whichever is simpler and measured cheaper; if neither fits, `DRIVER.md` states the limit
and the transport refuses the pairing at construction.

## Acceptance criteria

- A1 A test: three nodes, the journal on InlineCompletion, a follower's sync blocked 6 s: no peer is
  lost and the leader keeps its term; a `--fails-without` job at main.
- A2 The native-completion path is unchanged: groups_test and the driver tests pass.
- A3 [artifact] `DRIVER.md` states where syncs run for each completion and what that means for
  links.
- A5 consensus-fetch-alongside-flake's review F3: groups_test's fetch-alongside-consensus case moved its group off every link loop, dropping coverage of a Driver sharing a loop with fetch-serving links, which production does by default. Once syncs cannot stall links, the case (or a second case) puts the group back on a shared link loop and passes A3's 25 runs beside 4 burners: strict jobs.
- A4 The suite passes and `ci:ci` is green at the landed commit.

## Contracts

The blocking entry's placement or its sync thread; no wire or format change.

## Decisions

Lead, 2026-11-23: its own quest; detentd is unaffected (native completion), so it is not urgent.

## May land early

A1's test marked expected-to-fail is not worth an increment.

## Tasks

None.
