---
title: "System Services"
---

On servers, run `detentd` and `detent-agent` under the platform's service
manager, as a dedicated unprivileged account. The repository ships templates
for systemd, launchd and FreeBSD rc.d in `packaging/`, installed to
`share/detent/` by `cmake --install`.

---

## Layout

| Platform | Configuration | Agent runtime | Distributor state | Logs |
| --- | --- | --- | --- | --- |
| Linux | `/etc/detent` | `/run/detent` | `/var/lib/detent` | systemd journal |
| FreeBSD | `/usr/local/etc/detent` | `/var/run/detent` | `/var/db/detent` | `/var/log/detent` |
| macOS | `/Library/Application Support/Detent` | `/var/run/detent` | `/var/db/detent` | `/var/log/detent` |

1. Configuration is root-owned and readable by the service account.
2. Distributor state is owned by the service account, mode `0700`.
3. The agent's runtime directory is service-owned, mode `0755`, with `0644`
   pid and snapshot files, so every local user can check status and read
   knobs, and only the service account can change them.

The units pass explicit configuration files, so create them first. An empty
object is a valid configuration that uses every default:

```sh
echo '{}' | sudo tee /etc/detent/detentd.json /etc/detent/agent.json
```

For an agent on a host without a local distributor:

```json title="/etc/detent/agent.json"
{"source_address": "distributor.internal:8317"}
```

:::hint[Knobs are not secrets]{.warning}
Anything published through a system agent is readable by every local account.
Never put credentials, tokens or personal data in knobs.
:::

---

## Linux: systemd

Create the `detent` system user and group without a login shell, then install
the units:

```sh
sudo useradd --system --no-create-home --shell /usr/sbin/nologin detent
sudo cp /usr/local/share/detent/systemd/*.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now detentd detent-agent
```

systemd creates and owns the runtime and state directories through
`RuntimeDirectory=` and `StateDirectory=`, and sends stdout and stderr to the
journal:

```sh
journalctl -u detentd -u detent-agent
detent get search:max_results --system
```

The units run as `detent`, use `ProtectSystem=strict`, `ProtectHome`,
`PrivateTmp` and `NoNewPrivileges`, and grant no extra capabilities.
Configuration changes take effect on restart.

---

## macOS: launchd

1. Create a `_detent` service account and group without an interactive login.
2. Create `/var/run/detent` (`0755`), `/var/db/detent` (`0700`) and
   `/var/log/detent` (`0750`), owned by `_detent`.
3. Install `org.detent.*.plist` in `/Library/LaunchDaemons/`, owned by root
   with mode `0644`. Adjust `/usr/local/bin` if you installed elsewhere.

```sh
sudo launchctl bootstrap system /Library/LaunchDaemons/org.detent.prepare-runtime.plist
sudo launchctl bootstrap system /Library/LaunchDaemons/org.detent.detentd.plist
sudo launchctl bootstrap system /Library/LaunchDaemons/org.detent.detent-agent.plist
```

The root-owned `org.detent.prepare-runtime` job recreates only the runtime
directory after a reboot. launchd doesn't order jobs, so the agent relies on
`KeepAlive` to retry if it starts first. launchd captures stdout and stderr in
`/var/log/detent`; rotate them with the host's logging policy. Don't use
`--daemonize` under launchd.

---

## FreeBSD: rc.d

```sh
sudo pw useradd detent -d /nonexistent -s /usr/sbin/nologin
sudo install -m 0755 /usr/local/share/detent/rc.d/detentd /usr/local/etc/rc.d/
sudo install -m 0755 /usr/local/share/detent/rc.d/detent_agent /usr/local/etc/rc.d/
sudo sysrc detentd_enable=YES detent_agent_enable=YES
sudo service detentd start
sudo service detent_agent start
```

The scripts supervise the daemons with `daemon(8)`, using separate supervisor
pidfiles, run them as `detent`, and write output to `/var/log/detent`. Add
`/var/log/detent/*.log` to `newsyslog` and send HUP to the supervisor to reopen
output after rotation.

---

## Operating System Agents

```sh
detent get NAME --system
detent-agent --status --system
```

With `--system`, readers attach to the managed agent and never start a private
replacement in a user's home. If the service isn't running, `get` says so;
`--default VALUE` still provides a per-call fallback. Stop and restart through
the service manager.

:::hint[Verify on each platform]{.caution}
These templates are deployment artifacts, not an installer. macOS functional
tests don't exercise systemd or rc.d at runtime, so verify service-manager
integration on each target platform.
:::

### Managed Agents

Managed generations are persistent state, separate from runtime files: the
systemd template provisions `StateDirectory=detent-agent` (`/var/lib/detent-agent`,
mode `0755`). On macOS and FreeBSD, provision `/var/db/detent-agent` owned by
the service account. Keep `managed.json` private (`0600`) and distribute the
public `agent.json` separately.

## Durable relay

The optional [durable relay](/Detent/guides/durable-relay/) has matching systemd, launchd and rc.d templates. It uses `relay.json`, private durable roots and authenticated TLS on loopback port `8318` by default. Run it in the foreground under the service manager. Hosting remains under Stage 5B qualification.
