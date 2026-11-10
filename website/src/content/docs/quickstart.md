---
title: "Quick-start"
---

This guide builds Detent, starts a durable authority and a managed agent on
your own machine, publishes a knob and reads it from the shell. The authority
listens only on 127.0.0.1. **DON'T PANIC**, it takes just a few minutes.

Run the commands in order, in one terminal. The next pages continue in the
same shell, with the same helpers and variables.

---

## Building

```sh
git clone https://github.com/Kronuz/Detent.git
cd Detent
cmake -B build -DDETENT_BUILD_TESTS=OFF -DDETENT_BUILD_EXAMPLES=OFF -DDETENT_BUILD_BENCHMARKS=OFF
cmake --build build -j 4
export PATH="$PWD/build/src:$PATH"
```

This builds the three programs the guide uses, in `build/src`: `detentd`, the
authority; `detent-agent`, the managed agent; and `detent`, the shell reader.
The [Installation](/Detent/installation/) section covers compiler requirements,
build options and platform notes.

---

## Certificates and Credentials

The agent verifies the authority's TLS certificate, so the authority needs one.
The repository's test fixture makes a throwaway CA and `localhost` leaf
certificates with OpenSSL, valid for one day. Work in `build/quickstart`:

```sh
mkdir -p build/quickstart && cd build/quickstart
OPENSSL_CONF=../../tests/detent_authority/tls_ambient.cnf \
  cmake -DOUTPUT="$PWD/certs" -DOPENSSL=openssl \
  -P ../../tests/detent_authority/tls_certificates.cmake
```

<!-- e2e:begin
stdout:
run: test -f certs/ca.pem && test -f certs/one.pem && test -f certs/one.key && test "$(basename "$PWD")" = quickstart
e2e:end -->

Then pick an authority identity and two tokens: one for the publisher, one for
the agent. Identities are 32 hexadecimal characters; tokens are at least 32
characters:

```sh
IDENTITY="\"cluster\":\"$(openssl rand -hex 16)\",\"group\":\"$(openssl rand -hex 16)\",\"epoch\":\"$(openssl rand -hex 16)\""
WRITER_TOKEN=$(openssl rand -hex 24)
READER_TOKEN=$(openssl rand -hex 24)
```

<!-- e2e:begin
stdout:
run: test ${#IDENTITY} -gt 90 && test ${#WRITER_TOKEN} -eq 48 && test ${#READER_TOKEN} -eq 48
e2e:end -->

:::hint[Development only]{.caution}
These certificates and tokens are for a local tutorial. A real deployment
issues its own; see [Security](/Detent/operations/security/).
:::

---

## Starting the Authority

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

<!-- e2e:begin
stdout:
run: test -s run/detentd-8317.pid
e2e:end -->

With no `voters`, the authority is a single voter. It listens on 127.0.0.1
port 8317 and keeps its durable store in `authority-data`. The `writer`
credential may publish to the three namespaces; the `agent` credential may only
read them, as principal `agent-reader`.

---

## Publishing

Every request carries the writer's token and the authority identity. Define a
helper and open a publishing session:

```sh
api() {
  curl --fail-with-body -sS -w '\n' --cacert certs/ca.pem \
    -H "Authorization: Bearer $WRITER_TOKEN" -H 'Content-Type: application/json' \
    --data "{$IDENTITY,$2}" "https://localhost:8317/v2/$1"
}
until api open '"expected_generation":0,"nonce":"00000000000000000000000000000001"'; do sleep 1; done
```

<!-- e2e:begin
stdout-matches: ^\{"authority":\{.*\},"status":"applied","revision":0,"namespace_revision":0,"generation":1,"sequence":0\}$
e2e:end -->

`--daemonize` returns once the authority listens, and the single voter elects
itself leader shortly after. Until then the open returns `outcome_unknown`, and
the loop retries the same request; the nonce marks each retry as the same open.
A start looks like this, with the identity elided:

```text
curl: (22) The requested URL returned error: 503
{"authority":{...},"error":"outcome_unknown"}
curl: (22) The requested URL returned error: 503
{"authority":{...},"error":"outcome_unknown"}
{"authority":{...},"status":"applied","revision":0,"namespace_revision":0,"generation":1,"sequence":0}
```

The reply's `"generation":1` is this producer's session. Each transaction in it
carries the next `sequence` number. A second helper publishes one knob
definition per transaction, in the namespace before the colon:

```sh
sequence=0
publish() {
  sequence=$((sequence + 1))
  api transactions "\"generation\":1,\"sequence\":$sequence,\"namespace\":\"${1%%:*}\",\"operations\":[{\"key\":\"$1\",\"value\":$2}]"
}
```

<!-- e2e:begin
stdout:
run: test "$sequence" = 0 && type publish >/dev/null
e2e:end -->

---

## Starting the Agent

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

<!-- e2e:begin
rewrite: "ca": "certs/ca.pem" => "ca": "certs/ca.pem", "port": 8317
run: detent-agent --status --generation-dir agent-state --runtime-dir run | grep -q '^running'
e2e:end -->

The agent connects to `localhost:8317` over TLS, checks the certificate against
`certs/ca.pem`, and publishes immutable generations in `agent-state`.
`--initialize-managed` creates that store on the first start; later starts
leave it out. `detent get` reads the newest generation from
`DETENT_GENERATION_DIR` and never opens `managed.json`.

---

## Reading

Publish an integer knob and read it:

```sh
publish search:max_results '{"type":"int","default":25}'
sleep 1
detent get search:max_results
# 25
```

<!-- e2e:begin
stdout-matches: "status":"applied","revision":1,"namespace_revision":1,"generation":1,"sequence":1\}$
stdout-matches: ^25$
e2e:end -->

Each transaction replies with its outcome and the authority's new revision:

```text
{"authority":{...},"status":"applied","revision":1,"namespace_revision":1,"generation":1,"sequence":1}
```

Now change the value without restarting anything:

```sh
publish search:max_results '{"type":"int","default":100}'
sleep 1
detent get search:max_results
# 100
```

<!-- e2e:begin
stdout-matches: "status":"applied","revision":2,"namespace_revision":2,"generation":1,"sequence":2\}$
stdout-matches: ^100$
e2e:end -->

`sleep 1` gives the agent time to deliver the update. Your applications see it
the same way: the next read after the update lands returns the new value, with
no restart and no redeploy.

---

The next pages walk through each idea in turn, in the same shell and directory:

1. [Basic Concepts](/Detent/basics/): knobs, snapshots, versions and defaults.
2. [Settings and Flags](/Detent/settings-and-flags/): numeric settings and on/off switches.
3. [Gradual Rollouts](/Detent/rollouts/): percentage ramps with stable buckets.
4. [Group Overrides](/Detent/group-overrides/): forcing or ramping specific groups.
5. [Next Steps](/Detent/next/): stopping the daemons, and where to go from here.
