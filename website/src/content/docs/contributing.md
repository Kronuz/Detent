---
title: "Contributing"
---

Contributions are welcome: bug reports, documentation fixes, tests and code.

---

## Reporting Issues

[Open an issue](https://github.com/Kronuz/Detent/issues/new) with:

1. What you ran, including the exact command or code.
2. What you expected and what happened, with logs (`-v` helps).
3. Your platform, compiler and Detent commit.

Report security issues privately through
[GitHub security advisories](https://github.com/Kronuz/Detent/security/advisories/new).

---

## Building and Testing

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Run `tests/e2e/` after any change to `src/detentd/` or the Raft wiring; it
spawns real processes and kills them with real signals.

---

## Expectations for Changes

1. **Tests with the change.** New behavior comes with tests; bug fixes come
   with a regression test that fails before the fix.
2. **Sanitizers.** Concurrency and memory changes should pass ASan/UBSan and
   TSan.
3. **Measurements, not adjectives.** A performance claim needs a reproducible
   benchmark, its before and after numbers, and its caveats.
4. **Bounded everything.** New inputs, queues and caches need explicit limits
   checked before allocation.
5. **Keep the read path clean.** Nothing that allocates, locks or does I/O
   goes on `eval`/`get`.
6. **Document as you go.** Update the design documents in `docs/` and this
   website alongside the code.

---

## This Website

The site lives in `website/` and is built with
[Astro](https://astro.build) and [Starlight](https://starlight.astro.build),
using the same theme as the Xapiand documentation:

```sh
cd website
npm ci
npm run dev       # http://localhost:4321/Detent
npm run build     # static site in website/dist, with Pagefind search
```

Diagrams are ```` ```d2 ```` fences rendered at build time, so install
[D2](https://d2lang.com) to preview them. Every page has an **Edit page** link.
Pushing to `main` deploys the site to GitHub Pages.
