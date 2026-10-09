---
title: "Testing"
---

Tests shouldn't depend on whatever a live distributor happens to say. Detent
gives you three tools for deterministic knobs: in-memory test clients, CLI
overrides, and defaults-only clients.

---

## In-Memory Test Knobs

`detent::testing::TestKnobs` builds a client from an in-memory snapshot, with
no files, no sources and no background thread:

```cpp
#include "detent/testing.hh"

detent::Snapshot snap;
snap.knobs["checkout:killswitch_new_path"] = detent::BoolDef{.default_value = true};
snap.knobs["search:max_results"] = detent::IntDef{.default_value = 3};

auto guard = detent::testing::TestKnobs(snap).set_thread_local();

// Within this thread, detent::global::* now reads the test snapshot.
assert(detent::global::eval_or_panic("checkout:killswitch_new_path"));
assert(detent::global::get_or_panic("search:max_results") == 3);
```

The guard installs the test client as a **thread-local** override for the
`detent::global` adapter. Destroying the guard restores whatever was active
before, so guards nest and tests can't leak knobs into one another.

Rollouts and group overrides work as they do in production:

```cpp
detent::BoolDef def{.default_value = false, .rollout_pct = 10};
def.overrides["employees"] = true;
def.overrides["beta"] = detent::GroupRollout{.rollout_pct = 50};
snap.knobs["payments:new_processor"] = def;
```

:::hint[Inject the client where you can]{.tip}
Thread-local guards only affect code that reads through `detent::global`. Code
that takes a `detent::Client&` can simply be handed a client constructed from
an in-memory snapshot: `detent::Client client(snap);`.
:::

---

## CLI Overrides

CLI overrides pin a knob for one process, shadowing both baked defaults and
every live snapshot:

```cpp
opts.cli_overrides = {
    "checkout:killswitch_new_path=true",
    "search:max_results=999",
};
```

They're ideal for debugging a single process locally, or for integration tests
that run a real binary. The rules are strict on purpose:

1. Only lowercase `true` and `false` parse as booleans; anything else must be a
   signed base-10 64-bit integer.
2. A name given twice is a startup error. There's no "last one wins".
3. An override is checked before the snapshot, so it answers even for a knob
   no snapshot defines. Reading it with the other type's call (a bool override
   through `get()`, an int through `eval()`) is a `WrongType` error.
4. Overrides are fixed at startup and stay in effect across every update.

Detent parses the `name=value` pairs; splitting your own command line is up to
your application. A typical integration is a repeatable flag:

```sh
my_app --knob checkout:killswitch_new_path=true --knob search:max_results=999
```

---

## Defaults-Only Clients

A client with `defaults_path` and no source reads exactly what's in the file,
forever. That's a good fit for integration tests that want the real loading
path without a live source:

```cpp
detent::InitOptions opts;
opts.defaults_path = "testdata/just_knobs.json";
detent::Client client(std::move(opts));
```

:::hint[Environment switch to freeze knobs]{.unimplemented}
The specification Detent grew from describes an opt-in environment variable,
honored only when `InitOptions` allows it, that prevents the source worker from
starting so a test binary always sees baked defaults. Detent doesn't implement
it yet; use a defaults-only client or CLI overrides instead.
:::
