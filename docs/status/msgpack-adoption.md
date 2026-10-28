# Bounded MessagePack adoption

**Updated:** 2026-10-28. **State:** Isolated candidate on current main; no adoption. The [validated blueprint](../MSGPACK_EXTRACTION_ARCHITECTURE.md) defines the remaining gates.

The candidate replaces the private lexical engine with `msgpack::bounded`, retains 200-byte token storage and 512-byte encoding scratch, and uses the existing canonical packer. Schema validation and resource scheduling stay local. The internal `detent_distribution/messagepack.hh` header is removed; internal callers use `wire_codec.hh`, and external lexical consumers use `msgpack/bounded.hpp`. Matching consumers must rebuild.

The library prerequisite is msgpack:f8975e3 on `track/bounded-integration`, with eight green hosted jobs in run 40721775099. Bounded-only native tests pass 2/2 without fetched dependency sources; the default DOM profile passes 4/4 including header coexistence. The initial configure at msgpack:3c408cf fails because that public revision lacks the saved, uncommitted bounded extension; it is not used for qualification.

The reconciled public-pin consumer builds all enabled products and passes 66 focused cases in 153.36 seconds, plus bootstrap-retention/TLS setup 3/3. Fourteen captured logical payloads are byte-identical at 34-byte and 200-byte key lengths across raw and zstd framing paths. This comparison covers decoded logical bytes, not compressed-frame identity. The [native record](../measurements/msgpack-adoption-native-20261028.json) preserves exact hashes, scope and remaining work.

Production-path CPU, maximum owner-turn and admitted-memory measurements, full consumer platform/sanitizer checks, and consolidated component review remain open. No unrelated dependency pins change. Saved original candidates remain backed up under `.scratch/msgpack-adoption-20261028/recovery/`.
