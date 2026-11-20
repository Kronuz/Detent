# reactor-tls-cache-bound: journal

State: waiting on jobs CI run 44718434892 (GitHub Actions, reactor:6cd8f73)
Quest: .campaign/detent/quests/reactor-tls-cache-bound.md (the lead's; never edit it)

## Doing

Code, bench, docs and CHANGELOG done at reactor:6cd8f73 (pushed). Waiting on CI run 44718434892.
The run at reactor:72aff48 (44660573405) was cancelled by infrastructure: the "ccache" install
step of linux-tsan and linux-x86_64-clang hung for the 1 h limit before configure ran; every
other job passed. The run at reactor:1310405 (44650984320) passed.

## Next

1. CI 44718434892 green: set State to ready for review. Red in an install step again: report
   it to the lead as CI infrastructure, with the step and timing.

## Blocked on

Nothing for this quest. For the lead:

1. (Committed at reactor:72aff48.) CHANGELOG.md, under `## 2026-11-19`, `### Changed`:

   ```
   - `tls::configure`'s parse cache keeps a parse only while a context configured from it lives:
     the context holds its parses and frees them with itself, so the cache holds no entry and no
     decrypted key once every context using it is gone. Configuring a context after freeing the
     last one using a file parses that file again. Password digests are cleansed when freed.
   ```
2. (Resolved by the lead: the bench is fixed in this quest; see Doing.) The CHANGELOG entry
   waits for reactor-abortlist-thread to land; the lead then adds the file and I commit it.

## Evidence

Head reactor:72aff48 (merged main reactor:fbad85f, then the CHANGELOG entry). campaign reused every
job across the merge ("only merges from main since"). TSan 261119-170547-7f622f failed in
reactor_lag under load 22; the rerun 261119-174122-8e50d9 passed.

| Criterion | Evidence |
|---|---|
| A1 | 261119-170319-db4214 (passed at 388e161ce232; only merges from main since); fails without: 261119-170321-faabcc |
| A2 | 261119-170558-97b11f (passed at 1310405bed4f; only merges from main since); fails without: 261119-170326-388a57 |
| A4 | 261119-170600-875ac8 (passed at 1310405bed4f; only merges from main since); fails without: 261119-170332-6dcc8d |
| A3 | suite 261119-170544-6cb23b, ASan+UBSan 261119-170545-daa85c, TSan 261119-174122-8e50d9 (passed at 1310405bed4f; only merges from main since) |

Mutants (reactor repo, kept by refs/mutants/reactor-tls-cache-bound/*): pin reactor:b0f85ab (the
cache also keeps a strong reference to every parse, as before F3); digest reactor:5cced28
(`~Digest` does not cleanse, as before F7).

## Result

reactor:388e161 changes `reactor_tls.h`, `test/tls_test.cc`, `docs/mechanisms/tls-certificate-cache.md`:

1. The cache maps `(use, path)` to a `weak_ptr<const Pem>`; each `configure` stores the parses it
   used in the context's OpenSSL ex_data (`detail::hold`, freed by an ex_data free callback in
   `SSL_CTX_free`), replacing what an earlier configure of that context left. A parse's deleter
   (`detail::Release`) erases its entry if still expired, then frees it, so the cache holds no
   entry and no key once its last context is gone (F3).
2. Password digests are a fixed 32-byte `detail::Digest` that cleanses itself in its destructor and
   compares with `CRYPTO_memcmp`; `Pem::password` and `Password::digest` are Digests, so the
   entry's copy, each `same_password` copy and a failed parse's copy are cleansed (F7). No
   heap-allocated string copy of a digest remains.
3. Tests: `release` (entry and decrypted key freed with the last holder; a reconfigured context
   lets go of its old parses), `bounded` (16 paths, 48 entries, empty after the contexts go),
   `digest` (bytes a Password's and a Pem's digest occupied read zero after destruction).

Perf (exclusive A/B 261119-234119-dba69c, before reactor:1dd5f32, after reactor:72aff48, median
of 5 round medians): held configures cost the same (server-ec 43.7 vs 43.4 us, server-rsa 39.4 vs
39.5, client-ca 22.4 vs 22.5, peer-ec 60.8 vs 61.4; paired round differences straddle zero, so the
strict run 61aafd's held slowdown was load). Unheld configures now parse in full: server-ec 43.7 ->
161 us (3.7x), peer-ec 61.1 -> 237 us (3.9x). Written up in docs/perf/tls-configure.md, with the
hit and miss conditions in docs/mechanisms/tls-certificate-cache.md (When it hits).

## Rulings I made

1. A context holds only the parses of its latest `configure`; OpenSSL's own references keep the
   objects a reconfigured context still uses valid, so dropping older parses is safe.
2. The parses are attached only when `configure` succeeds; a throwing configure leaves the
   context's earlier holdings.
3. The cache is a leaked function-local singleton, so a context freed during static destruction
   can still lock it.
4. The digest is a fixed array, not a std::string, so no allocator copy (SSO move, reallocation)
   can escape cleansing; a test reads the storage after destruction through a volatile pointer.
5. A fresh context configured with no other live holder now reparses; that is the bound the
   quest asks for (Blocked on 2 for the bench).

## Lead: review probes

- 261120-022622-5209e0 failed in the reviewer's own probe, which expected `std::invalid_argument` for a mismatched pair; OpenSSL refuses the pair first with `std::system_error` (review F4, a pre-existing comment). The quest's suite and `newer_survives` passed in it.
- 261120-022624-ecbfa6 failed as intended: the reviewer's mutant (`Release` erases unconditionally) passes the `reactor_tls` suite and fails `newer_survives` (review F1, non-blocking: costs a reparse, no stale material).
