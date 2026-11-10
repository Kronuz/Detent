# docs-site-audit: Docs site audit: every page's claims against main

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status docs-site-audit`.

## Outcome

`docs/dev/docs-site-audit.md` lists, page by page, what the public site (`website/src/content/docs/**`, 62 pages) and the top-level guides (README.md, QUICKSTART.md, docs/*.md) say that is no longer true of Detent on `main`, what they lack, and what the rewrite should be, so that the area quests start with a fixed scope. It changes no page (D52).

## Acceptance criteria

One line each: `- A1 <criterion>: <the job, test or check that shows it>`. An untagged criterion
needs a job that passed at the landed code; tag one `[artifact]` (a committed `path`),
`[decision]` (an id in PLAN.md's Decisions table) or `[manual]` (checked by hand) only when a job
cannot show it.

- A1 [artifact] Every page has a row: path, verdict (current, stale, wrong, missing content, delete), and each stale or wrong claim with the code or doc it contradicts (`file:line`): `docs/dev/docs-site-audit.md`: "## Pages"
- A2 [artifact] A section per area (architecture; features and quick start; usage guides and operations; configuration and limits; reference and the rest), naming its pages, what the code serves that no page documents, and the source of truth to write from: `docs/dev/docs-site-audit.md`: "## Rewrite scope"
- A3 [artifact] Every user-visible claim the audit could not verify from code, each with the probe that would settle it: `docs/dev/docs-site-audit.md`: "## Unverified"
- A4 [artifact] The pages that open quests change (dense-pages, reader-s6, legacy-h, namespace-s3b, async-drivers, observability OB*), so the area quests re-check those rows after they land: `docs/dev/docs-site-audit.md`: "## Owned by other quests"
- A5 [artifact] The inputs: Xapiand triage #24, a glossary page (authority, voter, leader, relay, agent, namespace, generation, store, page, knob, rollout); D47 #25, disk or volume encryption, with no at-rest encryption in Detent, on the security page; D47 #26, DNS voter addresses in the configuration reference: placed in the rewrite scope: `docs/dev/docs-site-audit.md`: "## Inputs"

## Contracts

Provides the audit table that the five area quests take their scope from.

## Decisions

D43 (no compatibility notes), D44, D52.

## May land early

None: one commit.
