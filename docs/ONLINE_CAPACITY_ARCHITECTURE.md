# Online authority capacity

**Author:** Germán Méndez Bravo

**Date:** 2026-11-20. **Status:** Not planned. Raising an existing group's limits at a coordinated restart replaces it; that build is a separate quest.

A running group does not change its `application_limits`. An operator raises `keys`, `state_bytes` or `image_bytes` by restarting every voter with the larger values. The replicated online transition this document used to propose is dropped; its last version is `Detent:03da5be5` at this path.

## Why not online

1. A ceiling is an admission bound, not a reservation. `State` checks its limits and reserves nothing in proportion to `keys`, `state_bytes` or `image_bytes` (`src/detent_authority/state.cc:764-780`); the journal pools it must fund are logical admission budgets (`src/detent_authority/service.cc:370-380`). The October 23 trials ran 1K to 130K knobs under an explicit 5M profile; at 1K the seven services' sampled peak sum was 89.6 MiB ([capacity qualification](CAPACITY_QUALIFICATION.md#full-system-trials-october-23)). Sizing the envelope at startup costs little.
2. The online transition touches the layers most expensive to reverse: a replicated capacity command and profile epoch in State, a new application image version, per-position batch decoding, a proposal gate behind a read barrier, a two-format extension of the shared consensus Worker, a new administrative credential and route, and six qualification stages ending in a 5M three-voter run.
3. The parked design predates namespace history. History decodes blocks, seals and tables against the limits (`src/detent_authority/history_format.hh:51-139`) and sizes the peer fetch lane from them (`src/detent_authority/service.cc:528`, `:654`); the image is `DTAIMG03` (`src/detent_authority/state.cc:1983`). Building it would start with a redesign.
4. No release exit needs it. RC-3 grows a fresh group configured for 5M without recreating it ([qualification plan](CAPACITY_QUALIFICATION.md)); RC-4's restore configures its target's profile ([restore](RESTORE_ARCHITECTURE.md)).
5. Reads never wait on the authority. Agents keep serving their last generation while upstream is down ([actors](ACTORS.md)), so a raise at a restart pauses publication, not reads.

## Raising capacity at a coordinated restart

The mechanism the build quest implements:

1. Only `keys`, `state_bytes` and `image_bytes` rise, and only upward. The other seven limits stay equal. Recovery under a configured value below the recorded one refuses to start and names the field.
2. Every replicated decision uses the limits its entry carries. Each `DTABAT02` group and `DTAADM01` administrative entry already writes all ten limits in its header (`src/detent_authority/state.cc:1207`, `:1813`); the decoders accept a carried profile within rule 1 instead of requiring equality (`:1236`, `:1828`), and the entry applies under it. A group that failed `Capacity` before the raise fails the same way on every replay.
3. Each use of `State::limits_` is either a replicated decision (rule 2) or a local bound such as image capture size; the build classifies every site.
4. Image recovery accepts a recorded profile within rule 1 (`src/detent_authority/state.cc:2124` requires equality today). The recovered State runs under the configured limits, and later captures record them.
5. The peer fingerprint keeps binding the profile (`src/detent_authority/peers.hh:16`). Voters with different profiles refuse each other, so a group runs one profile at a time; a rolling restart stops publication until a quorum shares the new profile.
6. A producer retry of a sequence accepted before the raise returns its original receipt: the command digest (`src/detent_authority/state.cc:366`) does not depend on the raisable fields.
7. No new bytes on disk or on the wire, and no change to the consensus library.

Relay, agent and reader capacities are local settings and change with their own restarts. Agent images bind the delivery profile (`include/detent/cursor.hh:33`), not the authority's limits, so agents keep their generations across a raise.

## What this gives up

Publication pauses for one coordinated restart of the voters. A single voter reached readiness on 5M knobs in 37.74 seconds in the October 24 trial ([capacity qualification](CAPACITY_QUALIFICATION.md)); that is one sample on older code, not a bound. A raise never happens without that pause, and limits never decrease.
