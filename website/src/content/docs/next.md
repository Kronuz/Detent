---
title: "Next Steps"
---

You've published knobs, read them from the shell, ramped a rollout and
overridden groups. Here's where to go next, depending on what you're building.

---

## Stop the Quick-start Daemons

Press Ctrl-C in the terminal running `detentd`, or, if you started it with
`--daemonize`, find its PID with `detentd --status` and send it SIGTERM. The
agent that `detent get` started keeps running in the background; inspect it
and stop it with:

```sh
detent-agent --status
# running (pid 65214)
# OK version=12 knob_count=4
kill -TERM <pid>
```

---

## Read Knobs from Your Application

1. [Using Detent in a C++ Application](/Detent/guides/cpp-application/): link
   the library, ship baked defaults, and read knobs.
2. [Live Updates](/Detent/guides/live-updates/): point clients at a file or a
   distributor.
3. [Python and Rust](/Detent/guides/python-and-rust/): the language bindings.
4. [Shell Scripts and the Agent](/Detent/guides/shell-and-agent/): read knobs
   from any process on the host.

## Run It for Real

1. [System Services](/Detent/operations/services/): systemd, launchd and rc.d.
2. [Clustering](/Detent/guides/clustering/): a three-node Raft distributor.
3. [Security](/Detent/operations/security/): what the distributor does and
   doesn't protect.
4. [Durable Authority](/Detent/guides/durable-authority/): authenticated,
   transactional publishing with durable consensus.

## Understand It

1. [Evaluation Semantics](/Detent/reference/evaluation/): the precise rules.
2. [Architecture](/Detent/architecture/): how reads stay allocation-free and
   updates stay atomic.
3. [Roadmap](/Detent/roadmap/): what's done, what's in progress, and what's
   still missing.
