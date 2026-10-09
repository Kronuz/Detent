---
title: "Command Line"
---

Detent installs three programs, plus an admin tool in authority builds.

| Program | Role |
| --- | --- |
| [`detent`](/Detent/reference/cli/detent/) | Read a knob from the shell, auto-starting the user agent when needed |
| [`detent-agent`](/Detent/reference/cli/detent-agent/) | The host-local agent daemon |
| [`detentd`](/Detent/reference/cli/detentd/) | The distributor, or the durable authority |
| [`detent-authority-admin`](/Detent/reference/cli/detent-authority-admin/) | Import into, and export from, an authority |

---

## Shared Conventions

1. **Precedence.** Command-line flags override environment variables, which
   override configuration-file values, which override built-in defaults.
2. **Configuration files.** `agent.json` and `detentd.json` live in the
   platform's Detent config directory; `--config PATH` selects one explicitly.
   Relative paths inside a config file are resolved against the file's
   directory. See [Configuration](/Detent/reference/configuration/).
3. **Foreground by default.** Both daemons run in the foreground, which is what
   service managers want. `--daemonize` backgrounds a user instance and reports
   readiness or the startup error to the caller.
4. **`--status` is read-only.** It reports what's running and never starts
   anything.
5. **`--system`** selects system-wide configuration and runtime locations, and
   stops `detent get` from ever starting a private agent.
6. **Stopping.** Send SIGTERM to an unmanaged daemon; supervised services are
   stopped and restarted through the service manager.

### Logging Flags

Both daemons accept the same logging flags:

| Flag | Effect |
| --- | --- |
| `--log-level LEVEL` | `emergency`, `alert`, `critical`, `error`, `warning`, `notice`, `info` (default) or `debug` |
| `-v`, `-vv` | Raise verbosity one or two steps |
| `--verbosity 0..2` | Same as repeating `-v` |
| `--color auto\|always\|never` | Terminal colors; `auto` honors `NO_COLOR` and redirection |
| `--no-color` | Same as `--color never` |

See [Logging](/Detent/operations/logging/).
