# reactor-tls-cache-bound review: correctness

Commit: reactor:6cd8f73145ef (quest/detent/reactor-tls-cache-bound; code last changed at reactor:388e161, docs and bench since)
Verdict: APPROVE
Date: 2026-11-20

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- A1 `release` 261119-170319-db4214 passes at reactor:388e161; the pin mutant reactor:b0f85ab
  (the cache also keeps a strong reference) fails it, 261119-170321-faabcc, at tls_test.cc:475,
  :476 and :481: the entry, the decrypted key's parse and a reconfigured context's old authority.
- A2 `bounded` 261119-170558-97b11f passes at reactor:1310405; the same mutant fails it,
  261119-170326-388a57, at `entries() == 0` (:505).
- A4 `digest` 261119-170600-875ac8 passes; the no-cleanse mutant reactor:5cced28 fails it,
  261119-170332-6dcc8d, at :538 and :550 (the Password's and the Pem's digest bytes).
- A3 at reactor:1310405: suite 261119-170544-6cb23b, ASan+UBSan 261119-170545-daa85c, TSan
  261119-174122-8e50d9 (full ctest, `reactor_tls` passed in 10.4 s under TSan). Commits since are
  a merge from main, CHANGELOG, bench and docs; `reactor_tls.h` and `tls_test.cc` are unchanged.
- Perf: I recomputed the A/B 261119-234119-dba69c from its log (5 rounds each side). Medians of
  round medians: server-ec held 43.68 -> 43.38 µs, peer-ec held 60.78 -> 61.37, server-rsa 39.43
  -> 39.52, client-ca 22.43 -> 22.54, server-ec unheld 43.69 -> 161.15 (3.7x), peer-ec unheld
  61.11 -> 236.99 (3.9x); paired differences server-ec -6.53 to +1.02, peer-ec -5.35 to +6.20.
  Every number in docs/perf/tls-configure.md matches. Host OpenSSL is 4.1.0, x86_64, as stated.

## What I checked myself

Code reading of `reactor_tls.h` at 6cd8f73, then probes under
`.scratch/detent/review-reactor-tls-cache-bound-correctness/` (`run.sh base|erase`, `probe.cc`,
which includes the suite's test file for its helpers).

1. **The bound.** Every parse is created in `load` (:242) with the `Release` deleter, so every
   parse's death runs the erase check. An entry is set only while `load` holds a strong reference
   (:243-244), so an entry can name an expired parse only until that parse's deleter takes the lock;
   the deleter erases it unless a newer parse replaced it, and a newer expired one is erased by
   whichever deleter comes first. No path leaves an entry: errors (a throwing `parse` frees its
   `unique_ptr`; a throwing `configure` drops `chain`, `key` and `held`, and their deleters
   erase), reload (`hold` swaps the vector and deletes the previous one, :260-266), and concurrent
   configure and free (all map access under the one mutex, :234, :243, :220). The full suite
   checks this end to end: `bounded` runs last in the one `reactor_tls` process and asserts
   `entries() == 0`, after `race` (4 threads, rotation, half-rotated pairs that throw), `threads`
   and `password` (wrong passwords that throw) ran, so any leak on those paths fails the suite.
   A context freed mid-configure is outside the API (configure takes it by reference).
2. **No deadlock.** No last strong reference is dropped with the cache locked: under the lock,
   `load` only copies a `weak_ptr::lock()` into an empty `cached` and assigns a weak entry;
   `Release` erases a weak entry; `delete pem` is outside the lock (:223). `free_held` runs from
   `SSL_CTX_free`'s ex_data callback; nothing under the cache lock calls into OpenSSL ex_data, so
   no lock-order inversion with OpenSSL's ex_data lock. TSan 261119-174122-8e50d9 ran `race` and
   `threads`, where 4 threads each configure and free contexts on the same paths with no outside
   holder, so deleters run against concurrent `load`s of the same entry; it passed.
3. **Staleness.** A hit requires `cached->bytes == bytes` over the whole file this call read
   (:237), plus the password digest for an encrypted key, so a same-size, same-mtime rewrite or a
   rotation cannot serve old material. The suite's `rotation` test rewrites in place with mtimes
   restored while the old contexts live (the hit path is open) and checks the served certificate
   by `X509_cmp` and by handshakes. A parse is immutable once published; `bytes` is read outside
   the lock only on a held strong reference.
4. **Key material.** The decrypted `EVP_PKEY` and the key's PEM bytes now live only while a
   context holds the parse; before this quest they lived for the process. `~Pem` still cleanses
   `bytes` and frees the key; the hit path still cleanses the read buffer (:238); `same_password`
   still cleanses its buffer (:123). The old `md` stack buffer is gone: `supply` digests straight
   into a `Digest`, and every `Digest` (the entry's, `supplied` in `parse` on success and on throw,
   `same_password`'s) cleanses itself. No `std::string` copy of a digest remains.
5. **Probes.** `run.sh base` at reactor:6cd8f73, job 261120-022821-9ae3e0 (exit 0, suite `tls: ok`): `newer_survives`
   (rotate, configure the new bytes, free the context holding the old parse: the new entry stays
   and the next configure hits it, same `X509*` and `EVP_PKEY*`) and `error_paths` (a mismatched
   pair leaves `entries()` unchanged; a context whose reconfigure throws keeps its earlier parses,
   and the failed configure's parse is gone). The first base run, 261120-022622-5209e0, passed
   `newer_survives` and the suite, then aborted in my own probe, which expected
   `std::invalid_argument` for a mismatched pair (see F4). `run.sh erase`, job
   261120-022624-ecbfa6: with `Release` erasing unconditionally, the whole `reactor_tls` suite
   still passes (`tls: ok`) while `newer_survives` fails at probe.cc:25, :29 and :30 (F1).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | no | gap | The `expired()` check in `Release` (reactor_tls.h:221), which keeps a newer parse's entry when an older parse of the same path dies, has no test: a mutant that erases unconditionally passes the full suite. Under that mutant a reload that frees the old context drops the new parse's entry, so the next configure reparses (about 120 µs per server context); no stale material and no leak, so it costs only the hit the quest keeps. | 261120-022624-ecbfa6 (suite `tls: ok`, probe fails at :25, :29, :30) | Add `newer_survives` from `.scratch/detent/review-reactor-tls-cache-bound-correctness/probe.cc` to `release` or `rotation` in test/tls_test.cc. |
| F2 | no | nit | The review request's summary "private keys are deliberately not cached" does not describe the code: a `Use::Key` entry holds the decrypted `EVP_PKEY` (reactor_tls.h:179) while some context holds the parse, and is freed with the last one. The mechanism doc, perf doc and CHANGELOG state this correctly. | reactor_tls.h:179, :242; docs/mechanisms/tls-certificate-cache.md (Sharing) | Keep that sentence out of the landing note. |
| F3 | no | nit | Journal ruling 2 says parses are attached only when `configure` succeeds, but `hold` (:326) runs before `set_default_verify_paths` (:327), which can throw; a context that fails there keeps the new parses. They are still owned by that context and freed with it, so the bound holds. | reactor_tls.h:326-327 | None needed; or move `hold` last. |
| F4 | no | nit | Pre-existing, outside the quest: `configure`'s comment promises `std::invalid_argument` for a key that does not match the certificate, but OpenSSL 4.1's `SSL_CTX_use_PrivateKey` refuses the pair first, so a `std::system_error` ("key values mismatch") is what callers get; the `invalid_argument` branch is unreachable for these keys. Same code at reactor:fbad85f. | 261120-022622-5209e0 log; reactor_tls.h:309-310 | A later quest: fix the comment or map the error. |

## Not run

- CI run 44718434892 at reactor:6cd8f73 was still running; I did not rely on it, and Linux and
  its sanitizers are CI's.
- No deterministic test forces the interleaving of a parse's deleter racing a `load` of the same
  path on another thread; the TSan stress (`race`, `threads`) reaches it by chance only. The logic
  is one mutex plus atomic `weak_ptr` operations, and the sequential form is probed, so I did not
  ask for one.
- Configuring one context from two threads at once is unsupported by OpenSSL and not probed.
