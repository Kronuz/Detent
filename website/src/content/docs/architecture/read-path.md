---
title: "Read Path"
---

The read path is the code that runs every time your application calls `eval`,
`get` or `lookup` on a `detent::Reader`. Its contract is strict: once warm, no
lock, syscall, sleep, logging I/O or heap allocation, and no thread of its own.
The core is `libdetent_reader` (`src/detent_reader/core.cc`), behind the C ABI
in `include/detent/reader.h` that C++, Python and Rust all call.

---

## Step by Step

```d2 alt="A lookup validates the name, pins the installed view, compares the namespace's change sequence, walks the mapped generation, evaluates, and unpins; a moved sequence makes one thread refresh while the others keep reading"
direction: down
app: "reader.eval() / get() / lookup()"
validate: "validate ns:key\nand the handle's namespaces"
pin: "pin the installed view\n(striped counter)"
seq: "load the\nchange sequence" {shape: diamond}
walk: "walk the mapped generation\n(verified pages)"
eval: "type check + precedence\n(group > rollout > default)"
refresh: "one thread refreshes;\nthe rest keep reading"
app -> validate -> pin -> seq
seq -> walk: "unchanged"
seq -> refresh: "moved"
walk -> eval
```

1. **Validate the name.** `ns:key` splits at the first colon; an invalid name,
   or a namespace outside the handle's list, returns the fallback before any
   handle state is touched.
2. **Pin the view.** The handle holds an immutable view of the attached
   generations. A lookup adds one to its stripe of the view's counters, so
   threads spread over 64 cache lines instead of sharing one reference count.
3. **Check the sequence.** One acquire load of the entry's sequence in the
   mapped `change` file. Unchanged, the view is current. The store has one
   entry, the selected generation, serving every namespace.
4. **Walk and evaluate.** The lookup walks the memory-mapped generation and
   evaluates with the [precedence rules](/Detent/reference/evaluation/). A
   page is verified once, the first time any lookup touches it; later lookups
   test one bit. Only a rollout with a hash value pays for SHA-256.
5. **Unpin** and return the value, or the caller's fallback with the status.

---

## Switching Generations

When the agent publishes, the sequence moves. The first lookup that sees it
tries one compare-and-swap on the handle's `refreshing` flag; the winner
attaches the new generation, publishes a new view and answers from it. Every
other thread keeps answering from the installed view without waiting. A failed
attach keeps the installed view. See [Update Path](/Detent/architecture/update-path/).

The first lookup in a namespace attaches it. Concurrent lookups in that
namespace wait for the winner, so no thread answers a published knob with its
fallback because it lost the race.

---

## Pinning a Consistent Read

A lookup reads one namespace from one generation. The reader promises nothing
across namespaces, and two lookups may straddle a switch. `revision(namespace)`
reports the revision a namespace's attached generation carries.

---

## Measured Cost

| Benchmark | Result |
| --- | --- |
| `detent_hash_bench 5000000` | 198.2 ns per `stable_bucket()` call |

The lookup benchmark is `reader_bench` (`benchmarks/reader_bench.cc`); its
header comment gives the usage.
