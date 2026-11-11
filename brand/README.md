# Brand

A detent is the click-stop that holds a knob at its setting. `logo.svg` is the vector Detent logo:
a white knob set at one of eight click-stops, cut out of a green rounded hexagon inside a white
rim. It shares its silhouette with Xapiand's logo and its green `#93c34d` with Kronuz's, in four
paths: `rim`, `hexagon`, `knob` and `stops`. Its origin is the hexagon's center.

| Part | Geometry |
|---|---|
| Rim | Xapiand's rim: a regular hexagon, half-width 90, corners rounded with radius 39.3, then stretched 1.039 vertically |
| Hexagon | Xapiand's hexagon: the rim scaled by 5/6 about the center (half-width 75, corner radius 32.75) |
| Knob | A disc of radius 42, with a pointer slot cut from the center toward the upper-right stop: a capsule 16 wide whose round end stops 8 short of the rim |
| Stops | Eight dots of radius 7 on a circle of radius 63, every 45° from the top; the nearest clears the hexagon's edge by 9 |

`family.png` shows the logo beside the Kronuz and Xapiand logos, 512 px tall on light and dark
and at 16 and 32 px scaled up; `family.sh` renders it.

`node brand/build-icons.mjs`, run from the repository root after `npm ci` in `website/`, renders
every variant from the master into `variants/<name>/`, rasterizing each PNG once at its exact
size, then copies the site's files into `website/`:

- `logo.svg` is the master in the variant's colors.
- `favicon.svg` serves tabs at any DPR, and `favicon.ico` (16, 32 and 48 px frames) is the
  fallback. At those three sizes a gamma on alpha firms the outline.
- `apple-touch-icon.png` (180 px), `-167` and `-152` serve iOS, and `icon-192.png` and
  `icon-512.png` serve Android. They are opaque tiles in the variant's page color, because iOS and
  Android flatten a transparent icon onto white. The tile takes the rim's place, and the hexagon
  fills 74% of its height, inside the maskable safe circle (r = 40%).

| Variant | Colors | For |
|---|---|---|
| `light` | The master: green `#93c34d`, white knob, stops and rim; tiles `#fdfdfc` | Light pages and the touch icons |
| `dark` | No rim, so no white halo on a dark page; tiles `#1a1a1a` | Dark pages, and the site's logo and favicon, since without the rim it reads on both themes |

The site serves `dark/logo.svg` as `website/src/assets/logo.svg`, `dark/favicon.svg` and
`dark/favicon.ico` in `website/public/`, and the `light` touch tiles beside them.
