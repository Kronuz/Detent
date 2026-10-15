# Owned snapshot sources and bounded transfer

This component implements the snapshot source and transfer portion of durable-storage Stage 2. The Architect supplies one component blueprint, implementation proceeds autonomously through scoped commits, and final review covers the completed component. It does not select the public authority transaction protocol or provide socket/TLS integration.

## Validated Architectural Blueprint

1. Scalability: One outbound source and one incoming/local replacement per Worker, with independent 64 KiB mailboxes. Maintenance rotates among checkpoint preparation, source work and reclamation without erasing timer debt. One bounded decoder and small control-output slot per configured remote voter, plus one outbound data frame and stop-and-wait credit, bound transfer memory independently of image size.
2. Edge cases: Persist application format in version 2 local bundles. Keep version 1 readable as unknown format and refuse to infer export format. Bind a source to a completed Core boundary, completed publication and currently selected pinned dependency. Revalidate before trusted readiness; retain callbacks while busy. Cancel legal staging on disconnect, and complete irreversible publication/activation independently after TooLate.
3. Dependency and state: Reuse weak-session published selections and the shared incremental-verification lease pool. Read fixed metadata before bounded verification, promote without IO and stream through the same pinned file. Preserve ordinary Worker outputs and application callbacks. Only a final receiver installation result can advance the sender's correlated Core flight; retain result admission across Busy or Pressure.
4. Security and scale: The adapter accepts externally authenticated reliable ordered sessions with fresh connection identities. Bind configuration, both nodes, both sessions and the complete transfer key in every exact-length frame. Malformed peer input closes its channel without fencing healthy storage. Control output remains independent of data; older legacy Raft APIs and wire formats remain unchanged.

## Component acceptance

Generic publication selection and incremental opening are committed in cluster `1951c79`; their 98,254 native/sanitizer Journal checks cover selection ownership, zero-body-read opening, shared capacity, pin/reclamation accounting and POSIX reopen. Cluster `99d01be` adds persisted format provenance, Worker-owned sources and a bounded bulk adapter. Final consolidated Architect review gives GO after five concrete corrections. All eight native cluster CTests, both ASan/UBSan consensus/Worker CTests and all 23 existing Detent CTests pass. Core and Worker suites pass 36,171 and 92,227 checks respectively in both profiles; the correction suite passes 1,295 checks.

Tests connect actual sender, receiver and healthy-voter Workers on independent POSIX stores. Receiver validation, durable publication and application activation precede its generated installation result; ordinary replication then supplies the suffix. Failure schedules cover pre-publication reconnect, post-publication disconnect, lost final result and fresh prefix proof. Existing normal-close reopen schedules cover publication and activation windows. They are not abrupt process-kill tests.

Qualification includes malformed/truncated frames, fragmented input, partial output, credit backpressure, duplicate Begin/Accepted, one-frame dispatch, term-zero failure, fenced TooLate teardown, session replacement, empty/multiple-chunk images, source IO/CRC faults, foreign callbacks and simultaneous held inbound/outbound views while ordinary proposals, reads, timers and reclamation progress. Measurements cover 1 MiB and 64 MiB images and distinguish occupied frame bytes, allocated payload buffers and application/fixture RSS.

## Remaining architecture gates

Production transport must provide authentication, authorization, socket scheduling and connection lifecycle. This component has no reconnect resume or chunk retransmission; it retries via correlated Core failure and fresh prefix probes. Authority transaction/session/CAS integration, immutable agent views, resumable relay distribution, crash/process-kill integration and fleet/capacity qualification remain pending. The narrow snapshot envelope does not replace the planned measured comparison for public transaction encoding.

## Local measurements

The regenerated Release benchmark transfers 1 MiB in 1.30680 seconds (0.053057 CPU seconds) and 64 MiB in 2.41281 seconds (1.017820 CPU seconds). Both cases peak at 131,224 occupied frame bytes and 459,519 allocated payload-buffer bytes. Process peak RSS is 8,581,120 and 412,426,240 bytes respectively, including application/fixture copies. Full wire/read counters and reproduction commands are in the [cluster component documentation](https://github.com/Kronuz/consensus/blob/b7073bbe4d94e66dce526f193b0dd81a57cdb661/README.md). These single local samples establish neither production tails nor fleet capacity or power savings.
