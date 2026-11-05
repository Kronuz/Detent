# Dense deltas and relocation

The overlay addresses an immutable page plus an inline node offset. The assembler
feeds changed nodes and clean canonical clusters into the resumable packer. It
retains a base extent only at the corresponding entering trie path with equal
digest, length, keys and height. Final removal accounting belongs to assembly:
opening an old page does not imply that its occurrence becomes unreachable.

Relocation copies existing page bytes and changes only external placement
entries. The parent content tables, page digests and root commitment remain
stable. Selected pages and their ancestor pages receive new extents.

Validation: the standalone Debug fixture `test_dense_delta_assembly.cc` passes
50 seeded mixed insert/update/delete trials over 2,000 definitions against an
independent full-build packer oracle. It compares occurrence paths, summaries,
retained physical extents and live accounting. It also covers repeated content,
no-change and no-net-change zero writes, both directions across an inline/sealed
boundary, partial and full relocation with zero page hash calls, one-byte budgets,
and empty/add-watermark/delete-last-watermark transitions. The executable links
the production overlay, assembler, packer, segment builder, codec and image code.

Pending: composed full-builder/read-validator integration, sanitizer execution,
and updated pre-dense delta/compaction fixture execution. No throughput or write
amplification improvement is claimed from the fixture run.
