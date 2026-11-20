# Relay

`detent-relay` mirrors namespace history from the authority's voters, or from another relay, and serves it to readers: W5 sessions (version 2) with W6 frames, and the history routes a downstream relay reads. It is a reader, never a consensus member, and links no consensus. It mirrors sealed history only; the authority's leader seals changes that stay unsealed for one second, so on a quiet cluster a relay trails the authority by about one second plus `interval_ms`. How the mirror works is in [namespace S8](status/namespace-s8.md).

```text
Authority voters ──layouts, files, W6──▶ Relay A ──▶ Relay B ──▶ ...
                                          │            │
                                     W5/W6 readers  W5/W6 readers
```

Build the daemon with `-DDETENT_BUILD_RELAY=ON` (requires OpenSSL 3.0). A relay-only build can disable `DETENT_BUILD_AUTHORITY`.

## Private configuration

The default configuration is `relay.json` in the user configuration directory, or the system configuration directory with `--system`. `--config PATH` selects another file. It holds credentials, so it must be a regular single-link file owned by root or the service account, with mode `0600` or a controlled group-readable `0640`; symlinks are refused. Paths in it are relative to the file.

```json
{
  "address": "127.0.0.1",
  "port": 8318,
  "hostname": "localhost",
  "certificate": "tls/relay.pem",
  "private_key": "tls/relay.key",
  "ca": "tls/ca.pem",
  "mirror": {
    "directory": "mirror",
    "cluster": "63000000000000000000000000000000",
    "namespaces": ["app", "app.flags"],
    "upstreams": [
      {"address": "127.0.0.1", "port": 8317},
      {"address": "127.0.0.2", "port": 8317}
    ],
    "token": "replace-with-the-relay-upstream-token"
  },
  "credentials": [
    {
      "token": "replace-with-a-private-downstream-token",
      "principal": "application-reader",
      "namespaces": ["app.*"]
    }
  ]
}
```

The listener requires TLS; `hostname` is the certificate name `--status` checks, and must match its SAN.

`mirror` is required. Its fields:

| Field | Default | Meaning |
| --- | --- | --- |
| `directory` | required | The mirror's own directory, mode `0700`; created at first start |
| `cluster` | required | The authority's cluster identity, 32 hex digits |
| `namespaces` | required | The names mirrored, in increasing order, at most 4,096 |
| `upstreams` | required | Up to 64 `{"address", "port"}` objects: the authority's voters or another relay, read in turn; a failure, a `503`, or an answer older than the mirror moves to the next |
| `token` | required | The relay's credential at its upstream |
| `hostname`, `ca` | `localhost`, the relay's `ca` | How the relay verifies its upstream |
| `interval_ms` | 250 | Time between syncs, 1 to 60,000; each failed sync in a row doubles it, up to 30 seconds or one interval |
| `timeout_ms` | 10,000 | Deadline for each upstream request |
| `compression` | `false` | Read W6 answers in zstd |
| `sessions` | 1,024 | W5 sessions the relay holds; one idle for 60 seconds is closed |
| `answer_bytes` | 67,108,864 | Memory for answers in flight, 1 MiB to 4 GiB |

At the authority, the relay's upstream credential reads and never publishes: `{"token": ..., "producer": "relay", "principal": "relay", "publish": false, "namespaces": ["*"]}`, or the patterns covering the names it mirrors. A relay reading another relay uses one of that relay's downstream credentials.

Each downstream credential has a `token` of 32 to 128 printable bytes, a `principal`, an optional `policy_epoch` (default 1), and `namespaces`: one or more W13 patterns, each an exact name, a whole-component prefix `p.*` (every name below `p.`, never `p` itself), or `*`. The principal owns the sessions opened with the credential. Tokens are unique; the file holds at most 10,000 credentials within its 1 MiB bound.

## Routes

Every route is `POST` with a bearer credential, the same at a voter and at a relay:

| Route | Request | Answer |
| --- | --- | --- |
| `/v2/distribution/session/{open,next,close}` | W5, version 2 | W6 frames in DTDIST01 fragments, exactly as the authority's |
| `/v2/history/layouts` | `{"namespaces": [name, ...]}`, each granted | `200`, `application/vnd.detent.layouts`: a DTMIRR01 table of the live names' layouts |
| `/v2/history/file` | `{"namespace": name, "name": file}`, the namespace granted | `200`, `application/octet-stream`: the file's bytes when the namespace's current layout names it; `404` otherwise |

A session follows only names the relay mirrors and the credential grants (`403 scope_forbidden` otherwise, `400` for `all`), and a next before the first sync gets `503 source_behind`. A version-1 W5 body gets `400 invalid_request`. An unknown token gets `403 scope_forbidden`. Member routes (`/v2/open`, `/v2/transactions`, `/v2/read`, `/v2/result`, ...) and `/v2/distribution` are `404`.

## Run and inspect

```sh
detent-relay --config relay.json
detent-relay --config relay.json --status
```

The daemon runs in the foreground. `--daemonize` provides unmanaged user operation; supervised services use foreground operation and service-manager logging. `--system --daemonize` and `--status --daemonize` are refused. `--runtime-dir PATH`, `--log-level`, `-v` and `--color` follow the other daemons. The PID file is `relay.pid`; distinct relay instances use separate runtime directories.

`--status` queries `GET /v1/health` over HTTPS and never starts the daemon. The health snapshot reports `ready`, the heap's use, peak and limit, the requests in flight per lane (`opens`, `polls`, `closes`), and the mirror's counters under `mirror` (`syncs`, `sync_failures`, `entries`, `fetched_files`, `fetched_bytes`, `reused_files`, `collected_files`, `answers`, `idle`, `sessions`, `failed_namespaces`, and under `pages` the generations built `builds` (`incremental` of them over the previous head), the canonical relocations `relocations` and `build_failures`, and the answers `full_copies` and `deltas` sent as pages and `delta_records` kept as records by D6). It holds no tokens. The relay reports ready after the mirror's first sync; a mirror that stops on a logic error stops the relay. SIGTERM stops intake, drains requests in flight, stops the mirror thread and exits. A restart resumes from the mirror on disk.

## Capacity

`connections` bounds accepted connections (default 1,024, range 1 to 65,536). `service_limits` sets the requests in flight per admission lane, each 1 to 65,536: `opens` (default 16; the history routes share it), `polls` (640) and `closes` (16).

```json
{
  "connections": 16384,
  "service_limits": {"opens": 64, "polls": 10000, "closes": 64}
}
```

Memory is admitted from one heap: `control_bytes` (64 MiB by default, 16 MiB to 1 GiB) for requests, `baseline_bytes` (16 MiB, 1 MiB to 1 GiB) for the registry and runtime, and the mirror's `answer_bytes`. `heap_bytes` defaults to their sum and may only raise it. These are admission ceilings, not measured fleet throughput; TLS and framework allocations and process RSS are outside them. Raising counts requires the heap and operating-system descriptors to match: connection admission bounds what the relay accepts, it does not reserve descriptors.

## System services

Install the matching template from `packaging/systemd`, `packaging/launchd` or `packaging/rc.d`, and follow [system services](SERVICES.md) for service accounts. Keep the mirror directory below a private persistent parent: `/var/lib/detent-relay` on Linux, `/var/db/detent-relay` on macOS and FreeBSD, as an absolute path such as `/var/lib/detent-relay/mirror` (a relative path resolves beside the configuration, which the Linux unit's filesystem protection keeps read-only). The Linux unit and FreeBSD script provision the parent; for launchd, provision it with owner `_detent` and mode `0700`, and `/var/run/detent/relay` with service ownership and mode `0755`. Keep durable state outside runtime directories, which are cleared at boot. Configuration and grant changes take a restart.
