Engineering specification / implementation contract

# JustKnobs
Implementation Spec

> **Note on this repository:** the C++ implementation built from this spec is
> called **Detent**, not JustKnobs -- an independent project inspired by the
> publicly observable behavior documented below, not a Meta product and not
> claiming the JustKnobs name. This document is kept under its original title
> because it describes the real, publicly-named system being cloned; every
> `justknobs`/`JustKnobs` identifier in the *code* (namespaces, targets,
> binaries) was deliberately renamed to `detent`/`Detent` to keep that
> distinction unambiguous. See `README.md` for the implementation itself.

A self-contained blueprint for a faithful client-side clone of Meta's publicly observable JustKnobs behavior, completed with explicit design decisions wherever the public record is silent.

**Fast path**Atomic snapshot acquire, immutable map lookup, local evaluation. No network, disk, mutex, or allocation required.

**Update path**Versioned snapshots arrive asynchronously, validate off-path, then replace state in one atomic swap.

**Failure posture**Missing knobs fail fast. Distribution failures preserve the last-known-good snapshot and alert operators.

**Scope**Bool and integer knobs, overrides, file and optional gRPC sources, observability, tests, and build milestones.

**Reading rule.** Green **SOURCED** labels identify behavior supported by Meta public code or literature. Amber **FILLED BLANK** labels are normative choices for this clone, not claims about Meta's private implementation.

Version 1.0 · 29 September 2026 · Portrait A4

## Contents and status legend

**SOURCED** SOURCED **FILLED BLANK** FILLED BLANK

**01**Goal and non-goals

**02**Sourced facts vs filled blanks

**03**Architecture overview

**04**Data model and config formats

**05**API surface

**06**Read path

**07**Update path

**08**Evaluation semantics

**09**Failure semantics

**10**Distribution backends

**11**Overrides and debugging

**12**Observability

**13**Testing plan

**14**Build milestones

**15**Explicit non-goals

**A**Reference module map

**B**Sources

### Label semantics

**SOURCED** SOURCED

Supported by public Meta code, an open-source mirror of internal client interfaces, public commits, or Meta's configuration-management literature. Citations are listed in Appendix B.

**FILLED BLANK** FILLED BLANK

A deliberate, normative choice required to produce a complete system. Implement it exactly unless an Architecture Decision Record replaces it.

**Critical boundary.** The exact JustKnobs control plane, evaluation algorithm, latency and scale figures, audit facilities, ownership/expiry metadata, management UI, and relationship to Gatekeeper are not public. This specification never attributes the filled choices in those areas to Meta.

### Normative language

**MUST** and **MUST NOT** define acceptance criteria. **SHOULD** is the default unless a documented constraint justifies deviation. **MAY** is optional. “Snapshot” means the immutable, fully validated map visible to readers at one instant.

### Implementation target

The reference signatures use Rust because the public implementation evidence uses `ArcSwap`. The architecture is language-neutral; the concurrency mapping covers Go, Java, and Python.

## 1. Goal and non-goals

**SOURCED** SOURCED **FILLED BLANK** FILLED BLANK

### Goal

Implement a process-local runtime configuration client for namespaced boolean and integer knobs. Applications read knobs synchronously without network or disk I/O. A background task receives versioned snapshots, validates them, merges them over code-versioned defaults, and publishes each accepted state atomically.

**Compatibility target.** Preserve the publicly visible JustKnobs shape: names such as `pytorch/torchrec:killswitch_safe_tolist`; bool and integer types; `eval`, `get`, `get_as`, and Python `check`; per-group bool overrides; process overrides; fail-fast missing-knob behavior; and asynchronous whole-map replacement. [S1-S7]

### Performance contract

- **Read path:** MUST perform no network call, file read, mutex acquisition, sleep, retry, logging I/O, or heap allocation in steady state.

- **Concurrency:** readers MUST NOT block the updater or one another. A reader sees either the complete old snapshot or complete new snapshot.

- **Freshness:** eventual; not transactional. During propagation or refresh failure, readers continue on the last-known-good snapshot.

- **Latency:** no numeric service-level objective is claimed. Public sources expose the atomic in-process mechanism but no JustKnobs benchmark.

### Product contract

Kill switches are ordinary bool knobs following the `killswitch_*` naming convention. The system supports rollout, instant revert, and deliberate code/config cleanup after the rollout becomes permanent. [S5-S7]

### Out of scope at a glance

### Not built

- Management UI or approval workflow

- Audit history and ownership catalog

- Expiry automation or dead-flag detector

- Multi-knob transactions

### Not claimed

- Meta control-plane equivalence

- Meta scale or latency figures

- Gatekeeper algorithm or lineage

- Configerator compatibility

The complete non-goal rationale appears in Section 15.

## 2. Sourced facts vs filled blanks

**SOURCED** SOURCED **FILLED BLANK** FILLED BLANK

| Area | SOURCED | FILLED BLANK for clone v1 |
|---|---|---|
| Types / names | Bool and int; `<knobset>:<name>` | UTF-8, 1-200 bytes; exactly one colon; restricted printable pattern |
| Read storage | Process-global `ArcSwap<JustKnobsInMemory>`; atomic load + map lookup | Immutable `Map<String, KnobDef>`; snapshot metadata |
| Update | Watcher waits for next config, builds map, atomically swaps | Monotonic versions, schema validation, defaults merge, keep last-known-good |
| Bool eval | `hash_val`, `switch_val`; global value plus per-group overrides | CLI → lookup → group → SHA-256 bucket → default precedence |
| Failure | Missing reads error/panic; failures not cached; defaults in `just_knobs.json` | Typed errors; update rejection rules; Python exception mapping |
| Distribution | Meta-internal implementation is Configerator-based | `KnobSource.watch()`; file default; optional gRPC stream |
| Operations | Kill switches; manual dead-flag cleanup; refresh-failure metric in cached impl | Metric names, staleness gauge, alert suggestions, lifecycle checklist |

### Evidence boundary

The Rust `justknobs_stub` mirror establishes the API and client-side caching pattern. Public commits establish real usage, process overrides, kill-switch lifecycle, and the post-incident fail-fast posture. Meta's Configerator literature establishes only the broader distribution context, not JustKnobs' private control plane. [S1-S10]

**Do not “improve” labels away.** The implementation may evolve, but documentation and code comments MUST retain the distinction between externally evidenced behavior and clone-specific choices. In particular, the SHA-256 bucketing rule is normative here and must never be described as Meta's algorithm.

### Conformance rule

A clone conforms to this specification when all MUST requirements and milestone acceptance tests pass. Substituting a different hash, precedence order, refresh policy, or merge rule is a breaking semantic change and requires a versioned compatibility mode.

## 3. Architecture overview

**SOURCED** SOURCED **FILLED BLANK** FILLED BLANK

AUTHORING / CONTROL PLANE (out of scope) | | publishes complete, versioned snapshot v +-------------------+ watch() stream +----------------------+ | KnobSource | ---------------------------> | background updater | | - file watcher | | parse -> validate | | - optional gRPC | | merge defaults | +-------------------+ +----------+-----------+ | atomic whole-map swap v +--------------------+ | SnapshotRef / RCU | | immutable map | +---------+----------+ | acquire snapshot; local lookup v +--------------------+ | application reads | | eval/get/get_as | +--------------------+

### Components

### Client library

Owns API, CLI override state, current snapshot reference, evaluation, errors, metrics, and language bindings. It is linked into each application process.

### Snapshot builder

Parses untrusted source bytes, checks schema and version, merges live definitions over baked defaults, and produces a new immutable map off the read path.

### Updater

Consumes exactly one `KnobSource` stream per client instance. It serializes updates and performs the only snapshot swaps.

### Distribution adapters

The default watches an atomically replaced JSON file. An optional gRPC adapter receives full snapshots from an external distributor.

### Concurrency model: read-copy-update style

The updater constructs a complete replacement while readers retain references to the current object. One release-store publishes the replacement; each read begins with an acquire-load. Reference counting or garbage collection keeps old snapshots alive until existing readers release them.

| Language | Snapshot holder | Publication | Reader lifetime |
|---|---|---|---|
| Rust | `ArcSwap<Snapshot>` | `store(Arc)` | `Guard` / cloned `Arc` |
| Go | `atomic.Pointer[Snapshot]` | `Store` | GC-managed pointer |
| Java | `AtomicReference<Snapshot>` | `set` | GC-managed reference |
| Python | module-level immutable mapping ref | reference assignment | atomic under CPython GIL |

Python's guarantee is CPython-specific. Alternate interpreters MUST provide an explicit atomic reference abstraction.

## 4. Data model and config formats

**SOURCED** SOURCED **FILLED BLANK** FILLED BLANK

### In-memory model

```
type KnobName = String

enum KnobDef {
  Bool {
    default: bool,
    overrides: ImmutableMap<String, bool>,
    rollout_pct: Option<u8>,       // inclusive range 0..=100
  },
  Int { default: i64 },
}

struct Snapshot {
  version: u64,
  published_at: Option<Timestamp>,
  loaded_at: MonotonicInstant,
  knobs: ImmutableMap<KnobName, KnobDef>,
  source_id: String,
}
```

**SOURCED** SOURCED

Public code exposes separate bool and int collections, namespaced string keys, an in-process map, and per-group bool switch overrides. [S1-S4]

**FILLED BLANK** FILLED BLANK

The tagged union, rollout field, snapshot metadata, validation limits, and immutable representation are clone choices.

### Name validation

- MUST match `^[A-Za-z0-9._/-]+:[A-Za-z0-9._/-]+$`, contain exactly one colon, and be 1-200 UTF-8 bytes.

- MUST be unique across both types after merge. A type change for an existing name is allowed only in a new snapshot and SHOULD be treated as an operational warning.

- Group keys are opaque, case-sensitive UTF-8 strings, 1-128 bytes.

### Merge contract

`just_knobs.json` is loaded first as the base map. Every accepted source snapshot is a complete live overlay: keys present in the source replace the full base definition; keys absent from the source retain their baked default definition. A name absent from both layers is missing and MUST fail fast. Snapshot replacement is atomic at the merged-map boundary.

**No field-wise merge.** Replacing a bool definition replaces its default, override map, and rollout percentage together. This prevents stale overrides from surviving a publisher's deletion.

### Version rules

Version `0` is reserved for the baked-default startup snapshot. Source versions MUST be positive and strictly greater than the last accepted source version. Equal or lower versions are ignored, counted, and logged as stale; they never swap state.

## 4. Data model and config formats (continued)

**FILLED BLANK** FILLED BLANK

### Source snapshot JSON

```
{
  "schema_version": 1,
  "version": 42,
  "published_at": "2026-09-29T15:00:00Z",
  "knobs": {
    "checkout:killswitch_new_path": {
      "type": "bool",
      "default": false,
      "overrides": { "employees": true },
      "rollout_pct": 10
    },
    "search:max_results": { "type": "int", "default": 50 }
  }
}
```

### `just_knobs.json` baked defaults

```
{
  "schema_version": 1,
  "knobs": {
    "checkout:killswitch_new_path": {
      "type": "bool", "default": false,
      "overrides": {}, "rollout_pct": null
    },
    "search:max_results": { "type": "int", "default": 25 }
  }
}
```

### Normative schema constraints

| Field | Rule | On violation |
|---|---|---|
| `schema_version` | Integer; exactly 1 | Reject file/update |
| `version` | Required only for source; unsigned 64-bit, > 0 | Reject update |
| `published_at` | Optional RFC 3339 UTC timestamp | Reject if present and invalid |
| `knobs` | Object; max 100,000 entries by default | Reject update |
| `type` | Exactly `bool` or `int` | Reject update |
| `overrides` | Bool only; object; default empty | Reject wrong type/shape |
| `rollout_pct` | Bool only; null or integer 0..100 | Reject update |
| Unknown fields | Rejected at every object level | Reject update |

### Startup contract

The defaults file is optional. When supplied, it SHOULD be packaged and versioned with application code; an explicitly configured file that is missing, unreadable, or invalid is a startup-fatal configuration error. The client publishes defaults as version 0 before starting any source watcher. With no defaults file, version 0 contains no knobs, and reads report missing knobs until a source snapshot arrives.

Safe defaults in `just_knobs.json`, rather than call-site fallbacks, follow the public post-incident guidance. The exact schema above is filled for this clone. [S1, S6]

## 5. API surface

**SOURCED** SOURCED **FILLED BLANK** FILLED BLANK

### Rust reference API

```
pub fn init(opts: InitOptions) -> Result<ClientHandle, InitError>;
pub fn eval(
    name: &str,
    hash_val: Option<&str>,
    switch_val: Option<&str>,
) -> Result<bool, KnobError>;
pub fn get(name: &str, switch_val: Option<&str>)
    -> Result<i64, KnobError>;
pub fn get_as<T>(name: &str, switch_val: Option<&str>)
    -> Result<T, KnobError>
where T: TryFrom<i64>;

pub fn eval_or_panic(... ) -> bool;
pub fn get_or_panic(... ) -> i64;
```

### Semantics

| Function | Contract | Errors |
|---|---|---|
| `init` | Load defaults, parse CLI overrides, publish v0, start source worker | Invalid/missing defaults or CLI; duplicate init |
| `eval` | Evaluate bool using the exact precedence in Section 8 | Uninitialized; missing; wrong type; invalid name |
| `get` | Return integer default or CLI integer override | Uninitialized; missing; wrong type; invalid name |
| `get_as<T>` | Call `get`, then checked conversion | All `get` errors; conversion overflow/failure |
| `*_or_panic` | Crash on any `KnobError`; message includes name and category | Never returns an error |

**Fail-fast default.** Application integration SHOULD use panic wrappers for statically named knobs so typos and config drift surface immediately. Libraries that cannot abort the process MAY propagate typed errors, but MUST NOT silently substitute a call-site default.

### Error model

```
enum KnobError {
  Uninitialized,
  InvalidName,
  MissingKnob { name },
  WrongType { name, expected, actual },
  ConversionFailed { name, target },
}
```

Lookup failures MUST NOT be negatively cached. This preserves the sourced behavior and avoids stale “missing” results after a later snapshot adds the knob. [S1, S3, S6]

## 5. API surface (bindings and injection)

**SOURCED** SOURCED **FILLED BLANK** FILLED BLANK

### Python binding

```
def check(name: str,
          hash_val: str | None = None,
          switch_val: str | None = None) -> bool: ...

def get(name: str, switch_val: str | None = None) -> int: ...
```

**SOURCED** SOURCED Public usage exposes `pyjk.check()`; the production binding fails with `SystemError` where Configerator is unavailable. [S7]

**FILLED BLANK** FILLED BLANK Clone exception mapping: `MissingKnob → KeyError`; `WrongType/InvalidName → ValueError`; `Uninitialized → SystemError`; integer conversion overflow → `OverflowError`. The binding releases no GIL around reads because evaluation is in-memory and non-blocking.

### Test-only client

```
pub struct TestKnobs { snapshot: Snapshot }
impl TestKnobs {
  pub fn from_defs(defs: Map<String, KnobDef>) -> Self;
  pub fn set_thread_local(self) -> TestGuard;
}
// Dropping TestGuard restores the prior thread-local client.
```

A thread-local in-memory implementation is publicly described for tests. The guard-based nesting behavior is a clone choice that prevents cross-test leakage. [S1]

### Initialization options

```
struct InitOptions {
  defaults_path: PathBuf,            // optional; empty starts with no knobs
  source: Box<dyn KnobSource>,       // file source by default
  source_id: String,
  refresh_alert_after: Duration,     // default 5 minutes
  max_knobs: usize,                  // default 100_000
  allow_test_disable_env: bool,      // default false in production
}
```

### Global vs injected access

The package MAY expose global functions for source compatibility, but core logic MUST live on a `Client` instance. Application code SHOULD depend on a narrow `Knobs` interface where testing warrants injection. Only the global adapter owns process-level initialization and CLI parsing.

**Do not expose mutable maps.** Neither bindings nor test helpers may return the live backing map. Diagnostics may return copied metadata and counts, never a mutable snapshot reference.

## 6. Read path, step by step

**SOURCED** SOURCED **FILLED BLANK** FILLED BLANK

1. **Validate initialization and name.** Reject an uninitialized client or malformed name before reading state.

2. **Acquire CLI override pointer.** Perform one acquire-load. If unset/null, continue without map access. If set and the name exists, return the typed override immediately.

3. **Acquire current snapshot.** Load the atomic reference once. Hold that exact reference for the entire call.

4. **Lookup by name.** Perform one immutable-map lookup. Missing means `MissingKnob`; do not consult a call-site default and do not cache the failure.

5. **Type-check.** `eval` requires `Bool`; `get` requires `Int`.

6. **Evaluate locally.** Bool logic follows Section 8. Integer `get` returns `default`. The integer `switch_val` argument is accepted for API compatibility and ignored in v1.

7. **Increment a low-cardinality read counter.** Use type/outcome/path labels only. Do not label by knob name.

8. **Release snapshot reference.** The call returns a scalar; no caller-visible object retains internal state.

```
fn eval(name, hash_val, switch_val) -> Result<bool> {
  validate_name(name)?;
  if let Some(v) = overrides.load_acquire().and_then(|m| m.get(name)) {
    return v.as_bool_or_wrong_type(name);
  }
  let snap = current.load();              // one atomic acquire
  let def = snap.knobs.get(name).ok_or(MissingKnob(name))?;
  let out = match def {
    Bool { default, overrides, rollout_pct } =>
      eval_bool(name, *default, overrides, *rollout_pct,
                hash_val, switch_val),
    Int { .. } => Err(WrongType(name, "bool", "int")),
  };
  metrics.read("bool", outcome(out), "snapshot").inc();
  out
}
```

### Allowed in hot path

Atomic loads, immutable lookups, scalar comparisons, SHA-256 for rollout-enabled reads, and non-blocking metric increments.

### Forbidden in hot path

RPC, filesystem, locks, retries, parsing, snapshot cloning, dynamic config fetches, synchronous log writes, or per-read tracing spans.

The atomic in-process pattern is sourced; exact precedence, validation, and instrumentation are filled. No latency number is asserted. [S1-S4]

## 7. Update path, step by step

**SOURCED** SOURCED **FILLED BLANK** FILLED BLANK

1. **Start from a valid version-0 state.** Parse and validate `just_knobs.json`; publish its immutable map before starting the worker.

2. **Await source event.** The single background worker consumes `KnobSource.watch()`. Reads never await this stream.

3. **Read a complete candidate.** File events trigger a full file read after atomic replacement; gRPC messages carry a full snapshot payload.

4. **Parse off-path.** Decode strict JSON/protobuf with size and count limits. Reject unknown fields and malformed values.

5. **Validate version.** Require a positive version greater than the last accepted source version.

6. **Validate definitions.** Check names, types, group keys, rollout range, integer bounds, and duplicate/type conflicts.

7. **Merge over baked defaults.** Clone/build a fresh immutable map; a live key replaces the entire default definition.

8. **Atomically publish.** Store the new snapshot with release semantics. Readers see old or new, never partial state.

9. **Record success.** Increment swaps, set last-success monotonic time, update version metadata, and clear degraded status.

10. **Continue watching.** The worker stays alive until explicit client shutdown.

```
loop {
  match source.next().await {
    Ok(raw) => match build_candidate(defaults, raw, accepted_version) {
      Ok(next) => {
        current.store_release(Arc::new(next));
        accepted_version = next.version;
        mark_success();
      }
      Err(e) => mark_refresh_failure(e), // no swap
    },
    Err(e) => {
      mark_refresh_failure(e);            // keep last-known-good
      source.reconnect_with_backoff().await;
    }
  }
}
```

### Backoff and recovery

For retryable source errors, use full-jitter exponential backoff: random delay in `[0, min(30s, 250ms × 2^attempt)]`. Reset attempt count after the next valid swap. Parse/schema errors are not retried until a new source event or file change arrives.

**Intentional divergence.** The public test-oriented cached-config updater exits after a refresh error. This clone instead logs, alerts, reconnects, and preserves last-known-good. That behavior is a FILLED BLANK, not a claim about Meta production. [S2]

## 8. Evaluation semantics

**SOURCED** SOURCED **FILLED BLANK** FILLED BLANK

### Boolean precedence (normative)

1. If a per-process CLI override exists for `name`, return it. A wrong-typed override is an error.

2. Look up `name` in the acquired snapshot. Missing is an error/panic according to the caller API.

3. If `switch_val` is present and exactly matches a key in `overrides`, return that value.

4. If `rollout_pct` is set and `hash_val` is present, compute the stable bucket and return `bucket < rollout_pct`.

5. Otherwise return `default`.

The sourced evidence establishes the API parameters and per-group bool overrides, but not their exact algorithm. The precedence and hash below are filled choices. [S1, S8]

### Stable hash (normative)

```
input  = UTF8(knob_name) || 0x00 || UTF8(hash_val)
digest = SHA256(input)
u64    = unsigned_big_endian(digest[0..8])
bucket = u64 % 100
return bucket < rollout_pct
```

- **Domain separation:** the zero byte prevents ambiguous concatenations.

- **Stickiness:** identical `(knob_name, hash_val)` always yields the same bucket across hosts, processes, languages, and restarts.

- **Independence:** including the knob name gives the same entity different cohorts for different knobs.

- **Boundary behavior:** 0 enables nobody; 100 enables everybody when a hash is supplied.

- **No hash supplied:** skip rollout and return `default`. Do not hash an empty string implicitly.

### Integer semantics

`get(name, switch_val)` checks CLI overrides, then returns the integer definition's `default`. The `switch_val` parameter is reserved and ignored in v1 because the specified integer model has no group overrides. `get_as<T>` performs a checked conversion; it never truncates or wraps.

### Worked example

| Input | Definition | Result |
|---|---|---|
| `switch=employees` | override `employees=true` | `true` |
| `hash=user-42` | `rollout_pct=10` | `bucket(name,user-42)<10` |
| no switch/hash | `default=false` | `false` |

## 9. Failure semantics

**SOURCED** SOURCED **FILLED BLANK** FILLED BLANK

| Failure | Required behavior | State after failure |
|---|---|---|
| Defaults missing/invalid | Fail `init()`; process integration SHOULD abort startup | No initialized client |
| Missing knob read | Return `MissingKnob` or panic wrapper; no fallback; no negative cache | Snapshot unchanged |
| Wrong-type read | Return `WrongType` or panic wrapper | Snapshot unchanged |
| Invalid CLI override | Fail startup with exact offending name/value | No initialized client |
| Source unavailable at startup | Serve version-0 defaults; log and increment refresh failure | Degraded, usable |
| Malformed update | Reject whole update; log category; increment failure | Keep last-known-good |
| Stale/duplicate version | Ignore; count as stale update; do not alert unless sustained | Keep last-known-good |
| Source disconnect | Reconnect with jittered backoff; alert on staleness threshold | Keep last-known-good |
| Metric backend failure | Drop metric locally; never fail a knob read or swap | Configuration unaffected |

### Fail-fast rationale

A public post-incident change cites SEV S667796, where a binary referenced a nonexistent knob. Public guidance says safe defaults belong in `just_knobs.json` and warns that lookup failures are not cached. The clone therefore treats missing names as configuration/programming defects, never as “false” or zero. [S1, S3, S6]

### Last-known-good rationale

Distribution is control-plane work; serving is data-plane work. A transport outage must not inject synchronous fetches into reads or erase known-good state. This is a clone-specific availability choice. An operator can tell whether state is stale through the gauge and alerts in Section 12.

### Logging rules

- Refresh failures are rate-limited and structured: source, category, attempted version, current version, retryability.

- Do not log `hash_val` or `switch_val`; they may carry user or tenant identifiers.

- Missing/wrong-type panics include the knob name but not snapshot contents.

- One failed update produces one failure event, not one event per reader.

## 10. Distribution backends

**SOURCED** SOURCED **FILLED BLANK** FILLED BLANK

### `KnobSource` contract

```
trait KnobSource: Send + Sync {
  fn source_id(&self) -> &str;
  fn watch(&self) -> Pin<Box<dyn Stream<Item = SourceEvent> + Send>>;
}

enum SourceEvent {
  Snapshot { bytes: Bytes },   // complete versioned snapshot
  RetryableError(SourceError),
  FatalError(SourceError),
}
```

The source emits complete snapshots, not patches. It MUST preserve event order per connection. The client, not the adapter, owns parsing, version checks, merging, metrics, backoff, and publication.

### Default: file watcher

1. Watch the parent directory, not only the inode, so atomic rename is observed.

2. Publishers write a sibling temporary file, `fsync` it, then atomically rename over the target.

3. Coalesce events for 100 ms; then open and read the entire target with a configurable 16 MiB limit.

4. Emit bytes. The shared builder validates schema/version. Partial or malformed files are rejected without a swap.

5. On watcher overflow or recreation, force a full reread.

### Optional: gRPC streaming backend

```
service KnobDistributor {
  rpc Watch(WatchRequest) returns (stream SnapshotEnvelope);
}
message WatchRequest { string client_id = 1; uint64 after_version = 2; }
message SnapshotEnvelope {
  uint64 version = 1;
  google.protobuf.Timestamp published_at = 2;
  bytes json_snapshot = 3;
  bytes sha256 = 4;
}
```

The client verifies the payload SHA-256 before parsing, resumes after its accepted version, enforces message size limits, and uses TLS plus deployment-specific authentication. A reconnect may resend a duplicate snapshot; monotonic version checks make this harmless.

**Configerator boundary.** Public code says Meta's internal client is Configerator-based and Python fails where Configerator is unavailable. This clone does not implement or emulate Configerator. The file and gRPC adapters are explicit substitutes. [S1, S7, S9-S10]

## 11. Overrides and debugging

**SOURCED** SOURCED **FILLED BLANK** FILLED BLANK

### Per-process CLI overrides

```
app --just-knob checkout:killswitch_new_path=true \
    --just-knob search:max_results=100
```

- The flag is repeatable. Parse bool only from lowercase `true`/`false`; otherwise parse signed base-10 `i64`.

- Duplicate names are startup errors. No last-one-wins behavior.

- Overrides are immutable after startup. An unset override pointer remains null, so normal reads pay one acquire-load and branch.

- An override shadows both baked defaults and live snapshots. It remains active across swaps.

- Startup logs overridden names and types, but not values. Expose the count in diagnostics.

The CLI pattern and cheap unset path are sourced; parsing, duplicate policy, and logging are filled. [S4]

### Alternate config path

`--just-knobs-config-path PATH` selects the file-watcher backend and replaces any other configured source. Combining it with a gRPC endpoint is a startup error. The named path itself is publicly evidenced; the exclusivity rule is filled. [S4]

### Deterministic test disable mode

```
PYTORCH_DISABLE_JUSTKNOBS=1
```

**SOURCED** SOURCED TorchRec uses this variable so tests do not depend on live JustKnobs values. [S11]

**FILLED BLANK** FILLED BLANK When explicitly allowed by `InitOptions`, value `1` prevents the source worker from starting and freezes version-0 baked defaults. CLI overrides still apply. In production mode the variable is ignored with a warning, preventing ambient environment drift.

### Kill-switch lifecycle

1. **Introduce:** add a safe code-versioned default and a `killswitch_*` bool around the risky path.

2. **Roll out:** use group overrides or percentage rollout; monitor product and system health.

3. **Revert:** flip the live bool to the safe state; propagation is asynchronous but reads require no restart.

4. **Stabilize:** once behavior is fully rolled out and validated, make it unconditional.

5. **Remove:** delete branch, knob read, live definition, and baked default in coordinated code/config changes.

Public commits show kill-switch naming, safe rollout/easy revert, and manual removal after full rollout. No automated dead-flag detector is implied. [S5-S7]

## 12. Observability

**SOURCED** SOURCED **FILLED BLANK** FILLED BLANK

The public cached-config updater increments a refresh-failure counter intended to alert on-call. The full metric contract below is filled for the clone. [S2]

| Metric | Type / labels | Meaning |
|---|---|---|
| `jk_reads_total` | Counter: `type`, `outcome`, `path` | All API reads; path is snapshot or CLI |
| `jk_snapshot_swaps_total` | Counter: `source` | Validated snapshots published |
| `jk_refresh_failures_total` | Counter: `source`, `reason` | Read, parse, schema, version, or transport failure |
| `jk_stale_updates_total` | Counter: `source` | Duplicate or lower source versions ignored |
| `jk_snapshot_staleness_seconds` | Gauge: `source` | Monotonic time since last successful swap; since init before first swap |
| `jk_snapshot_knobs` | Gauge: `type` | Current bool/int definition counts |
| `jk_source_connected` | Gauge: `source` | 1 while source stream is healthy, else 0 |

### Cardinality and privacy

- MUST NOT attach knob name, `hash_val`, `switch_val`, file path, client ID, or snapshot version as metric labels.

- Allowed enums are bounded: `type={bool,int}`; `outcome={ok,missing,wrong_type,conversion}`; `path={snapshot,cli}`; `reason={io,transport,parse,schema,stale,oversize,checksum}`.

- Snapshot version and source ID MAY appear in a protected diagnostics endpoint or structured log, not in labels.

### Operational alerts

### Refresh failure

Alert when failures increase and no successful swap follows within the configured window.

### Staleness

Alert when staleness exceeds `refresh_alert_after`; severity depends on environment and kill-switch expectations.

### Missing reads

Page only if the process does not already abort; otherwise aggregate crash reason outside this library.

### No read-latency SLO

Benchmark locally, but do not market a public JustKnobs latency figure; none was found.

### Health snapshot

```
{ "initialized": true, "source_connected": true,
  "accepted_version": 42, "staleness_seconds": 1.8,
  "bool_knobs": 812, "int_knobs": 64, "cli_overrides": 1 }
```

## 13. Testing plan: semantic matrix

**FILLED BLANK** FILLED BLANK

All tests run against the instance API first, then through the global adapter and Python binding. Golden vectors are language-neutral JSON committed beside the implementation.

| Case | Setup | Expected |
|---|---|---|
| Bool default | No override, rollout null | Returns default |
| Group match | `switch_val` exact key | Returns group value |
| Group miss | Unknown switch, no rollout hash | Returns default |
| Group precedence | Group match plus rollout | Group value wins |
| Rollout 0 | Any non-null hash | False |
| Rollout 100 | Any non-null hash | True |
| Rollout no hash | Percentage set; hash null | Returns default |
| CLI bool | Bool process override | Override wins over all |
| CLI int | Int process override | Override wins |
| Wrong CLI type | Bool read of int override | `WrongType` |
| Missing knob | Absent from both layers | Error; panic wrapper aborts |
| Wrong type | `eval` int / `get` bool | `WrongType` |
| Checked conversion | i64 outside target range | `ConversionFailed` |
| Source overlay | Live key replaces default key | Whole definition replaced |
| Source omission | Live snapshot omits default key | Baked default retained |
| Unknown field | Extra JSON property | Whole update rejected |
| Stale version | Version ≤ accepted | No swap; stale counter +1 |

### Hash-stickiness golden test

- Compute SHA-256 over `UTF8(name) || 0x00 || UTF8(hash)`.

- Assert the first eight bytes are interpreted unsigned, big-endian.

- Commit at least 20 input/digest-prefix/bucket vectors generated once by an independent tool.

- Run the same vectors in Rust, Go, Java, and Python implementations.

- Repeat across process restarts and host architectures; results MUST be identical.

- Assert changing only the knob name changes the cohort for most vectors.

**Acceptance.** 100% of semantic matrix and golden-vector tests pass; no implementation substitutes platform hash functions, which may be randomized or version-dependent.

## 13. Testing plan: concurrency and faults

**SOURCED** SOURCED **FILLED BLANK** FILLED BLANK

### Concurrency torture test

1. Create snapshot A with 10,000 knobs whose values encode generation A; snapshot B encodes generation B.

2. Run at least 64 reader threads. Each reader acquires one snapshot and validates a set of related keys from that single reference.

3. Run one updater swapping A/B at least 100,000 times; periodically force scheduler yields.

4. Assert no reader observes a partially built map, use-after-free, panic, mixed generation within one acquired snapshot, or blocked lock profile.

5. Run under race detector / ThreadSanitizer where supported; include one 10-minute soak in continuous integration nightly.

### Failure injection

| Injected fault | Assertion |
|---|---|
| Source unavailable before first live update | Version-0 defaults serve; failure metric and staleness advance |
| Truncated/invalid JSON | Reject; last-known-good remains byte-for-byte identical |
| Oversized file/message | Reject before allocation exceeds configured cap |
| Bad checksum (gRPC) | Reject; checksum reason increments |
| Watcher overflow / file replace storm | Coalesce and full reread; latest valid version eventually wins |
| Disconnect during stream | Reads continue; reconnect follows bounded full-jitter backoff |
| Metric exporter blocked | Reads and swaps remain non-blocking |
| Repeated missing reads | Every read fails; later valid snapshot immediately resolves it |

### Performance verification

- Microbenchmark the unset-override path, map hit, group hit, rollout hash, missing name, and atomic swap separately.

- Use allocation instrumentation to assert zero steady-state allocations for successful `get` and non-rollout `eval`.

- Profile contention with updater active; no mutex or file/network stack may appear in reader traces.

- Report observed distributions for the clone only. Do not compare them to an unsourced JustKnobs latency claim.

### Compatibility and parser tests

Fuzz both JSON formats; property-test parse → serialize → parse; verify unknown-field rejection; test Unicode byte limits; verify atomic rename handling on each supported operating system; and test protobuf compatibility with unknown fields and duplicate messages.

## 14. Build milestones (1-4)

**FILLED BLANK** FILLED BLANK

### Milestone 1 - model, schema, defaults

Implement `KnobDef`, `Snapshot`, strict JSON decoders, name validation, and defaults loading.

**Acceptance criteria**

- Both example files parse to the expected immutable map.

- Every schema violation in Section 4 rejects the complete input.

- Missing/invalid defaults make initialization fail.

- Fuzz target runs without crash or unbounded allocation.

### Milestone 2 - instance read API

Implement `Client.eval/get/get_as`, error types, exact bool precedence, and SHA-256 bucketing against a fixed in-memory snapshot.

**Acceptance criteria**

- Semantic matrix passes for defaults, groups, rollout boundaries, missing names, and wrong types.

- Golden hash vectors pass byte-for-byte.

- No call-site fallback API exists.

- Successful non-rollout reads allocate zero memory after initialization.

### Milestone 3 - atomic publication

Add the language-specific atomic snapshot holder and one updater-owned swap operation.

**Acceptance criteria**

- Readers acquire exactly one snapshot per call.

- Concurrency torture and race detector pass.

- Readers observe only complete old or new snapshots.

- Retired snapshots are eventually reclaimed with no use-after-free.

### Milestone 4 - process overrides and global adapter

Parse repeatable CLI flags, install immutable override state, expose global compatibility functions, and add panic wrappers.

**Acceptance criteria**

- Unset override path is one acquire-load and branch.

- Duplicate/invalid overrides fail startup.

- Overrides persist across snapshot swaps and win precedence.

- Panic messages identify name and error category without values.

## 14. Build milestones (5-9)

**FILLED BLANK** FILLED BLANK

### Milestone 5 - updater and file source

Implement `KnobSource`, directory watcher, atomic-file protocol, parser/builder pipeline, monotonic versions, backoff, and last-known-good.

**Acceptance criteria**

- Valid atomic replacement produces exactly one accepted swap.

- Malformed, stale, partial, and oversized updates never change state.

- Watcher overflow triggers a full reread.

- Reads continue during every injected source failure.

### Milestone 6 - observability and health

Add bounded counters, staleness, source health, structured logs, and diagnostics snapshot.

**Acceptance criteria**

- Metric labels match the bounded enums exactly.

- No name/hash/switch value appears in metrics or refresh logs.

- Staleness resets only after a successful swap.

- Blocked exporter cannot block reads or swaps.

### Milestone 7 - test modes and Python

Add thread-local test guards, deterministic disable mode, and Python bindings.

**Acceptance criteria**

- Nested test guards restore prior state.

- Disable mode starts no source and serves defaults plus CLI.

- Python exception mapping matches Section 5.

- Python golden vectors match core implementation.

### Milestone 8 - optional gRPC source

Implement authenticated streaming, checksums, resume version, size limits, and reconnect.

**Acceptance criteria**

- Duplicate delivery is harmless.

- Bad checksum/oversize never reaches parser.

- Reconnect resumes after accepted version.

- Transport integration tests pass under forced disconnects.

### Milestone 9 - release readiness

**Acceptance criteria**

- All semantic, race, fault, fuzz, binding, and transport suites pass.

- Operational dashboard and staleness alert are exercised in staging.

- A kill-switch drill flips, observes propagation, reverts, and removes a sample flag.

- Public API and JSON schema are versioned and documented.

## 15. Explicit non-goals

**SOURCED** SOURCED **FILLED BLANK** FILLED BLANK

| Excluded capability | Why excluded | Extension seam |
|---|---|---|
| Management UI / control plane | No public JustKnobs design; not needed for client correctness | Any publisher that emits valid snapshots |
| Audit log | No public JK-specific evidence; transport is deliberately read-only | Implement upstream of `KnobSource` |
| Ownership metadata | Not in the runtime evaluation model | Add publisher-side catalog, not hot-path fields |
| Expiry / automated cleanup | Public evidence shows manual code removal, not automation | Offline lint against source and config |
| Multi-knob transactions | Each snapshot is atomic, but application reads are independent | Acquire one snapshot and expose a future batch API |
| Attribute-rule language | JK's exact rules are not public; Gatekeeper must not be imported by assumption | Future versioned `KnobDef` variant |
| Meta Configerator client | Internal dependency is not publicly implementable | Write a private `KnobSource` adapter if available |
| Per-read network lookup | Violates the core in-process hot-path architecture | None; architecturally forbidden |

### Gatekeeper disclaimer

Meta's 2015 Configerator paper documents Gatekeeper's ordered restraints and deterministic percentage sampling. Those mechanics are useful historical context but are not evidence of JustKnobs' algorithm or lineage. This clone's group-before-rollout precedence and SHA-256 bucket are independent design choices. [S9]

### Security boundary

This client consumes snapshots; it does not decide who may publish them. Deployments MUST secure file ownership/permissions or gRPC authentication and TLS. The client MUST enforce input limits and strict parsing, but authorization, code review, approvals, and human audit belong to the upstream publisher.

### Completion definition

The implementation is complete when Milestone 9 passes and an application can start from safe code-versioned defaults, consume a live file snapshot, serve lock-free reads during repeated swaps and outages, apply exact group/rollout/CLI precedence, expose health, and fail loudly on missing knobs.

All exclusions above are deliberate for v1. Adding one requires a versioned design and must not change existing read semantics accidentally.

## Appendix A. Reference module map

**FILLED BLANK** FILLED BLANK

```
justknobs/
  model          KnobDef, Snapshot, validation
  config         strict JSON schema + defaults loader
  client         eval/get/get_as, errors, panic wrappers
  hashing        SHA-256 bucket + golden vectors
  snapshot_ref   ArcSwap / platform atomic abstraction
  overrides      CLI parser + immutable override pointer
  updater        KnobSource consumer, merge, backoff, swap
  source/file    directory watcher + bounded full read
  source/grpc    optional stream, auth, checksum, resume
  metrics        counters, gauges, health snapshot
  testing        thread-local client + failure fakes
  python         pyjk-compatible binding
```

### Core invariants

1. **One read, one snapshot.** No read mixes definitions across versions.

2. **Publish only valid state.** Parse, validate, and merge finish before swap.

3. **No hidden defaults.** Every fallback is explicit in `just_knobs.json`.

4. **No hot-path I/O.** Distribution and observability exporters stay asynchronous.

5. **No partial update.** A candidate snapshot is accepted or rejected as a whole.

6. **No negative cache.** Missing lookups remain visible and recover after a valid swap.

7. **Bounded telemetry.** No per-knob metric cardinality and no user identifiers.

### Recommended code-review checklist

- Does this change add any lock, allocation, I/O, retry, or log to the read path?

- Does every reader retain one acquired snapshot until evaluation completes?

- Can malformed input alter the currently published map?

- Does a new field require a schema-version change or golden test update?

- Are hash changes explicitly versioned and covered across languages?

- Can any metric label grow with knob, user, tenant, version, or path cardinality?

- Is a new default in the code-versioned defaults file rather than at the call site?

- Does a completed rollout include a plan to remove the knob and dead branch?

**Implementation warning.** Do not replace SHA-256 with a language runtime's default hash. Many runtimes randomize hashes per process, breaking cohort stickiness across restarts and hosts.

## Appendix B. Public sources

**SOURCED** SOURCED

1. **S1.** rust-shed, *justknobs_stub/src/lib.rs* - API, implementations, Configerator-based internal client, uncached failures. [github.com/facebookexperimental/rust-shed/.../lib.rs](https://github.com/facebookexperimental/rust-shed/blob/main/shed/justknobs_stub/src/lib.rs)

2. **S2.** rust-shed, *cached_config.rs* - ArcSwap map, watcher, whole-map swap, bool/int shape, refresh-failure metric. [github.com/facebookexperimental/rust-shed/.../cached_config.rs](https://github.com/facebookexperimental/rust-shed/blob/main/shed/justknobs_stub/src/cached_config.rs)

3. **S3.** rust-shed commit - panic on failed JustKnobs read after SEV S667796. [github.com/facebookexperimental/rust-shed/commit/c1e979...](https://github.com/facebookexperimental/rust-shed/commit/c1e979e7ebee7809a6bfac6ee50366259a9cf389)

4. **S4.** Sapling commit - per-process `--just-knob` overrides and cheap unset path. [github.com/facebook/sapling/commit/3788138...](https://github.com/facebook/sapling/commit/3788138afe7ba271d794b646eb4416d1ebcd59a7)

5. **S5.** Sapling rollout guidance - per-client/repo rollout and instant rollback. [github.com/facebook/sapling/.../config_rollout_safety.md](https://github.com/facebook/sapling/blob/HEAD/eden/.llms/rules/config_rollout_safety.md)

6. **S6.** Sapling Rust guidance - defaults in `just_knobs.json`; missing reads are expensive. [github.com/facebook/sapling/.../rust_unwrap_safety.md](https://github.com/facebook/sapling/blob/HEAD/eden/.llms/rules/rust_unwrap_safety.md)

7. **S7.** TorchRec commit - `pyjk.check()`, Configerator-unavailable failure, kill-switch usage. [github.com/meta-pytorch/torchrec/commit/e9f1bb0...](https://github.com/meta-pytorch/torchrec/commit/e9f1bb00cdd950c8c1a3bd97db3b2711e81a0ae4)

8. **S8.** HHVM commit - global bool plus zero per-group switch overrides; dead-flag removal. [github.com/facebook/hhvm/commit/ceca2a0...](https://github.com/facebook/hhvm/commit/ceca2a08396fd153a4712ac9c1686a2448f6ee25)

9. **S9.** *Holistic Configuration Management at Facebook*, SOSP 2015 - Configerator and Gatekeeper context. [github.com/jwolski/papers/.../holistic_configuration_management_at_facebook.pdf](https://github.com/jwolski/papers/raw/refs/heads/master/holistic_configuration_management_at_facebook.pdf)

10. **S10.** Meta Engineering, *Location-Aware Distribution: Configuring servers at scale* (2018) - subscription/push distribution context. [engineering.fb.com/.../location-aware-distribution-configuring-servers-at-scale/](https://engineering.fb.com/2018/07/19/data-infrastructure/location-aware-distribution-configuring-servers-at-scale/)

11. **S11.** TorchRec commit - `PYTORCH_DISABLE_JUSTKNOBS=1` for deterministic tests. [github.com/meta-pytorch/torchrec/commit/d130d6e...](https://github.com/meta-pytorch/torchrec/commit/d130d6e3cc39ccd6edb894d03b0648a9abc37b25)

12. **S12.** TorchRec commit - removing fully rolled-out JustKnobs gates. [github.com/meta-pytorch/torchrec/commit/20ed1bd...](https://github.com/meta-pytorch/torchrec/commit/20ed1bd996c2c13bc9fb8ad5e2689d8f04c473b8)

**Source caveat.** No dedicated public JustKnobs paper, architecture post, or management-console documentation was found. Accordingly, this specification treats control-plane internals, exact evaluation semantics, scale, and latency as unknown and labels clone decisions explicitly.

### Traceability

The strongest direct evidence is S1-S8 and S11-S12. S9-S10 describe Meta's broader configuration ecosystem and are used only for context. None of the sources establishes a JustKnobs-Gatekeeper relationship.
