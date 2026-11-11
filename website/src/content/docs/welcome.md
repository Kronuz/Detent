---
title: "Welcome"
---

This documentation aims to be a comprehensive guide to Detent. We'll cover
publishing knobs, reading them from C++, Python, Rust and the shell, rolling
features out gradually, running the durable authority, deploying the daemons as
system services, and how the whole thing works underneath.

## So what is Detent, exactly?

Detent is **a runtime-configuration system for namespaced boolean and integer
knobs**. Applications read knobs synchronously from an in-memory snapshot; a
background thread keeps that snapshot fresh from a file, an HTTP endpoint, or
a durable authority, and swaps each validated update in atomically.

A *detent* is the click-stop that holds a knob at its setting. That's the
idea: your process always sees a complete, validated configuration, never
something half-turned.

Detent has three moving parts:

1. **`detent::Client`**, the library your application links. It owns the
   read path, the evaluation rules and the background updater.
2. **`detentd`**, the durable authority. Publishers open authenticated
   sessions and submit atomic, per-namespace transactions; it commits them
   through durable Raft consensus and delivers them to managed agents.
3. **`detent-agent`** and **`detent`**, the host-local agent and its shell
   reader. One agent per host receives deliveries and publishes immutable
   generations, so `detent get NAME` reads local files rather than the network,
   from any script or language.

The knob model, with its names, groups, overrides, rollout and evaluation
rules, is specified in
[`docs/design/knob-model.md`](https://github.com/Kronuz/Detent/blob/main/docs/design/knob-model.md).

---

## Document Conventions

Throughout this guide there are a few conventions in the way documentation is
laid out and formatted.

### API Snippets

Examples of HTTP calls to the durable authority look like this:

```rest
GET /v1/health
```

For those, you can always copy the equivalent _curl_ command, aimed at a local
`detentd` on its default port `8317`, by hovering over the snippet and clicking
the copy-to-clipboard button.

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
