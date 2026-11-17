# rpc-lane-codec review: security

Commit: consensus:8db3ce393f63cfb42c9b16de70084ecf08e540af
Verdict: APPROVE
Date: 2026-11-17

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

This review also carries the correctness lens (call table, pools, TSan). The attacker is the
authenticated peer on the other end of a Bulk connection: buggy, stalled or compromised. Nothing
at this commit is on the wire, because `Lane` has no caller yet, so every hazard below is latent
until consensus-transport wires the lane.

## Evidence I relied on

- 261117-050434-95ef3e: the suite at 8db3ce3 passes.
- 261117-050435-5170c5: the rpc test (19 cases) and the corpus replay pass.
- 261117-050439-c01d2b: TSan passes the whole rpc test. That includes `pools-threads` (4
  threads, 20,000 operations each, 1,523 wakes) and `pipes` (two lanes on two loops, 12 streamed
  echo calls, every pool back to 0).
- 261117-050443-3fb2a3: ASan+UBSan pass the rpc test and the 13-input replay.
- 261117-050447-ac8101: the shipped fuzz job. It ran 20 minutes, 13,747,835 runs, about 11,400
  per second, with 0 findings and exit 0. Its corpus was lost with the job's checkout. The earlier
  crash (R11) is now `fuzz/corpus/rpc/crash-send-past-credit`, which the replay covers.
- The eight mutant jobs, 261117-050452-8737e6 through 261117-050526-8966c1, are all done, and
  each was killed (ctest exit 8). Each runs the whole rpc test, so on its own a kill does not say
  which case caught it. My probe below settles that.

## What I checked myself

The probe scripts are in `.scratch/review-rpc-lane-codec-security/` in the home checkout, built
through its `cmake/` wrapper.

- **Decoder against an independent oracle.** 261117-055231-96b757 runs `run_probe.sh oracle`.
  The oracle `valid()` is written from the text of D2 and D3 without reading `violation()`. It
  checked 20,000,000 mutated frames: header bytes, kinds 0-8, edge u64 values in `call`,
  `timeout_ms`, `window`, `total`, `offset` and `limit` (0, 1, 2^64-1, 2^64-2, 2^63, 65,536±1),
  edge words, truncations, and appends of 1 and 1,100 bytes. Frames up to 65,584 bytes were
  included. 3,897,437 were accepted, there were **0 mismatches**, and every accepted frame
  re-encoded byte-identically.
- **Decoder by reading.** All reads are bounded:
  - The header is checked to be at least 24 bytes before any field is read.
  - The kind's fixed fields are checked before they are read.
  - `journal::get32` and `get64` throw on truncation (journal@7c08540 `codec.h:50-63`).
  - Rule 7.3 is computed as `offset > total || n > total - offset` (`rpc.h:87`).
  - `received += n` cannot wrap, because `received == offset` and `n <= total - offset`.
  - `in.limit - f.offset` cannot underflow, because `received <= limit` always holds (`rpc.h:525`).
  - The Drain parity check and the allocated-id check are at `rpc.h:709`.
- **Mutants, case by case.** 261117-055301-75aff3 runs `mutants_by_case.sh`. For each of the
  eight, the case of its name passes on the clean header and fails with only that mutant applied.
  `forget-is-violation` and `id-at-call` die on an uncaught `ProtocolError`. The other six die on
  a value CHECK: lines 468, 445, 481, 491, 507 and 412. I compared each edit with the model's
  mutant (consensus-wt/xapiand/consensus-rpc-lane `docs/design/rpc-lane-model.py:31-40`, `:95`,
  `:117-147`, `:191`, `:236`, `:260`, `:280`). Each is the same fault. The one gap is that
  `grant-without-reserve` mutates only the caller's first grant, while the model's mutant applies
  to every grant (see N6).
- **Fuzz depth.** 261117-055239-13601d first fuzzes 10 minutes at `-max_len=70000`: 6,842,277
  runs, cov 1142, 0 findings. Then it replays the grown corpus with coverage. The corpus reaches
  81.9% of `rpc.h`'s lines and 86.9% of its regions. It never reaches the following (N4):
  - rule 7.6 (`rpc.h:526`);
  - `ByteBudget`'s waits, `cancel` and `serve` (`:191-211`, `:246-251`);
  - `granted()` and the wake (`:582-597`);
  - `next_deadline`.
- **Peer that never reads.** The `flood` probe in 261117-055231-96b757 sends 1,000,000 `Call`s
  (half to an unknown method, half served with an empty response) and never drains `next()`.
  That leaves 1,000,000 unsent frames: 500,000 `Error` and 500,000 `Body`. They total 32,000,000
  bytes, all with `rpc_outgoing_bytes` charged 0 and `live()` 0. This is N1.
- **send past a known total.** The `overrun` probe is in the same job. Its result is N2.
- **Re-entrant `ready`.** The `reenter` probe is in the same job. ASan reports a
  heap-use-after-free at `rpc.h:703`. This is N3.

Answers to the lead's questions:

1. **Decoder.** Every rule in the validity list refuses with no OOB read, no overflow and no
   length trusted before it is bounded. This is shown by the oracle and by reading. The fuzzing
   is real: 20 minutes plus my 10 minutes, about 20.6M runs in total, with no findings. It is
   enough for A1. The hour that design A6 asks for belongs to the rpc-lane quest's stream fuzzer.
   The fuzzer does not exercise pool contention (N4).
2. **Design match.** The code matches rules 1-10 and D2-D5. The contributor made choices where
   the design left the question open. Each is safe:
   - R1 makes `source_failed` code 7 (N5).
   - R2: headers and small frames are not charged (N1).
   - R3 renews FIFO-fair.
   - R4 regrants when outstanding credit is at most half a window.
   - R6: the server's call ends `ok` once its end is queued.
   - The codec also checks rules 7.2, 7.3 and 7.5. Design D3 assigns those to the table, so a
     malformed `Body` for a retired id is fatal instead of `late`. That is stricter, and still a
     peer bug under D3.
3. **Call table.** Peer calls are bounded by `serving` (256), and `index_` holds only live calls.
   Ids never repeat: `next_id_` increases by 2, and a peer `Call` must exceed `high_peer_`. Every
   call has a deadline. The server's deadline is clamped to `max_call_deadline`. A caller still
   waiting for a pool ends `timed_out`, provided the host drives `expire()`. Each call's `done`
   runs exactly once (`finish` erases before it calls back). The only unknown outcome is
   `connection_aborted` for a call whose `Call` was queued (`rpc.h:456`), as D4 #6 intends. A
   call ends `not_processed` only if it was never queued or the peer's `Drain` did not cover it.
   The queue that a non-reading peer grows is not bounded (N1).
4. **Pools.** `renew` subtracts and takes under one lock (`rpc.h:216-227`). While anyone waits,
   it returns `min(wanted, returned)`. Combined with the R4 threshold, the holding always covers
   `min(limit, total) - consumed`, so a stream cannot be stranded, and `take` caps `used_` at the
   limit. Bytes cannot leak:
   - Every exit path releases `held`.
   - A grant raced by `cancel` is released in `granted()` or by the expired `alive` check.
   - `split_release` proves the contended case deterministically.
   - TSan is clean.
5. **Mutant jobs.** Yes, each one tests the model's fault through its own named case (see N6).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| N1 | no | gap | **A peer that keeps sending while it never reads grows the unsent queue without bound.** Two kinds of frame are never charged to `rpc_outgoing_bytes` (R2): refused `Call`s answered `Error`, and served calls whose empty end `Body` stays queued after the call frees its slot. D5 says "a host's queued lane bytes stay under 64 MiB, whatever its peers grant", and that holds only for body bytes. D3's trusted-cluster model makes this a peer bug, but this is the class behind HTTP/2's reset and ping floods (CVE-2019-9512, CVE-2019-9514). The same mechanism lets one stalled peer pin the host-wide 64 MiB `outgoing` pool with charged response chunks after their calls end (the design review's cross-peer note) | probe `flood`, job 261117-055231-96b757: 1,000,000 Calls give 1,000,000 frames, 32 MB queued, 0 charged | Owner's call, options: (a) the Lane stops reading, or throws `ProtocolError`, once its unsent small-frame count passes a bound (e.g. 2 × `serving`), with a test feeding refused Calls past it; (b) charge small frames to `outgoing`; (c) keep it, record it in the design's Limits, and leave the bound to consensus-transport's reader (T5). Recommend (a) |
| N2 | no | defect | **`Lane::send` accepts bytes past a known total when `end` is false.** `encode` then throws `invalid_argument` from inside `chunk()`. By then the chunk is popped, `sent` has advanced (4 to 11 of 10) and the call is wedged: the correct end is refused with `rpc send`, and the call lives until its deadline. When the pump runs from a posted wake, the throw escapes on the loop thread. This is local misuse, not something a peer can trigger | probe `overrun`, job 261117-055231-96b757; `rpc.h:385-386` | In `send`: `o.total != unknown_total && bytes.size() > o.total - o.sent` throws `logic_error` before any state changes; a test for it |
| N3 | no | defect | **A `ready` callback that closes the lane re-enters while the lane holds a `Call &`, which is a use-after-free.** The call sites are `called()` (`rpc.h:702-703`), `chunk()` (`:654-656`) and `stop()` (`:429-433`; `finish` on an erased handle dereferences `end()`). The Lane's contract does not forbid this, and a connection whose write fails synchronously would do it | probe `reenter`, job 261117-055231-96b757: ASan heap-use-after-free at `rpc.h:703` | Before consensus-transport wires the lane, choose one: document that `ready` must not call into the Lane and only schedules a write; or defer `close()` requested inside a callback until the outermost call returns. Either way, add a test with a `ready` that closes |
| N4 | no | gap | The fuzz target never contends a pool: each pool is 256 KiB for 2 calls of 128 KiB windows. So libFuzzer never reaches `ByteBudget` waits or `cancel`, `granted()`, or rule 7.6. Those paths are covered only by unit cases and TSan. The shipped job also runs at libFuzzer's default `max_len` of 4,096, below the 65,584-byte largest frame | job 261117-055239-13601d coverage: lines 191-211, 246-251, 526, 582-597 unexecuted | Size the harness's `calling` and `outgoing` pools below two windows, and add `-max_len=65600` to the fuzz job (or to A6's) |
| N5 | no | nit | R1 makes `source_failed` code 7. That is inside D2's "7-255 reserved" range for Error, and the design never numbers it. Error 7 is still refused, so nothing breaks. The number is open with the Xapiand lead | `rpc.h:39`, `:55`; journal R1 | When the number is confirmed, change `Code::source_failed`, `cancel_code` and `decoder_rules`' list together, and state the number in the design's D2 |
| N6 | no | nit | `grant-without-reserve` mutates only the caller's first `Call.window`. The model's mutant also covers regrants and the server's first grant. Its case kills it on the advertised value (line 468) rather than on a pool overrun | `rpc_mutants.py:21`; model `rpc-lane-model.py:95` | Optional: a second edit in `regrant` |
| N7 | no | nit | Two notes for consensus-transport, the next consumer of this Lane. The inline bytes of a served `Call` (up to 64 KiB each, 256 per peer) reach the handler without being reserved from `rpc_serving_bytes`, because D5 does not gate `Call`s by credit. `drained()` turns true while a served call's response chunks are still queued, and the Lane's doc already says to write `next()` dry before closing | `rpc.h:703`, `:443` | None here; carry both to consensus-transport |

## Not run

- Nothing on Bulk, TLS, the hello or T1-T8. None of that exists at this commit.
- No TSan run of my own. Job 261117-050439-c01d2b and the deterministic `split-release` and
  `pools` cases were enough.
- No sequential-model stress of the Lane under two loops beyond `pipes`.
- No fuzzing longer than 10 minutes. No Linux, and no fuzzing under g++.
- I did not confirm that the 20-minute fuzz corpus reached the pool paths either: its corpus was
  lost with the job's checkout, and my 10-minute corpus did not reach them.
