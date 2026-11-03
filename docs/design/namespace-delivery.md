> Status: BRIEF (2026-11-03). Owner rulings for the namespace delivery design; the designer drafts the rest.

# Namespace delivery

The design covers namespace-granular delivery, agents seeded from an image shipped with the application, and fast agent restart. The agent-startup measurements (`docs/perf/agent-startup-2026-11-03.md`, branch `explore/agent-startup`) are its input.

## Decisions settled by the owner

**D1. The namespace is the one unit.** A namespace is the unit of atomic writes, consistent reads, subscription, revision, cursor, delta retention, relay caching and consensus-group placement. Detent offers no read consistency across namespaces: a client sees each namespace as a consistent snapshot that never goes backwards, independently of the others. Knobs that must change together, or be read as of one moment, belong in one namespace. Ruled 2026-11-03.

**D2. All namespaces by default.** An agent or client that names no namespaces follows every namespace its credential allows, including namespaces created later. A namespace list narrows the subscription; it is never required.

## Open for the designer

1. Per-namespace generations on the agent, shared by every application on the host, and how a client opens the namespaces it reads.
2. The namespace directory that "all namespaces" follows: its shape, revision and delivery.
3. Durable per-namespace delta history on the authority or relay, so an agent seeded from an old image asks for the changes since its revision instead of a snapshot.
4. Seeding from an image: a prepared generation built offline must be byte-identical to one an agent writes, and an agent starts serving from it before it contacts any upstream.
5. Agent restart over a prepared store in time independent of dataset size, using the startup measurements.
6. The cursor, scope identity, relay bundle and agent store formats that replace the per-scope global revision.
