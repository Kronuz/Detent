# superseded-sweep: journal

State: ready for review
Quest: .campaign/detent/quests/superseded-sweep.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Done; the ledger is committed.

## Next

1. Lead: review, spot-check three rows' users (A2).

## Blocked on

Nothing. Owner decisions are listed under "Proposed" in the doc (cluster's multicast bus; fantasyname).

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | [artifact] `docs/dev/superseded-libraries.md` at 80478d09: rows for stash, scheduler, cluster; users per repo and file; migration order; Proposed; checked-and-current list |
| A2 | [manual] lead (2026-11-11): `logger.h:50` includes scheduler.h; Detent `cmake/dependencies.json:105,117` pin scheduler and stash; no repo CMakeLists/cmake json or cmake file names Kronuz/cluster |

## Result

`docs/dev/superseded-libraries.md`. stash and scheduler: users are logger, Xapiand, Detent (pins), prism and http-log (via logger); not archivable yet. cluster: no users, so archivable, but only the Raft half has a successor (consensus). No other library is superseded.

## Rulings I made

- cluster is a table row though only partly superseded (the owner named it; D73 says "likely"); the caveat and the Proposed item state the gap.
- Users counted from tracked files (`git grep`) in main checkouts; pins in `cmake/dependencies.json` count as users.
