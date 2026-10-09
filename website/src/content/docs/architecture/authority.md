---
title: "Durable Authority"
---

The authority is Detent's answer to the classic distributor's limits at scale:
whole-map copies on every write, publisher-chosen versions, volatile consensus
and an unauthenticated network surface. It's a separate, opt-in profile; the
classic distributor remains available and unchanged.

---

## Layers

```d2 alt="Publishers reach the authority service over HTTPS; the service admits bounded requests and groups them into Raft entries for the durable consensus Node, which applies them to the immutable State and persists through completion-based storage; voters replicate over mutually authenticated TLS"
direction: down
pub: "Publishers\n(HTTPS, bearer tokens)" {shape: person}
service: "Service\n(admission, sessions,\ngrouping, deferred replies)"
node: "Node\n(strict durable Raft)"
state: "State\n(immutable AVL roots,\nreceipts, revisions)" {shape: cylinder}
storage: "Worker storage\n(journal + checkpoints,\ncompletion-based I/O)" {shape: cylinder}
peers: "Other voters\n(mutual TLS + pins)"
pub -> service
service -> node: "grouped commands\n(≤ 64 producers, ≤ 5 ms)"
node -> state: "apply committed\nentries in quanta"
node -> storage: "durable before ack"
node <-> peers: "control + bulk lanes"
```

---

## The State Machine

`State` holds knob definitions in **immutable balanced trees**. A mutation
copies only the affected tree path and shares everything else, so applying a
one-knob change to 500,000 knobs allocates about 27 tree nodes instead of
copying the whole map:

| Knobs | Legacy patch | Authority update | CPU per update |
| ---: | ---: | ---: | ---: |
| 100,000 | 133.7 ms | 0.0127 ms | 0.0141 ms |
| 500,000 | 668.6 ms | 0.0113 ms | 0.0114 ms |

Application-only measurements, without I/O, HTTP or replication; 21 legacy and
1,000 authority samples on an Intel Mac, October 2026.

1. **Transactions are atomic per namespace.** Preconditions compare against the
   pre-transaction root.
2. **The authority assigns revisions.** Changed transactions get a new
   revision; identical upserts and absent deletes are `no_change`.
3. **Absence checks use namespace revisions**, which detects delete-and-recreate
   races without keeping a tombstone for every deleted key.
4. **Captures are constant-time.** Pinning the current roots for a checkpoint
   or a read takes microseconds; obsolete nodes are reclaimed by budgeted
   maintenance rather than all at once.

The application library has no dependency on consensus, I/O or HTTP.

---

## Sessions and Exactly-Once Results

Producers open a session; each open fences older generations. Transactions
carry contiguous sequence numbers, and the last 256 results of the current
generation are part of replicated state. That gives every transaction exactly
one outcome, even across lost replies, retries, leader changes and restarts:
an identical retry returns the original result, a changed retry is a conflict,
and an evicted result is reported as unavailable rather than re-executed.
There's no wall-clock expiry anywhere in the protocol.

---

## Durable Consensus

The authority uses a strict consensus module that lives alongside, not inside,
the legacy `cluster::Raft`:

1. Terms and votes are persisted before granting a vote or sending a
   higher-term message.
2. Appended entries and truncations are durable before acknowledgment.
3. A publisher is acknowledged only after a durable majority commits and the
   state machine records the result, never at proposal time.
4. A storage error fences the member instead of letting it keep voting.
5. Membership is a fixed list of 3 or 5 voters with stable IDs.
6. Strong reads and result lookups pass through a quorum barrier.

Replication runs over point-to-point, length-delimited TLS connections with a
fixed 120-byte authenticated hello, separate control and bulk lanes per voter,
and bounded snapshot transfer.

---

## Storage and I/O

The Worker persists the journal and checkpoints through a completion-based I/O
layer: a native BSD completion backend on macOS and FreeBSD, io_uring on
Linux, and a portable fallback. The authority owner runs one event loop and never blocks it on
`fsync`. Checkpoints are captured from a frozen state, encoded incrementally
in 64 KiB chunks, published atomically, and only then allow old journal
segments to be reclaimed.

Measured with real POSIX storage and 64 producers grouped per commit, about 490
transactions per second on a local disk, with the 500,000-knob state using
about 0.042 CPU cores at that rate. Each group waited roughly 128–131 ms for
its durability barrier.

---

## Bounded Work Per Turn

Every phase is budgeted so no single scheduling turn monopolizes the owner:
committed entries apply in quanta of at most 8 commands, 128 mutations or
64 KiB; checkpoint encoding, retirement and response encoding all advance in
bounded slices. The measured maximum service turn for a 100,000-knob load fell
from 73.2 ms to 3.7 ms with command quanta, spreading the same total CPU over
more turns.

---

## Qualification

The authority has passed native, ASan/UBSan and TSan suites on macOS, Linux
and FreeBSD, three real HTTPS processes through leader `SIGKILL`, snapshot
catch-up and recovered voter reconnection. Sustained pressure, churn,
whole-service telemetry overhead and fleet-scale gates remain open; see the
[Roadmap](/Detent/roadmap/).
