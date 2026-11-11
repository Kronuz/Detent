# detent-brand: Detent brand: a polished logo family and brand/

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status detent-brand`.

## Outcome

Detent has a logo of the same family and finish as the Kronuz logo
(`~/code/KronuzBlog/Kronuz.github.io/brand/`, `src/assets/logo.svg` there) and Xapiand's
(`~/code/Kronuz/Xapiand/docs/src/assets/logo.png`): flat geometric shapes, no shading or gradients,
the Kronuz green `#93c34d` and gray `#a7a7a7`, a hexagonal silhouette the family shares, clean at
16 px. Its subject is a detent: a knob held at a setting by a click-stop (notches around a dial, the
pointer seated in one). It replaces today's `website/src/assets/logo.svg` (a shaded dark-green dial,
off the family's style). `brand/` in this repository holds the master vector and every derived
asset, the way Kronuz's does, and the README and the site's welcome page say what the name means:
"A detent is the click-stop that holds a knob at its setting." (wording may be polished).

## Acceptance criteria

- A1 [manual] The owner picks the logo from three candidates rendered side by side
  (`brand/candidates/`, each at 512 px and 16 px, light and dark backgrounds, next to the Kronuz and
  Xapiand logos); the journal records the pick and who made it.
- A2 [artifact] `brand/` holds `logo.svg` (the master, hand-written or cleaned SVG, no embedded
  rasters), `README.md` (what each file is and how the rest derive from the master, with the
  command), and `variants/<name>/` with favicon.svg, favicon.ico, apple-touch-icon(s), icon-192 and
  icon-512 PNGs and logo.svg in that variant's colors (at least a light and a dark variant).
- A3 The site uses the new assets (logo, favicon, footer logo, touch icons) and builds with its link
  check: a job.
- A4 [artifact] README's opening and the welcome page carry the meaning line.

## Contracts

None.

## Decisions

- Owner, 2026-11-11: make the name's meaning visible; Detent's logo matches the Kronuz and Xapiand
  family; each of the three projects keeps a `brand/` directory (Xapiand's is its campaign's to do;
  Kronuz's exists).

## May land early

None.
