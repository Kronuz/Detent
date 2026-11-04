> Status: CURRENT (2026-11-03) How the test generation writer works. Workstream: reader-s1-s4. Design: [reader.md](../design/reader.md) D9, ruling R2.

# Test generation writer

`detent::testing::GenerationWriter` (`include/detent_testing/generation_writer.hh`, `src/detent_testing/`, library `detent_testing`) writes real generations into a store directory so tests read values the way production does. There is no override path in a reader (R2); this library is the only way tests get values, and no reader links it.

## One publication

```
JSON  -> detent::parse_defaults -> Snapshot (ns:name -> KnobDef)
      -> distribution wire (MessagePack, grouped by namespace)
      -> SnapshotDecoder -> ValidatedSnapshot
      -> PagedSnapshotPlanBuilder (base = recovered selected generation, if any)
      -> DiskStore::begin(plan), driven to Published
      -> ManagedRecovery::recover + confirm_recovered
```

This is the agent's path: the store writes the segments, root image, lease, `.read` reader metadata and the `selected` descriptor. Each call publishes the next cursor revision with exactly the given knobs (not a merge). A knob set is replaced whole, so a test that wants to change one value publishes the set again.

## Surfaces

- C++: `GenerationWriter(path)`, `publish(Snapshot)`, `publish_json(json)`, `revision()`, `store()`. Opening an existing store continues from its selected revision.
- C: `detent_testing_open`, `detent_testing_publish(writer, json, length, &revision)`, `detent_testing_error`, `detent_testing_close` in `generation_writer.h`, for Python and Rust tests.

## Not yet

The `change` file and its per-namespace sequences belong to S5, so the writer moves only the selected generation. The hand-built fixture (`tests/detent_local/prepared_reader_fixture.hh`) stays for malformed-input tests.
