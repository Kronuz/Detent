# Quick start

Change settings and roll out features against a running distributor. Run the examples in order from the repository root.

## Build and start the distributor

```sh
cmake -B build
cmake --build build -j
export PATH="$PWD/build/src:$PATH"
detentd
```

Leave `detentd` running and open another terminal in the repository root:

```sh
export PATH="$PWD/build/src:$PATH"
```

The distributor listens on port `8317`. These examples assume a fresh snapshot; if it already has data, use versions greater than its current version. If an agent is already running with another source, inspect it with `detent-agent --status` and send SIGTERM to its PID before the first read.

## 1. Change a numeric setting

Publish a result limit and read it:

```sh
curl --fail-with-body -sS -X PUT localhost:8317/v1/snapshot \
  -d '{"schema_version":1,"version":1,"knobs":{"search:max_results":{"type":"int","default":25}}}'
detent get search:max_results
# 25
```

The first `get` starts the local agent automatically and connects it to the distributor. No agent configuration is needed.

Change the limit without restarting the agent or your application:

```sh
curl --fail-with-body -sS -X PATCH localhost:8317/v1/snapshot \
  -d '{"schema_version":1,"version":2,"knobs":{"search:max_results":{"type":"int","default":100}}}'
sleep 1
detent get search:max_results
# 100
```

Every publish needs a higher version. PATCH preserves unrelated knobs and replaces each supplied knob definition as a whole. The agent picks up updates on its next poll (200 ms by default); `sleep 1` lets the examples wait for propagation before reading.

## 2. Toggle a feature

Enable a new checkout path, then disable it if needed:

```sh
curl --fail-with-body -sS -X PATCH localhost:8317/v1/snapshot \
  -d '{"schema_version":1,"version":3,"knobs":{"checkout:new_path":{"type":"bool","default":true}}}'
sleep 1
detent get checkout:new_path
# true

curl --fail-with-body -sS -X PATCH localhost:8317/v1/snapshot \
  -d '{"schema_version":1,"version":4,"knobs":{"checkout:new_path":{"type":"bool","default":false}}}'
sleep 1
detent get checkout:new_path
# false
```

## 3. Ramp a rollout

Start with 10% of users:

```sh
curl --fail-with-body -sS -X PATCH localhost:8317/v1/snapshot \
  -d '{"schema_version":1,"version":5,"knobs":{"payments:new_processor":{"type":"bool","default":false,"rollout_pct":10}}}'
sleep 1
detent get payments:new_processor --hash user-1
# false
detent get payments:new_processor --hash user-42
# true
```

Use a stable user or account ID as `--hash`. The same ID keeps its assignment across reads and restarts. Increasing the percentage keeps previously included users enabled; small populations need not split exactly 10/90.

```sh
curl --fail-with-body -sS -X PATCH localhost:8317/v1/snapshot \
  -d '{"schema_version":1,"version":6,"knobs":{"payments:new_processor":{"type":"bool","default":false,"rollout_pct":50}}}'
# Check your application before advancing.
curl --fail-with-body -sS -X PATCH localhost:8317/v1/snapshot \
  -d '{"schema_version":1,"version":7,"knobs":{"payments:new_processor":{"type":"bool","default":false,"rollout_pct":100}}}'
sleep 1
detent get payments:new_processor --hash user-1
# true

# Roll back everyone, removing the rollout gate.
curl --fail-with-body -sS -X PATCH localhost:8317/v1/snapshot \
  -d '{"schema_version":1,"version":8,"knobs":{"payments:new_processor":{"type":"bool","default":false}}}'
```

At 100%, every read with `--hash` returns `true`. Reads without a hash use the knob's default, which remains `false` in this example.

## 4. Use different rollout percentages for different groups

Force employees on, exclude a group, and ramp beta users separately from production users:

```sh
curl --fail-with-body -sS -X PATCH localhost:8317/v1/snapshot -d '{
  "schema_version":1,"version":9,"knobs":{
    "payments:new_processor":{
      "type":"bool","default":false,"rollout_pct":10,
      "overrides":{
        "employees":true,"excluded":false,
        "beta":{"rollout_pct":50},
        "production":{"rollout_pct":5}
      }
    }
  }
}'
sleep 1
detent get payments:new_processor --hash user-1 --switch beta
# true
detent get payments:new_processor --hash user-1 --switch production
# false
detent get payments:new_processor --hash user-1 --switch employees
# true
detent get payments:new_processor --hash user-1 --switch excluded
# false
```

Your application supplies the group with `--switch`. A matching boolean forces its value; a matching rollout compares the same user bucket against that group's percentage. An unknown group uses the knob-wide rollout. Without `--hash`, group rollouts use the knob's default. When publishing a changed percentage, include the complete knob definition to preserve its other groups.

## 5. Supply a fallback for a missing knob

```sh
detent get checkout:unpublished_feature --default false
# false
```

This fallback applies only to this read; it does not publish or change a knob.

## Stop

Press Ctrl-C in the distributor's terminal to stop `detentd`. Use `detent-agent --status` to inspect the agent, and send SIGTERM to its reported PID when finished. System services use their service manager for shutdown.

## Next steps

1. [Usage guide](docs/USAGE.md): group overrides, configuration, and library examples.
2. [System services](docs/SERVICES.md): Linux, FreeBSD, and macOS deployment.
3. [README](README.md): architecture, bindings, and validation.
