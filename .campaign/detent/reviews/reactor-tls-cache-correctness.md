# reactor-tls-cache review: correctness

Commit: reactor:afd82b5696cb (quest/detent/reactor-tls-cache)
Verdict: APPROVE
Date: 2026-11-19

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- 261119-153629-4fd091 (cache test) with mutants 261119-145839-5b1364 and 261119-153630-49e28b
  (no cache) failing it: A1. The test checks object identity (one leaf, one key, one trusted
  certificate across 50 live contexts, server_context's included) and a handshake, not counts.
- 261119-153631-a8c953 (rotation) with 261119-145842-13f356 and 261119-153633-22e918 (path-only
  cache) failing it: A2. It checks the presented certificate by X509_cmp against the file,
  handshakes against clients trusting each generation, that old contexts keep their objects after
  their entry is replaced, and the half-rotated pair refusal.
- 261119-153635-07f53d suite, 261119-153636-5c01d3 ASan+UBSan (covers old contexts using objects
  of a replaced entry), 261119-153637-4b4e5a TSan (threads part): A4. 261119-153634-6252d5: A3.
  All at reactor:c1c0393; afd82b5 adds only docs/perf.
- CI 44641725379 was still running when I wrote this; I did not rely on it.

## What I checked myself

Code reading, `reactor_tls.h` at afd82b5:

1. Staleness (Q1). `detail::load` (reactor_tls.h:151-166) returns either the parse of the bytes
   this call read (`parse` takes the read bytes; nothing reopens the file) or a cached `Pem`
   whose `bytes` compare equal to them under the mutex. So a context always gets the parse of
   what its own call read: no TOCTOU between read and parse, and a racing older parse that
   overwrites a newer entry (the unconditional `cache[...] = pem` at :168) costs only a reparse
   on the next call, never a stale serve. Certificate and key are read separately; a mismatched
   pair is caught by `SSL_CTX_check_private_key` (:207), as before.
2. Sharing (Q2). Objects are installed by up-ref (`SSL_CTX_use_certificate`, `add1_chain_cert`,
   `use_PrivateKey`, `X509_STORE_add_cert/crl`); each context owns its own store, and adding to a
   store does not mutate the X509. Extensions are decoded at parse (:145); CRL revoked-list
   sorting is lazy but under the CRL's own lock in OpenSSL. Cross-context sharing adds no hazard
   beyond what one context's connections already share. A replaced entry drops only its own
   references (`~Pem`, :79-84).
3. Bounds (Q3). One entry per (use, path), never evicted; a failed read or parse throws before
   touching the cache.

Probes, scripts under `.scratch/detent/review-reactor-tls-cache-correctness/` (`run.sh MODE`,
`probe.cc`), at afd82b5:

- 261119-160156-9daa79 (release, done, exit 0): `combined` (one file as certificate, key and
  authority, two rounds so the second hits the cache) serves and handshakes; `failure` (file
  unreadable, absent, garbage, empty, then a new generation) throws every time and never serves
  the cached parse, then serves the new one; `password` prints `wrong-before-cached=refused
  right=accepted wrong-after-cached=accepted`; `race` (4 threads configuring while a fifth
  rotates certificate, key and authority by rename through 40 generations, with a half-rotated
  window each step) reports served=282, refused(half-rotated)=25, stale=0, mismatched=0,
  bad-handshakes=0, and the configure after rotation serves the last generation.
- 261119-160158-536ada (the same under TSan): pending at writing.
- 261119-160200-e63012 (mutant-bytes: the cache compares paths only; exit 0 when the probe
  catches it, to show `race`/`failure` have teeth): pending at writing.
- 261119-160202-2b78a6 (mutant-use: the cache key drops the use; exit 0 when `reactor_tls_test`
  still passes and `probe combined` fails, confirming F2): pending at writing.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | no | gap | No suite test configures while files rotate. `threads` contends on unchanged files only, so a plausible wrong cache (on a miss, return the entry a racing thread inserted instead of this call's own parse) passes the suite; the code is right by construction (item 1). | test/tls_test.cc `threads`; probe 261119-160156-9daa79 `race` | Adopt the probe's `race` part as a suite part (it runs under TSan with the rest). |
| F2 | no | gap | The `Use` in the cache key is load-bearing but untested: a file used as both certificate and key (supported by OpenSSL's loaders, and working here) would get the chain's parse as its key without it. No suite test uses one path for two uses with different parses. | reactor_tls.h:157,164; probe 261119-160202-2b78a6 (pending) | Add the probe's `combined` part to the suite. |
| F3 | no | gap | The cache is unbounded and outlives its contexts: one entry per distinct (use, path) for the process, so decrypted private keys stay in memory after every context using them is gone, and a deployment rotating to new file names (dated paths) accumulates one key per rotation. Documented ("Entries live for the process"). Owner's call: keep as is; drop entries no context holds (hold the `Pem` from each SSL_CTX through ex_data, cache `weak_ptr`); or do not cache keys. | reactor_tls.h:153-164; docs/mechanisms/tls-certificate-cache.md "Sharing" | Owner's choice; none required to land. |
| F4 | no | gap | A context whose password callback is wrong is accepted once another context decrypted the same key bytes; before this change it was refused. Documented under Limits. Owner's call: keep; or do not cache a key whose parse invoked the password callback (wrap the callback to record the call), so encrypted keys are always decrypted with the caller's password. | probe 261119-160156-9daa79 `password`: wrong-after-cached=accepted | Owner's choice. |
| F5 | no | nit | `read` has no size bound and `static_cast<int>(bytes.size())` wraps for files of 2 GiB or more (a negative length makes `BIO_new_mem_buf` use `strlen`). Paths are operator configuration, so this is hygiene; a cap (a few MiB) would refuse a mistaken path such as a device. | reactor_tls.h:95-106, :115 | Refuse files over a fixed cap. |
| F6 | no | nit | `read` grows a `std::string` by appends, so reallocation frees earlier copies of key bytes uncleansed; only the final buffer and the stack buffer are cleansed. | reactor_tls.h:97-100 | `fstat` and reserve the size first. |

## Not run

- CI 44641725379 and the platform matrix; Linux and other OpenSSL versions than the local 4.1.
- The TSan and mutant probes above were queued or running when this was written; their results
  are to be read from the job ids, not assumed.
- Encrypted keys under concurrency; CRL-checking handshakes with shared CRLs (no consumer sets
  `X509_V_FLAG_CRL_CHECK`; reasoning only).
- `system_roots` (unchanged path) and server_context's stat-then-read of the key mode, which
  predates this quest.
