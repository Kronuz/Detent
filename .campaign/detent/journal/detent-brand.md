# detent-brand: journal

State: blocked on lead: file set for the touch-icon head links (astro.config.mjs)
Quest: .campaign/detent/quests/detent-brand.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Stage two done but for the touch-icon `<link>`s, which need `website/astro.config.mjs`.

## Next

1. Lead: extend the file set to `website/astro.config.mjs` (+ `website/public/site.webmanifest`)
   for the touch-icon head links (see Blocked on).

## Blocked on

Lead (file set): the site can only use the touch icons through `<link rel="apple-touch-icon">`
(and a manifest for icon-192/512) in Starlight's `head`, which lives in `website/astro.config.mjs`,
outside this quest's files. The site is under `/Detent/`, so iOS's root-path convention
(`/apple-touch-icon.png`) does not reach them. Options: (1) add `website/astro.config.mjs` and
`website/public/site.webmanifest` to the file set and I add the links (recommended); (2) land
without, the PNGs served but unlinked.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | [manual] Owner chose B plus a bottom stop, eight stops 45° apart (D67, Detent:0adf85db); candidates at Detent:1988b239 `brand/candidates/` |
| A2 | `brand/logo.svg` (4 paths, no rasters), `brand/README.md`, `brand/build-icons.mjs`, `brand/variants/light/` and `brand/variants/dark/`, each with logo.svg, favicon.svg, favicon.ico (16/32/48), apple-touch-icon 180/167/152, icon-192, icon-512; rerunning `node brand/build-icons.mjs` at Detent:ccb584b6 leaves the tree unchanged |
| A3 | 261111-130317-c63a14: site builds (63 pages), dist favicon.svg/.ico and touch PNGs match `brand/variants/`, header logo is the new master, 11 local asset links resolve. Touch-icon `<link>`s pending (Blocked on) |
| A4 | `README.md` opening sentence and `website/src/content/docs/welcome.md` "So what is Detent" carry the line |

## Result

- `brand/logo.svg`: candidate B with eight stops every 45° from the top, the pointer at the
  upper-right stop. The bottom stop clears the hexagon's edge by 14.7 (the top's mirror); the
  diagonal stops are nearest, at 9.05. `brand/family.png` (by `brand/family.sh`): the logo beside
  Kronuz and Xapiand at 512 px light/dark and 16/32 px scaled; the pointer reads at both.
- `brand/build-icons.mjs` (Xapiand's `build-icons.mjs` ported; sharp from website/) renders
  `variants/{light,dark}/` and installs the site's copies.
- `brand/candidates/` dropped from the tree (history at Detent:1988b239).

## Rulings I made

- Every candidate used `#93c34d` and `#a7a7a7` only (B also white, as Xapiand's master does).
- The site serves the rimless `dark` logo and favicon on both themes (on a light page the white
  rim is invisible anyway; on a dark one it would halo), and the light touch tiles.
- `website/public/footer-logo.png` is GitHub's "hosted by" mark, not Detent's: left as is.
- The build script lives in `brand/` (website/package.json is outside the file set), loading
  the site's sharp through `createRequire`; command `node brand/build-icons.mjs`.
