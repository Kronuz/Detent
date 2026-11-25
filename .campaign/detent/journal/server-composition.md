# server-composition: journal

State: ready for review
Quest: .campaign/detent/quests/server-composition.md (the lead's; never edit it)

## Doing

Draft committed at cpp-libraries:b1c27d9 (`docs/server-composition.md`, about 1,060 words of prose plus
the tables). Docs check job 261112-110157-56d88b queued at that commit.

## Next

1. Lead sends the draft to the Xapiand lead (A4) and relays answers to Q1-Q5.
2. README pointer: not done; the quest's file set is `docs/server-composition.md` only. For the lead:
   one line in cpp-libraries' README linking `docs/server-composition.md`.

## Blocked on

Nothing. Questions Q1-Q5 in the note carry options and recommendations (owner or Xapiand lead).

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | `docs/server-composition.md`: "Composition"; seven steps (options, limits as http::Limits, TLS, loop and services, serve, signals, housekeeping), each with the code it calls; the check in "Checks" prints 7 |
| A2 | `docs/server-composition.md`: "Options"; eight rows (connection bound, in-flight cap, idle timeout, shutdown bound, TLS files, port, bind, reactors), each app's spelling with file:line; the Options check prints 8 |
| A3 | `docs/server-composition.md`: "Mains"; eight steps by four mains, file:line cells; the deviation table names each reason and its D62 status. No deviation has a D62 case today; the 1 ms driver loops are the one that can earn one |
| A4 | the Xapiand lead agrees at cpp-libraries:b1c27d9 once F1-F3 are in the text; the lead folded F1-F3, the owner's Xapiand D76 and D77 rulings and the sizing at cpp-libraries:2fd6ecb |

## Result

`docs/server-composition.md` in cpp-libraries. It extends task:0e71a38's service shape with the steps
before `serve`: a proposed `http::Limits` (contract for the http quest), `reactor::tls::server_context`
(Xapiand's `tls_setup` lifted), shared option names and defaults, and the three-app table. No code
changed. Cites read at Detent:11f793c5, prism:e0aea0e, Xapiand:9afebe6c5, http:473bbc3, reactor:4de16f2.

Findings: Xapiand's `--max-clients` bounds no connection (`manager.cc:950`); Xapiand sets idle, header
and inactivity timeouts only on TLS, Detent and Prism have none; the three apps pin three http versions
(Detent c813531, Prism d76c5ef with `tls_connection_factory_timed`, Xapiand 51d153a); service-shape's
Detent line numbers have drifted by hundreds of lines.

## Rulings I made

- Option names are flag and config key alike; durations are milliseconds with `_ms` (note C3).
- Defaults 1024 for `max_clients` and `max_in_flight`, the largest present value (Q5).
- A differing value is an input, not a deviation (C4).
- `Limits` fields beyond D81's text (header, inactivity, request timeouts, TLS handshake) come from the
  setters http has today.
