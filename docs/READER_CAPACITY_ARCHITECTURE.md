# Managed CLI reader capacity

**Status:** Source review and focused native/process qualification passed; real CLI at 1M remains open.

The reader and status commands currently open managed generations under default limits. Startup daemon capacity does not establish capacity for those independent consumers. Extend the existing public `agent.json` schema with typed unsigned reader settings; preserve flag, environment and configuration precedence. Keep signed legacy fields unchanged.

Configured reader admission requires a checked, root/effective-user-owned regular file without group/other write permission or a final symlink. Inspect the same descriptor used for the bounded read and reject an altered extent. Legacy files without reader settings retain their current behavior. Never read capacity authority from image metadata or private delivery credentials.

One private command-owned holder forwards existing `ViewLimits`, heap and mapping ledgers, and the retirement runtime/domain. Without explicit settings, preserve the default path. Explicit settings preserve the existing 1 MiB runtime baseline, 8 MiB heap, 512 MiB mapping and 16,384 resident-owner defaults. Ownership count is independent of payload or segment capacity. Sources and views must be destroyed before the holder closes its domain and joins the runtime.

Wire both status `open_generation` and managed get `ClientSourceOptions`; retain identity validation, stopped-daemon reads, defaults and the legacy shared-memory path. The reader remains independent of delivery, HTTP and TLS parsing. No new public ABI change is needed beyond the capacity component's existing `ViewLimits` change.

Validation covers unsigned widths, unsafe/duplicate configuration, symlink behavior, precedence, independent resource teardown, explicit rejection and acceptance through both read boundaries, and the actual CLI at the 1M checkpoint. Complete the component before its boundary review; no per-commit approval is required.

The component passes the capacity unit test and existing CLI suite (2/2, 7.23 seconds), plus real managed flat and TLS relay/paged process checks. The source review finds no remaining blocker. The baseline startup-capacity component separately passes 217 product tests; this component has not yet repeated that complete suite.
