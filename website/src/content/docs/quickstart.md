---
title: "Quick-start"
---

This guide takes you through building Detent, starting a distributor, and
publishing and reading your first knobs. **DON'T PANIC**, it takes just a few
minutes.

---

## Building and Running

Detent builds with CMake and fetches its dependencies on the first configure:

```sh
git clone https://github.com/Kronuz/Detent.git
cd Detent
cmake -B build
cmake --build build -j
export PATH="$PWD/build/src:$PATH"
```

Start the distributor in the foreground:

```sh
detentd
```

It listens on port `8317` and keeps its snapshot in your user state directory.
Leave it running and open a second terminal in the repository root:

```sh
export PATH="$PWD/build/src:$PATH"
```

You can check the [Installation](/Detent/installation/) section for compiler
requirements, build options and platform notes.

:::hint[Turn up the logs]{.tip}
Run `detentd -v` to see every accepted publish as it happens, and `detent-agent
-vv` to also see the agent's source metrics. See
[Logging](/Detent/operations/logging/) for more.
:::

---

## Publishing

Publish a first snapshot containing one integer knob:

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

The distributor answers with the version it accepted:

```json
{"accepted_version":1}
```

Every publish carries a `version`, and every new publish needs a **higher**
one. A stale or repeated version is rejected with `409 Conflict`.

---

## Reading

Read the knob from the shell:

```sh
detent get search:max_results
# 25
```

The first `get` starts a local `detent-agent` automatically and points it at
`127.0.0.1:8317`. No configuration is needed.

Now change the value without restarting anything:

```rest
PATCH /v1/snapshot

{
  "schema_version": 1,
  "version": 2,
  "knobs": {
    "search:max_results": { "type": "int", "default": 100 }
  }
}
```

```sh
sleep 1
detent get search:max_results
# 100
```

The agent polls every 200 ms by default, so the new value shows up almost
immediately. Your applications would see it the same way: the next read after
the update lands returns the new value, with no restart and no redeploy.

---

The next pages walk through each idea in turn:

1. [Basic Concepts](/Detent/basics/): knobs, snapshots, versions and defaults.
2. [Settings and Flags](/Detent/settings-and-flags/): numeric settings and on/off switches.
3. [Gradual Rollouts](/Detent/rollouts/): percentage ramps with stable buckets.
4. [Group Overrides](/Detent/group-overrides/): forcing or ramping specific groups.
5. [Next Steps](/Detent/next/): where to go from here.
