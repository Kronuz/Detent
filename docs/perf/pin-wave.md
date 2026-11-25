# The November pin wave: syscalls, input classes and the hot path

Every Kronuz pin in `cmake/dependencies.json` moves to its library's default-branch head (quest
`pin-wave`). completion's head (`completion:b4600a8`) makes `PosixIOOptions::flush_mode` required,
and Detent passes `FlushMode::full` at every PosixIO it builds: the authority's journal and history,
the agent's disk store, the relay's mirror, and the tests and benchmarks.

## Syscalls of an open and a sync (macOS)

`pin-wave-syscalls/run.sh OLD NEW OUT` builds one scenario through every PosixIO sync entry point
(Xapiand's completion-flush-mode review, probe P5) against completion at Detent's old pin
(`560c288`, default options, as Detent built it) and at the new pin (`3061516`, `b4600a8` plus pin
moves only; `FlushMode::full`).
An interposer logs every `fsync`, `fdatasync`, `F_FULLFSYNC` and `F_BARRIERFSYNC`; the script
diffs the logs and checks the difference against `pin-wave-syscalls/expected-delta.txt`.

| Arm | Calls | Steps |
|---|---:|---:|
| new, `FlushMode::full`, every step | 72 | 31 |
| new, `FlushMode::full`, the steps the old pin has | 68 | 28 |
| old pin, default options | 66 | 28 |
| old pin, `full_flush_ordering = true` | 66 | 28 |

Full mode is stronger than Detent's old default on macOS, in two ways, each with a library commit
as its cause:

1. Every ordering point (`order_files_and_directory`, the `OrderFiles` primitive) ends in
   `F_FULLFSYNC` where the old pin issued `F_BARRIERFSYNC`: 7 calls of the scenario.
   `completion:5942250` makes full mode the full flush at every sync; the old default ordered with a
   barrier unless `full_flush_ordering` was set, which Detent never set.
2. The `Sync` primitive, through `drive_synchronously` and the native queue, issues one `fsync`
   before its `F_FULLFSYNC`: 2 calls. `completion:d5c6fd4` runs the executor's `Sync` as
   `IO::sync_files` of its one file, which stages with `fsync` and flushes the device.

On Detent's own paths only the second difference is reached: no Detent source issues
`order_files_and_directory` or `OrderFiles`, and in journal only `Host` and `TurnLog` do, which
Detent does not use. The agent store and the relay mirror sync through `sync_files` and
`sync_files_and_directory`, which make the old pin's calls in full mode, so the durable row below
(the authority's journal) cannot show a cost for moving those refetchable stores to `plain`; a
row of their own would measure them.

Against the old pin with `full_flush_ordering`, only the second difference remains. `plain` would
weaken every `File::sync` to `fsync`, so no mode reproduces the old default exactly; full is the
superset, durable on any drive. A follow-up may move the refetchable stores (the agent's disk store,
the relay's mirror) to `plain` if the write-path cost below calls for it.

Linux not traced here: no Linux host, the interposer is macOS-only, and CI's Linux jobs run only
the suite, which no probe can join without editing the workflow or a test CMake file. completion
shows it upstream: `test/sync_calls_test.cc` and `test/sync_primitives_test.cc` record every sync
site's call in each mode through the seam (`completion:5942250`), and they pass on Linux x86_64
(gcc, clang), Linux arm64, ASan and TSan in completion's CI run 45541875670 at `3061516`. By that
table Linux makes the same calls in both modes (`fsync`, `fdatasync` for `sync_data`); the `Sync`
primitive's route through `sync_files` (`d5c6fd4`) applies on Linux too, where `sync_files` starts
writeback before its `fsync`.

## Input classes (A5)

`pin-wave-w5.sh OUT` builds the M1 identity drivers (`tools/w5`) into the old tree and the new one,
each against its own pins, runs them and compares class by class. Job 261124-234929-bde848 at
`Detent:0b8a0df9`, old arm main `bc86991a` (its code equals the quest base `18c5bd23`):

| Class | Cases | Different |
|---|---:|---:|
| snapshot JSON and wire (`w5_detent`) | 71 | 0 |
| wire frames and paged images (`w5_store`, files) | 20 | 0 |
| compressors | 8722 | 0 |
| http-parser | 840 | 0 |
| strict-stox | 1089 | 0 |
| enum-reflection, hashes | 14845 | 0 |
| random | 31 | 0 |

The logger transcript differs in one line, 437, by `__FILE__` depth (`../../../tools/w5` against
`../../tools/w5`): the two checkouts' paths, not a library. http's replay class did not run in
either arm: `http_replay.py` needs a `detent-authority` with the classic `--persist-path` profile,
which this tree no longer has, so http's 73 commits are covered by the suite's service-wire and
transport tests (A4) instead of a byte diff.

`w5_store` compares what the tree now persists: the flat image builder left before the wave
(`Detent:f6e8d3d6`), so each corpus image is the paged root plus its segments sorted by segment id,
and the snapshot fixture's value is the four-field tuple the decoder takes in both arms
(`[i, nil, nil, []]`, as `tests/detent_distribution/test_encoded_update.cc`). Both arms run the
same driver source.
