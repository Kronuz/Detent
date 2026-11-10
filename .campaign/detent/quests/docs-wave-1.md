# docs-wave-1: Docs wave 1: the public site and guides refreshed in full, plus a docs check in CI

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status docs-wave-1`.

## Outcome

Every page of the public site (`website/src/content/docs/**`: welcome, basics, installation, quick start, architecture, guides, operations, reference, FAQ, roadmap) and the top-level guides (README.md, QUICKSTART.md, docs/*.md) describe Detent as it is on `main` after N1 and Dense pages: the Reader API (no `detent::Client`, no defaults file), Dense v3 stores, namespace delivery, `detentd` readiness, configuration and limits. Prose is present tense and factual (the docs-cleanup shape), with no history and no backward-compatibility notes (D43). A docs check, run in CI, keeps the reference pages and the source in step between waves (D44).

## Acceptance criteria

One line each: `- A1 <criterion>: <the job, test or check that shows it>`. An untagged criterion
needs a job that passed at the landed code; tag one `[artifact]` (a committed `path`),
`[decision]` (an id in PLAN.md's Decisions table) or `[manual]` (checked by hand) only when a job
cannot show it.

- A1 [artifact] An audit table in the journal: every page, what it claims, the source or job that confirms it, and what changed
- A2 `tests/docs/check_docs.py` checks both directions for CLI flags (`detent`, `detentd`, `detent-agent`), configuration keys, HTTP endpoints, error/status names and knob types: every one a reference page names exists in the source, and every one in the source is documented: the `docs` gate
- A3 The site is the end-to-end suite (D46): every `detent`/`detentd`/`detent-agent` command, HTTP request and code sample on the quick start, guides and reference pages runs as written against a fresh build of `main`, each followed on the page by a hidden `<!-- e2e:begin --> ... e2e:end -->` assertion block that checks its output (values, not exit codes alone); the docs check walks pages in sidebar order and runs them; the journal reports pages with tests over all pages, driving to every page with a runnable example: the `docs` gate
- A4 A mutant check: renaming one CLI flag in the source and one config key on a reference page each makes the docs check fail: a job
- A5 The site builds with no broken internal links: the `site` gate
- A6 [artifact] CI runs the docs check on every push: `.github/workflows/ci.yml`
- A7 [manual] The lead reads the rendered site end to end (architecture, features, quick start, usage, configuration) and records what was checked

## Contracts

Provides the docs check later waves and quests reuse. Consumes the landed surface of N1 and Dense pages; changes no code outside tests/docs and CI.

## Decisions

D44 (owner ask, lead schedule), D43 (no compatibility notes). On landing, the lead tells xapiand-lead: Xapiand evaluates replacing docs_to_postman.py + newman with this runner (one runner for both, incl. xapiand-py samples). Pages are published by pages.yml on push to main.

## May land early

The docs check and its CI step, before the page rewrites, once it passes on current pages with recorded exceptions.
