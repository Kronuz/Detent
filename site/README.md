# Detent documentation website

The source of https://kronuz.github.io/Detent/, built with
[Astro](https://astro.build) and [Starlight](https://starlight.astro.build)
using the Kronuz theme shared with the
[Xapiand docs](https://kronuz.github.io/Xapiand/).

```sh
npm ci
npm run dev       # preview at http://localhost:4321/Detent
npm run build     # build to dist/, including the Pagefind search index
```

- Pages live in `src/content/docs/`; navigation is `src/lib/sidebar.mjs`.
- ```` ```d2 ```` fences render to light and dark SVGs at build time; install
  the [D2](https://d2lang.com) CLI to build them locally.
- ```` ```rest ```` snippets copy as `curl` against `http://localhost:8317`.
- Hints use `:::hint[Title]{.tip|info|caution|warning|unimplemented|construction}`.
  Every `unimplemented` hint should also appear on `roadmap.md`.

Pushing changes under `website/` to `main` deploys the site through
`.github/workflows/pages.yml`.
