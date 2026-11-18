> Status: CURRENT (2026-11-18) how `detentd` refuses a directory of another storage format, read from `src/detent_authority/service.cc`.

# Format refusal

Detent reads one storage format. A voter started on a journal written in another format exits
nonzero with a named error; it does not replay, fence and wait.

## The stamp

`storage_format` in `service.cc` names the format this build reads and writes. A new authority
directory is stamped before its journal exists: the file `format` beside the history files
(`<directory>.history/format`) holds `detent authority storage format N`, written to a temporary
file, synced, renamed, and the directory synced. The stamp sits outside the journal directory so
the journal's inventory never counts it, and outside the history inventory (`history_name` does
not match it, so the collector and the peer reader never touch it).

`check_storage_format` runs at startup, before the node opens the journal:

1. A stamp equal to `storage_format` starts normally.
2. A stamp of another number, or one that does not parse, is refused naming the stamp found.
3. No stamp and an existing journal (`manifest`) is format 1: every directory written before the
   stamp existed. It is refused naming format 1.
4. No stamp and no journal is a new directory and is stamped.

The error reads `authority storage format error: <directory> is in storage format 1 (no format
stamp), this build expects storage format 2; Detent keeps no compatibility, so start from an empty
directory`. The service constructor throws it, and `detentd` prints it and exits 1.

## Startup failures settle the constructor

`Service::run` settles the constructor's `ready` promise with any failure between the runtime's
start and admission (a corrupt manifest, a failed history restore, an application replay that
fences), so the constructor throws and `detentd` exits nonzero. After admission `ready` is already
settled and the same handler fences the service.

## Format numbers

| Format | Directories |
|---|---|
| 1 | Written before the stamp, among them the journals of 2026-10-23. Refused. |
| 2 | Written with a stamp. Current. |

Bump `storage_format` with any change to a stored or replicated byte layout: a magic, a command,
image, history or layout field, the encoding of a value. Nothing migrates; a changed layout
without a bump is not detected, which is how `Detent:53abd637` and `Detent:711a114e` changed the
layout under unchanged names.

## Tests

`tests/detent_authority/test_format_refusal.cc` (`test_authority_format_refusal`) starts voters on
a stamped directory, the same directory with its stamp removed (format 1), stamps of format 1, 9
and garbage, and a corrupt manifest. Each refusal must name both formats (or the unreadable stamp)
and settle within the test's 20 s watchdog.
