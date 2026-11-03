> Status: CURRENT (2026-11-03) how `detent::authority::Node` bounds the decoding of an applied command group, read from `src/detent_authority/node.cc` on `chore/salvage`.

# Historical command groups

`Node::advance_application` decodes each committed command entry with a `BatchDecoder` before it
applies the group. The decoder takes a byte bound for the encoded group.

The bound is `options_.scheduling.retained.command_bytes`, the `RetainedStatePolicy` command
bound of the consensus worker. It defaults to the operating default (1 MiB) and is set apart
from `options_.consensus.command_bytes` ([retained-state envelope](authority-retained-state.md)). A node that lowers the operating command bound still reads
groups its journal accepted under a larger bound; new proposals stay under the operating bound
(`propose` and `propose_prepared` check `consensus.command_bytes`).

`tests/detent_authority/test_node.cc` `retained_application_range(..., lowered)` writes a group of
32 transactions over 2048 bytes, restarts with `consensus.command_bytes = 2048`, and requires the
node to apply through the whole range without fencing.
