# consensus-follower-read: A follower asks its leader to confirm a read index (ReadIndex forwarding)

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status consensus-follower-read`.

## Outcome

Detent's voter-read-freshness (D144; design docs/design/voter-read-freshness.md on its branch, reviewed CHANGES V1-V9 in .campaign/detent/reviews/voter-read-freshness-design.md) needs a follower half of consensus `Read`: today `Read` rejects at a follower (core.h:985) and `read_probe` is the leader's alone. After this quest a follower sends a read request to its leader; the leader confirms leadership with its quorum probe and answers with a commit index; the follower completes the read once it has applied that index. A refused read (no leader, a term change, a leader that steps down) completes with a refusal the host can classify. Each forwarded read has an identity unique across the follower's restarts, so a restarted follower can never match an answer meant for a read before its restart (review V1).

## Acceptance criteria

- A1 A follower's read completes only at an index the leader confirmed with a quorum in its term, after the follower applied it; leader change, term change, stale leader and follower restart mid-read each refuse or retry, never answer stale: deterministic tests in test/follower_read_test.cc, each with a mutant that fails it (`--fails-without`)
- A2 A restarted follower in the same term never matches a pre-restart answer (V1): a test plus a mutant that reuses the per-Core token
- A3 A read in flight never answers requests that arrived during it, and the follower's wait to apply has a deadline (V4, V5): tests
- A4 The new frames are in Message with codec kinds, the decoder fuzz corpus has seeds, and a 30 CPU-minute ASan+UBSan decoder fuzz job runs clean: jobs
- A5 [artifact] docs/mechanisms/follower-read.md states the rule; the suite, TSan and CI pass

## Contracts

<What this quest provides or consumes (APIs, formats, protocols), stated precisely enough that a
quest depending on it can start before it lands.>

## Decisions

<Decisions made, and by whom; open questions, naming the owner or the lead.>

## May land early

<Internal code behind tests, unless the quest is a format, wire or public API. "None" when
everything waits for the review; `land --upto` refuses then.>

## Tasks

<The steps that build the feature, one agent each: `T1 <what> (files; assignment, tier, effort;
after T0)`. Each merges into the quest branch on its own tests.>
