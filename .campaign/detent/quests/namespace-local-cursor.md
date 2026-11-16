# namespace-local-cursor: Local layer on the one W1 cursor, store layout 2 and root format 4 (P3)

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status namespace-local-cursor`.

## Outcome

The local layer runs on the one W1 cursor (`include/detent/cursor.hh`): `detent_local`, `read_view`, `detent_testing` and benchmarks no longer use the old 88-byte cursor; the agent store layout is 2 and the paged root is `DTROOT04` / format 4, and a store or root of the previous version is refused once with one status and the agent bootstraps fresh (D43). The coordinator maps the v1 cursor at its boundary, rebuilding the old identity from configuration, until namespace-s6-v1-removal moves consumers to v2. The flat-v1 image format is deleted with its `image_format` configuration value, readers and tests; paged is the only local format, and a flat image is refused like any old store (D95, design F12) (P3 of D94).

## Acceptance criteria

- A1 A store whose table carries layout 1, and a root with format 3, each synthesized from current bytes, are refused with one status and the agent bootstraps fresh (design F2): tests plus a mutant that accepts the old version, killed
- A2 `detent_local`, `read_view`, `detent_testing` and benchmarks are free of the old `Cursor` type, and flat-v1 (`FlatV1`, `flat-v1`, `DTIMAGE1`, `image_format`) is gone from code, configuration, docs and the website, with a `DTIMAGE1` file refused by the reader: a grep job, a refusal test and the suites
- A3 A power cut at every primitive of publication still recovers to the last acknowledged generation or the one before (T7) and derived names still hold (`test_derived_names`), at the new layout: the crash-model job and the suite
- A4 [artifact] `docs/LOCAL_IMAGE_FORMAT.md`: "DTROOT04"

## Contracts

Changes bytes on disk: agent store layout 2, root `DTROOT04` format 4, flat-v1 removed (`image_format` is no longer a configuration key). Provides the one cursor to namespace-s6-v1-removal.

## Decisions

D94 (lead, from the design review `reviews/namespace-s6-v1-removal-design.md`): P3 lands separately to keep the on-disk change out of the riskiest landing. D95 (owner, 2026-11-16): delete flat-v1, no backward compatibility.

## May land early

Nothing: it changes bytes on disk.
