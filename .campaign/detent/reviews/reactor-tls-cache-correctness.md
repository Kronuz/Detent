# reactor-tls-cache review: correctness

Commit: reactor:1dd5f32bf6fe (quest/detent/reactor-tls-cache; first round at reactor:afd82b5696cb)
Verdict: APPROVE
Date: 2026-11-19

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

At reactor:1dd5f32 (the fix round, after the lead ruled F4 a regression to fix):

- 261119-160733-baa48c suite, 261119-160734-96e25b ASan+UBSan, 261119-160735-2f6e65 TSan: all exit 0.
- 261119-160728-d08851 `password` (wrong password refused before and after caching, the right
  one shares one parse); mutant 261119-160729-ff811c fails it (exit 1).
- 261119-160725-a36699 `rotation` and `race` (F1).
- 261119-160730-f04f47 `combined`; the no-use mutant 261119-160732-d23c52 aborts (F2).

From the first round, at reactor:c1c0393 (whose code is the same as afd82b5): cache test 4fd091
with mutants 5b1364 and 49e28b failing it, and rotation test a8c953 with mutants 13f356 and
22e918 failing it. Those tests check values: object identity, X509_cmp against the file, and
handshakes. CI 44641725379 is not relied on.

## What I checked myself

Code reading of `reactor_tls.h` at 1dd5f32:

1. **Staleness.** `load` (:198-212) returns either the parse of the bytes this call read, or
   a cached parse whose bytes match them. For an encrypted key, the cached parse is reused only
   when `same_password` (:112) matches the digest. That makes stale serving impossible:
   - Nothing reopens the file between the read and the parse.
   - A racing overwrite of the entry costs only a reparse.
   - `SSL_CTX_check_private_key` still refuses a mismatched pair.
2. **Sharing.** Objects are installed by up-ref, each context keeps its own store, and
   extensions are decoded before the entry is published (unchanged since the first round).
3. **Questions from the fix round:**
   - **Q1 (where the digest lives):** the digest is kept only in `Pem::password` (:77), but nothing cleanses it (see F7).
   - **Q2 (no callback):** `Password::supply` falls back to `PEM_def_callback` with the context's
     userdata (:98), as OpenSSL's own loader does. A plaintext key never asks for a password, so it is reused
     with or without a callback (probe 5f5b9d).
   - **Q3 (callback count):** see F8.
4. **Read cap.** `read` refuses anything over 16 MiB with EFBIG and reserves the size first. The
   `int` cast in `BIO_new_mem_buf` is now bounded. This fixes F5 and F6.

Probes, kept as scripts under `.scratch/detent/review-reactor-tls-cache-correctness/`
(`run.sh MODE [PART]`, `probe.cc`):

- First round, at afd82b5:
  - 261119-160156-9daa79 (Release): the `combined`, `failure`, `password` and `race` parts. `race` reported 282 served, 25 half-rotated refused, 0 stale.
  - 261119-160158-536ada: the same under TSan, exit 0.
  - 261119-160200-e63012 (path-only mutant): caught by the probe.
  - 261119-160202-2b78a6 (no-use mutant): `reactor_tls_test` still passed and `probe combined` failed. This confirmed F2 then; the suite's `combined` part now covers it.
- Fix round, at 1dd5f32:
  - 261119-161319-5f5b9d (`calls` part) counts password-callback calls per configure: 1 on a miss for an encrypted key, 1 on a hit with the right password, 2 on a hit with a wrong password, 0 for a plaintext key. A plaintext key's parse is shared by contexts with and without a callback.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | no | gap | Resolved at 1dd5f32: the suite has `race` (configures during a rotation). | 261119-160725-a36699 | Done. |
| F2 | no | gap | Resolved: the suite has `combined`, and the no-use mutant aborts it. | 261119-160730-f04f47, 261119-160732-d23c52 | Done. |
| F3 | no | gap | The cache is unbounded and keeps decrypted keys after every context using them is gone. Documented in Limits and filed as reactor-tls-cache-bound. | docs/mechanisms/tls-certificate-cache.md | Follow-up quest. |
| F4 | no | defect | Resolved: an encrypted key is reused only when the context supplies a password with the same SHA-256 digest; a wrong or empty password is refused before and after caching. | 261119-160728-d08851, mutant 261119-160729-ff811c; probe 261119-161319-5f5b9d | Done. |
| F5, F6 | no | nit | Resolved: 16 MiB cap, and the size is reserved up front. | reactor_tls.h:120, `read` | Done. |
| F7 | no | nit | The password's unsalted SHA-256 digest is a fast offline check of a guessed password: anyone with a heap image can test a reused password against it. Nothing cleanses it: `~Pem` (:81) cleanses `bytes` but not `password`; `same_password`'s local `Password::digest` is freed uncleansed on every encrypted-key hit (:112); so is `supplied.digest` when a parse fails (:171). The digest stays only in the entry, but freed copies outlive both the entry and the key, whose material OpenSSL clears on free. | reactor_tls.h:77, :81, :91-110, :171 | `OPENSSL_cleanse` the digest in `~Pem` and in a `~Password`. |
| F8 | no | gap | A hit with a different password calls the context's callback twice per configure: once in `same_password`, then again in the reparse. Before this quest every case called it once. With no callback, `PEM_def_callback` prompts the terminal, so a wrong entry is asked for twice. A deterministic callback is not affected; an interactive or one-shot one is, and only on a path that ends in a refusal or a re-encrypted key. Your call: keep; or hand the password `same_password` already obtained to `parse`, through a replaying callback that is cleansed after, so every path asks once. | probe 261119-161319-5f5b9d: hit-wrong=2 | Your choice; not needed to land. |

## Not run

- CI 44641725379 and the platform matrix; Linux and OpenSSL versions other than the local 4.1.
- An interactive terminal prompt (`PEM_def_callback` with no callback on a tty); this was reasoned only.
- CRL-checking handshakes with shared CRLs (no consumer sets `X509_V_FLAG_CRL_CHECK`); this was reasoned only.
- `system_roots`, and `server_context`'s stat of the key's mode before reading it, both unchanged by this quest.
