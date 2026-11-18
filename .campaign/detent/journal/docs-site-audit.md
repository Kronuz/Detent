# docs-site-audit: journal

State: ready for review
Quest: .campaign/detent/quests/docs-site-audit.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State, the first step under Next, and the Evidence table. State is one of: working,
waiting on jobs <ids>, ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Done: `docs/dev/docs-site-audit.md` is committed. Nothing else is in flight.

## Next

1. The lead reads the audit, answers "## Code questions the audit raised" (items 1-5), and orders the area quests against `legacy-h`.

## Blocked on

Nothing.

## Evidence

One row per criterion, with what its kind needs: a job id (it, or the same job rerun, passed at
the current code), a `path`, a decision id, or for a [manual] one who checked what. Name every job
you examined, failures included, and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | [artifact] `docs/dev/docs-site-audit.md`: "## Pages" (every page, README.md, QUICKSTART.md and docs/*.md has a verdict and file:line claims; the table is "## Page table") |
| A2 | [artifact] `docs/dev/docs-site-audit.md`: "## Rewrite scope" (five areas: architecture; features and quick start; usage guides and operations; configuration and limits; reference and the rest) |
| A3 | [artifact] `docs/dev/docs-site-audit.md`: "## Unverified" (each claim with its probe) |
| A4 | [artifact] `docs/dev/docs-site-audit.md`: "## Owned by other quests" (legacy-h, reader-s6, reader-s7-s9, legacy-i, dense-pages, namespace-s3b, namespace-s4-s10, async-drivers, daemon-ready-leader, observability, rc3-rc5) |
| A5 | [artifact] `docs/dev/docs-site-audit.md`: "## Inputs" (glossary #24, no at-rest encryption #25, DNS voter addresses #26 with the IP-only finding) |

## Result

Added `docs/dev/docs-site-audit.md`; no page changed, no job run (reading and writing only).
Totals: 62 site pages: current 11, stale 7, wrong 21, missing content 22, delete 1; `README.md` wrong; `QUICKSTART.md` current; 76 `docs/*.md`: current 39, stale 30, wrong 4, missing content 2, delete 1.
Open: (1) `detent::Client`, `InitOptions`, `detent::global` and the C++ `--defaults` file still exist on `main` (`include/detent/client.hh:59,121`): `legacy-h` and `reader-s6` are blocked, so those pages are verdicted by `main` and tagged `OWNED legacy-h` (51 rows), not "wrong because removed"; (2) D47 #26: voter addresses are IP-only (`src/detent_authority/service.cc:381`); (3) `--upgrade-managed` is a D43 conflict no quest owns; (4) `welcome.md` is in no area quest's list.
Method: seven read-only auditors each read one slice and wrote a fragment; the lead merged them and re-checked the headline cites (`service.cc:381`, `coordinator.hh:30`, `main.cc:116,235,298`, `ci.yml:5`, `kDefaultPort`, `client.hh:59,121`). The other `file:line` cites are the auditors', not independently re-read; the area quests verify as they rewrite.

## Rulings I made

1. A page is verdicted by what is true on `main` today; text about code that a non-landed quest removes is tagged `OWNED <quest>` (A4), not called wrong.
2. Area placement: the quest files name `guides*`/`operations/*` (usage), `reference/configuration.md` and `limits.md` (config) and "the rest of reference/**" (area 5); `welcome.md`, `README.md` and `QUICKSTART.md` are placed in area 2.
3. `docs/*.md` are triage rows (internal records are `current` when labelled as dated records); the user guides among them got a full claim check.
