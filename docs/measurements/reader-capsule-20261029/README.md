# Reader capsule prototype screen

**Status:** Passes the bounded prototype screen. Production integration and acceptance remain pending.

The reader prototype is `Detent:9465d8a`, published in the history of `track/reader-capsule`. It combines the existing storage, root and reader metadata records in 624 bytes. Production writers, readiness and collection remain legacy at this revision. The owner approves the changed refusal behavior and coordinated rollout described in the [architecture](../../READER_CAPSULE_ARCHITECTURE.md).

## Result

One exclusive, rotated four-arm run uses twenty unchanged integer keys from the persisted 5M corpus, a matching fresh-process warmup for every sample and the same caller-allocation observer in all arms. All eighty measured reads return the expected values and 5M cardinality. This is an instrumented screen, not the uninstrumented 200/200 acceptance gate.

| Resource closure | Current median wall | Capsule median wall | Paired wall delta | Paired caller CPU delta | Wall wins |
| --- | ---: | ---: | ---: | ---: | ---: |
| Private | 740.349 µs | 706.783 µs | −44.764 µs | −44.785 µs | 17/20 |
| Canonical | 748.288 µs | 713.038 µs | −27.195 µs | −27.107 µs | 14/20 |

Paired deltas are medians of per-key differences; they need not equal the difference of the two column medians. Both closures meet the reviewed screen criterion: paired end-to-end wall and caller CPU benefit. Every pair uses four fewer caller allocations. Private capsules incur four more minor faults at the paired median; the canonical median difference is zero. No major faults occur. RSS is not measured in this screen.

The measured probe's executable text grows by 4 KiB in both closures, with its 4 KiB data segment unchanged. LLVM 23 x86-64 prologues reserve 3064 versus 3528 local stack bytes in `PreparedReader::open`, an additional 464 bytes excluding saved registers, return address and callees. These byte counts are not resident-memory measurements.

## Method and evidence

The APFS clone helper creates independent regular, single-link files with matching owner, permissions and size, while holding the source selected-generation lease. Both trial directories have equal depth and equal-length names. The converter validates existing records and bindings, acquires the copied generation's exclusive lease and changes only the copied `.read`. It never changes the published source corpus. `corpus-summary.json` summarizes the checked clones; its full local inode manifest is identified by hash.

`screen-inputs.json` records the original qualification checkout heads, compile commands and archive hashes. `build_screen.py` replaces only the prepared-reader object in separate copies of each archive and adds the capsule codec to proposed arms. It reuses the same probe source and production dependency closure. The original archives are unchanged. `screen-source-commit.txt` identifies the frozen prototype used to build the arms, before later documentation and test-only commits.

`screen.json` contains every measured phase, expected value, ordering, warmup duration and binary/controller hash. End-to-end timing is factory+client+lookup and excludes executable launch. `score_screen.py` validates cardinality and pairing before computing the criterion. The one-key `smoke.json` checks the complete harness before the exclusive run; its timings carry no acceptance claim. `focused-tests.log` records five focused Release passes and one privileged-path skip. `ancestor-ci-failure.txt` preserves the separate, earlier snapshot-test failure addressed by `Detent:12bad92`.

## Reproduction and remaining gates

The scripts expect the qualification workspace layout under `.scratch/reader-capsule-20261029` and the two resource builds under `.scratch/resource-adoption-20261027`, as recorded in the input manifest. Restore the frozen prototype revision before rebuilding this screen; newer publication work is a different candidate. Copying the scripts alone does not reconstruct those archives or the persisted 5M corpus. `pyrefly.toml` lists the controllers; the configure-only helper is separate from this evidence package.

Run builds and correctness checks under `quiet light`, then the timing controller under `quiet exclusive`. The scratch converter requires `--test-only-copied-fixture`; never use it on a production directory. Results refuse to overwrite an existing screen. The next component implements publication, additional-byte accounting, readiness, partial-prefix collection and coordinated rollback. Format/fuzz/trust/crash/retained-reader/platform proof and 200/200 uninstrumented resident-data first reads below 1 ms in each resource closure remain required before adoption.
