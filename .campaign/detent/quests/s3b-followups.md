# s3b-followups: Pack repair follow-ups N1-N5

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status s3b-followups`.

## Outcome

The non-blocking findings of `.campaign/detent/reviews/namespace-s3b-durability.md` are resolved:
resource exhaustion is not mistaken for damage, a missing pack in a leased root does not fail the
whole collection, and the docs say what the scrub withdraws.

## Acceptance criteria

- A1 N1: EMFILE, ENFILE and ENOMEM on a pack open or read are transient (retried later, no damage
  report, no repair): a test that fails without it (`--fails-without` on a revert).
- A2 N3: a pack missing from a leased root is skipped and retried with backoff; the rest of the
  collection proceeds: a test that fails without it.
- A3 N4, N5: the scrub resumes rather than restarts on a new root when its packs are unchanged, and
  an obsolete report waits for the tick; or the journal shows, with a job, that neither costs
  anything measurable and they are dropped.
- A4 [artifact] N2: `docs/mechanisms/trusted-restart.md` "Limits" says a reader process keeps its
  own verified bits.

## Contracts

None changed.

## Decisions

- Lead, 2026-11-11: follow-ups from the namespace-s3b durability review (N1-N5), landed after
  namespace-s4, which shares `coordinator.cc`.

## May land early

Each of A1-A4 alone.
