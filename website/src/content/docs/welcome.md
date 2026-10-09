---
title: "Welcome"
---

This documentation aims to be a comprehensive guide to Detent. We'll cover
getting a distributor up and running, publishing knobs, reading them from C++,
Python, Rust and the shell, rolling features out gradually, deploying the
daemons as system services, and how the whole thing works underneath.

## So what is Detent, exactly?

Detent is **a runtime-configuration system for namespaced boolean and integer
knobs**. Applications read knobs synchronously from an in-memory snapshot; a
background thread keeps that snapshot fresh from a file, a distributor, or a
durable authority, and swaps each validated update in atomically.

A *detent* is the mechanism that makes a rotary knob click into discrete,
stable positions. That's the idea: your process always sees a complete,
validated configuration, never something half-turned.

Detent has three moving parts:

1. **`detent::Client`**, the library your application links. It owns the
   read path, the evaluation rules and the background updater.
2. **`detentd`**, the distributor. Publishers `PUT` or `PATCH` snapshots to
   it; clients poll it with conditional `GET`s. It can replicate itself with
   Raft, or run as a durable, authenticated authority.
3. **`detent-agent`** and **`detent`**, the host-local agent and its shell
   reader. One agent per host keeps a warm client and publishes a pre-parsed
   snapshot into shared memory, so `detent get NAME` answers in microseconds
   from any script or language.

:::hint[Where the name comes from]{.info}
Detent began as a faithful, independent clone of the publicly observable
behavior of Meta's JustKnobs, and the original specification is kept in the
repository as
[`justknobs-implementation-spec.md`](https://github.com/Kronuz/Detent/blob/main/justknobs-implementation-spec.md).
Detent is not a Meta product. Every identifier in the code is `detent`, and
everything beyond the spec, from the distributor to the durable authority, is
Detent's own design.
:::

---

## Document Conventions

Throughout this guide there are a few conventions in the way documentation is
laid out and formatted.

### API Snippets

Many examples of HTTP calls to the distributor look like this:

```rest
PUT /v1/snapshot

{
  "schema_version": 1,
  "version": 1,
  "knobs": {
    "search:max_results": { "type": "int", "default": 25 }
  }
}
```

For those, you can always copy the equivalent _curl_ command, aimed at a local
distributor on its default port `8317`, by hovering over the snippet and
clicking the copy-to-clipboard button.

### Helpful Hints

A number of small-but-handy pieces of information can make using Detent
easier, more interesting, and less hazardous. Here's what to look out for:

:::hint[Tips help you get more from Detent]{.tip}
These are tips and tricks that will help you be a Detent wizard!
:::

:::hint[Notes are handy pieces of information]{.info}
These are for the extra tidbits sometimes necessary to understand Detent.
:::

:::hint[Things to notice to help you get better results]{.caution}
Be aware of these messages if you want to get the results you expect.
:::

:::hint[Warnings help you not blow things up]{.warning}
Be aware of these messages if you wish to avoid outages and surprises.
:::

:::hint[You'll see this by a feature that hasn't been implemented]{.unimplemented}
Some pieces describe behavior that is designed, and documented here, but not
implemented yet. The [Roadmap](/Detent/roadmap/) tracks all of them.
:::

:::hint[Documentation section under construction]{.construction}
This part of the Detent documentation is still not completed.
:::

---

If you come across anything along the way that we haven't covered, or if you
know of a tip you think others would find handy, please [file an
issue](https://github.com/Kronuz/Detent/issues/new) and we'll see about
including it in this guide.
