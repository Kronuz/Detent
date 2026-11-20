# reactor-tls-cache-bound: journal

State: waiting on jobs 261119-170319-db4214 261119-170321-faabcc 261119-170324-9cfdf1 261119-170326-388a57 261119-170329-84676b 261119-170332-6dcc8d 261119-170343-3f5263 261119-170344-6ff1a8 261119-170347-1c4e01
Quest: .campaign/detent/quests/reactor-tls-cache-bound.md (the lead's; never edit it)

## Doing

Fix at reactor:388e161 (pushed). Jobs queued for every criterion; nothing compiled locally (an
exclusive hold held the machine), so the first job results are also the compile check.

## Next

1. Read the jobs: `campaign status reactor-tls-cache-bound`. A failing build or test: fix, commit,
   rebuild the two mutants on top (refs/mutants/reactor-tls-cache-bound/{pin,digest} in the reactor
   repo, one-line edits named in their messages), resubmit.
2. All green: paste `campaign brief`'s table below, set State to ready for review.

## Blocked on

Nothing for this quest. For the lead:

1. CHANGELOG.md (held by reactor-abortlist-thread). Under `## 2026-11-19`, `### Changed`:

   ```
   - `tls::configure`'s parse cache keeps a parse only while a context configured from it lives:
     the context holds its parses and frees them with itself, so the cache holds no entry and no
     decrypted key once every context using it is gone. Configuring a context after freeing the
     last one using a file parses that file again. Password digests are cleansed when freed.
   ```
2. docs/perf/tls-configure/bench.cc (outside this file set) frees each context before the next
   configure, so under this change it measures a parse on every call (the "before" numbers, about
   176 us for server-ec). The cached figure in docs/perf/tls-configure.md now holds only while a
   context using the files lives. Recommendation: a follow-up has the bench keep one configured
   context alive per case (a server rebuilding on rotation) and reruns the A/B.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | release 261119-170319-db4214; at pin mutant (fails-without) 261119-170321-faabcc |
| A2 | bounded 261119-170324-9cfdf1; at pin mutant (fails-without) 261119-170326-388a57 |
| A4 | digest 261119-170329-84676b; at no-cleanse mutant (fails-without) 261119-170332-6dcc8d |
| A3 | suite 261119-170343-3f5263, ASan+UBSan 261119-170344-6ff1a8, TSan 261119-170347-1c4e01 |

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
