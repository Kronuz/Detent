# System services

Install the binaries in `/usr/local/bin` and run them under a dedicated, unprivileged service account. The service manager owns process startup, restarts, and logging. Daemons run in the foreground when supervised. The application-specific `--system` option selects system locations and prevents shell readers from spawning or stopping unmanaged system agents. Explicit `--config PATH` and path settings override these locations.

| Platform | Configuration | Agent runtime | Distributor state | Logs |
| --- | --- | --- | --- | --- |
| Linux | `/etc/detent` | `/run/detent` | `/var/lib/detent` | systemd journal |
| FreeBSD | `/usr/local/etc/detent` | `/var/run/detent` | `/var/db/detent` | `/var/log/detent` |
| macOS | `/Library/Application Support/Detent` | `/var/run/detent` | `/var/db/detent` | `/var/log/detent` |

Configuration is root-owned and readable by the service account. State is owned by the service account with mode `0700`. Shared agent runtime is service-owned with mode `0755`; its pid and snapshot files are `0644` so other users can inspect status and read knobs. Configuration values published through the shared agent are therefore visible to local users. Keep secrets out of knobs. Only the service account can modify the runtime files.

Use `agent.json` and `detentd.json` in the configuration directory. Neither file is required when using the built-in port `8317` and paths. Templates pass explicit config files, so create them before starting services:

```json
{}
```

For another distributor, the agent configuration is:

```json
{"source_address": "distributor.example:8317"}
```

A distributor has no baked-defaults file. Its snapshot is created by the first accepted publish. An agent may use optional `defaults.json` beside its config to serve fallback values before connecting to the distributor.

## Linux: systemd

Create the `detent` system user and group without a login shell. Install the units from `packaging/systemd/` in `/etc/systemd/system/`. systemd creates and owns the runtime and state directories using `RuntimeDirectory=` and `StateDirectory=`. stdout and stderr go to the journal; avoid a second, application-managed log file and rotation policy.

```sh
sudo systemctl daemon-reload
sudo systemctl enable --now detentd detent-agent
journalctl -u detentd -u detent-agent
/usr/local/bin/detent get search:max_results --system
```

The units run as `detent`, restrict filesystem writes, and grant no extra capabilities. An agent can run on a host without a local distributor; point its config at the remote distributor. Config changes require a restart.

## macOS: launchd

Create a service account and group named `_detent`, without an interactive login. Create `/var/run/detent` (mode `0755`), `/var/db/detent` (mode `0700`), and `/var/log/detent` (mode `0750`), owned by `_detent`. The root-owned `org.detent.prepare-runtime` launch job recreates only the runtime directory after reboot. launchd has no ordering guarantee between these jobs; the agent retries through `KeepAlive` if it starts before directory preparation completes. Persistent state and log directories are provisioned at installation. The agent itself does not escalate privileges.

Install `packaging/launchd/org.detent.*.plist` in `/Library/LaunchDaemons/`, owned by root with mode `0644`. Adjust `/usr/local/bin` if installation uses a different prefix. Use `launchctl bootstrap system PLIST` and `launchctl bootout system PLIST` to load and unload. launchd owns restarts and captures stdout/stderr in `/var/log/detent`. Configure log rotation using the host's logging policy. Do not use the agent's `--daemonize` option.

## FreeBSD: rc.d

Create the `detent` service user and group. Install `packaging/rc.d/detentd` and `packaging/rc.d/detent_agent` in `/usr/local/etc/rc.d/` with mode `0755`. The scripts use `daemon(8)` as supervisor with separate supervisor pidfiles, run the programs as `detent`, and write output to `/var/log/detent`. The start hook recreates service-owned directories after reboot. Enable `detentd_enable="YES"` and `detent_agent_enable="YES"` in `/etc/rc.conf`. Manage them with `service detentd start` and `service detent_agent start`. Add `/var/log/detent/*.log` to the host's `newsyslog` policy; send HUP to the daemon supervisor to reopen output after rotation.

## Operations and deployment limits

`detent get NAME --system` and `detent-agent --status --system` attach to the managed agent. If it is absent, `get` reports that the service must be started through its manager. It never starts a private replacement in a user's home. `--default VALUE` still provides a per-call fallback. Stop and restart through the service manager, not `service-manager stop`.

The legacy distributor HTTP API and multicast transport have no built-in authentication or TLS. Restrict their network exposure by a firewall or an authenticated TLS proxy. The opt-in durable authority has bearer-authenticated publisher requests, verified publisher TLS and mutually authenticated fixed-voter transport; configure it using the [authority transport guide](AUTHORITY_TRANSPORT.md). Read the audit report for remaining deployment limits.

These templates are deployment artifacts, not an installer that modifies the host. Native service-manager integration must be verified on each platform. macOS functional tests do not validate systemd or FreeBSD rc.d at runtime.

References: [systemd directory management](https://github.com/systemd/systemd/blob/main/man/systemd.exec.xml), [launchd process supervision](https://developer.apple.com/library/archive/documentation/MacOSX/Conceptual/BPSystemStartup/Chapters/CreatingLaunchdJobs.html), [FreeBSD rc.d](https://docs.freebsd.org/en/articles/rc-scripting/), [FreeBSD daemon](https://man.freebsd.org/cgi/man.cgi?apropos=0&manpath=FreeBSD+14.2-RELEASE&query=daemon&sektion=8).

When upgrading to group rollout support (shared-memory layout version 2), restart the agent through its service manager and upgrade the readers too. Older readers reject the new layout.

## Log controls

Both daemons emit timestamped, severity-marked logs to stderr. The default level is `info`; use `log_level`, numeric `verbosity`, and `color` in the service's JSON configuration, or `--log-level`, `-v`, and `--color` on the command line. Automatic color produces plain output when redirected. Use `color: "never"` when the service manager allocates a terminal. See [Daemon logging](LOGGING.md) for examples, lifecycle behavior, and measured footprint.

## Foreground, background, and status

Both `detent-agent` and `detentd` run directly in the foreground. They accept `--daemonize` for unmanaged user operation, `--runtime-dir PATH`, and read-only `--status`. Lifecycle subcommands are removed. Detached startup reports readiness or an error to the caller; logs remain in the selected runtime directory. Distributor PID and log filenames include the port, so distinct instances can share one runtime directory.

`detent-agent --status` reports the locked PID and local snapshot version/count. `detentd --status` queries the configured local port's health endpoint and reports the PID when a local lock is available. Neither status operation starts a daemon. Send SIGTERM to stop an unmanaged process; service managers own supervised stop/restart. `--system --daemonize` and `--status --daemonize` fail for both binaries.

## Opt-in durable authority

The authority profile uses `detentd --authority CONFIG` in the foreground under the same service-manager conventions. Keep its credential configuration owned by root or the service account with mode `0600` (controlled group-readable `0640` is accepted). Provision a private state-directory parent owned by the service account and use a separate authority directory; do not point it at a legacy snapshot or mix multicast peers. Loopback single-voter and authenticated fixed-voter configurations are supported. [The authority guide](AUTHORITY_ARCHITECTURE.md) describes durability, retry and migration boundaries; [authority transport](AUTHORITY_TRANSPORT.md) specifies TLS, peer identities and replicated configuration. The opt-in managed agent consumes this authenticated endpoint; see [managed delivery configuration](USAGE.md#managed-authority-delivery). The assembled managed-agent/SDK component has Architect acceptance; sustained fleet qualification remains separate.

### Managed agent state

Managed agent generations are persistent state, separate from distributor authority data and runtime PID files. System defaults are `/var/lib/detent-agent` on Linux and `/var/db/detent-agent` on macOS and FreeBSD. The systemd template provisions `StateDirectory=detent-agent` with mode `0755`; the FreeBSD template provisions the matching directory. Provision the macOS directory with the service-account owner and mode `0755` before loading its job.

Keep the managed credential file private (`0600` or controlled `0640`), and distribute public `agent.json` configuration separately. System artifacts default to `0644` so application accounts can read and lease generations; private deployment can select `0640` and a shared group. Directory traversal permission must match the intended readers. Authority storage remains private. Journald, launchd, or the configured rc logging owns supervised logs; runtime PID paths remain temporary.

## Durable relay service

Build `detent-relay` with `DETENT_BUILD_RELAY=ON` and install the matching systemd, launchd or rc.d template. It follows foreground supervision, `--system`, `--config`, `--status`, logging and unmanaged `--daemonize` conventions. The default private configuration is `relay.json`; TLS listens on loopback port `8318` unless configured otherwise. Configure all upstream bundle credentials and exact downstream grants before startup; see [the relay guide](RELAY.md).

Relay durable roots require service ownership and mode `0700`, and their artifacts remain `0600` even in system mode. Agents consume the relay through authenticated TLS; application readers access their own agent generations. Readiness requires durable recovery and fresh authentication for every configured bundle. The daemon drains listener originals and shared disk completions before retiring stores and workers. Hosting has Stage 5B component acceptance; system-service templates do not establish sustained fleet acceptance.

### Reader capacity and service credentials

Applications and service status checks use the public `agent.json` reader settings described in [usage](USAGE.md#managed-reader-capacity). Configure `reader_records`, image bounds and reader heap/mapping budgets for the admitted deployment size; the private daemon capacity does not automatically authorize a reader increase. A root-owned `0644` public file is valid when its ACL does not add access rules. Keep TLS material and tokens in the existing private managed configuration.
