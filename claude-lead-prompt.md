You are the Claude Code lead resuming your existing campaign and its full scope. Recover your own master plan, responsibilities and workstreams, including any other repositories you already lead. Keep that scope. Dense pages is one small delegated task returning to you; this prompt does not replace your plan with the Dense pages campaign.

Upgrade your campaign workflow to Campaign 2.0 from github.com/Kronuz/agent-campaign, then continue all of your authorized work. Work without stopping for confirmation unless a step is irreversible or only I can decide it.

First recover your latest master plan and ledgers. Then read `docs/handoffs/dense-pages-2026-11-06.md` as the return handoff for the Dense pages assignment. It contains that task's branches, evidence and remaining jobs. `docs/handoffs/detent-codex-2026-11.md` is another historical source for the Detent part of your work. Neither handoff defines the limits of your lead role.

## Take over without duplicating work

1. Park anything your old Campaign 1.x session still owns. Let its agents finish their current step and commit their ledgers. Inventory their branches and `quiet list`. Preserve the old ledgers and reviews.
2. Dense pages already has a Campaign 2.0 home in `/Users/kronuz/code/Kronuz/Detent`, quest `dense-pages`, branch `quest/dense-pages`, worktree `../Detent-wt/dense-pages`. Reuse its state, journals and accepted reviews. Do not reinitialize or recreate it.
3. Reconcile `campaign status`, `campaign check`, the journals and job records before launching agents. A journal may say waiting even though its jobs ended during the pause. Record results before rerunning anything.
   At the final park, all agents are stopped and all remaining jobs are cancelled. Partial measurement output is retained. Resubmit only the work your resumed plan needs.
4. Keep Dense pages as one feature with one final landing. Its implementation is composed; reader acceptance, the real 5M delta-write evidence and the architect's landing ruling remain open. Do not declare it done because a component's tests pass.
5. Migrate every remaining workstream in your existing full plan to the appropriate Campaign 2.0 home and quests. Reuse the Dense home for its existing quest; use the campaign registry to keep other repository campaigns distinct. Keep each feature's ownership separate. The Dense implementers do not inherit every unrelated pending item.

## Install or update Campaign 2.0 safely

The machine already has 2.0 installed. At the checkpoint, dotfiles is clean on `main`, and its campaign submodule is clean on `main`. Recheck those facts; do not overwrite later local work.

```sh
cd ~/.local/share/dotfiles
 git status --short
 git fetch origin
 git switch main
 git merge --ff-only origin/main
 git submodule update --init
 ./install.sh --dry-run
 ./install.sh
 ./install.sh --check
 campaign --help
 quiet --help
 pace claude
```

Use `git submodule update` only when nobody is developing in the submodule. It can detach their branch. Read the installed `SKILL.md`, `references/migration.md` and `references/harness.md` after updating. `quiet` and `pace` are Python tools; `lease` is retired. Remove an obsolete `~/.local/bin/lease` link if present.

If the installer refuses a destination, inspect it. `--force` backs it up under `~/.local/state/dotfiles/backups/`. You may force only the campaign skill, campaign agent types and the `campaign`, `quiet` and `pace` links. Ask before forcing other files.

Agent types load when a session starts. Until you restart, prepend the installed `~/.claude/agents/adventurer.md` or `paladin.md` body to a general-purpose agent's prompt.

## Prove and adopt

1. Run the installed campaign suite under `quiet light`, with `CAMPAIGN_TEST_TMP=~/.scratch/campaign-tests`. Use the newest managed Python, `/usr/local/bin/python3` on this machine. Run Ruff and Pyrefly from the skill directory when changing Python.
2. For each remaining 1.x workstream, follow “Resume a 1.x project” in `references/migration.md`: create a quest, adopt the existing branch, copy acceptance criteria and ledger state, and carry only reviews that actually passed at the relevant content. Multiple complementary branches for one feature become tasks.
3. Dense pages is already adopted. Its security acceptance is recorded by `campaign`; correctness is CHANGES pending quantitative evidence, and design has no final acceptance. Reuse the actual review files and confirming jobs.
4. Keep `campaign check` clean. Use only the tool to edit `docs/campaign/state.json`. The campaign registry and repository landing lock coordinate other campaigns on this machine; check `campaign status --all` before shared-library or landing work.
   The installed version warns that this campaign still stores metadata in `docs/campaign` and offers `campaign migrate` when no quest is in flight. Preserve this working legacy location while Dense is active; follow the installed migration instructions when that condition is satisfied. Do not create a duplicate campaign to remove the warning.

## Continue engineering

Run implementation with adventurers and whole-feature reviews with fresh paladins. Finish complementary implementation pieces before reviewing the feature. Confirm fixes with the reviewer's probes and `campaign accept --jobs`; do not build a review loop around every small edit. The architect gives the final landing ruling.

Builds, tests and benchmarks are commit-pinned `campaign` jobs. Builds use at most four jobs. Measurements that decide acceptance use `exclusive` holds. Only direct-path checks gate the next step; extended soaks and other-platform matrices use deferred checks. Required decoder fuzzing, sanitizer evidence and durability tests remain gates.

Check `pace claude` before parallel launches. Preserve quota for integration and landing. While agents are out, keep your turn until their results are handled unless you have verified that completion wakes Claude Code. When only jobs remain, use one background `campaign wait --commits`; consume its events and wait again. It reports new commits and ended jobs once. A new installation starts its event cursor from now, so run `campaign status` first. Inspect remote holds with `etctl run SESSION 'quiet check; quiet list'`.

Resolve the S6/Dense boundary explicitly. Current main lacks `libdetent_reader` and `benchmarks/reader_bench`; S6 remains on its separate branch. `docs/design/reader.md` states that the existing Client misses the 1 ms gate in three of four prior arms. Preserve the Dense threshold and actual measurements, but do not claim that baseline misses are introduced Dense regressions. Use your full plan to choose and validate the integration order. Do not silently waive acceptance or make the Dense task implement all of S6.

Before final landing, reconcile all A1–A15 criteria against executable evidence, finish correctness/performance and decoder-safety acceptance, obtain the architect's ruling, run the required CI and repeat accepted measurements on main. Read `docs/SHARED_LIBRARY_DECISIONS.md` before changing or pinning shared libraries. No cloud machines or billable resources.

## Check Claude Code support without delaying adoption

Inspect the installed tool first; other sessions may already have implemented these items. Keep the skill working in Claude Code, Copilot CLI and Codex. Harness details belong in `references/harness.md` and tool code, with verified dates. Add no harness-specific rules to `SKILL.md`.

1. Verify fresh-session agent discovery, model/effort selection, background launch, resumption, result delivery, skill discovery and global guidance. Keep the cmux sidebar current with `report`.
2. Test whether an adventurer's completion wakes an idle lead in a bounded, harmless trial. If it does not, keep the lead's turn while work is out and document the actual waiting mechanism.
3. If absent, add a `claude_runs(since)` usage reader from this version's real local transcripts. Preserve launch prompts unchanged for `launch()`, distinguish lead/subagents, and report model, effort, cumulative tokens and time. Use scrubbed real event shapes as fixtures.
4. Inspect this version's quota records before adding a `pace` reader. Read 5-hour, weekly and purchased-credit information only where recorded. Keep the existing status-line snapshot as fallback; document unavailable fields honestly.
5. Keep the instruction word budget green. Run the full suite, `ruff check scripts tests` and `pyrefly check` before pushing tool fixes. Write a short `docs/claude-trial.md` with measured migration costs and limitations.

Tool fixes go on a branch from agent-campaign main, `claude-code`, pushed to Kronuz/agent-campaign. Dotfiles fixes go on `campaign-2-claude`. Never push those repositories' main branches. Set local identity to `Germán Méndez Bravo <german.mb@gmail.com>` and add no AI attribution. Detent's own landing follows its project instructions; its GitHub remote is `github`, while `origin` is a stale local path.

Report the quests, repository-prefixed commits and owner-only decisions. That report is a checkpoint: continue until the campaign's authorized goals are complete or I ask you to park it.
