# Source file splits

How Detent's large source files are split, and why some stay whole. The rule is cpp-libraries
`docs/SPLITTING.md`: size flags a file for a look, and a cut is made only where each piece is
easier to work on than the whole. The check is `tools/split_check.py` (moves only).

A file over 1,000 lines gets a verdict here: split along named boundaries, or kept whole with the
unit that justifies it.

## Split

### `src/detent_authority/service.cc` (3,613 lines)

| File | Lines | Holds |
|---|---|---|
| `service.cc` | 785 | The HTTP front of `Service`: construction, admission, request bodies, enqueue, stream and reply settlement. |
| `service_options.cc` | 347 | `load_service_options`: the configuration file read, bounded and validated, with the voter-host checks only it uses. |
| `service_runtime.cc` | 2,364 | `Service::Runtime` and `Service::run`: the owner loop, with the storage-format stamp, the CPU clock and the result and read-failure JSON only it uses. |
| `service_internal.hh` | 186 | What the three share: the JSON field helpers (`check`, `field`, `text`, `number`, `identity`, `hex`, `document`) in `detail`, and the types nested in `Service` (`Accounting`, `Delivery`, `Admission`, `ReplyAdmission`, `Extension`, `Prepared`, `Outcome`). |

`service_runtime.cc` stays above 1,000 lines on purpose: `Service::Runtime::run` is one coroutine of
about 2,050 lines, the owner's state machine, and nothing is cut inside a function.

### `src/detent_authority/state.cc` (2,447 lines)

| File | Lines | Holds |
|---|---|---|
| `state.cc` | 430 | `State`: construction, reclamation, namespace lookups, `apply`, `apply_batch`, `lookup`. |
| `state_codec.cc` | 661 | The wire codec: command validation, `encode`, `decode`, `digest`, `CommandEncoder`, `CommandDecoder`, and the batch encode, header and `BatchDecoder`. |
| `state_application.cc` | 521 | `CommandApplication` and `BatchApplication`: the stepwise application of a command and of a batch. |
| `state_administrative.cc` | 210 | Administrative commands: their codec, `AdministrativeApplication` and `State::administer`. |
| `state_image.cc` | 446 | The checkpoint image: `State::snapshot`, `ImageEncoder`, `ImageDecoder`, `State::checkpoint` and `State::restore`. |
| `state_internal.hh` | 266 | The codec primitives (`Writer`, `Reader`, the definition, result, policy and limits rows) and the value helpers `State`, the application and the image share, in `detail`. |

## Kept whole

### `src/detent_delivery/coordinator.cc` (1,299 lines)

`Coordinator::Impl` is one class body of about 825 lines (construction, recovery, apply, compaction,
scrub and disk loops) and `Impl::candidate_loop` is its member coroutine. A class body is not cut
(SPLITTING.md, "Where to cut"), and a class holding several mechanisms is restructured on its own
quest, by out-of-class definitions per mechanism, proved by `split_check.py --members`. The rest of
the file (the public `Coordinator` members and the test access shims) is under 150 lines. The branch
`quest/detent/managed-failover-isolated-leader` edits this file, which also waits for it. The
restructure is the follow-up.

### `docs/design/dense-pages-model.py` (1,725 lines) and `docs/design/dense-distribution-model.py` (1,605 lines)

Each is one self-contained measurement model run as a script (`python3 -I docs/design/NAME.py MODE`),
the numbers behind its design document. `dense-distribution-model.py` loads the pages model by file
path (`--dense=PATH`, `$JOB_REF_DENSE`, pinned at `Detent:28d3ed9`), and
`tests/detent_relay/delta_scenarios.py` loads both by file name, so a module split would change how
they are found. Neither is C or C++, so `split_check.py` cannot prove a split moved code only. They
are design records, edited only with their design.

## Not in this quest's file set

`tests/detent_authority/test_state.cc` (1,084 lines) is over 1,000 lines and outside the files this
quest may edit. `tests/detent_delivery/test_coordinator.cc`, once 1,346 lines, no longer exists; the
largest test there is `test_pages_coordinator.cc` at 514 lines.

## Non-moves

Each is a linkage or lookup change the check reports as a warning.

1. A shared helper left an anonymous namespace for `detail` in `service_internal.hh` and
   `state_internal.hh` and gained `inline`. Class and struct bodies, and the `constexpr` constants,
   move as they were.
2. `using namespace detail;` follows each `detail` namespace, so moved bodies call the helpers
   unqualified, as they did from the anonymous namespace. Lookup is the same: both names sit in
   `detent::authority`.
3. `service_runtime.cc` and `service_options.cc` open an anonymous namespace for the helpers only
   they use (they were in the one in `service.cc`), and `state.cc` keeps its own for
   `copy_definition` and `canonical_bytes`. Their linkage stays internal.
4. The platform include block of `service.cc` (`completion/*`, `journal/mutation.h`) moved to
   `service_runtime.cc`, the only file that uses it.

No namespace-scope object with a dynamic initializer moved.
