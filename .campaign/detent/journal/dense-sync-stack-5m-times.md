# dense-sync-stack-5m-times: journal

State: blocked on the lead: A1's arm jobs cannot be rerun at the head (see Blocked on); main merged, CI pending
Quest: .campaign/detent/quests/dense-sync-stack-5m-times.md (the lead's; never edit it)

## Doing

All eight exclusive jobs finished exit 0 and are written into `docs/perf/dense-sync-stack.md` (5M section, Runs table). Waiting on the check suite job at the head.

## Next

1. When the suite job is green and CI is green on the pushed head, set State to ready for review.

## Blocked on

The lead: A1's eight jobs run each arm at its own commit (dense 9852a3c4, s2, stacked, main; harness via --ref), so no twin exists at the quest head and brief says "ran other code" by design. `campaign job rerun` at the head would run the same code in every arm (refused for 9852a3c4: only on wip/laptop/head-dense-cb); `--commit ARM` reruns the arm but is still other code than the head. Options: 1. accept A1 as [manual] on the existing ids (it is [manual]: the lead checks ids against `campaign job list`; recommended, the arms are the experiment). 2. Resubmit with the head as the job commit and each arm's commit as a --ref the command builds from: new submit script and about 2 h of exclusive time; not done.

CI at aa8a5c04 (run 44698269834): failed only test_system_managed_relay_ipv6 on macos-x86_64 ("managed agent 0 did not converge", 275 of 276 passed). This branch changes docs only, so it is not this branch's; main since gained 5bff29c7 (dial the bound address, not localhost; the macOS ::1 self-connect fault), merged here at b2b451e0, and CI is rerun on that head. The quest asks for a window agreed with the Xapiand lead; I have no channel to it and did not open `campaign window` (it holds other campaigns' light work). The lead may open one to start the queue.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | [manual] Eight `exclusive` jobs, 10 min bound, quest-tagged, each exit 0, each at its named commit (dense Detent:9852a3c, s2 Detent:35296ba, stacked Detent:d04794d, main Detent:ea1fddc; harness Detent:ea1fddc). Round 1: dense 261119-155051-8896b4, s2 261119-155053-88d935, stacked 261119-155055-bede28, main 261119-155057-fc4275. Round 2: dense 261119-155100-6a6707, s2 261119-155102-50abde, stacked 261119-155104-b28b3c, main 261119-155106-5540f0. No job has a "beside" record in its state, log or runner log; each ran its measured part from `began` to `ended` with no other hold (5540f0 waited about two hours to start and then ran alone). Lead checked 2026-11-20: all eight ids are `done` at level `exclusive` in `campaign job list`. |
| A2 | [artifact] `docs/perf/dense-sync-stack.md`: the 5M times table and paired differences beside the 100K table, and the Runs rows. |

## Result

5M cold bootstrap (`agent_selected_s`, median of two rounds, rounds in the doc): stacked 136.24 s, Dense alone 158.23 s, S2 alone 298.11 s, main (own fixture) 162.69 s. Stacked beats Dense alone by 22.0 s (14%) and S2 alone by 161.9 s (54%), rounds not overlapping; the 22 s is 738 fewer device flushes at about 30 ms. Restart at 5M is not flat: stacked 0.101 s, Dense 0.128 s, S2 0.375 s, main 0.084 s (0.07 s everywhere at 100K); the S2 cause is unmeasured. Main is 26.5 s (19%) slower than stacked at 5M on its own fixture, the reverse of 100K (34% faster); fixture or code is not separable with 2 rounds. Doc: `docs/perf/dense-sync-stack.md`. The doc's old claim "Restart stays at 0.07 s in every arm" is now scoped to 100K.

## Rulings I made

- No `--long`: it defers the job; the arms took 3 to 6 min in the hold. Holds are at most 10 min, inside the 25 min limit.
- No `--for`: A1 is [manual] and campaign refuses `--for` on it; the jobs carry `--quest`.
- Smoke: main uses `10000-main`, the others `10000-r3`, as on 2026-11-18.
- `docs/perf/dense-sync-stack.md` is outside the quest's file set but is A2's named artifact; I edited only its 5M text, Method line, summary and Runs rows.
- 5M spread is the two rounds (no new A/A); a difference counts when the arms' rounds do not overlap. Merged main into the branch (the brief asked).
