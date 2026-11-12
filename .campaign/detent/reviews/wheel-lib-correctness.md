# wheel-lib review: correctness

Commit: wheel:45f98eb
Verdict: APPROVE
Date: 2026-11-12

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

Round 1 was at wheel:f5406791bbb2: CHANGES on F1. Round 2 reviews only f5406791bbb2..45f98eb:
wheel:b822ac5 (F1, N1-N3) and wheel:45f98eb (destructor for the A7 review's F6, its F3/F4 docs
and tests).

## Evidence I relied on

- Round 1: suite 261112-042845-375f03, mutants 261111-233426-1629a0, -739727,
  261111-233427-f38b25, -8621ae, fuzz 261111-233426-cba5bf and 261112-042845-c55172, CI
  261112-042946-cdf883 (single-threaded paths, risks 2-5).
- Round 2, all done, exit 0, at 45f98eb: suite 261112-045015-68d268 (adds `wheel_test shared`,
  `reentry destroy`, the dying-fire `drop` case); mutants killed 261112-045016-03086f (cancel
  bits-only), 261112-045020-d35b49 (arm bits-only), 261112-045024-2acb3a (drain bits-only),
  261112-045028-3630df (old destructor); CI 261112-045138-2d2840 (ASan+UBSan, TSan).

## What I checked myself

- Probe 261112-050220-1a2489 (`.scratch/review-wheel-lib-correctness/run.sh`, unchanged script)
  at 45f98eb, Debug and -O2 -DNDEBUG: cancel a=1 b=0, arm a=1 b=1 c=1 size=0. Both pass; at
  f5406791bbb2 the same script failed (261112-044022-404973).
- The F1 argument, by the C++ model. An entry pushed behind another has `e.next = first !=
  nullptr`, written before its release CAS; the owner acts on it only after a happens-before from
  its pusher, so it reads that `next`, and its relaxed load of each stripe head reads that CAS or
  a later value. Only pushes (non-null) and the owner's own exchange (which would already have
  filed the entry) follow it, so `drain(true)` sees the stripe non-empty and its acquire exchange
  takes the chain. An entry at the bottom (`next == nullptr`) was pushed by a thread that saw the
  stripe empty and set the bit before returning, so `queued()` finds it. The pending bit of the
  held push later sets a stale bit: one empty drain, harmless.
- Stale `e.next`: every way an entry leaves the wheel or the inbox nulls it (unlink, drain before
  file, detach before relinking, the destructor's unlinks), and expire unlinks an entry before
  its fire, so a fire's arm or cancel of itself never sees the next due entry's address. A stale
  `next` can only come from a contract breach (copying a live entry); at worst it costs one
  spurious 64-stripe drain. A missed drain needs a queued entry with `next == nullptr` that is not
  at the bottom of its stripe, which the push cannot produce.
- Drain during `expiring_`: `drain(true)` files into slots exactly as `drain()` does and never
  touches `due_`; an entry fires only from `due_`, so the round-1 answer to risk 5 stands.
- Destructor: every entry armed or queued at entry is detached (all words, upper levels too) and
  gets exactly one `fire(e, false)`, unlinked before its fire, so a fire may destroy its own entry;
  a fire's cancel of a later detached entry removes it without a fire, as documented; what fires
  arm or `arm_remote` lands in slots or the inbox, the second drain files it, and the last loop
  unlinks it unfired, so destruction always ends. Matches docs/timer-wheel.md and the `drop` test.
- N2's replacement: `(uintptr_t(pprev) - uintptr_t(head_.data())) / 8 < all_slots` is a single
  unsigned compare; an address below `head_` wraps far past `all_slots`, `&due_` lies at
  `all_slots + words`, and an entry's `next` is outside `head_`. Sound.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | no | defect | Fixed at wheel:b822ac5: an owner arm or cancel of an unarmed entry with a `next` files every non-empty stripe. Was: with shared stripes a cancelled entry fired and an arm corrupted the stripe. | Probe 261112-050220-1a2489 passes (failed 261112-044022-404973); suite 261112-045015-68d268 with `wheel_test shared`; mutants 261112-045016-03086f, -045020-d35b49, -045024-2acb3a | Done. |
| N1 | no | gap | Resolved as documented: `expire` and `next()` see a push held behind a pending bit only once that bit lands (docs/timer-wheel.md, Threading contracts); only shared stripes. | docs/timer-wheel.md at 45f98eb | Done. |
| N2 | no | nit | Resolved by an integer range compare rather than `std::less`. | wheel.hh unlink at 45f98eb | Done. |
| N3 | no | nit | Resolved: arm's comment no longer claims a caller's wrapper inlines. | wheel.hh arm at 45f98eb | Done. |
| N4 | no | nit | A dying fire (`fire(e, false)`) that calls `expire()` starts its tail at `&due_` and overwrites the destructor's detached list; the docs forbid arming or cancelling the dying entry but say nothing of expire. | wheel.hh ~timer_wheel and expire at 45f98eb | One line in the destructor's docs ("a dying fire never calls expire"), or a Debug assert. |

## Not run

- No timing-dependent stress of 65+ real producer threads; the probe and `wheel_test shared` force
  the interleaving deterministically.
- A1 numbers for b822ac5's extra `e.next` load in arm and cancel (the journal's A1 rows are the
  lead's to read); A5 fuzz at 45f98eb; TSan locally (CI's); an arm64 litmus of the inbox protocol.
