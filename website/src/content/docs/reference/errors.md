---
title: "Errors and Failures"
---

Detent separates **read errors**, which a caller sees on a single read, from
**update rejections**, which the background updater handles without ever
disturbing readers.

---

## Read Errors

Every read returns `Result<T, KnobError>`. `KnobError` has a `category`, the
knob `name`, a human-readable `detail`, and `what()` to format all three.

| Category | Cause |
| --- | --- |
| `Uninitialized` | Reading through `detent::global` before `init()` |
| `InvalidName` | The name doesn't follow the [naming rule](/Detent/reference/knobs/#knob-names) |
| `MissingKnob` | No CLI override, and the name is in neither the live snapshot nor the baked defaults |
| `ConversionFailed` | `get_as<T>` on a value that doesn't fit `T`, such as `get_as<bool>` on a value other than 0 or 1 |

The `_or_panic` helpers abort the process on any of these, with a message that
names the knob and category.

### Why There's No Call-Site Default

Detent's C++ API deliberately has no `get(name, /*fallback=*/0)`. A
fallback at the call site hides the two most common configuration bugs, a typo
in the knob name and a knob that was never published, behind a value that looks
plausible. Instead:

1. Put the safe default in your **baked defaults**, next to the code.
2. Use the `_or_panic` helpers for knob names written in your code, so a typo
   surfaces the first time the line runs, in tests or canary, not as a silent
   behavior change in production.
3. Use the `Result` forms where aborting isn't acceptable, such as in a library
   that runs inside someone else's process, and handle the error explicitly.

Shell scripts are the exception: `detent get --default VALUE` exists because a
script has no baked defaults file of its own.

Lookup failures are never cached. If a knob is missing now and published a
second later, the next read finds it.

---

## Update Rejections

When a candidate update fails, the client keeps serving its last known good
snapshot and records the reason:

| Reason | Cause |
| --- | --- |
| `Io` | The source file couldn't be read |
| `Transport` | The source couldn't be reached, or a cluster had no quorum decision in time |
| `Parse` | Invalid JSON or encoding |
| `Schema` | Valid JSON that violates the [snapshot schema](/Detent/reference/snapshot-format/) |
| `Version` | Missing, non-positive, stale or duplicate version |
| `Oversize` | Too many knobs or too many bytes |
| `Checksum` | A payload failed its integrity check |

---

## Failure Behavior

| Failure | Behavior | State afterwards |
| --- | --- | --- |
| Defaults missing or invalid | The `Client` constructor throws; abort startup | No client |
| Invalid CLI override | The constructor throws, naming the offending pair | No client |
| Source unavailable at startup | Serve baked defaults as version `0`; retry in the background | Degraded, usable |
| Malformed update | Reject the whole update; count and log the category | Last known good |
| Stale or duplicate version | Ignore; count as stale | Last known good |
| Source disconnects | Reconnect with full-jitter backoff; staleness grows | Last known good |
| Missing knob read | Return `MissingKnob` (or abort with `_or_panic`) | Unchanged |
| Value that doesn't fit `get_as<T>` | Return `ConversionFailed` | Unchanged |

The guiding rule: distribution is control-plane work, serving is data-plane
work. A distribution failure never adds I/O to a read and never erases a
known-good configuration. Operators see the problem through
[staleness and failure counters](/Detent/reference/observability/), not through
application errors.

---

## Authority Errors

The durable authority reports each transaction's outcome, such as `applied` or
`conflict`, in its response. Some failures return a status code and a JSON
body instead, such as `503` with `{"error":"outcome_unknown"}`. The
[Authority API](/Detent/reference/http-api/authority/#outcomes) documents both.
