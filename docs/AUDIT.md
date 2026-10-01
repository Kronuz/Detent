# Source audit, October 2026

This review covers the owned C++ client, JSON schema and patch handling,
agent lifecycle and shared-memory layout, HTTP polling, distributor persistence
and HTTP routes, Raft adapter, Python and Rust bindings, and build/test wiring.
Dependency implementations are inspected where their contracts affect owned
code; this is not a full audit of every fetched dependency.

Changes are recorded here with their triggering conditions and validation.
Each audit fix is committed separately. Native tests run on macOS; Linux and
FreeBSD service templates require platform integration testing.

## Deployment limits

HTTP and UDP cluster traffic have no built-in authentication or TLS. Use an
access-controlled network and an authenticated TLS proxy for remote publishers.
The cluster adapter relies on a volatile Raft implementation and restart silence;
it is not a durable production Raft log. See `src/detentd/CLUSTERING.md`.

The shared-memory design currently uses a seqlock over non-atomic payload
fields. Its formal C++ data-race limitation is documented in `shm_format.hh`.
Stress-test success alone does not establish language-level race freedom.
No performance claim in this review is based solely on code inspection.
