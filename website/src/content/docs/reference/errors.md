---
title: "Errors and Failures"
---

A `detent::Reader` fails loudly in one place, opening. Every lookup after that
returns the caller's fallback on any error, and `lookup` reports the status
that explains it. Distribution failures stay in the agent and never disturb
readers.

---

## Opening

`detent::Reader(store, publisher_uid, namespaces)` throws `detent::ReaderError`
when the open fails; `status()` gives the `detent_status` and `what()` reads
`detent_open: <status name>`. Python raises `detent.ReaderError` with `.status`,
and Rust's `Reader::open` returns `Err(ReaderError { status })`.

A store that doesn't exist yet opens: lookups return the fallback with
`UNAVAILABLE` until a generation appears. An untrusted directory, such as one
that is group- or world-writable or not owned by `publisher_uid`, is
`UNTRUSTED`.

---

## Lookup Statuses

`eval` and `get` return the fallback for any status but `OK`; `lookup` returns
the status with the value. The C names are `DETENT_<NAME>` in
`include/detent/reader.h`.

| Status | Cause |
| --- | --- |
| `OK` | The value was read |
| `MISSING` | The generation doesn't hold the knob |
| `WRONG_TYPE` | `eval` on an int knob, or `get` on a bool |
| `INVALID_NAME` | The name doesn't follow the [naming rule](/Detent/reference/knobs/#knob-names) |
| `NOT_OPENED` | The namespace is outside the handle's list |
| `SOURCE_FAILURE` | The generation failed verification |
| `UNAVAILABLE` | No prepared generation for this namespace yet |
| `UNTRUSTED` | Ownership, mode, ACL or ancestry refused |
| `INVALID_ARGUMENT` | A malformed argument |
| `NO_MEMORY` | An allocation, or the reader's memory budget, refused |
| `IO` | A system call failed |

Any value other than `OK`, known or not, is an error: a newer library may add
statuses. Python maps an unknown one to an `UNKNOWN_<n>` member of `Status`,
and Rust to `Status::Unknown(n)`.

:::hint[A fallback can hide a typo]{.caution}
A misspelled knob name returns the fallback silently. Check `lookup`'s status
in tests and canaries for the names your code reads, so a typo or a knob that
was never published shows up as `MISSING` before it ships.
:::

Lookup failures are never cached. If a knob is missing now and published a
second later, the next read after the agent publishes finds it.

---

## The Shell

`detent get` prints the value and exits `0`, or prints `ERR <reason>` to stderr
and exits `1`. `--default VALUE` prints `VALUE` instead for a missing knob, a
wrong type or no generation yet; it doesn't mask a generation that fails
verification, or `no source configured`. See
[detent](/Detent/reference/cli/detent/).

---

## Failure Behavior

| Failure | Behavior | State afterwards |
| --- | --- | --- |
| Untrusted store directory | The open throws `UNTRUSTED` | No reader |
| No generation yet | Lookups return the fallback with `UNAVAILABLE` | Usable, retried at most once per 200 ms |
| A new generation fails to attach | Keep answering from the installed view | Last known good |
| The agent loses the authority | The agent retries; readers keep the selected generation | Last known good |
| Missing knob read | Return the fallback with `MISSING` | Unchanged |
| Wrong-type read | Return the fallback with `WRONG_TYPE` | Unchanged |

The guiding rule: distribution is control-plane work, serving is data-plane
work. A distribution failure never adds I/O to a warm read and never erases a
known-good configuration.

---

## Authority Errors

The durable authority reports each transaction's outcome, such as `applied` or
`conflict`, in its response. Some failures return a status code and a JSON
body instead, such as `503` with `{"error":"outcome_unknown"}`. The
[Authority API](/Detent/reference/http-api/authority/#outcomes) documents both.
