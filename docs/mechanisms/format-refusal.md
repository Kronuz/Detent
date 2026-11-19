> Status: CURRENT (2026-11-18) how `detent-authority` refuses a directory of another storage format, read from `src/detent_authority/service.cc`.

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
2. A stamp of another number (format 1, or a newer one), or one that does not parse, is refused
   naming the stamp found.
3. No stamp and no journal (`manifest`) is a new directory and is stamped.
4. No stamp and an existing journal is unknown, and its content decides. The node replays it. If
   recovery fails before admission (an image of another magic, a committed entry that does not
   apply and fences, a history that does not restore), the directory is format 1 and is refused
   naming it and the failure. If recovery succeeds, the directory reads as format 2 and is
   stamped before admission.

The error reads `authority storage format error: <directory> is in storage format 1 (no format
stamp, and it does not read as format 2: <failure>), this build expects storage format 2; Detent
keeps no compatibility, so start from an empty directory`. The service constructor throws it, and
`detent-authority` prints it and exits 1.

## Startup failures settle the constructor

`Service::run` settles the constructor's `ready` promise with any failure between the runtime's
start and admission (a corrupt manifest, a failed history restore, an application replay that
fences), so the constructor throws and `detent-authority` exits nonzero. After admission `ready` is already
settled and the same handler fences the service.

## Format numbers

| Format | Directories |
|---|---|
| 1 | Written before `Detent:53abd637` (image magic `DTAIMG01`, no namespace directory), among them the journals of 2026-10-23. Refused. |
| 2 | Written by this build. Current. |

Bump `storage_format` with any change to a stored or replicated byte layout: a magic, a command,
image, history or layout field, the encoding of a value. Nothing migrates. A change that a replay
reads without failing is not detected in an unstamped directory, so a bump is what keeps a stamped
one honest.

## Tests

`tests/detent_authority/test_format_refusal.cc` (`test_authority_format_refusal`) starts voters on
a stamped format 2 directory (opens), the same directory with its stamp removed (opens and is
stamped), a format 1 journal built by `Detent:53abd637^`, unstamped and stamped 1 (refused naming
both formats), a stamp of 3 and an unreadable stamp (refused), and a corrupt manifest (the start
settles with its error). Each start runs under a 20 s watchdog.
