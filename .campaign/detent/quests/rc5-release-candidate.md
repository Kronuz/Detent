# rc5-release-candidate: RC-5: the composed release candidate

Risk: critical. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status rc5-release-candidate`. Planned from `docs/RELEASE_PLAN.md` RC-5 and its row "Packaging and product contract", `docs/STAGE6_ARCHITECTURE.md` components 5 and 6, `docs/EXTRACTION_CHECKLIST.md` (conditional candidates) and the lead's plan (M5 row);
the criteria are the plan's own, not new ones.

## Outcome

The integrated source and its exact public dependency closure are qualified, installable, upgradable and documented, and the owner tags the release.

## Acceptance criteria

- A1 The final supported-platform and sanitizer matrix on the integrated source, Linux rich/NFSv4 and FreeBSD NFS4/ZFS ACLs included (`reader-acl` condition C1): CI and jobs
- A2 Clean installation and build profiles with every dependency pinned and no live overrides (component 5): jobs
- A3 Service lifecycle: foreground, daemon, status, directories and restart (component 5): jobs
- A4 Compatible upgrade and rollback (component 5): jobs
- A5 [artifact] The product contract: topology, capacity profile, retry and result horizon, staleness, restore, audit limits, reader-before-writer upgrade (component 6): `docs/PRODUCTION_CONTRACT.md`
- A6 [decision] Keep-local decisions for the conditional extraction candidates (`docs/EXTRACTION_CHECKLIST.md`): a Decisions row
- A7 [artifact] Installation and measured limits documented (RC-5 closure): `docs/USAGE.md`
- A8 [decision] The release is tagged, by the owner (reserved; the Homebrew tap stays per D22): a Decisions row

## Contracts

To be fixed when the lead splits this quest.

## Decisions

1. Critical: the release freezes formats and the public ABI. Owner approval and a `design` review.
2. `legacy-h`, `actors-docs`, `how-it-works` and `quickstart-managed` feed A5 and A7.

## May land early

None until the lead splits it.

## Inputs to weigh at split/start

- Xapiand triage (msg-13): #11 packaging: deb, rpm and a container image per release, each with a service unit; #28 sandboxed hosts: run the service under gVisor/seccomp and check reactor, io and http socket options (Xapiand saw SO_LINGER rejected); a shared-library fix is made with Xapiand.
- D47: #25 the security page states no at-rest encryption (use disk or volume encryption; replication is mutual TLS with pinned certificates); #26 voter addresses accept DNS names, the voter set stays static and pinned.
