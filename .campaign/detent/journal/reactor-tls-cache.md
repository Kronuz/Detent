# reactor-tls-cache: journal

State: waiting on jobs 261119-160723-6a8eaa 261119-160724-3d6c3e 261119-160725-a36699 261119-160727-e1c9ad 261119-160728-d08851 261119-160729-ff811c 261119-160730-f04f47 261119-160732-d23c52 261119-160733-baa48c 261119-160734-96e25b 261119-160735-2f6e65
Quest: .campaign/detent/quests/reactor-tls-cache.md (the lead's; never edit it)

## Doing

Fix round on correctness review (APPROVE at reactor:afd82b5): fixed at reactor:1dd5f32, pushed.

- F4 (fixed): a key entry records whether its parse asked for a password and the password's
  SHA-256 digest (memory only); a hit on an encrypted key asks the context's callback (outside the
  lock) and reuses the entry only on a matching digest. Test part `password`; mutant
  reactor:564a83b (no-password) must fail it: 261119-160728-d08851, 261119-160729-ff811c.
- F1 (adopted): test part `race` (4 configuring threads, 40-generation rotation by rename, half-
  rotated window); runs in 261119-160725-a36699 and the suite, ASan and TSan.
- F2 (adopted): test part `combined`; mutant reactor:b97973d (no-use: key drops Use) must fail it:
  261119-160730-f04f47, 261119-160732-d23c52.
- F3 (not fixed, lead's follow-up quest): listed under Limits in docs/mechanisms/tls-certificate-cache.md.
- F5, F6 (taken): read refuses files over 16 MiB (EFBIG) and reserves the file's size first.
- A3 not rerun: the hit path for unencrypted files only adds one flag test.

## Next

1. Read the jobs; fill Evidence; ready for review.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261119-160723-6a8eaa (cache at reactor:1dd5f32); fails without: 261119-160724-3d6c3e (mutant reactor:f0cb1b7, no-cache) |
| A2 | 261119-160725-a36699 (rotation + race at reactor:1dd5f32); fails without: 261119-160727-e1c9ad (mutant reactor:58dee1e, path-only) |
| A3 | 261119-161155-c85aa4 (strict, main reactor:6fdd45f vs reactor:c1c0393, beside 10 light holds; docs/perf/tls-configure.md) |
| A4 | 261119-160733-baa48c suite, 261119-160734-96e25b ASan+UBSan, 261119-160735-2f6e65 TSan; F4 261119-160728-d08851 / mutant 261119-160729-ff811c; F2 261119-160730-f04f47 / mutant 261119-160732-d23c52 |

At reactor:0b3d05d every job passed and both mutants failed; superseded by the merge.
Mutants live on local branches mutant/reactor-tls-cache/{no-cache,path-only} (not pushed).

Coordinator message (merge main, resubmit): done as above. The 0b3d05d no-cache mutant's exit 139
came after the checks at tls_test.cc:179-181: `same(*leaves.begin(), cert)` read a leaf freed with
its context. The test now keeps every context alive, so no freed object can be reused at the same
address (which could also have hidden a missing cache) and the mutant fails on its checks alone.

## Result

reactor_tls.h: `configure` reads each file and parses it only when its bytes differ from the
last parse of that (use, path); contexts share the parsed X509/EVP_PKEY objects, extensions
pre-decoded before sharing. Tests: test/tls_test.cc parts cache, rotation, threads. How it works:
docs/mechanisms/tls-certificate-cache.md. CHANGELOG 2026-11-19. Numbers (A3, round 5 medians, beside 10 light
holds): server-ec 176 -> 44 us (4.0x), server-rsa 158 -> 41 us, client-ca 85 -> 23 us, peer-ec
248 -> 61 us; system roots ~10.2 -> ~10.0 ms (unchanged, out of scope). docs/perf/tls-configure.md.

## Rulings I made

1. Bytes, not stat: every call reads the files and compares bytes; a stat check can serve a stale
   in-place rewrite (same mtime/size) and races the read. The rotation test restores mtimes.
2. Cache key is (use, path) as given, no canonicalization; process-lifetime entries, one per key.
   Cached bytes are OPENSSL_cleanse'd when freed.
3. Parsing mirrors OpenSSL's file loaders from a memory BIO (chain: X509_AUX leaf then X509s, EOF
   by PEM_R_NO_START_LINE; key with the context's password callback; authority: X509_INFO certs +
   CRLs, none found = error). Errors stay std::system_error (asio's type), with the asio call names.
4. Encrypted keys (superseded by review F4, lead's ruling): an entry is reused only for a
   context supplying the same password (SHA-256 digest compare).
5. system_roots stays out of scope: SSL_CTX_set_default_verify_paths reparses the system bundle
   (~13 ms per call in the smoke run, ~100x a file configure), but replicating its env and
   per-version lookup setup is riskier, and no consumer (Xapiand, consensus) sets system_roots.
   Follow-up for the lead if a client ever configures per connection.
6. Bench lives in docs/perf/tls-configure/ (bench.cc, ab.sh), as benchmarks/ and CMakeLists.txt
   are outside the file set; ab.sh builds it against both trees from a configured build's cache.
7. Overlap with xapiand:reactor-server-composition (campaign check): I touched only reactor_tls.h,
   test/tls_test.cc, docs/mechanisms/tls-certificate-cache.md, docs/perf/tls-configure/, CHANGELOG.md.
