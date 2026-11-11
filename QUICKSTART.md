# Quick start

Run a durable authority and a managed agent on loopback, publish knobs, and read them from the shell. Run the examples in order, in one terminal, starting from the repository root. The authority listens only on 127.0.0.1, and every file the guide writes stays inside `build/quickstart`.

## Build

```sh
cmake -B build -DDETENT_BUILD_TESTS=OFF -DDETENT_BUILD_EXAMPLES=OFF -DDETENT_BUILD_BENCHMARKS=OFF
cmake --build build -j 4
export PATH="$PWD/build/src:$PATH"
```

This builds `detentd` (the authority), `detent-agent` (the managed agent) and `detent` (the shell reader) in `build/src`.

## Make development certificates and credentials

The agent verifies the authority's TLS certificate, so the authority needs one. The repository's test fixture makes a throwaway CA and `localhost` leaf certificates with OpenSSL, valid for one day:

```sh
mkdir -p build/quickstart && cd build/quickstart
OPENSSL_CONF=../../tests/detent_authority/tls_ambient.cnf \
  cmake -DOUTPUT="$PWD/certs" -DOPENSSL=openssl \
  -P ../../tests/detent_authority/tls_certificates.cmake
```

Use these certificates for local development only. Then pick an authority identity and two tokens, one for the publisher and one for the agent. Identities are 32 hexadecimal characters; tokens are at least 32 characters:

```sh
IDENTITY="\"cluster\":\"$(openssl rand -hex 16)\",\"group\":\"$(openssl rand -hex 16)\",\"epoch\":\"$(openssl rand -hex 16)\""
WRITER_TOKEN=$(openssl rand -hex 24)
READER_TOKEN=$(openssl rand -hex 24)
```

## Start the authority

```sh
cat > authority.json <<EOF
{
  $IDENTITY,
  "directory": "authority-data",
  "publisher_tls": {
    "certificate": "certs/one.pem", "key": "certs/one.key", "ca": "certs/ca.pem"
  },
  "credentials": [
    { "token": "$WRITER_TOKEN", "producer": "writer",
      "namespaces": ["search", "checkout", "payments"] },
    { "token": "$READER_TOKEN", "producer": "agent", "principal": "agent-reader",
      "publish": false, "namespaces": ["search", "checkout", "payments"] }
  ]
}
EOF
chmod 600 authority.json
detentd --authority authority.json --runtime-dir run --daemonize
```

With no `voters`, the authority is a single voter. It listens on 127.0.0.1 port 8317 and keeps its durable store in `authority-data`. The `writer` credential may publish to the three namespaces; the `agent` credential may only read them, as principal `agent-reader`.

## Open a publishing session

Every request carries the writer's token and the authority identity. Define a helper, then open a session:

```sh
api() {
  curl --fail-with-body -sS -w '\n' --cacert certs/ca.pem \
    -H "Authorization: Bearer $WRITER_TOKEN" -H 'Content-Type: application/json' \
    --data "{$IDENTITY,$2}" "https://localhost:8317/v2/$1"
}
api open '"expected_generation":0,"nonce":"00000000000000000000000000000001"'
```

`--daemonize` returns once the authority has a leader, so the open succeeds on its first attempt. The reply looks like this, with the identity elided:

```text
{"authority":{...},"status":"applied","revision":0,"namespace_revision":0,"generation":1,"sequence":0}
```

The reply's `"generation":1` is this producer's session. Each transaction in it carries the next `sequence` number. A second helper publishes one knob definition per transaction, in the namespace before the colon:

```sh
sequence=0
publish() {
  sequence=$((sequence + 1))
  api transactions "\"generation\":1,\"sequence\":$sequence,\"namespace\":\"${1%%:*}\",\"operations\":[{\"key\":\"$1\",\"value\":$2}]"
}
```

## Start the managed agent

```sh
cat > managed.json <<EOF
{
  $IDENTITY,
  "principal": "agent-reader",
  "namespaces": ["search", "checkout", "payments"],
  "token": "$READER_TOKEN",
  "ca": "certs/ca.pem"
}
EOF
chmod 600 managed.json
detent-agent --managed managed.json --generation-dir agent-state --runtime-dir run \
  --initialize-managed --daemonize
export DETENT_GENERATION_DIR="$PWD/agent-state"
```

The agent connects to `localhost:8317` over TLS, checks the certificate against `certs/ca.pem`, and publishes immutable generations in `agent-state`. `--initialize-managed` creates that store on the first start; later starts leave it out. `detent get` reads the newest generation from `DETENT_GENERATION_DIR` and never opens `managed.json`.

## 1. Change a numeric setting

```sh
publish search:max_results '{"type":"int","default":25}'
sleep 1
detent get search:max_results
# 25
```

Each transaction replies with its outcome and the authority's new revision:

```text
{"authority":{...},"status":"applied","revision":1,"namespace_revision":1,"generation":1,"sequence":1}
```

Change the limit without restarting anything:

```sh
publish search:max_results '{"type":"int","default":100}'
sleep 1
detent get search:max_results
# 100
```

A transaction replaces the whole definition of each key it names and leaves every other knob alone. `sleep 1` gives the agent time to deliver the update before the read.

## 2. Toggle a feature

```sh
publish checkout:new_path '{"type":"bool","default":true}'
sleep 1
detent get checkout:new_path
# true

publish checkout:new_path '{"type":"bool","default":false}'
sleep 1
detent get checkout:new_path
# false
```

## 3. Ramp a rollout

Start with 10% of users:

```sh
publish payments:new_processor '{"type":"bool","default":false,"rollout_pct":10}'
sleep 1
detent get payments:new_processor --hash user-1
# false
detent get payments:new_processor --hash user-42
# true
```

Use a stable user or account ID as `--hash`. The same ID keeps its assignment across reads and restarts. Raising the percentage keeps included users enabled; small populations need not split exactly 10/90.

```sh
publish payments:new_processor '{"type":"bool","default":false,"rollout_pct":100}'
sleep 1
detent get payments:new_processor --hash user-1
# true
detent get payments:new_processor
# false
```

At 100%, every read with `--hash` returns `true`. A read without a hash skips the rollout and returns the knob's default, `false` here.

## 4. Use different rollout percentages for different groups

Force employees on, exclude a group, and ramp beta users separately from production users:

```sh
publish payments:new_processor '{
  "type": "bool", "default": false, "rollout_pct": 10,
  "overrides": {
    "employees": true, "excluded": false,
    "beta": {"rollout_pct": 50}, "production": {"rollout_pct": 5}
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
detent get payments:new_processor --hash user-1 --switch unknown
# false
```

Your application supplies the group with `--switch`. A matching boolean forces its value; a matching rollout compares the same user bucket against that group's percentage. An unknown group uses the knob-wide rollout. When changing one group's percentage, publish the complete definition so the other groups stay.

## 5. Supply a fallback for a missing knob

```sh
detent get checkout:unpublished_feature --default false
# false
```

The fallback applies only to this read; it publishes nothing.

## 6. Remove a knob

A `null` value deletes the key:

```sh
publish search:max_results null
sleep 1
detent get search:max_results
# ERR MissingKnob: search:max_results (scoped lookup failed)
```

## Stop

```sh
detent-agent --status --generation-dir agent-state --runtime-dir run
# running (pid 97274)
# durable revision=8 knob_count=2
kill "$(cat run/agent.pid)" "$(cat run/detentd-8317.pid)"
cd ../.. && rm -rf build/quickstart
```

The durable store and the generations live in `build/quickstart`, so removing it resets the tutorial. System services use their service manager instead; see [System services](docs/SERVICES.md).

## Next steps

1. [Usage guide](docs/USAGE.md): managed agent configuration, `agent.json`, and library examples.
2. [Authority guide](docs/AUTHORITY_ARCHITECTURE.md): credentials, sessions, retries and namespace compare-and-set.
3. [System services](docs/SERVICES.md): Linux, FreeBSD, and macOS deployment.
4. [README](README.md): architecture, bindings, and validation.
