# The November pin wave: syscalls, input classes and the hot path

Every Kronuz pin in `cmake/dependencies.json` moves to its library's default-branch head (quest
`pin-wave`). completion's head (`completion:b4600a8`) makes `PosixIOOptions::flush_mode` required,
and Detent passes `FlushMode::full` at every PosixIO it builds: the authority's journal and history,
the agent's disk store, the relay's mirror, and the tests and benchmarks.

## Syscalls of an open and a sync (macOS)

`pin-wave-syscalls/run.sh OLD NEW OUT` builds one scenario through every PosixIO sync entry point
(Xapiand's completion-flush-mode review, probe P5) against completion at Detent's old pin
(`560c288`, default options, as Detent built it) and at the new pin (`b4600a8`, `FlushMode::full`).
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

Against the old pin with `full_flush_ordering`, only the second difference remains. `plain` would
weaken every `File::sync` to `fsync`, so no mode reproduces the old default exactly; full is the
superset, durable on any drive. A follow-up may move the refetchable stores (the agent's disk store,
the relay's mirror) to `plain` if the write-path cost below calls for it.

Linux is not traced (no Linux host here; the interposer is macOS-only). By completion's table,
Linux makes the same calls in both modes (`sync_call`: `fsync`, `fdatasync` for `sync_data`), so the
mode changes nothing there; the `Sync` primitive's route through `sync_files` (`d5c6fd4`) applies on
Linux too, where `sync_files` starts writeback before its `fsync`.
