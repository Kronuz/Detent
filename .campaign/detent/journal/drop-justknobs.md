# drop-justknobs: journal

State: waiting on jobs 261111-105343-9301ba 261111-105327-78d555 261111-105330-44be2b 261111-105331-6b15c0
Quest: .campaign/detent/quests/drop-justknobs.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Committed 1658291f. Jobs queued: A1 pass (261111-105343-9301ba), A1 mutant (261111-105327-78d555, commit 3d31990421bd = head plus the FAQ entry restored, ref refs/mutants/drop-justknobs-a1; delete the ref when done), A3 site build + link check (261111-105330-44be2b), A3 suite (261111-105331-6b15c0).

## Next

1. `campaign wait --quest drop-justknobs`, paste `campaign brief` Evidence table below, fill Result, State "ready for review".
2. Branch pushed (origin/quest/detent/drop-justknobs) for CI; check its result.

## Blocked on

Nothing. For the lead (outside the file set, not edited; none matches `justknobs|just knobs`, so the check passes, but they are the same lineage under other spellings):

- `--just-knob` flag name in error strings and a doc comment: src/detent/overrides.cc:18,25,52; include/detent/overrides.hh:25; tests/detent/test_duplicate_precedence.cc:191,197 (asserts the string); tools/w5/w5_detent.cc:2,125,145,147; tools/w5/w5_stox.cc:1; CHANGELOG.md:237; website reference/cpp-api/global-and-testing.md:102 and roadmap.md:69 (`--just-knobs-config-path`); docs/status/m1-pins.md:94; docs/perf/m1-w5-evidence.md:51 and its identity-w5_detent.txt (recorded tool output); benchmarks/container-alternative/*.patch context (patches already stale: they target btree::map).
- `just_knobs.json` example file name: include/detent/json_schema.hh:22; docs/ACTORS.md:129; website basics.md:62 (also says `defaults.json`, which the agent reads), guides/live-updates.md:44, guides/testing.md:91, guides/python-and-rust.md:31,63, guides/kill-switches.md:23, guides/cpp-application.md:32,55,185, reference/bindings/rust.md:35. README and diagrams/01_overview.d2 now say `defaults.json`, so README and the site disagree until these change.
- 18 files under include/src/tests cite "Section N" of the deleted spec in comments.
- To catch these spellings, widen `tools/check_lineage.sh` to `just[ _-]?knob` once those files are in a quest.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | pending jobs |
| A2 | [artifact] `docs/design/knob-model.md` (new, from model.hh, evaluation.hh, hashing.cc, json_schema.cc/json_patch.cc, client.cc and their tests); README line 3 and `docs/USAGE.md` "Where to go next" link it; website FAQ entry, copyright note, welcome hint removed (welcome now ends its "what is Detent" section with a link to the model doc); design-documents row replaced by a knob-model.md row; `justknobs-implementation-spec.md` deleted |
| A3 | pending jobs |

## Result

Pending the jobs.

## Rulings I made

- Check scope: `justknobs`/`just knobs` (any case), Meta as "Meta's", "Meta Platforms" (any case) or the capitalized word (word boundaries, so storage.meta, metadata and import.meta pass), PNG text chunks; D2 sources and SVGs are scanned as text. Tracked plus untracked-unignored files, excluding `.campaign/` and the script itself. A rendered PNG's pixels cannot be grepped; every PNG in the repo has a D2 source whose text is scanned.
- Diagrams: only `diagrams/01_overview` and `08_detent_deps` named the lineage (a `just_knobs.json` label and a tooltip). Both have an SVG, no PNG; re-rendered with `d2` using each source's own config. The other PNGs (10-16, docs/diagrams) have sources and no mention.
- README `just_knobs.json` became `defaults.json` (README is in the set); see the lead's list above.
- Replaced the dangling "spec" citations in docs/design/reader.md and observability.md with knob-model.md or the code (`include/detent/client.hh` Metrics), and reworded DOCUMENTATION_FINDINGS section 4 as "Designed features not implemented".
- Removed the "Section N" lineage comments in model.hh and hashing.hh (in the set); the rest are listed above.
- The docs-site audit's rows that describe the real `--just-knob`/`just_knobs.json` code stay as they are: they record what the code prints today.
- knob-model.md describes today's two types (bool, int); one-knob-type rewrites it (D65).
