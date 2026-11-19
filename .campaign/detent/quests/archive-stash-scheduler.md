# archive-stash-scheduler: archive the stash and scheduler repositories

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status archive-stash-scheduler`.

## Outcome

D73 has the lead archive stash and scheduler once nothing uses them (`docs/dev/superseded-libraries.md`; superseded-sweep quest:13). Both are still unarchived; Detent, logger, prism and http-log no longer use them, but Xapiand pins scheduler (`Xapiand/CMakeLists.txt:546`). `superseded-libraries.md:38,47,90` also still lists Detent's pins and cluster as unarchived (audit D4). After this quest both repositories are archived on GitHub, the cpp-libraries README marks them archived, and the doc is current.

## Acceptance criteria

- A1 [manual] `gh repo view Kronuz/stash` and `Kronuz/scheduler` show archived.
- A2 [artifact] Pins, archive state and cluster are current: `docs/dev/superseded-libraries.md`.

## Notes

Filed 2026-11-19 from the untracked-commitment audit (U4, D4).
Archiving is an owner-visible external action: ask the owner before running it; blocked on Xapiand dropping its scheduler pin.
Order after xapiand's server-composition adoption quest (`--after xapiand:ID` once filed); the pin goes away there.
