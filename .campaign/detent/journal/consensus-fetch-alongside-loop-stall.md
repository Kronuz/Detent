# consensus-fetch-alongside-loop-stall: journal

State: working
Quest: .campaign/detent/quests/consensus-fetch-alongside-loop-stall.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Reading CI 45372452058 (log: Detent/.scratch/detent/ci-pipesim/run-45372452058.log). Findings:
- The 1.5 s dump's `sample` maps loop threads 64392-64399, 64404-64407 to nodes 1-3 x loops 0-3.
  Node 2 loops 0 and 1 (64396, 64397) and node 3 loop 3 (64407) sit in
  serve_fetch -> the case's reader (`pattern(8 MiB)`, inlined in std::function's operator()).
  Node 2 serves {3,2} on loop 0 and {1,2} on loop 1; node 3 serves {2,3} on loop 3. Every
  other loop is in kevent or a condvar. No TLS, connect, DNS or lock frame anywhere.
- `sample` got 2 samples in its 1 s and started ~5 s after the dump began (Date/Time 10:47:03.9
  vs the rounds' start ~10:46:57.2): the runner barely ran a new process then.
- pattern(8 MiB) takes 13-22 ms here (-O2 -g, extensive hardening, x86_64 i9;
  .scratch probe pat.cc). CI held the loops ~7 s with the reader still running at ~6.5 s.
- Main's heartbeat-gap failures (45366654906, 45374547236, 45425539843) are all
  rpc_host `fetch`: gaps 1564/3746/3765 ms, host lag 10-22 ms, 16 MiB exact; its reader copies
  16 MiB (`return expected;`) per call on the serving link's loop. Same shape: one reader turn.
- Prior art: oversize-abort's 45453078372 (sendto 5.2 s in U on macos-15-intel), groups-upload
  (write in U 0.3-4.6 s). Same runner class.

## Next

1. <step>

## Blocked on

<What, and who decides. "Nothing" when nothing.>

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| <A1> | <from `campaign brief`> |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

<Decisions inside the quest's scope, so the reviewer can check them.>
