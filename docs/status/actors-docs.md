# Actors documentation

> Status: HISTORY (2026-11-08): superseded by `.campaign/detent/` (`campaign map`); kept for its results and reasoning. Was: (2026-11-03) Branch `docs/actors`; explains Detent's actors in the README, `docs/ACTORS.md` and the website.

**Done criteria:** the README carries a short "How Detent fits together" section with one diagram; `docs/ACTORS.md` states what, how, when and where for every actor; the website overview draws the relay and has a relay architecture page; the site builds; every fact is checked against the code.

## State

README, `docs/ACTORS.md` and the website pages are written; the site builds (68 pages, no warnings).

## Done

1. `Detent:26157e8`: `docs/diagrams/actors-overview.d2`, `actors-where.d2`, `actors-write-to-read.d2` and their PNGs.
2. `Detent:585ca9d`: README "How Detent fits together" (replaces the classic ASCII picture) and `docs/ACTORS.md`; README image-format default corrected to `paged-v2`.
3. Website: the overview diagram draws the relay and the classic path; new `architecture/relay.md` in the sidebar; authority voter count, delivery status, agent profiles and the distributor's legacy status corrected.

## Next

1. Owner review; the owner lands it. There is no docs index (`docs/README.md` or `docs/INDEX.md`) to link from.

## Blocked on

Code text and older docs that disagree with the code; the owner decides whether to change them:

1. `src/detentd/main.cc:52` usage calls `--authority` "opt-in durable loopback authority mode"; `src/detent_authority/service.cc:359-415` supports TLS and fixed 3, 5 or 7 voters on non-loopback addresses.
2. `docs/USAGE.md:510` says the authority has a "loopback transaction API" and that agents use the legacy profile "until the distribution component is integrated"; managed delivery is integrated (`src/detent_agent/main.cc:210-225`).
3. `docs/AUTHORITY_ARCHITECTURE.md:42` requires `FETCHCONTENT_SOURCE_DIR_CLUSTER` and `..._SHA256` for the authority build; `.github/workflows/ci.yml:22` builds it from the pinned `consensus` (`cmake/dependencies.json:189-192`) with no overrides.
4. `CMakeLists.txt:44,46` default `DETENT_BUILD_RELAY` and `DETENT_BUILD_AUTHORITY` to `OFF` while `docs/PRODUCTION_CONTRACT.md:3` names the authority, relays and agents as the production topology.
