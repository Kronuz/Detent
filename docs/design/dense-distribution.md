> Status: DRAFT (2026-11-08) Ready for design review. Decisions D1 to D9 are the designer's; the full-copy wire format is the owner's (PLAN.md, "Reserved decisions"), so D2 and D3 stand as recommendations until Q1 and Q2 are ruled. Every byte figure is the [model](dense-distribution-model.py)'s, exact for the 5M corpus except where it says "records, modeled", and comes from the campaign jobs named beside it.

# Dense distribution

DTPAGE03 state reaches relays and agents as verified pages: a receiver checks and writes what it is sent, and builds nothing.

- Author: [Germán Méndez Bravo](mailto:german.mb@gmail.com). Created 2026-11-08. Quest `dense-distribution`.
- Base: `Detent:8874c71` (`main`); the page format is [dense-pages.md](dense-pages.md) as implemented on `quest/dense-pages` at `Detent:28d3ed9`, cited as dense-pages D*n*; [namespace-delivery.md](namespace-delivery.md) is cited as namespace D*n*.
- Depends on: dense-pages D1 (one digest, placement outside it), D5 (canonical bottom-up sealing and seal order), D7 (occurrence reuse), D8 (relocation computes no digest), D9 (canonicity); namespace D6 (records history and the cut), D7 (relays), D8 (derived ids), D10 (first touch, scrub, repair).
- Changes: namespace W6 (a frame names its target's root digest and carries pages or records); namespace D10.3 (repair fetches the damaged pages, not a full copy); dense-pages Q2 (answered by D2 and Q1 here).
- Model: [dense-distribution-model.py](dense-distribution-model.py) imports [dense-pages-model.py](dense-pages-model.py) at `Detent:28d3ed9` (SHA-256 `96dfd0a7…06bd1`) as the job ref `DENSE`, and runs on the r12 5M corpus (`corpus-5m.bin`, SHA-256 `bb87ae5d…2bc835`). Its receiver replays every transfer and must reproduce the target's root digest, and for a full copy the build's files byte for byte.

| Review | State |
| --- | --- |
| Design | not started |

## Abstract

PENDING

## Design

PENDING
