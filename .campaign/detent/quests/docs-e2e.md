# docs-e2e: Docs E2E harness: pages run as tests, docs check, link check, CI

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status docs-e2e`.

## Outcome

The public site is Detent's end-to-end suite (D46, D52). `tests/docs/run_e2e.py` extracts every `detent`/`detentd`/`detent-agent` command, HTTP request and code sample from the pages under `website/src/content/docs/`, in sidebar order. It runs each one as written against a fresh build of `main`, with its own authority, relay and agent on a per-run port, and checks each against the hidden `<!-- e2e:begin --> ... <!-- e2e:end -->` assertion block that follows it on the page (values, not exit codes alone). `tests/docs/check_docs.py` checks both directions for CLI flags, configuration keys, HTTP endpoints, error/status names and knob types: everything a reference page names exists in the source, and everything in the source is documented. `tests/docs/check_links.py` fails on broken internal links in the built site. CI runs all three on every push that touches the site, the docs or the code they describe.

## Acceptance criteria

One line each: `- A1 <criterion>: <the job, test or check that shows it>`. An untagged criterion
needs a job that passed at the landed code; tag one `[artifact]` (a committed `path`),
`[decision]` (an id in PLAN.md's Decisions table) or `[manual]` (checked by hand) only when a job
cannot show it.

- A1 `run_e2e.py --fresh-build` runs every runnable block on current pages, and a page block without an assertion block is an error unless it is listed in `tests/docs/e2e_exceptions.txt` with a reason: the `e2e` check
- A2 A mutant job: changing one asserted value on a page, and one CLI flag in the source, each makes the `e2e` check fail; renaming one config key on a reference page makes the `docs` check fail
- A3 Overlapping runs do not interfere: each run takes its own port and kills only its own listener (`lsof -ti tcp:PORT -sTCP:LISTEN`), shown by two runs started together that both pass: a job
- A4 `check_docs.py` both directions over CLI flags, config keys, endpoints, error/status names and knob types, with current gaps listed in `tests/docs/docs_exceptions.txt`: the `docs` check
- A5 The site builds and `check_links.py` finds no broken internal link: the `site` check
- A6 [artifact] `.github/workflows/docs.yml` runs the three checks on push to main and to `quest/**` for site, docs, source and CLI paths (ci.yml ignores docs and website): `.github/workflows/docs.yml`
- A7 [artifact] `docs/dev/docs-e2e.md` explains the assertion block form and how to run one page: `docs/dev/docs-e2e.md`

## Contracts

Provides `docs-e2e`: the page form (a runnable fenced block followed by `<!-- e2e:begin -->` assertions `<!-- e2e:end -->`), the three commands with their exit codes, and the exceptions files. The area quests consume it and must shrink the exceptions lists for their pages.

## Decisions

D44, D46, D52 (owner ask relayed by xapiand-lead, msg-16 in Xapiand's numbering). The extractor follows Xapiand's docs_to_postman.py + harness shape, with Detent's own Python runner; the Xapiand lead offered theirs to compare. On landing, the lead tells xapiand-lead (D44: Xapiand evaluates one runner for both).

## May land early

The runner and the checks, once they pass on current pages with their exceptions recorded.
