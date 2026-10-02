# Daemon logging

`detentd` and `detent-agent` use [Kronuz/logger](https://github.com/Kronuz/logger), extracted from Xapiand. Logs go to stderr with a severity marker and an ISO 8601 timestamp including milliseconds. Terminal output has severity colors and a timestamp gradient; redirected output is plain by default. iTerm2 window customization is disabled.

```sh
# Lifecycle messages and warnings (the default).
detentd

# Accepted snapshots and agent publication details.
detentd -v
detent-agent start -v

# Include agent health samples, every 200 milliseconds.
detent-agent start -vv

# Quiet service logs, or explicit terminal color policy.
detentd --log-level warning --color never
detent-agent start --color always
```

`--log-level` accepts `emergency`, `alert`, `critical`, `error`, `warning`, `notice`, `info`, and `debug`. The default is `info`. Each `-v` increases the selected level, capped at the library's verbose level; `--verbosity 0..2` supplies the same increment. `--color auto|always|never` controls color, and `--no-color` selects `never`. In automatic mode, `NO_COLOR` disables color. Explicit `always` overrides it. Command-line settings override configuration.

Both `agent.json` and `detentd.json` accept these fields:

```json
{"log_level":"info","verbosity":0,"color":"auto"}
```

Lifecycle messages include startup and shutdown. The distributor warns when it rejects a publish, identifying the rejection category without logging payload contents. The agent logs source connection transitions, warns about refresh failures, and reports shared-memory capacity failures. Repeated refresh failures move to debug until a newer snapshot arrives. Host hashes, switch values, and knob values are not logged.

The daemon executables own logging initialization and shutdown. Core, HTTP, distributor, and shared-memory libraries expose optional diagnostic callbacks without acquiring a logger dependency. The `detent` reader does not link the logger. Debug messages are compiled into Release daemon builds and filtered before formatting or evaluating arguments.

Agent background startup initializes logging after `fork()`. Background logs go to `agent.log` in the runtime directory. Foreground service managers capture stderr; use their journal or file rotation facilities. Logging drains before normal exit, including SIGTERM. Routine asynchronous logging is limited to 4,096 pending entries; this is an entry limit, not a byte limit. It does not cover the library's synchronous critical path.

## Measured footprint

Release build on macOS x86_64, Apple Clang 17.0.0. Before binaries were saved before integration; both sets were copied and stripped with `strip -x`. The distributor used its default four reactors and eight workers with an empty state. The agent used one small defaults file and no source. RSS is the median of five `ps -o rss=` samples, 200 ms apart after one second of startup. Thread counts come from `ps -M`. These are idle resident-memory samples, not peak allocation, virtual address space, or throughput measurements; RSS varies between launches.

| Executable | Stripped bytes before | Stripped bytes after | Change | Idle RSS before / after | Threads before / after |
| --- | ---: | ---: | ---: | ---: | ---: |
| `detentd` | 865,328 | 939,776 | +74,448 | 1,684 / 1,972 KiB | 37 / 38 |
| `detent-agent` | 387,240 | 475,568 | +88,328 | 984 / 1,228 KiB | 1 / 2 |
| `detent` | 253,768 | 257,864 | +4,096 | Not sampled | No logging thread |

The reader's size change comes from shared configuration code; symbol inspection finds no `Logging::` or `StderrLogger` symbols. The added thread is the logger scheduler. No CPU, latency, power, or cost improvement is claimed from this measurement.

## Library improvements to propose upstream

1. Make iTerm2 support a build option. The logger currently fetches `cppcodec` solely for badge encoding, even when the consumer disables iTerm2 customization. An option could remove that dependency and its code for services.
2. Let `finish()` and `join()` operate without creating an unused scheduler. Both call the lazy scheduler accessor, which starts the logging thread even if every message was filtered out. A quiet service should be able to shut down without creating it.
3. Serialize sink writes across synchronous and asynchronous callers. Critical messages run inline while routine messages run on the scheduler. `StderrLogger` and `StreamLogger` call a write loop without a sink lock, so a partial write can interleave with another caller. Keep the existing EINTR and short-write handling, and handle a zero-byte write defensively.
4. Offer an explicit single-line service format with severity names and escaping for control characters. The current human format uses severity glyphs. Detent avoids logging publish contents, but paths and exception strings can still contain line breaks. A service sink should preserve one event per line.
5. Define a configuration lifecycle. Global configuration and handler registration are mutable; Detent sets them once before producers start. A freeze operation or synchronized immutable configuration would make future runtime reloads safe by construction.
6. Pin dependency revisions for reproducible releases. Detent pins the logger revision, but the library's scheduler, term-color, and codec declarations follow branch tips, as do several pre-existing Detent dependencies. Publish a tested revision set rather than assuming unrelated branch updates remain compatible.

These are source-review proposals. No changes were made to the upstream library repositories, and no footprint savings are claimed for them.
