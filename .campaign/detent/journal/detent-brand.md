# detent-brand: journal

State: waiting on the owner's pick (A1)
Quest: .campaign/detent/quests/detent-brand.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Stage one done: three candidates and the comparison sheet are in `brand/candidates/`.

## Next

1. Owner picks A, B or C from `brand/candidates/sheet.png` (lead relays); record the pick here.
2. Stage two: the pick becomes `brand/logo.svg`; derive `variants/{light,dark}/` (favicon.svg/.ico,
   apple-touch-icon 180/167/152, icon-192/512, logo.svg) with a committed build command, like
   Xapiand's `docs/scripts/build-icons.mjs`; wire the site assets (A3); meaning line in README and
   welcome page (A4).

## Blocked on

The owner's pick (A1).

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | Candidates `brand/candidates/{a,b,c}.svg`, sheet `brand/candidates/sheet.png` (rebuild: `brand/candidates/sheet.sh`); pick: pending |
| A2 | <stage two> |
| A3 | <stage two> |
| A4 | <stage two> |

## Result

`brand/candidates/`: `a.svg`, `b.svg`, `c.svg` (written by `candidates.py`, which records each
one's geometry) and `sheet.png` (by `sheet.sh`): Kronuz, Xapiand, A, B, C, each 512 px tall on
light `#fdfdfc` and dark `#1a1a1a`, and at 16 and 32 px scaled by whole factors (18x, 9x).

- A, traces: Kronuz's language. Six gray traces wind around the hexagon (each runs 70% of an
  edge, then turns to run parallel to the next) and end in inner pads, the six stops; the stop at
  one o'clock is green and the green knob's cut-out pointer aims at it. Trace and pad sizes are
  Kronuz's (trace 11.4% of the circumradius, pad ring 12%/4%).
- B, solid hexagon: Xapiand's rim and hexagon paths verbatim, with a white knob (cut-out green
  pointer) and seven white stops over 270 degrees, the pointer at the 1:30 stop.
- C, notch ring: six gray chevrons make the hexagon, the gaps between them are the notches; a
  green ball sits in the one o'clock notch and the green knob's cut-out pointer aims at it.

## Rulings I made

- Every candidate uses `#93c34d` and `#a7a7a7` only (B also white, as Xapiand's master does); no
  gradients, strokes and fills only, no rasters.
- C's ring is 14% of the circumradius, 1.23x Kronuz's trace, so it survives 16 px.
- The sheet takes Xapiand's vector master from its unlanded `xapiand-brand` worktree when
  `Xapiand/brand/` is absent (`XAPIAND_LOGO` overrides), rather than upscaling the 180x230 PNG.
