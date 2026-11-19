# reactor-tls-cache: journal

State: waiting on jobs 261119-145836-ae0fa0 261119-145839-5b1364 261119-145840-a79c07 261119-145842-13f356 261119-145853-59a461 261119-145907-555a87 261119-145909-24b3f4 261119-145911-65e42b
Quest: .campaign/detent/quests/reactor-tls-cache.md (the lead's; never edit it)

## Doing

Work at reactor:0b3d05d (pushed, quest/detent/reactor-tls-cache). Jobs queued for A1-A4.

## Next

1. Read the jobs; fill docs/perf/tls-configure.md with A3's numbers (bench: docs/perf/tls-configure/).
2. Ready for review (correctness lens).

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261119-145836-ae0fa0 (`reactor_tls_test cache`), 261119-145839-5b1364 (fails-without at mutant reactor:56489bf, no-cache) |
| A2 | 261119-145840-a79c07 (`reactor_tls_test rotation`), 261119-145842-13f356 (fails-without at mutant reactor:0dff6af, path-only cache) |
| A3 | 261119-145853-59a461 (strict, bf874dc vs 0b3d05d alternating, docs/perf/tls-configure/ab.sh) |
| A4 | 261119-145907-555a87 (suite), 261119-145909-24b3f4 (ASan+UBSan), 261119-145911-65e42b (TSan, libcrypto visible) |

Mutants live on local branches mutant/reactor-tls-cache/{no-cache,path-only} (not pushed).

## Result

reactor_tls.h: `configure` reads each file and parses it only when its bytes differ from the
last parse of that (use, path); contexts share the parsed X509/EVP_PKEY objects, extensions
pre-decoded before sharing. Tests: test/tls_test.cc parts cache, rotation, threads. How it works:
docs/mechanisms/tls-certificate-cache.md. CHANGELOG 2026-11-19. Numbers: pending A3 (light-load
smoke, not evidence: server-ec ~240 -> ~55 us, peer-ec ~420 -> ~90 us).

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
