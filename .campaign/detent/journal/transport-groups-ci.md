# transport-groups-ci: journal

State: waiting on jobs 261117-084234-d7c1d4 261117-084243-55886d 261117-084252-8cea17 261117-084310-26bb40 261117-084320-74416f 261117-084333-32b0e6
Quest: .campaign/detent/quests/transport-groups-ci.md (the lead's; never edit it)

## Doing

Waiting on the A1/A2 jobs and the suite at consensus:27a403b.

## Next

1. Read the job logs: the mutant jobs must fail for the named reason (TSan report in libcrypto; the
   threads bound), not setup noise; the head jobs must pass.
2. If the uncached-context groups job (055886d) survives (timing), rely on connection_test
   shared_context (d7c1d4) as the deterministic `--fails-without`.
3. CHANGELOG and docs/mechanisms/host-connection.md lines; then ready for review (CI at landing).

## Blocked on

Nothing.

## Cause

1. TSan race (A1). One `asio::ssl::context` per host is shared by all its loops (HostTransport hands
   the same `tls` to every loop's Links). `load_verify_file` puts the CA's X509 in the context's
   store; OpenSSL decodes a certificate's extensions lazily (`ossl_x509v3_cache_extensions`) on its
   first verification, under the X509's own lock (M0, made by the CA load inside
   `reactor::tls::configure`, line 81), and publishes them via `ex_cached` with a release store; the
   next verifier takes the lock-free acquire-load fast path. libcrypto is uninstrumented, so TSan
   sees neither: writer = loop A's handshake, `X509_check_issued` -> cache_extensions ->
   `X509_get_ext_d2i(subject_key_identifier)` -> `ASN1_STRING_set` (the 21-byte block: a 20-byte
   key id + NUL); reader = loop B's handshake, `X509_check_akid` -> `ASN1_STRING_cmp(akid->keyid,
   CA->skid)` -> memcmp. Same code in 3.0.13 (CI) and 3.6.5 (here). Not a hardware race (OpenSSL's
   acquire/release is correct); a TSan-invisible publication. Timing: reported only when the second
   verifier arrives after the first finished caching and before any later lock edge (store lock,
   method-store write) orders them.
   Fix: `cache_certificates(context)` (tls.h) runs `X509_check_purpose(x, -1, 0)` on the context's
   own certificate, chain and extra chain, and every store certificate; Links' constructor calls it,
   on the thread building the HostTransport, before any loop handshakes. Handshakes then only read.
2. arm64 assertion (A2). `read()` counted `++posts` per cross-loop batch and `reads += any` after
   the loop. A post's batch can be delivered (and the test see `delivered == total`) before the
   reader counts the read, so a snapshot can hold up to L-1 posts of an uncounted read; each read
   posts to all 3 other loops (posts == 3 * reads), so that snapshot breaks the bound. Fix: a read
   counts itself before its first post; the test loads posts before reads and samples the bound
   throughout the exchange (which also makes the mutant fail reliably).

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | pending: 261117-084234-d7c1d4 (mutant uncached-context, shared_context, fails-without), 261117-084243-55886d (mutant, groups 50x, fails-without), 261117-084252-8cea17 (head: shared_context + groups threads/driver 50x each, TSan) |
| A2 | pending: 261117-084310-26bb40 (mutant reads-after-posts, 200x 4 at once, fails-without), 261117-084320-74416f (head, 200x 4 at once) |
| A3 | pending: suite 261117-084333-32b0e6; CI after push |

Mutants: consensus:8afc5d7 (uncached-context: Links' call removed), consensus:a1eeac6
(reads-after-posts: old counting), each one commit on consensus:27a403b, written with
test/transport/mutant_commit.py's mechanics from a scratch table (the script is outside this file
set; transport-identity edits it).

## Result

consensus:ad53e52 Cache a context's certificates before loops share it (tls.h, links.h,
connection_test shared_context); consensus:27a403b Count a read before its cross-loop posts
(host_transport.h, groups_test threads).

## Rulings I made

1. Cache before sharing, not per-loop contexts: HostTransport takes one configured context, and a
   context cannot be cloned without its options; per-loop contexts would change the
   consensus:transport contract. The call sits in Links' constructor (links.h) so host_transport.h,
   which transport-identity edits, stays untouched for it.
2. TSan jobs here run on macOS, whose TSan ignores uninstrumented modules by default; they set
   `ignore_noninstrumented_modules=0` to see libcrypto's memcpy/memcmp as Linux CI does.
3. Follow-up for the lead, not done here: reactor::tls::configure could cache the same way, for
   any multi-threaded reactor server sharing one context.
