You are the Claude Code lead resuming your existing campaign and its full scope. Recover your own master plan, responsibilities and workstreams, including any other repositories you already lead, and keep that scope. Dense pages is one delegated task returning to you; this prompt does not replace your plan with it.

Move your work onto `campaign` (github.com/Kronuz/agent-campaign, branch `preflight-hardening`), then continue all of your authorized work. Work without stopping for confirmation unless a step is irreversible or only I can decide it.

First recover your latest master plan and ledgers. Then read `docs/handoffs/dense-pages-2026-11-06.md`, the return handoff for Dense pages (its branches, evidence and remaining jobs), and `docs/handoffs/detent-codex-2026-11.md`, a historical source for the Detent part of your work. Neither handoff limits your role.

## What is already true (2026-11-08)

- All agents are stopped and no campaign job is queued or running. A parked Codex session may still be open in this home; it is idle. Do not drive it.
- Every Dense branch is on GitHub at the same commit as here (`github` remote; `origin` is a stale local path): `quest/dense-pages` and its six task branches. `performance` (4 commits) and `real-deltas` (7) are not merged into the quest; the other four are. Also pushed: `dense-pages-grouped-sync` (the quest's head), `dense-pages-first-read` (3 commits, the first-read attribution work) and three snapshots, `wip/dense-s2-parked`, `wip/dense-t2-base-parked` and `wip/main-parked-2026-11-08` (local `main`).
- `gh` on this laptop holds an invalid Kronuz token, so pushes fail until I run `gh auth login`. Do not work around it: keep working locally and list what waits to be pushed in your report.
- `Detent-wt/dense-s2` is mid-merge of `namespace/s1-s2` (grouped syncs) into the Dense head, with a conflict in `src/detent_local/disk_store.cc`. `wip/dense-s2-parked` holds it exactly as it was. That work belongs to the grouped-sync workstream, not Dense.
- Local `main` is 28 commits ahead of `github/main`: the Dense campaign's state, the handoffs and this prompt (the last commit).
- The Dense campaign keeps its files in `docs/campaign` on `main`, under the name "Detent Dense pages". The new tool reads only `.campaign/<name>/`, so it is moved in place below. The job records (`~/.scratch/campaign/jobs`, 12 GB) stay and are reused.

## Install

Install `preflight-hardening` through dotfiles on a local branch. `main` stays as it is, so rolling back is switching back.

```sh
cd ~/.local/share/dotfiles
git status --short                          # must be clean; stop and tell me if not
git switch -c campaign-trial
git -C home/.agents/skills/campaign fetch origin preflight-hardening
git -C home/.agents/skills/campaign switch --detach origin/preflight-hardening
cd home/.claude/agents
git rm -q adventurer.md paladin.md
ln -s ../../.agents/skills/campaign/agents/campaign-contributor.md .
ln -s ../../.agents/skills/campaign/agents/campaign-reviewer.md .
cd ../../..
home/.agents/skills/campaign/scripts/sync-agents.py      # regenerates home/.copilot and home/.codex agent files
git add -A home/.claude/agents home/.copilot/agents home/.codex/agents home/.agents/skills/campaign
git commit -m "Try the campaign skill at preflight-hardening"
./install.sh --dry-run && ./install.sh && ./install.sh --check
home/.agents/skills/campaign/scripts/sync-agents.py --check
command -v lease                                          # must print nothing
campaign --help; quiet --help
```

Push only `campaign-trial` if you push dotfiles at all. Roll back with `git switch main && git submodule update && ./install.sh` (the generated agent files are tracked, so switching restores them).

The agent types are now `campaign-contributor` and `campaign-reviewer`. They load when a session starts; until you restart, prepend the type's body (`agents/<type>.md` without its front matter) to a general-purpose agent's prompt. Then read the installed `SKILL.md`, `migration/checklist.md`, `migration/trial-runbook.md` and `references/harness.md`. Roles are owner, lead, contributor and reviewer; classes are now assignments. Records are terse: no writing-style skill for journals, reviews or notes.

Run the suite before relying on it: `cd ~/.agents/skills/campaign/tests && CAMPAIGN_TEST_TMP=~/.scratch/campaign-tests quiet light campaign-tests 20 "campaign suite" -- /usr/local/bin/python3 run.py`.

## Move Dense pages in place

Follow `migration/checklist.md`, step 3, the case "a pre-release campaign in `docs/campaign/`". It was rehearsed end to end on a copy of this layout. Here:

1. On `main` in `/Users/kronuz/code/Kronuz/Detent`: `git mv docs/campaign .campaign/detent`, set `"campaign": "detent"` in `.campaign/detent/state.json`, commit. Rename the registry entry: `mv ~/.scratch/campaign/campaigns/"Detent Dense pages" ~/.scratch/campaign/campaigns/detent` (without it, `status --all` and `job prune` do not see the campaign). The old build slot `slots/Detent Dense pages-dense-pages` stays for `job prune`; the first Dense build under the new name starts cold.
2. In `../Detent-wt/dense-pages` (`quest/dense-pages`): `git merge main`; then, in one commit, move every `docs/campaign/journal/*.md` to `.campaign/detent/journal/` and `git rm -r docs/campaign`.
3. In `../Detent-wt/dense-pages--performance` and `../Detent-wt/dense-pages--real-deltas`: `git -c merge.directoryRenames=true merge quest/dense-pages`, then the same move for anything left under `docs/campaign`.
4. Retag the old jobs, so status, usage and prune count them (their identity, and so their reuse, is untouched):
   ```sh
   /usr/local/bin/python3 -c 'import json,pathlib; h="/Users/kronuz/code/Kronuz/Detent"
   for f in pathlib.Path.home().glob(".scratch/campaign/jobs/*/spec.json"):
       d=json.loads(f.read_text())
       if d.get("campaign")==h: d["campaign"]=h+"/.campaign/detent"; f.write_text(json.dumps(d,indent=2,sort_keys=True)+"\n")'
   ```
5. `campaign check` must be clean. Compare `git for-each-ref refs/heads` with GitHub: nothing may be lost.

Do not run `campaign init` for Dense and do not create a second campaign. Do not prune jobs until the trial's first week is over.

## Reconcile Dense before launching anyone

Read `campaign status dense-pages`, `campaign brief`, the journals and the job records. A journal may say "waiting" though its jobs ended or were cancelled at the park. Record results before rerunning anything.

- Criteria: each criterion names its evidence. Untagged ones need a job that passed at the current code; `[artifact]`, `[decision]` and `[manual]` are the exceptions. The migrated journals keep evidence as prose, so give A1-A15 their Evidence rows from the job records. Rerun what ran at older code (`campaign job rerun`; merging `main` changed the code). Never weaken a criterion; tag one `[artifact]` or `[decision]` only when a job cannot show it. A14 is the landing reviews (the quest's lenses) plus the architect's ruling: make the ruling a `[decision]` criterion recorded in PLAN.md's Decisions.
- Reviews: the earlier tool's security acceptance is void, by design. `dense-pages-security.md` is CHANGES at `Detent:b7b37c6`: SEC-1 (warm-validator fuzz harness) and SEC-2 (one CPU-hour of decoder fuzzing) are open gaps. `dense-pages-correctness.md` is CHANGES at `Detent:c1323db`: COR-1 (defect) and PERF-1 (A15/T5 reader acceptance). Accept each only when every blocking finding has a job that passed at the fixed head: `campaign accept dense-pages LENS --confirm SEC-1=JOB --confirm SEC-2=JOB`. A fix touching files the reviewer never saw goes back to that reviewer for those files. `dense-pages-contracts.md` is history, not a lens. Design has no final acceptance yet.
- Keep Dense one feature with one final landing. Reader acceptance, the real 5M delta-write evidence (`real-deltas`) and the architect's ruling remain open. Do not declare it done because a component passes.
- Resolve the S6/Dense boundary explicitly. Current main lacks `libdetent_reader` and `benchmarks/reader_bench`, and S6 remains on `reader/s6`. `docs/design/reader.md` states that the existing Client misses the 1 ms gate in three of four prior arms. Keep the Dense threshold and the actual measurements, but do not call baseline misses Dense regressions. Choose and validate the integration order from your full plan; do not waive acceptance or make the Dense task implement S6.

## Move your other workstreams

For each remaining workstream in your full plan, follow `migration/checklist.md`, step 3. A live workstream becomes a quest that adopts its branch. Complementary branches of one feature become tasks of one quest. Parked work becomes planned quests marked blocked: `reader/s6`, `namespace/s3b`, the grouped syncs in `namespace/s1-s2` and `wip/dense-s2-parked`, the library adoptions. A 1.x review counts only for the exact code it examined; a conditional approval is history whose conditions become criteria. Use one campaign per home; check `campaign status --all` before shared-library or landing work. Read `docs/SHARED_LIBRARY_DECISIONS.md` before changing or pinning shared libraries.

## Continue engineering

Launch contributors in the background, by assignment and model tier, and resume them by message while their context is small. Run whole-feature reviews with fresh reviewers after the complementary pieces are done; each review names `Commit:` and a findings table. The architect gives the final landing ruling. Builds, tests and benchmarks are commit-pinned jobs. Builds use at most four parallel jobs; measurements that decide acceptance use `exclusive`. Only direct-path checks gate; soaks and other-platform matrices are deferred checks. Decoder fuzzing, sanitizers and durability tests stay gates. Lowering a risk or removing or changing a check needs `--weaken REASON`; tell me when you do.

Check `pace claude` before parallel launches and keep quota for integration and landing. Start with one contributor, and read `campaign usage` after its first turn. When only jobs remain, use one background `campaign wait --commits`. Collect the runbook's daily records and stop on its warning signs. Before the final landing, reconcile A1-A15, finish the correctness/performance and decoder-safety acceptances, obtain the architect's ruling, run the required CI and repeat accepted measurements on main. No cloud machines or billable resources.

## Claude Code support, without delaying the move

Inspect the installed tool first. Keep the skill working in Claude Code, Copilot CLI and Codex; harness details go in `references/harness.md` and the tool, with verified dates, never in `SKILL.md`.

1. Verify fresh-session agent discovery of `campaign-contributor` and `campaign-reviewer`, model and effort selection, background launch, resumption by message, result delivery, skill discovery and global guidance. Keep the cmux sidebar current with `report`.
2. Test, harmlessly and with a bound, whether a contributor's completion wakes an idle lead. If it does not, keep your turn while work is out and document the actual waiting mechanism.
3. Add a `claude_runs(since)` usage reader from real local transcripts: preserve launch prompts for `launch()`, tell lead from subagents, and report model, effort, input, cached, output and reasoning tokens, cost where recorded (never 0 for unknown), and time. Use scrubbed real event shapes as fixtures.
4. Inspect Claude Code's quota records before adding a `pace` reader. Read 5-hour, weekly and purchased-credit information only where recorded. Keep the status-line snapshot as fallback.
5. Run the full suite, `ruff check scripts tests` and `pyrefly check` before pushing tool fixes. Write measured migration costs and limits to `migration/claude-trial.md`.

Tool fixes go on a branch `claude-code` from `preflight-hardening`, pushed to Kronuz/agent-campaign; dotfiles changes stay on `campaign-trial`. Never push either repository's `main`. Use the local identity `Germán Méndez Bravo <german.mb@gmail.com>` and add no AI attribution. Detent's own landing follows its project instructions.

Report the quests, repository-prefixed commits, every `--weaken`, `--waive`, `--fix-scope` or `--no-ci` taken, and the decisions only I can make. That report is a checkpoint: continue until the campaign's authorized goals are complete or I ask you to park it.
