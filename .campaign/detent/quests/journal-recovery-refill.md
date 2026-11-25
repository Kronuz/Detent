# journal-recovery-refill: a crash never wedges an owned store: recovery refills replacement before it can fund a checkpoint

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status journal-recovery-refill`.

## Outcome

An owned store that could fund a checkpoint before a crash can fund one after it. Today journal
Admission's refill order (control before replacement, admission.h `refill` and the ctor at
journal:9e11713) moves the replacement pool's bytes into control when the census rebuilds the ledger,
so an owned store whose Control appends drained more than R minus the checkpoint peak reopens with a
replacement pool too short for any checkpoint; Normal is refused and nothing refills: wedged for good
(review F2 of consensus-recovered-over-limit-start, probe 261124-202348-6f464f).

## Acceptance criteria

- A1 [manual] First a table in the journal: each pool's share before a crash and after recovery, by
  how the store reached its limit (Control drain, a checkpoint mid-publication, an install), and the
  rule chosen (keep pool ledgers across a crash, refill replacement to its pre-crash share, or
  another), with why. If the only correct rule refuses a store that opens today, stop and ask the owner.
- A2 The probe's two cases as a deterministic test asserting by value that Replacement(R) is admitted
  after recovery and a checkpoint completes, with a `--fails-without` at main where it is refused.
- A3 [artifact] The rule stated in journal's admission mechanism doc.
- A4 Suite and CI green; consensus's deadlines tests pass against the fix (a pinned integration job).

## Contracts

Admission's public API is unchanged; only the post-recovery pool split changes.

## Decisions

The lead picks the rule from A1; a rule that narrows what opens goes to the owner.

## May land early

None.

## Tasks

One contributor.
