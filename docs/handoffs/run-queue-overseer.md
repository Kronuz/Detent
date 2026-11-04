> Status: CURRENT (2026-11-04) Brief for the agent that oversees the unattended run script and the shared run queue until both drain. Self-contained.

# Overseeing the run queue

Two `nohup` scripts on this host run long CPU jobs that need no agent: the fixed Detent run and a shared queue that runs beside it (both take quiet holds, so they never oversubscribe the host). Your job is to keep them moving, report each result to the lead (the Detent lead, per [detent-codex-2026-11.md](detent-codex-2026-11.md)), and stop them cleanly when they are empty. You change no code and land nothing.

## Where things are

Everything is under `~/code/Kronuz/Detent/.scratch/overnight/`:

- `run.sh`: the fixed Detent run (S6 flake soak, S1/S2 tests and sanitizers, cold-bootstrap timings at 100K and 5M, the ext4 VM probe). Logs in `logs/`, timing output in `coldboot/`.
- `queue.sh`: the queue runner. It takes `queue/*.sh` oldest first, moves each to `done/`, and writes `done/NAME.log` and `done/NAME.rc`.
- `SUMMARY`: one line per step and per queued job, with its exit code. Read it first, every time.
- `driver.log`, `queue-driver.log`: the scripts' own output; they matter only if a script dies.

## Each check (every few hours, and when asked)

1. `tail -20 SUMMARY`, then `ps -axo pid,etime,command | grep -E 'overnight/(run|queue).sh' | grep -v grep` to confirm the scripts are alive.
2. `quiet list` and `quiet check`: a step that has not advanced is usually waiting for a hold; `quiet list` shows who holds it and until when. Report free disk (`df -h ~`) each time, and say so plainly when it is below 20 GiB.
3. For each new nonzero `rc` in `SUMMARY`, read the last 40 lines of its log and report the failure in one or two lines: step, exit code, the error line, and whether it looks like the code under test or the harness.
4. When `run.sh` writes `done`, report its results to the lead:
   - The S6 soak: `run.sh`'s runs 1 to 4 used the pre-fix binary (2 of 4 failed with `link count (links 0, size 176)`, the T3 race); the binary was rebuilt at 15:18 with `Detent:47470ea`, so later runs and the queued `detent-s6-soak-fixed` job test the fix. Report failures in each separately.
   - The S1/S2 tests and sanitizers: which ones failed.
   - The cold-bootstrap pairs: time and flush count per run, before against after.
   - The VM probe numbers.

## Rules

1. Never edit `run.sh` while it runs (bash reads it as it goes). To change what runs, queue a new job.
2. Queue a job only when the lead or the owner asks for it. A job is a self-contained bash script named `PROJECT-what.sh`, with absolute paths, its own `quiet light|strict|exclusive NAME MINUTES "what" --` around each command, and bounded loops. Write it elsewhere and `cp` it into `queue/` when complete.
3. A step stuck far past its hold's deadline with no output growth: report it with the step, its pid and its log size, and kill it only on the lead's word (`kill PID`; the script moves to the next step).
4. If a script died: restart only `queue.sh` (`nohup .scratch/overnight/queue.sh </dev/null >.scratch/overnight/queue-driver.log 2>&1 &`). Never restart `run.sh` from the top; queue the remaining steps as a job instead.
5. When `run.sh` is done and `queue/` has been empty for a day, `touch queue/STOP`, wait for `queue stopped` in `SUMMARY`, and tell the lead.
6. No `/tmp` (use `.scratch/`), no AWS of any kind, no commits or pushes, no messages outside this host except to the lead and the owner.
