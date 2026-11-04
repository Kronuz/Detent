---
title: "Update Path"
---

The update path is how a new configuration reaches a running reader. The agent
validates and publishes; the reader only notices and attaches. A reader starts
no thread: the lookup that sees a change does the work, or
`refresh()` does it earlier from a thread the application picks.

---

## The Switch

```d2 alt="The agent publishes a generation, replaces selected and moves the change sequence; the next lookup sees the moved sequence, one thread attaches the new generation and publishes a view, and the old view is freed once its readers drain"
direction: down
publish: "agent publishes a generation\nand replaces selected"
bump: "agent moves the\nchange sequence"
see: "a lookup sees the\nmoved sequence"
win: "refreshing flag\n(compare-and-swap)" {shape: diamond}
attach: "attach the new generation\n(checked records, lease)"
view: "publish a new view"
old: "answer from the\ninstalled view"
free: "free old views\nwhose counters drain"
publish -> bump -> see -> win
win -> attach: "won"
win -> old: "lost"
attach -> view -> free
```

1. **Publish.** The agent writes a new immutable generation, then atomically
   replaces the `selected` descriptor, then moves the entry's sequence in the
   `change` file. The store has one entry, the selected generation, serving
   every namespace.
2. **Notice.** Each lookup loads that sequence from the mapped `change` file.
3. **Attach.** One thread wins `refreshing`, re-reads `selected`, attaches the
   generation if its id changed and publishes a new view. Everyone else keeps
   reading the installed view.
4. **Retire.** A handle keeps at most four views. An old view is freed only
   once its counters sum to zero, so no reader ever reads freed memory.

---

## Failures Keep the Installed View

A generation that fails verification, a refused directory or a lease that
can't be taken frees what the attempt built and keeps the installed view.
Lookups keep answering from it until the sequence moves again or `refresh()`
runs. A revision that goes backwards within one incarnation is refused.

A namespace with no generation yet answers each call's fallback with the
status `UNAVAILABLE`. A store that doesn't exist yet opens; the reader retries
it at most once per 200 ms.

---

## Validation

Validation happens in the agent, completely, before a generation is selected:
the delivery's authentication, framing and schema, then the generation's
records and digests. The reader checks ownership, mode and ACLs of the store
and every record it opens, and verifies each page against its digest on first
touch. See [Delivery and Local Images](/Detent/architecture/delivery/) and
[Snapshot Format](/Detent/reference/snapshot-format/#strictness).

---

## Fork

Pre-forking servers inherit handles. A child forked while another thread is
mid-lookup or mid-refresh keeps a working handle and sees the next publish.
