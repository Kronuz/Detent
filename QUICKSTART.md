# Quick start

Build Detent, publish a feature flag, and ramp it while a local agent serves
reads. This example uses a live snapshot file and requires no server setup.
Run these commands from the repository root.

## Build

```sh
cmake -B build
cmake --build build -j
export PATH="$PWD/build/src:$PWD/build/examples:$PATH"
```

## Publish and read a 10% rollout

```sh
detent_publish snapshot.json payments:new_processor=false \
  --rollout payments:new_processor=10

detent get payments:new_processor --hash user-1 --source-path "$PWD/snapshot.json"
# false
detent get payments:new_processor --hash user-42
# true
```

The first `get` starts the agent automatically. If an agent is already running
with another source, run `detent-agent stop` before the first read.

Use a stable user or account ID as `--hash`. The same ID keeps the same
assignment across reads and restarts. The percentage applies to hash buckets;
a small user population need not split exactly 10/90.

## Ramp to 50%, then 100%

```sh
detent_publish snapshot.json --rollout payments:new_processor=50
# Check your application before advancing.
detent_publish snapshot.json --rollout payments:new_processor=100
```

Each publish increments the snapshot version automatically. The agent picks up
changes on its next poll (200 ms by default). At 100%, every user supplied
through `--hash` gets `true`:

```sh
detent get payments:new_processor --hash user-1
# true
```

Increasing the percentage keeps previously included users enabled. Reads
without `--hash` use the knob's default, which remains `false` in this example.

## Roll back and stop

```sh
detent_publish snapshot.json --rollout payments:new_processor=0
# After the next agent poll:
detent get payments:new_processor --hash user-42
# false

detent-agent stop
```

## Next steps

1. [Usage guide](docs/USAGE.md): network distribution, group overrides, configuration, and library examples.
2. [System services](docs/SERVICES.md): Linux, FreeBSD, and macOS deployment.
3. [README](README.md): architecture, bindings, and validation.
