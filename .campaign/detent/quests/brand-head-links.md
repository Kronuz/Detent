# brand-head-links: site head links for the touch icons and web manifest

## Outcome

The docs site, served under `/Detent/`, links the brand icons from its head so iOS,
Android and browsers find them: `apple-touch-icon` (180, 152, 167), `icon-192`/`icon-512`
through `site.webmanifest`, `favicon.svg` with `favicon.ico` as fallback, all under the
site base path. Follows detent-brand (icons in `website/public/`) and docs-e2e (owns
`astro.config.mjs` while active).

## Criteria

- A1 The site builds and its pages carry the touch-icon and manifest links with the base path (check `site`).
- A2 [artifact] `website/public/site.webmanifest` names `icon-192.png` and `icon-512.png` under `/Detent/`.

## May land early

Nothing; it is one small change.
