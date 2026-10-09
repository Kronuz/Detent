---
title: "Guides"
---

Task-oriented walkthroughs, each one complete enough to follow from a fresh
build. They build on the [Quick-start](/Detent/quickstart/) and link into the
[Reference Guide](/Detent/reference/knobs/) for every detail they skip.

---

## Reading Knobs

1. [Using Detent in a C++ Application](/Detent/guides/cpp-application/): link
   the library, ship baked defaults, construct a client and read knobs.
2. [Live Updates](/Detent/guides/live-updates/): feed clients from a local file
   or an HTTP distributor, and watch versions converge.
3. [Shell Scripts and the Agent](/Detent/guides/shell-and-agent/): read knobs
   from any process on a host through shared memory.
4. [Python and Rust](/Detent/guides/python-and-rust/): the language bindings.

## Working with Knobs

1. [Kill Switches](/Detent/guides/kill-switches/): introduce, roll out,
   revert, stabilize and remove a risky change.
2. [Testing](/Detent/guides/testing/): deterministic knobs in unit tests, and
   CLI overrides for local debugging.

## Running the Servers

1. [Clustering](/Detent/guides/clustering/): three replicated distributors
   with Raft, and clients that fail over between them.
2. [Durable Authority](/Detent/guides/durable-authority/): authenticated,
   transactional publishing with durable consensus.
3. [Managed Delivery](/Detent/guides/managed-delivery/): agents that receive
   durable, immutable generations from the authority.
