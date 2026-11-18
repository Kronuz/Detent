# paged-decode-one-shape: Dense pages: every walker passes the page's nodes end; the Limits decoders go

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status paged-decode-one-shape`.

## Outcome

reader-s6 made `decode_node`, `decode_external` and `NodeAt::decode` take a page's nodes end, decoded once per page, and kept the `Limits` overloads as wrappers for the walkers outside its file set (contextual reuse, cursor, dependency walk, graph, page and warm validators, overlay assembler, packer, trie overlay) and their tests. After this quest every caller decodes the header once and passes the end; the `Limits` overloads are deleted. One way to decode a node.

## Acceptance criteria

Three to five, at outcome level: `- A1 <criterion>: <the test that shows it>`. An untagged one
needs a job tagged `--for A1` passing at the landed code, often the suite; tag one `[artifact]`
(a committed `path`), `[decision]` (a PLAN.md Decisions id) or `[manual]` (checked by hand) when no
job can show it.

- A1 `git grep` finds no caller of a `Limits` overload of decode_node or decode_external, and the overloads are gone: [artifact] `include/detent_local/paged_layout.hh`.
- A2 The suite passes, plus the codec fuzz smoke and CI: jobs.

## Contracts

None (internal).

## Decisions

Filed 2026-11-18 when reader-s6 kept the wrappers to stay in its file set; lands right after reader-s6.

## May land early

None.
