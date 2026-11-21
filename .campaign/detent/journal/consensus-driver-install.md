# consensus-driver-install: journal

State: working
Quest: .campaign/detent/quests/consensus-driver-install.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Building the Driver install surface in driver.h/worker.h on the contract at consensus:2c598fb
(docs/DRIVER.md "Install"; sent to Xapiand as msg-187).

## Next

1. driver.h: per-group SnapshotChannel, transfer(), SendTransfer/ValidateImage/ActivateImage,
   validated/validation_failed; worker.h: snapshot_image(); host_transport.h: send_transfer, is_transfer.
2. test/driver_install_test.cc: A1 (lagging follower installs, applies past), A2 (failed/validated).
3. driver_simulation: installs under seeds (A4), crash at every persisted step (A3) + mutant.
4. Merge consensus-quiescence / consensus-transport / node-admission when they land.

## Blocked on

Nothing.

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

<Decisions inside the quest's scope, so the reviewer can check them.>
