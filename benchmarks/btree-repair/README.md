# Independent btree failure probes

The [copy evidence](../btree-isolated-copy-20261022.json) records compiler/platform qualification, cost ledgers and the remaining application failure. Compile each source against the selected header revision, using C++20. Use an isolated output directory and the workspace quiet protocol.

`copy_refusal.cc` copies three values and refuses the third mapped-value construction. On `2e93b45` it leaves one copied value live and exits 1; on `5aadde0` it releases the copied prefix and exits 0. This is an independent lifetime regression probe, not a general insertion-failure test.

`growth_refusal.cc` includes Detent's actual model, seeds a patch with a nested definition and permits one allocation before refusing another during compact-root growth. Compile with both the application's `include/` directory and the selected cpp-btree headers. On the recorded Linux profile it prints a 160-byte allocation followed by a refused 56-byte allocation, then reports one unreleased allocation on both revisions. Its exit code checks the attempted operation's refusal and retained map size; inspect the explicit allocation balance to detect this leak. LeakSanitizer alone did not report the block in this tiny probe, although the full publication sweep reports 3,120 bytes in 20 allocations.

The intended distinction is that the copy correction closes its targeted ownership defect while leaving an existing mutation defect visible. Neither probe claims that const-key mutation is defined behavior, that all allocation failures are covered, or that the production pin has changed.
