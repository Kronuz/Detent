# drop-justknobs: journal

State: ready for review
Quest: .campaign/detent/quests/drop-justknobs.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Third pass at 4f6502db: observability.md's exporter hint now names the `Metrics` counters (no `jk_` names, no spec), `\bjk_` added to the check (text files only), audit rows reworded. Jobs: A1 pass 261111-110505-cac549; A1 mutant 261111-110502-60f894 (commit ccca4804 = a `jk_` name restored in observability.md, ref refs/mutants/drop-justknobs-a1, delete after); A3 site 261111-110509-574969; A3 suite 261111-110507-004d6c. Earlier jobs are cancelled (superseded).

## Next

1. `campaign wait --quest drop-justknobs`, paste `campaign brief` Evidence table, fill Result, State "ready for review", commit, push.
2. Check CI at the pushed head.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | pending jobs |
| A2 | `docs/design/knob-model.md`, `README.md`, `docs/USAGE.md`, `website/src/content/docs/faq.md`, `website/src/content/docs/welcome.md`, `website/src/content/docs/architecture/design-documents.md` (the model spec, its links, and the removed lineage pages; the old spec file is deleted) |
| A3 | pending jobs |

## Result

Pending the jobs.

## Rulings I made

- Check scope: `justknobs`/`just knobs` (any case), Meta as "Meta's", "Meta Platforms" (any case) or the capitalized word (word boundaries, so storage.meta, metadata and import.meta pass), PNG text chunks; D2 sources and SVGs are scanned as text. Tracked plus untracked-unignored files, excluding `.campaign/` and the script itself. A rendered PNG's pixels cannot be grepped; every PNG in the repo has a D2 source whose text is scanned.
- Diagrams: only `diagrams/01_overview` and `08_detent_deps` named the lineage (a `just_knobs.json` label and a tooltip). Both have an SVG, no PNG; re-rendered with `d2` using each source's own config. The other PNGs (10-16, docs/diagrams) have sources and no mention.
- Renamed the flag to `--knob` (error strings `duplicate --knob override for: ...`, `malformed --knob value ...`) and the example file to `defaults.json`; the w5 identity labels `just-knob` became `knob-flag` (same width) in tools/w5 and the recorded identity-w5_detent.txt. Docs-site-audit rows that quoted the old names were reworded.
- Spec citations: replaced by the rule stated in place, or by docs/design/knob-model.md; dropped where the citation added nothing.
- check_lineage.sh excludes CHANGELOG.md (history, held by namespace-s4).
- Replaced the dangling "spec" citations in docs/design/reader.md and observability.md with knob-model.md or the code (`include/detent/client.hh` Metrics), and reworded DOCUMENTATION_FINDINGS section 4 as "Designed features not implemented".
- Removed the "Section N" lineage comments in model.hh and hashing.hh (in the set); the rest are listed above.
- The docs-site audit's rows that describe the real `--just-knob`/`just_knobs.json` code stay as they are: they record what the code prints today.
- knob-model.md describes today's two types (bool, int); one-knob-type rewrites it (D65).
