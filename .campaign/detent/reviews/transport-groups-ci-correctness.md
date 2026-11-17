# transport-groups-ci review: correctness

Commit: consensus:0ed76ef4035d6b95d16bed569b004c71443c0fbe
Verdict: APPROVE
Date: 2026-11-17

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- 261117-084822-a16425: the suite at 0ed76ef, 48 of 48 (A3).
- 261117-084310-26bb40: A2 mutant consensus:a1eeac6 (old counting) fails at run 44 on the in-loop
  `assert(bounded(...))`, groups_test.cc:369, the new throughout check (A2 `--fails-without`).
- 261117-084320-74416f: A2 head, 200 runs at 4 at once, pass at consensus:27a403b;
  27a403b..0ed76ef changes only CHANGELOG.md and docs/mechanisms/host-connection.md, so it holds
  at head.
- 261117-084839-8524ae: TSan at 0ed76ef, shared_context once, groups threads and driver 50 runs
  each (counted in its log), no report (A1 as worded). It cannot tell fixed from unfixed (N1).
- CI run 44130104547 at 0ed76ef: linux-tsan, linux-arm64-gcc, macos-arm64, the Linux matrix,
  asan-ubsan and FreeBSD are green. macos-x86_64 fails only on peers_test.cc:1196, the
  peers-retry-flake failure (A3, and A5 together with main's run 44119963752).

## What I checked myself

No probe jobs. I read source against the TSan report. OpenSSL sources are saved under
`.scratch/review-transport-groups-ci-correctness/` (v3_purp, x509_vfy, ssl_lib, ssl_rsa, ssl_cert
at openssl-3.0.13, plus v3_purp at 4.1.0), alongside main's failing log `main-run.log` and the
branch's `branch-run.log`.

1. The diagnosis is correct. In OpenSSL 3.0.13 (CI's), `ossl_x509v3_cache_extensions`
   (v3_purp.c:407-411) takes a lock-free `tsan_ld_acq(&x->ex_cached)` fast path, and the first
   caller publishes with `tsan_st_rel` (v3_purp.c:620-622) under `x->lock`. libcrypto is
   uninstrumented, so TSan sees neither side. Main's report fits this: the writer is
   `ASN1_STRING_set` under M0, a 21-byte block (a 20-byte SKID plus NUL), and M0 was created in
   `load_verify_file` (reactor_tls.h:81), so the object is the CA's X509 in the store. The reader is
   `ASN1_STRING_cmp`. OpenSSL's own ordering is sound, so this is a publication TSan cannot see,
   not a hardware race. The fix stops the concurrent write from happening; it suppresses nothing.
2. Coverage. `X509_check_purpose(x, -1, 0)` caches before it returns (v3_purp.c:86-89). The
   context's own certificate and chain are already cached when they are loaded, through
   `ssl_security_cert` (ssl_rsa.c:219, 935, 941; ssl_cert.c:271). That leaves store certificates as
   the only shared X509s still uncached, and `X509_STORE_get1_all_certs` reaches them all.
   reactor's Peer role cannot use `set_default_verify_paths` (reactor_tls.h:58-61), so no
   by-directory lookup adds store certificates later.
3. Peer certificates are never shared. Each connection decodes its own peer certificate in its
   own SSL. No session is resumed across connections: consensus never sets a session id context or
   calls `SSL_set_session`. A verify-peer server with no sid_ctx returns from `ssl_update_cache`
   before caching (ssl_lib.c:3753-3755), and client caching is off by default.
4. Other shared objects. `ctx->param` is copied into each SSL at `SSL_new` (ssl_lib.c:761) and
   only read after configuration; `bind_hostname` writes per-SSL parameters. The policy cache is
   reached only with `X509_V_FLAG_POLICY_CHECK` (x509_vfy.c:245), which is not set.
5. Ordering. `Links` keeps `tls_` for its whole life and has no setter, so no reload path exists.
   Its constructor spawns no task (links.h:46-63). HostTransport builds every loop's Links in its
   own constructor (host_transport.h:68-78), before any `start`. If a later Links takes a context
   that is already in use, its cache calls only reach the fast path, which reads.
6. A2 product effect. Final counts are unchanged (each read still adds at most 1); only the order
   inside one `read()` moves. `HostTransportStats` has no consumer outside consensus (I searched
   ~/code/Kronuz). Writer order (reads, then posts, both seq_cst) and the test's load order
   (posts, then reads, in `bounded`) together make `posts <= reads * (L - 1)` hold in every
   snapshot. The bound is now asserted on every pass of the wait loop as well as at the end.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| N1 | no | gap | No test fails without the fix anywhere we can run it. On macOS both mutant jobs pass, so 8524ae's "no report" does not discriminate. The regression guard is Linux CI's shared_context, and that is probabilistic: both threads start at once, and a report needs the second verifier to reach the fast path after the first has cached, with no lock edge between them. A1's "removed" claim rests on the OpenSSL source reading above plus one green linux-tsan run (main's quest-branch CI was also green with the bug). | jobs 261117-084234-d7c1d4 and 261117-084243-55886d (mutant 8afc5d7, pass); connection_test.cc:810-832 | Options: 1. Keep it (the lead's A5 ruling). 2. Make shared_context sequence the handshakes: thread A completes its handshake, then sets a `memory_order_relaxed` flag, which gives TSan no happens-before edge, and B handshakes. The mutant would then report on Linux deterministically; run it once on Linux CI. |
| N2 | no | nit | The journal and A5 call the local OpenSSL 3.6.5, but the jobs build against Homebrew OpenSSL 4.1.0. The conclusion holds because 4.1.0 has the same `ex_cached` fast path (v3_purp.c:599, 864). | 74416f and d7c1d4 logs warn on `OSSL_DEPRECATEDIN_4_1`; /usr/local/lib/libcrypto.dylib links to openssl@4/4.1.0 | Correct the version in the journal. |
| N3 | no | nit | `cache_certificates` reaches only the current certificate slot (`SSL_CTX_get0_certificate`) and the context's main store, not a second key type's slot or a store set with `SSL_CTX_set1_verify_cert_store`/`chain_cert_store`. No code path reaches this today: reactor loads one chain and one CA file, and own certificates are cached on load anyway (point 2). | tls.h:17-35 | Keep it, or walk the slots with `SSL_CTX_set_current_cert(SSL_CERT_SET_FIRST/NEXT)` if a context ever carries two key types. |
| N4 | no | nit | HostTransport calls `cache_certificates` once per loop, L times on one context; after the first, every call is a read-only fast path. | links.h:53-54, host_transport.h:73 | None needed. |
| N5 | no | nit | The threads wait loop now spins with `yield` where it used to sleep 10 ms, taking a core under parallel ctest. It passes 200 runs at 4 at once and 50 under TSan. | groups_test.cc:366-371 | Optional: sleep about 1 ms per pass. |

The quest file has A1, A5, A2 and A3; there is no A4.

## Not run

- No Linux TSan run of the mutants. Docker is available locally, but sanitizers and Linux belong
  to CI, and A5 relies on CI by the lead's ruling.
- I did not check whether macOS TSan misses the race because of its interceptors or for some other
  reason.
- Peers (peers.h) owns its context, so I did not audit it for cross-thread sharing.
- I did not audit the `owner` hand-off of accepted connections between loops (transport-groups'
  code; instrumented, and TSan-visible if it were wrong).
