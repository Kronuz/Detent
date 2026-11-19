# reactor-tls-cache: journal

State: ready for review
Quest: .campaign/detent/quests/reactor-tls-cache.md (the lead's; never edit it)

## Doing

Head reactor:afd82b5 (pushed): reactor:c1c0393 (main merged) plus the docs-only perf record.
All jobs at c1c0393 passed as intended; CI run 44641725379 at afd82b5 is running. The c1c0393 CI
run (44639061341) was cancelled by that push (the workflow's cancel-in-progress); same code.

## Next

1. Correctness review; confirm CI run 44641725379 green.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261119-153629-4fd091 (passed at c1c03935d8e4; ignores docs-only docs/perf/tls-configure.md); fails without: 261119-145839-5b1364, 261119-153630-49e28b (mutant reactor:a8e6af0, no-cache) |
| A2 | 261119-153631-a8c953 (passed at c1c03935d8e4; ignores docs-only docs/perf/tls-configure.md); fails without: 261119-145842-13f356, 261119-153633-22e918 (mutant reactor:4caa36e, path-only) |
| A3 | 261119-153634-6252d5 (passed at c1c03935d8e4; strict, main reactor:6fdd45f vs reactor:c1c0393, beside 10 light holds; docs/perf/tls-configure.md) |
| A4 | 261119-153635-07f53d suite, 261119-153636-5c01d3 ASan+UBSan, 261119-153637-4b4e5a TSan (all passed at c1c03935d8e4; ignore docs-only docs/perf/tls-configure.md) |

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
4. Encrypted keys: decrypted with the first parsing context's password callback; later contexts
   reuse it. Documented under Limits.
5. system_roots stays out of scope: SSL_CTX_set_default_verify_paths reparses the system bundle
   (~13 ms per call in the smoke run, ~100x a file configure), but replicating its env and
   per-version lookup setup is riskier, and no consumer (Xapiand, consensus) sets system_roots.
   Follow-up for the lead if a client ever configures per connection.
6. Bench lives in docs/perf/tls-configure/ (bench.cc, ab.sh), as benchmarks/ and CMakeLists.txt
   are outside the file set; ab.sh builds it against both trees from a configured build's cache.
7. Overlap with xapiand:reactor-server-composition (campaign check): I touched only reactor_tls.h,
   test/tls_test.cc, docs/mechanisms/tls-certificate-cache.md, docs/perf/tls-configure/, CHANGELOG.md.
