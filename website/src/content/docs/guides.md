---
title: "Guides"
---

Task-oriented walkthroughs, each one complete enough to follow from a fresh
build. They build on the [Quick-start](/Detent/quickstart/) and link into the
[Reference Guide](/Detent/reference/knobs/) for every detail they skip.

---

## Reading Knobs

1. [Using Detent in a C++ Application](/Detent/guides/cpp-application/): link
   the library, open one reader and read knobs with a fallback on each call.
2. [Shell Scripts and the Agent](/Detent/guides/shell-and-agent/): read knobs
   from any process on a host through local generations.
3. [Python and Rust](/Detent/guides/python-and-rust/): the language bindings.

## Working with Knobs

1. [Kill Switches](/Detent/guides/kill-switches/): introduce, roll out,
   revert, stabilize and remove a risky change.
2. [Testing](/Detent/guides/testing/): fallbacks with no store, and real
   generations from the test writer.

## Running the Servers

1. [Durable Authority](/Detent/guides/durable-authority/): authenticated,
   transactional publishing with durable consensus.
2. [Managed Delivery](/Detent/guides/managed-delivery/): agents that receive
   durable, immutable generations from the authority.
