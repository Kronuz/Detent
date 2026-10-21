# Daemon logging

`detentd`, `detent-agent` and `detent-relay` use [Kronuz/logger](https://github.com/Kronuz/logger), extracted from Xapiand. Logs go to stderr with a severity marker and an ISO 8601 timestamp including milliseconds. Terminal output tints messages with the shared severity palette and gives timestamps a grey gradient; redirected output is plain by default. Tinting is the default, with no separate option. iTerm2 window customization is disabled.

```sh
# Lifecycle messages and warnings (the default).
detentd

# Accepted snapshots and agent publication details.
detentd -v
detent-agent -v

# Include agent source metric samples, every 200 milliseconds.
detent-agent -vv

# Quiet service logs, or explicit terminal color policy.
detentd --log-level warning --color never
detent-agent --color always
```

`--log-level` accepts `emergency`, `alert`, `critical`, `error`, `warning`, `notice`, `info`, and `debug`. The default is `info`. Each `-v` increases the selected level, capped at the library's verbose level; `--verbosity 0..2` supplies the same increment. `--color auto|always|never` controls color, and `--no-color` selects `never`. In automatic mode, `NO_COLOR` disables color. Explicit `always` overrides it. As in Xapiand, `--color truecolor`, `--color 256` and `--color 16` force a color depth; their aliases are `24bit`, `256color` and `ansi`. `on`/`off` alias `always`/`never`, and `stacked` preserves all three portable color tiers. Command-line settings override configuration.

```sh
# Preserve full RGB colors in a captured log.
detentd --color truecolor -v >detent.log 2>&1
```

## HTTP access logging

The classic distributor uses [Kronuz/http-log](https://github.com/Kronuz/http-log), the same middleware as Xapiand. Requests retain its method palettes (blue GET, orange PUT/PATCH, red DELETE); responses retain its green success, teal redirect, tan 404 and red error palettes. Requests and successful/404 responses appear at debug (`-v`), other 4xx at info, and 5xx at notice. Detent selects metadata-only rendering: escaped bounded method/path labels, status and handler body size, without headers, bodies, query values or exception descriptions. These blocks describe the handler response before HTTP transforms, not confirmed delivery.

Authority and relay access logging remains pending a [shared transport completion observer](HTTP_LOGGING_ARCHITECTURE.md). Their deferred/owned-stream replies outlive `handle()`; wrapping them with the synchronous middleware would report premature statuses. Daemon lifecycle tinting already applies to both.

Both `agent.json` and `detentd.json` accept these fields:

```json
{"log_level":"info","verbosity":0,"color":"auto"}
```

Lifecycle messages include startup and shutdown. The distributor warns when it rejects a publish, identifying the rejection category without logging payload contents. The agent logs source connection transitions, warns about refresh failures, and reports shared-memory capacity failures. Repeated refresh failures move to debug until a newer snapshot arrives. Host hashes, switch values, and knob values are not logged.

The daemon executables own logging initialization and shutdown. Core, HTTP, distributor, and shared-memory libraries expose optional diagnostic callbacks without acquiring a logger dependency. The `detent` reader does not link the logger. Routine agent diagnostics read existing atomic counters without counting knobs or traversing snapshots. Debug messages are compiled into Release daemon builds and filtered before formatting or evaluating arguments.

Agent background startup initializes logging after `fork()`. Background logs go to `agent.log` in the runtime directory. Foreground service managers capture stderr; use their journal or file rotation facilities. Logging drains before normal exit, including SIGTERM. Asynchronous notice, info, debug, and verbose messages are limited to 4,096 pending entries; this is an entry limit, not a byte limit. Warning and error messages bypass that limit, and critical messages use the synchronous path. A stalled sink can therefore still accumulate warnings and errors.

## Measured footprint

Release build on macOS x86_64, Apple Clang 17.0.0. Before binaries were saved before integration; both sets were copied and stripped with `strip -x`. The distributor used its default four reactors and eight workers with an empty state. The agent used one small defaults file and no source. RSS is the median of five `ps -o rss=` samples, 200 ms apart after one second of startup. Thread counts come from `ps -M`. These are idle resident-memory samples, not peak allocation, virtual address space, or throughput measurements; RSS varies between launches. Across integration runs, idle RSS ranged from 1,920 to 2,068 KiB for the distributor and 1,228 to 1,348 KiB for the agent; the table records the final launch. [Raw measurements](../benchmarks/logging-footprint.json) preserve the before and final samples.

| Executable | Stripped bytes before | Stripped bytes after | Change | Idle RSS before / after | Threads before / after |
| --- | ---: | ---: | ---: | ---: | ---: |
| `detentd` | 865,328 | 939,776 | +74,448 | 1,684 / 2,068 KiB | 37 / 38 |
| `detent-agent` | 387,240 | 475,520 | +88,280 | 984 / 1,316 KiB | 1 / 2 |
| `detent` | 253,768 | 257,864 | +4,096 | Not sampled | No logging thread |

The reader's size change comes from shared configuration code; symbol inspection finds no `Logging::` or `StderrLogger` symbols. The added thread is the logger scheduler. No CPU, latency, power, or cost improvement is claimed from this measurement.

## HTTP metadata and color-depth costs, October 21

The [accepted exploratory comparison](../benchmarks/logging-cost-comparison-20261021.json) uses three alternating pairs per mode, 100k synthetic boolean knobs and 20k conditional polls per sample under a strict host hold. Its 38 process snapshots contain no recognized competing build/test process. Ordinary macOS background activity remains, and the first filtered baseline is an outlier.

| Mode | Median daemon CPU before / after, per 20k polls | Median resident bytes before / after |
| --- | ---: | ---: |
| Default, success logs filtered | 0.51 / 0.52 seconds | 156,282,880 / 155,967,488 |
| Verbose, plain | 0.51 / 1.44 seconds | 152,227,840 / 160,284,672 |
| Verbose, RGB | 0.50 / 1.46 seconds | 154,988,544 / 156,545,024 |

The default difference is one 10ms CPU-counter tick; its paired median change is zero, with individual changes from -24.64% to +6.00%. This does not establish statistical equivalence or zero overhead. Verbose logging adds median paired costs of 47 CPU seconds per million polls in plain mode and 47.5 in RGB, about 0.013 core-hours. These separate time blocks establish no incremental RGB CPU cost. Every verbose candidate retains all 20,103 request and response entries, including setup and warmup. RSS is one post-workload sample, not peak memory; shutdown CPU and allocation accounting remain outside this comparison.

The unstripped classic distributor grows by 57,552 bytes (2.68%); the agent grows by 88 bytes. The component review accepts this explicit file-size exception for shared diagnostics. The baseline already includes tint, and this profile differs from the original stripped logger-addition table above. [Qualification evidence](../benchmarks/xapiand-logging-preliminary-20261020.json) records coherent pins, the clean public-pin product build and shared-library CI. Allocation/peak-memory qualification and authority/relay completion logging remain open.

The [initial run](../benchmarks/logging-cost-comparison-20261020.json) and [first repeat](../benchmarks/logging-cost-comparison-20261021-rejected.json) remain rejected because competing compiler/test work appears in their snapshots.

## Library improvements to propose upstream

1. Make iTerm2 support a build option. The logger currently fetches `cppcodec` solely for badge encoding, even when the consumer disables iTerm2 customization. An option could remove that dependency and its code for services.
2. Let `finish()` and `join()` operate without creating an unused scheduler. Both call the lazy scheduler accessor, which starts the logging thread even if every message was filtered out. A quiet service should be able to shut down without creating it.
3. Serialize sink writes across synchronous and asynchronous callers. Critical messages run inline while routine messages run on the scheduler. `StderrLogger` and `StreamLogger` call a write loop without a sink lock, so a partial write can interleave with another caller. Keep the existing EINTR and short-write handling, and handle a zero-byte write defensively.
4. Offer an explicit single-line service format with severity names and escaping for control characters. The current human format uses severity glyphs. Detent avoids logging publish contents, but paths and exception strings can still contain line breaks. A service sink should preserve one event per line.
5. Define a configuration lifecycle. Global configuration and handler registration are mutable; Detent sets them once before producers start. A freeze operation or synchronized immutable configuration would make future runtime reloads safe by construction.
6. Offer bounded delivery policies for warnings and errors as well as routine messages. The current queue limit exempts them, so repeated publisher rejections can accumulate behind a stalled sink. Let services choose bounded drop summaries or a finite blocking timeout.
7. Pin dependency revisions for reproducible releases. Detent pins the logger revision, but the library's scheduler, term-color, and codec declarations follow branch tips, as do several pre-existing Detent dependencies. Publish a tested revision set rather than assuming unrelated branch updates remain compatible.

These are source-review proposals. No changes were made to the upstream library repositories, and no footprint savings are claimed for them.
