# server-composition: One server composition note for Prism, Detent and Xapiand

Risk: normal. Repo `../cpp-libraries`. After service-shape. Design assignment. The lead writes this
quest; contributors read it and never edit it. Machine state: `campaign status server-composition`.

## Outcome

`docs/server-composition.md` in cpp-libraries is the one canonical note on how a Kronuz server is
put together (owner, Xapiand D74: the three mirror each other in shape, not only in which library
owns what). It extends service-shape's design and changes no code; each app adopts it in its own
quest.

## Acceptance criteria

- A1 [artifact] The composition, in order: flags mapped onto one http limits struct (D81), TLS
  setup, services started by `serve`, `stop_signals`, housekeeping as services on the loop's wheel
  (D76, D79, D80), with the code each step calls: `docs/server-composition.md`.
- A2 [artifact] One table of option names and defaults shared by the three (connection bound,
  idle timeout, in-flight cap, shutdown bound, TLS files), each name's current spelling per app:
  `docs/server-composition.md`: "Options".
- A3 [artifact] A side-by-side table of the three mains (detentd and detent-relay, prism,
  xapiand) by step, each deviation with its reason; a deviation kept for cost names the
  measurement under Xapiand D62 (mixed shape only past 5% CPU or 10% p99 after optimizing):
  `docs/server-composition.md`: "Mains".
- A4 [manual] The Xapiand lead agrees to the note.

## Contracts

Consumes `task:service-shape`. Provides nothing (a note).

## Decisions

D76, D79, D80, D81, D82.

## May land early

Nothing.
