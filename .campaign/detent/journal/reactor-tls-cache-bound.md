# reactor-tls-cache-bound: journal

State: waiting on jobs 261119-174128-f8387d
Quest: .campaign/detent/quests/reactor-tls-cache-bound.md (the lead's; never edit it)

## Doing

Fix at reactor:388e161; bench and mechanism doc at reactor:1310405 (pushed, head). msg from the
lead: fix the bench in this quest (file set now has docs/perf/tls-configure/**,
docs/perf/tls-configure.md). The bench keeps one configured context alive per held case and adds
server-ec-unheld and peer-ec-unheld (each context freed first). Strict A/B 261119-170530-61aafd:
before reactor:1dd5f32 (unbounded cache), after head. release passed at 388e161
(261119-170319-db4214), so the code compiles.

## Next

1. Read the jobs: `campaign status reactor-tls-cache-bound`. A failing build or test: fix, commit,
   rebuild the two mutants on top (refs/mutants/reactor-tls-cache-bound/{pin,digest} in the reactor
   repo, one-line edits named in their messages), resubmit.
2. TSan 261119-170547-7f622f failed in reactor_lag only (median wake within the 1,024 us edge,
   load ~22); rerun 261119-174122-8e50d9. If reactor_lag fails again, give the lead the counts
   (they file it as a load flake). Strict A/B 61aafd unusable (6 light holds, load 22;
   system-roots 19.5 ms vs 8.6); exclusive rerun 261119-174128-f8387d. If held cases are still
   slower after than at 1dd5f32, find why (suspects: hold()'s Held allocation and ex_data set,
   weak_ptr lock) before the perf doc.
3. From the A/B (261119-174128-f8387d, round 5 medians), rewrite docs/perf/tls-configure.md:
   before = reactor:1dd5f32, after = head; held cases should match, unheld after = full parse
   (compare the no-cache numbers of job 261119-153634-6252d5, labeled as that job's). Say plainly
   when the cache hits (live holder) and when it misses. Commit, push.
4. All green: paste `campaign brief`'s table below, set State to ready for review.

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
