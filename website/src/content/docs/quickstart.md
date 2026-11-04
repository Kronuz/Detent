---
title: "Quick-start"
---

This guide takes you through installing Detent, publishing a snapshot file, and
reading your first knobs. **DON'T PANIC**, it takes just a few minutes.

---

## Installing

### Using Homebrew under macOS

```sh
brew install Kronuz/tap/detent
```

### Building from Source

```sh
git clone https://github.com/Kronuz/Detent.git
cd Detent
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
sudo cmake --install build
```

You can check the [Installation](/Detent/installation/) section for compiler
requirements, build options and platform notes.

---

## Publishing

The simplest live source is a JSON snapshot file. Make a working directory and
a small helper that replaces the file atomically:

```sh
mkdir -p ~/detent-quickstart && cd ~/detent-quickstart
publish() { cat > knobs.json.tmp && mv knobs.json.tmp knobs.json; }
```

`mv` within one directory is a `rename()`, so a reader sees the old file or the
new one, never a half-written file.

Publish a first snapshot containing one integer knob:

```sh
publish <<'EOF'
{
  "schema_version": 1,
  "version": 1,
  "knobs": {
    "search:max_results": { "type": "int", "default": 25 }
  }
}
EOF
```

Every snapshot carries a `version`, and every update needs a **higher** one. A
reader ignores a stale or repeated version and keeps the snapshot it has.

---

## Reading

Read the knob from the shell:

```sh
detent get search:max_results --source-path "$PWD/knobs.json"
# 25
```

The first `get` starts a local `detent-agent` that polls `knobs.json`. Later
reads attach to that agent, so they don't need the flag.

Now change the value without restarting anything:

```sh
publish <<'EOF'
{
  "schema_version": 1,
  "version": 2,
  "knobs": {
    "search:max_results": { "type": "int", "default": 100 }
  }
}
EOF
```

```sh
sleep 1
detent get search:max_results
# 100
```

The agent polls every 200 ms by default, so the new value shows up almost
immediately. Your applications would see it the same way: the next read after
the update lands returns the new value, with no restart and no redeploy.

:::hint[Publishing for a fleet]{.info}
A snapshot file serves one host. To publish to many hosts, with authenticated
publishers and atomic transactions, run the
[durable authority](/Detent/guides/durable-authority/).
:::

---

The next pages walk through each idea in turn, in the same shell and directory:

1. [Basic Concepts](/Detent/basics/): knobs, snapshots, versions and defaults.
2. [Settings and Flags](/Detent/settings-and-flags/): numeric settings and on/off switches.
3. [Gradual Rollouts](/Detent/rollouts/): percentage ramps with stable buckets.
4. [Group Overrides](/Detent/group-overrides/): forcing or ramping specific groups.
5. [Next Steps](/Detent/next/): where to go from here.
