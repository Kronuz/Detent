# Quick start

Try three everyday uses: change a numeric setting, toggle a feature, and ramp
a rollout. These examples share a live snapshot file and require no server
setup. Run them in order from the repository root.

## Build

```sh
cmake -B build
cmake --build build -j
export PATH="$PWD/build/src:$PWD/build/examples:$PATH"
```

## 1. Change a numeric setting

Publish a result limit and read it:

```sh
detent_publish snapshot.json search:max_results=25
detent get search:max_results --source-path "$PWD/snapshot.json"
# 25
```

The first `get` starts the agent automatically. If an agent is already running
with another source, run `detent-agent stop` before the first read.

Change the limit without restarting the agent or your application:

```sh
detent_publish snapshot.json search:max_results=100
# After the next agent poll:
detent get search:max_results
# 100
```

Each publish increments the snapshot version and preserves unrelated knobs.
Updates become visible on the next agent poll (200 ms by default).

## 2. Toggle a feature

Enable a new checkout path, then disable it if needed:

```sh
detent_publish snapshot.json checkout:new_path=true
# After the next agent poll:
detent get checkout:new_path
# true

detent_publish snapshot.json checkout:new_path=false
# After the next agent poll:
detent get checkout:new_path
# false
```

## 3. Ramp a rollout

Start with 10% of users:

```sh
detent_publish snapshot.json payments:new_processor=false \
  --rollout payments:new_processor=10
# After the next agent poll:
detent get payments:new_processor --hash user-1
# false
detent get payments:new_processor --hash user-42
# true
```

Use a stable user or account ID as `--hash`. The same ID keeps its assignment
across reads and restarts. Increasing the percentage keeps previously included
users enabled; small populations need not split exactly 10/90.

```sh
detent_publish snapshot.json --rollout payments:new_processor=50
# Check your application before advancing.
detent_publish snapshot.json --rollout payments:new_processor=100
# After the next agent poll:
detent get payments:new_processor --hash user-1
# true

# Roll back all hashed users.
detent_publish snapshot.json --rollout payments:new_processor=0
```

At 100%, every read with `--hash` returns `true`. Reads without a hash use the
knob's default, which remains `false` in this example.

## Stop the agent

```sh
detent-agent stop
```

## Next steps

1. [Usage guide](docs/USAGE.md): network distribution, group overrides, configuration, and library examples.
2. [System services](docs/SERVICES.md): Linux, FreeBSD, and macOS deployment.
3. [README](README.md): architecture, bindings, and validation.
