# deps-workpool-repin: Repin logger on workpool, drop scheduler, rename threadpool to workpool

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status deps-workpool-repin`.

## Outcome

Detent builds against the renamed pool library (Kronuz/workpool, target `workpool::workpool`, WORKPOOL_* macros; renamed 2026-11-15, no aliases, head workpool:640c66b): its logger pin moves to a logger commit that links workpool (including logger:a07f3fe, deferred lines on wheel with per-thread queues, D86), the scheduler pin is removed because D72 retires scheduler, and every present-tense mention of threadpool in Detent's build, tools and docs says workpool. Dated history and `.campaign/` keep their wording.

## Acceptance criteria

- A1 `cmake/dependencies.json` pins workpool (URL github.com/Kronuz/workpool) and a logger commit that links `workpool::workpool`; scheduler is no longer pinned or linked, and `tools/check_pins.py` agrees: the pins check and a grep job
- A2 The native suite passes at the new pins, and the logger behaviour change (wheel-backed deferred lines) is covered: the suite, plus the logger-dependent tests named in the journal
- A3 No present-tense build file, tool or doc outside dated history names threadpool or the scheduler library: a grep job whose allow-list is dated history only
- A4 A logging-heavy benchmark (an existing one, named in the journal) shows no regression beyond its A/A band at the new logger: before/after jobs at named commits

## Contracts

Consumes workpool:640c66b and a logger commit on workpool. No Detent API change. If any Detent source still uses the scheduler library or the deleted ThreadPool family, message the lead with the files before changing them.

## Decisions

Lead, 2026-11-16, with the Xapiand lead (who found the pins block a rename alone): the rename lands with the repin, scheduler is dropped per D72. Tell the Xapiand lead when it lands so it can remove the ~/code/Kronuz/threadpool symlink after the lead updates state.json.

## May land early

Nothing: it changes pins.
