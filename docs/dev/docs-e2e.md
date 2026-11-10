# Docs end-to-end suite

The pages under `website/src/content/docs/` are Detent's end-to-end suite. Each runnable block runs
as written against a build of the checkout, and a hidden assertion block after it checks the values
it prints.

## Commands

All three run from the repository root and exit 0 when clean.

| Command | Checks | Exit status |
| --- | --- | --- |
| `python3 tests/docs/run_e2e.py --fresh-build` | Every `sh` and `rest` block runs; each is asserted or listed in `tests/docs/e2e_exceptions.txt` | 0 pass, 1 a block failed or is unasserted, 2 the build or run could not start |
| `python3 tests/docs/check_docs.py` | CLI flags, configuration keys, HTTP endpoints, error and status names and knob types, in both directions between the reference pages and `src/` and `include/`; gaps live in `tests/docs/docs_exceptions.txt` | 0 clean, 1 an unlisted gap or a stale exception, 2 a malformed exceptions file |
| `python3 tests/docs/check_links.py website/dist` | Every internal link and `#anchor` of the built site (`cd website && npm ci && npm run build`) | 0 clean, 1 a broken link |

`run_e2e.py --build-dir DIR` reuses the binaries of an existing build (`DIR/src/detentd`,
`detent-agent`, `detent`); `--deps-from DIR` lets a fresh build take its dependency sources from
another build; `--page SLUG` runs one page's blocks (earlier pages' state is not set up);
`--list` prints each block with `asserted`, `excepted` or `MISSING`; `--keep` leaves the sandbox
and build for inspection.

## The page form

A runnable block is a fenced `sh` or `rest` block. Its assertion block follows it, hidden in an
HTML comment:

````markdown
```sh
publish search:max_results '{"type":"int","default":25}'
sleep 1
detent get search:max_results
# 25
```

<!-- e2e:begin
stdout-matches: "status":"applied","revision":1
stdout-matches: ^25$
e2e:end -->
````

Blocks run in sidebar order in one bash session, so a page continues the shell of the one before
it. The session's working directory is a per-run sandbox where `build/src` holds the built
binaries and `tests` is the checkout's `tests`.

Blocks in other languages (`cpp`, `json`, `text`, ...) are not run.

## Assertion keys

One `key: value` per line; an indented line continues the previous value.

| Key | Applies to | Meaning |
| --- | --- | --- |
| `exit` | sh | Expected exit status of the block (default 0) |
| `stdout` | sh | Whole stdout, compared after trimming; continuation lines make it multi-line |
| `stdout-contains` | sh | Substring of stdout (repeatable) |
| `stdout-matches` | sh | Regular expression found in stdout, multi-line mode (repeatable) |
| `stderr-contains` | sh | Substring of stderr |
| `run` | sh | Shell run in the session after the block; must exit 0 (repeatable) |
| `rewrite` | sh | `FROM => TO`: edits the block text before it runs |
| `timeout` | sh | Seconds the block may take (default 120) |
| `status` | rest | Expected HTTP status (default 200) |
| `body-contains` | rest | Substring of the body |
| `json.PATH` | rest | The dotted path in the JSON body equals the JSON value |
| `ca` | rest | CA file, relative to the sandbox: the request goes over HTTPS to `localhost` |

An assertion block must assert a value (`stdout*`, `run`, `status`, `body-contains` or `json.*`);
an exit status alone is an error.

## Ports and isolation

Each run takes its own random free port (a lock file in `.scratch/docs-e2e/` keeps two runs from
choosing the same one) and rewrites the literal `8317` in every block to it, so pages stay as a
reader would type them. `DETENTD_PORT` and `DETENT_E2E_PORT` carry it too. The run uses its own
`HOME`, `TMPDIR` and XDG directories. At the end it stops only what it started: the pid files under
its sandbox and `lsof -ti tcp:PORT -sTCP:LISTEN` for its port. `rewrite` covers what an environment
variable cannot: the Quick-start's `managed.json` gets `"port"` that way.

## Exceptions

`tests/docs/e2e_exceptions.txt` lists a runnable block that has no assertion block, as
`page#N -- reason`, where N counts the page's `sh` and `rest` blocks from 1. A listed block that
gains an assertion block fails the run, so the list shrinks as pages become runnable. The area
quests remove the lines for their pages.

`tests/docs/docs_exceptions.txt` lists gaps `check_docs.py` finds, as
`docs-only|source-only KIND NAME -- reason`, and fails on a listed gap that is gone.

## CI

`.github/workflows/docs.yml` runs the three commands on push to `main` and `quest/**` when the
site, the docs, the source or the CLI change; `ci.yml` ignores those paths.
