# M1: pins to head (docs/PLAN.md "M1: on the tip", done criterion 1)

Branch `m1/pins-to-head`, cut from `m1/pin-check` (`Detent:1098592`). Follows
`cpp-libraries/docs/UPGRADING-2026-10.md` "Updating an application (for agents)".

## State

In progress. Library chain bumped dependency first; the Detent build and suites are not run yet.

## Library commits (each on the library's main)

| Library | Commit | What | CI run | Result |
|---|---|---|---|---|
| completion | `completion:9acfba3` | metrics db490b4 -> 9db73de, task 6558c2e -> bdafa5f | 41778240864 | all jobs green |
| hashes | `hashes:03ab2d6` | static-string, char-classify, constexpr-phf to heads | 41778269055 | all jobs green |
| reactor | `reactor:ab69cf2` | task -> bdafa5f | 41778274173 | all jobs green |
| repr | `repr:d2eb895` | char-classify -> 25decb5 | 41778277262 | green |
| sha256 | `sha256:f24692f` | endian -> 6b223e2 | 41778282059 | green |
| term-color | `term-color:76a400b` | static-string -> 7eaa8f9 | 41778285115 | green |
| enum-reflection | `enum-reflection:a7202ad` | constexpr-phf, hashes to heads | 41778946627 | green |
| strings | `strings:6799a6e` | char-classify, split, static-string, repr, term-color to heads | 41778955290 | green |
| cluster | `cluster:e4fc57d` | reactor, varint to heads | 41779537676 | green |
| http-parser | `http-parser:fcdad32` | enum-reflection -> a7202ad | 41779542035 | see below |
| journal | `journal:e7c8a0f`, `journal:424c6c2` | completion -> 9acfba3; test IOs implement the read-only opens | 41778949860 (red), 41779506689 (red) | see API changes |

## API changes

- completion made the read-only opens pure virtuals of `IO` (`completion:c315c03`):
  `open_existing_read_only`, `open_reclaim_candidate_read_only` and the two `_into` forms. Every
  `IO` subclass implements them; journal's test IOs and `journal/memory_io.h` did not.

## Blocked on

- scheduler (`scheduler:f080858`) pins stash 8fa5d2a, threadpool eb4751a, random e040c2b. Bumping it
  is W4's (Xapiand campaign); the push was refused by the permission classifier and not retried.
  The logger (pins scheduler 1e5ccc9) and http-log (pins logger 801105c) follow.
