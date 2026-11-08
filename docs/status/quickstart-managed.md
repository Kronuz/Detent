> Status: HISTORY (2026-11-08): superseded by `.campaign/detent/` (`campaign map`); kept for its results and reasoning. Was: (2026-11-04) Ledger for the hands-on tutorial rewrite. Workstream: quickstart-managed (tech writer).

# quickstart-managed

- Branch / worktree: `docs/quickstart-managed` from `Detent:72ed8ee`, at `.claude/worktrees/agent-a0182892652fc7d4c`.
- Done criteria (the lead's brief): `QUICKSTART.md` and the website tutorial pages (`quickstart.md`, `settings-and-flags.md`, `rollouts.md`, `group-overrides.md`, `next.md`, the `index.mdx` hero snippet) teach one loopback recipe: a single-voter `detentd --authority` on 127.0.0.1, knobs published through its transactional API, a managed `detent-agent` over verified TLS, and `detent get` reading the generation. Every command runs on this host exactly as written. The Client's `source_path`/`source_poll` docs stay as they are (step H). The website build job is green.
- State: done; waiting for the lead's review.

## Done

1. `Detent:0593628` This ledger.
2. `Detent:73669a9` The seven pages. Each publish is one `/v2/transactions` call through a `publish KEY VALUE` shell helper; reads use `DETENT_GENERATION_DIR`, so `detent get` never opens the private `managed.json`.

## Evidence

- Build: `cmake -B build -DDETENT_BUILD_TESTS=OFF -DDETENT_BUILD_EXAMPLES=OFF -DDETENT_BUILD_BENCHMARKS=OFF` and `cmake --build build -j 4` under `quiet light` on `Detent:72ed8ee`'s tree; `build/src/detentd`, `detent-agent` and `detent` built.
- Certificates: `tests/detent_authority/tls_certificates.cmake` with `OPENSSL_CONF=tests/detent_authority/tls_ambient.cnf`, the invocation `tests/detent_delivery/CMakeLists.txt:7-12` uses, and `openssl` from `PATH` (OpenSSL 4.1.0 at `/usr/local/bin/openssl`).
- Runs: the `sh` fences of `QUICKSTART.md` (after the build fence) extracted and run with `sh -v`, then with `zsh -v`; the website pages' fences in reading order (quickstart, the hero, settings-and-flags, rollouts, group-overrides, next) run with `sh -v`. Every read printed the value the page states. The only failures were the expected ones: the first `/v2/open` returned `503 outcome_unknown` twice before `applied` (the `until` loop retries it), and reads of deleted knobs printed `ERR MissingKnob: ... (scoped lookup failed)`. Both daemons stopped on `kill`; `pgrep` found no `detentd` or `detent-agent` afterwards; `build/quickstart` removed by the guide's last line.
- Rollout buckets behind the examples, from SHA-256 over `payments:new_processor\0<id>`: `user-1` 10, `user-2` 93, `user-42` 2.
- Local site build (`npm ci`, `npm run build` under `quiet light`): 63 pages; the home and quick-start pages checked in headless Chrome screenshots of `astro preview`.
- CI: `pages.yml` dispatched on `Detent:f1fcb56`, run 41905491096: `build` green; `deploy` refused, as expected, by the `github-pages` environment's branch policy ("Branch "docs/quickstart-managed" is not allowed to deploy to github-pages"). `ci.yml` ignores `docs/**`, `website/**` and `**/*.md` (`.github/workflows/ci.yml:5`), so it does not run for this branch.

## Next

1. The lead reviews and lands the branch; the Pages deploy runs on `main`.

## Blocked on

Nothing blocks the pages. For the lead and the owner:

1. `packaging/homebrew/detent.rb:13-16` pins `Detent:323b971` (2026-10-09), 737 commits behind `Detent:72ed8ee` and still on the `--source-path` reader, so `brew install Kronuz/tap/detent` installs the removed flow. The pages drop Homebrew and teach the source build until the formula is bumped (owner's or maintenance's call).
2. `detentd --daemonize` returns once the listener is up (`src/detentd/main.cc:190-193`), about 2 s before the single voter accepts `/v2/open`; `--status` already reports `"ready":true` with `"role":0` (Follower) in that window (`src/detent_authority/service.cc:1595-1596`). The guide retries the open with the same nonce, as `docs/AUTHORITY_ARCHITECTURE.md:95` prescribes for `outcome_unknown`. Whether `--daemonize` should wait for leadership is the owner's call.
3. `docs/SCALING.md:54` still says the `/v1/snapshot` endpoint "remains useful for bootstrap and administration"; `docs/AUTHORITY_ARCHITECTURE.md:80` says the authority does not serve it. Not in this brief; left as is.
4. `website/src/content/docs/basics.md:78` describes the Client's `GET /v1/snapshot` source; left for step H.
