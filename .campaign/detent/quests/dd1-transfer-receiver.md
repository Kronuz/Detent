# dd1-transfer-receiver: Dense distribution DD1: transfer codec and verifying receiver

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status dd1-transfer-receiver`.

## Outcome

D133. Build docs/design/dense-distribution.md's DD1: `src/detent_local/paged_transfer.{hh,cc}` makes a transfer (D2, D3: the pages a receiver lacks, in seal order, derived fields zero) from a generation and a page list, and receives one per D4: it verifies every page against the one root digest and writes packs, building nothing. It calls dense-pages' page and canonicity validators. The model (dense-distribution-model.py) is the oracle and gains a `--out` writer.

## Acceptance criteria

- A1 The C++ receiver takes the model's 100K and 5M full-copy transfers and reproduces the root and every pack byte for byte, with the model's segment ids.
- A2 At 5M its page and pack hash counters equal 306,989,416 and 315,759,038 bytes (dense-pages' execution contract 2).
- A3 The four delta transfers of job 261109-030635-4e209f give their roots.
- A4 Each refusal in D4's table is a test: P1 (segment breaks the sender chose), P2 (children first, not seal order), the sealed forged-root cases for header keys (P4), header height, node count, a duplicate label and a tree packed against D5, each refused by its own check with every digest matching; and the damage, reuse and truncation rows. A mutant without each check accepts its case and fails the test (a `--fails-without` job per check, or one job naming each).
- A5 One CPU-hour of libFuzzer on tests/detent_testing/paged_transfer_fuzz.cc finds nothing, and ASan/UBSan and the suite pass.
- A6 [artifact] docs/mechanisms/paged-transfer.md states the transfer, the receiver's checks in order and what it refuses.

## Contracts

<What this quest provides or consumes (APIs, formats, protocols), stated precisely enough that a
quest depending on it can start before it lands.>

## Decisions

<Decisions made, and by whom; open questions, naming the owner or the lead.>

## May land early

<Internal code behind tests, unless the quest is a format, wire or public API. "None" when
everything waits for the review; `land --upto` refuses then.>
