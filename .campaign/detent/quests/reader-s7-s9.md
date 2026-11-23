# reader-s7-s9: Reader S7-S9: `detent get` on the core, Python and Rust over the C ABI

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status reader-s7-s9`. Planned from `docs/design/reader.md` "First steps" S7, S8 and S9;
the criteria are the plan's own, not new ones.

## Outcome

`detent get` reads through the reader core, and the Python and Rust bindings are thin layers over the C ABI.

## Acceptance criteria

- A1 S7: D8, `detent get` on the core, with T5 (needs S6 and Q5): the `native` gate
- A2 S8: a Python extension over the C ABI replaces pybind11 (needs S6 and Q2): the binding suite
- A3 S9: a Rust crate over the C ABI replaces cxx (needs S6 and Q2): the binding suite

## Contracts

To be fixed when the lead splits this quest.

## Decisions

1. Needs `reader-s6`. D21: `detent get` keeps `GenerationReader` and loses `--defaults` until S7.
2. `legacy-h` (client-local-source B1-B6) also moves the bindings onto `Reader`: the lead reconciles S8 and S9 with it before starting.

## May land early

None until the lead splits it.
