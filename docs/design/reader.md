> Status: BRIEF (2026-11-03). Owner rulings for the reader design; the designer drafts the rest.

# Reader

Applications read knobs the way the `detent` CLI does: map the agent's selected generation, look up the knob, evaluate it. One reader core with a C ABI serves every language; C++, Rust and Python wrappers are thin. The legacy inventory (`docs/status/legacy-removal-inventory.md`, branch `explore/legacy-inventory`) maps what it replaces.

## Decisions settled by the owner

**R1. Defaults per call.** Every lookup takes the caller's default and returns it when the knob is absent. There is no defaults file and no defaults merged under the generation. Ruled 2026-11-03.

**R2. No overrides in the reader.** The reader returns what the authority published. Tests get their values by writing a real generation through a test helper, not through an override path in the production reader. Ruled 2026-11-03.

**R3. Prepared attachment only.** The reader attaches to agent-prepared generations. Full validation is an agent-side check, not a reader mode. (Lead's recommendation; the owner may revise.)

**R4. Explicit handles.** One reader handle per process, owned by the application; no global free functions. (Lead's recommendation; the owner may revise.)

## Open for the designer

1. The C ABI: open, lookup and evaluate (bool with hash and switch, integer), generation switching through one cheap check with no thread, close, errors.
2. Evaluation in one place: the three copies today collapse into the core over `ReadView::try_eval` (`src/detent_local/view.cc:373`).
3. No shared lock on the read path: today every lookup takes one `std::mutex` per page shared by all readers (`src/detent_local/lazy_page_access.cc:133`).
4. The C++, Rust and Python wrappers, and the CLI as one more user of the core.
5. Gate rows: per-lookup cost in each language (warm, p50/p99, zero allocations), first-read time on a fresh process, generation switch under concurrent reads.
6. How the per-namespace generations of `namespace-delivery.md` map into the reader's open and lookup.
