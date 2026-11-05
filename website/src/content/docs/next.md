---
title: "Next Steps"
---

You've run an authority and a managed agent, published knobs, read them from
the shell, ramped a rollout and overridden groups. Here's where to go next,
depending on what you're building.

---

## Stop the Quick-start Daemons

The authority and the agent keep running in the background. Check the agent,
stop both, and remove the tutorial directory:

```sh
detent-agent --status --generation-dir agent-state --runtime-dir run
# running (pid 98461)
# durable revision=13 knob_count=3
kill "$(cat run/agent.pid)" "$(cat run/detentd-8317.pid)"
cd ../.. && rm -rf build/quickstart
```

The durable store and the generations live in `build/quickstart`, so removing
it resets the tutorial.

---

## Read Knobs from Your Application

1. [Using Detent in a C++ Application](/Detent/guides/cpp-application/): link
   the library, ship baked defaults, and read knobs.
2. [Live Updates](/Detent/guides/live-updates/): point clients at a file or
   an HTTP endpoint.
3. [Python and Rust](/Detent/guides/python-and-rust/): the language bindings.
4. [Shell Scripts and the Agent](/Detent/guides/shell-and-agent/): read knobs
   from any process on the host.

## Run It for Real

1. [System Services](/Detent/operations/services/): systemd, launchd and rc.d.
2. [Security](/Detent/operations/security/): what Detent does and doesn't
   protect.
3. [Durable Authority](/Detent/guides/durable-authority/): authenticated,
   transactional publishing with durable consensus.
4. [Managed Delivery](/Detent/guides/managed-delivery/): the agent's
   credential, configuration, startup and resource bounds.

## Understand It

1. [Evaluation Semantics](/Detent/reference/evaluation/): the precise rules.
2. [Architecture](/Detent/architecture/): how reads stay allocation-free and
   updates stay atomic.
3. [Roadmap](/Detent/roadmap/): what's done, what's in progress, and what's
   still missing.
