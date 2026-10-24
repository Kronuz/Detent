# Reproducible dependency sources

[CMake's dependency manifest](../cmake/dependencies.json) records immutable revisions for direct and transitive fetched libraries. `DependencyPins.cmake` declares them before any library populates its own dependency graph, so FetchContent's first declaration wins consistently across authority, relay, managed-agent and reader builds. Changing the manifest triggers configuration again.

Profiles populate only the libraries they need. The manifest also pins the optional LZ4 and Zstandard source fallbacks; installed compression and TLS libraries remain system dependencies. The system-library discovery policy is unchanged. Record compiler, SDK/OS and system link inputs when reproducing a binary; source pins alone do not establish a hermetic toolchain.

The qualified completion baseline is `35d8e2a`, with consensus `b7073bb`, journal `57e6250` and metrics `9d7e724`. Newer sibling-library work remains independent. In particular, adopting the later completion `sync_files` API requires its separate durability-contract review and does not follow from updating a local sibling checkout.

A clean native macOS configure fetches 31 dependency trees from public repositories with no sibling overrides. All four daemon/reader executables build, and nine real-process checks pass in 103.13 seconds, covering multi-voter failover, snapshot catch-up, paged delivery and sustained writers. This run uses installed codec libraries; it does not qualify the optional fetched codec fallbacks. Exact source/dependency/binary fingerprints are retained in `.scratch/stage6-public-evidence.json` for this development run.

Developer overrides remain available through CMake's `FETCHCONTENT_SOURCE_DIR_*` variables. They override the manifest and can point at changing sibling work. Use immutable archives or detached checkouts for qualification. The system runner records actual Git revisions and content fingerprints rather than assuming an override matches the declared revision. Its build-source fingerprint distinguishes a copied source snapshot from an enclosing Git repository.

Detent no longer depends on cpp-btree. The model tables use `std::map`; see [Container selection](CONTAINER_SELECTION_ARCHITECTURE.md#decision-october-23).
