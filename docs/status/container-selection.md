# Container selection and extraction

**Updated:** 2026-10-24. P0 is not closed on main. The contributor's `track/container-selection` branch is the authoritative model implementation; the lead's duplicate assignment fix stays unpublished.

1. Contributor tip `da4ea02` includes strong copy assignment in `1234d08`. The [fix comparison](../reviews/container-fix-comparison-2026-10-24.md) confirms the original defect is addressed for current comparators and reproduces two generic comparator contract gaps. Do not replace the contributor's stronger failure tests.
2. The [architecture recheck](../reviews/container-extraction-architect-2026-10-24.md) rejects btree `44cb140`'s mutable-pair/const-pair view as a portable lifetime repair. Keep that branch experimental. No safe older rollback is established by the supplied failure sweeps; do not revert isolated correctness repairs blindly.
3. The extracted [flat-map library](https://github.com/Kronuz/flat-map) at `a9a52b02e387ab054ff38647d3f141f9f8c00593` includes the contributor's corrected engine, conditional comparator moves and a nonthrowing copy-commit constraint. Its [seven-job matrix](https://github.com/Kronuz/flat-map/actions/runs/40095399514) passes Linux x86/arm, macOS x86/arm, FreeBSD and Linux ASan/UBSan/LSan. Local standalone tests pass 2/2; macOS ASan/UBSan passes with leak detection disabled. Catalog adoption is recorded in `67fd84d`.
4. Consumer adoption is pending. Keep one engine after adoption: a forwarding alias is acceptable, a private fallback implementation is not. Preserve the public C++ rebuild requirement and exact serialized bytes.
5. Full enabled-product qualification of contributor tip `da4ea02` runs in `.scratch/reader-capacity-build`; build/test logs are `.scratch/container-corrected-{configure,build,tests}.log`. Authority, relay, distributor, managed agent, examples and benchmarks are enabled. Do not modify that checkout while it runs. A completed build at earlier `59e8c8f` does not qualify the subsequent assignment fix.

## CI blockers separate from container behavior

The first product branch run fails the macOS jthread probe because its Apple toolchain lacks the required library support. Shared workflow revision `8a88961` adds opt-in `macos_llvm: true`; other callers' defaults are unchanged. The product workflow also needs `DETENT_BUILD_RELAY=ON` to cover relay.

Linux arm64 runs 211 tests and fails three system tests before executing their workloads: generated scratch files overflow bounded git-status metadata. Main's `.scratch/` ignore correction addresses that source of output without relaxing metadata limits. The isolated workflow correction is preserved at `1415473` and is not pushed over the other contributor's branch. Coordinate before transplanting it.

The branch's existing flake, bindings, composed failure/byte checks, final public-pin qualification and sustained capacity remain open. Native baseline trials and model benchmarks do not establish full-system release readiness.
