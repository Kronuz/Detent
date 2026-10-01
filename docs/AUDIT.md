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

## Fixed: JSON string truncation and encoding validation

An escaped NUL in a knob name (`a:b\u0000hidden`) was truncated to `a:b` and
accepted. The new regression failed on the preceding implementation.
Parsers now use explicit JSON string lengths for field names, type names,
knob names, and group keys, validate UTF-8, and reject raw NUL bytes in JSON
text. Serializers preserve complete opaque group keys, including escaped
NULs. Schema, patch, and JSON-string regression tests pass.

## Fixed: invalid client poll intervals

The public C++ options (also used by bindings) accepted zero and negative
poll intervals, allowing a background source to loop without waiting.
Construction now rejects them before starting an updater. The file-source
regression checks both values with a configured polling source.

## Fixed: unsafe shared-memory file mapping

A reader mapped the full snapshot capacity without checking file size.
An empty or truncated runtime file could produce SIGBUS on access. Readers
now require a regular file large enough for the complete mapping and reject
symlinks. Missing files remain an ordinary startup condition; other open
errors are reported. Override offsets must also satisfy record alignment
before dereferencing. Regressions cover empty, short, symlink, and missing
files, alongside the normal agent publish/read test.
