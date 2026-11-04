# Bound-port report

How a daemon started on port 0 tells its caller which port it got, and how the fixtures use it.

## What the code does

1. `detentd` accepts `--port 0` (and `port: 0` in its config). `port_number` in `src/detentd/main.cc` takes 0 to 65535; every other bound is unchanged.
2. `detent-relay` accepts `"port": 0` (`src/detent_relay/configuration.cc`, range 0 to 65535).
3. After `HttpAsioService::start(port)` returns, `HttpAsioService::port()` is the bound port. `detentd` logs `listening with HTTPS|HTTP on <address>:<port>` (authority) or `listening on :<port>, persisting to ...` (legacy), and `detent-relay` logs `<n> durable bundles ready on <address>:<port>`. Each prints `port()`, so the number is the bound one, never the requested 0.
4. `tests/support/bound_port.sh` (`bound_port LOG PID`) reads the first matching line from the log and prints the port. It fails, with the log on stderr, when the daemon exits or 30 s pass.
5. A fixture starts each daemon on port 0, calls `bound_port`, and writes the number into the configs that name it (`set_port` in `test_managed_process.sh` patches the `port` field of the agent, upstream and relay configs). A restart of the same daemon passes the bound port back, so clients keep their address.
6. In-process fixtures call `server.start(0)` and build the client endpoint from `server.port()`.

## Limits

1. The pid file and log file of `detentd` are named by the requested port (`detentd-0.pid` for port 0), and `--status` probes the port it is given. A fixture that needs `--status` passes the bound port.
2. A restart on the bound port rebinds a port the kernel released moments earlier; another process that binds port 0 in that window can take it. The fixtures that restart (`test_daemon.sh`, `test_managed_process.sh`) do not retry.
3. Fixtures whose daemons name each other in their configs (the three-voter authority in `test_multivoter.py` and `tests/detent_system/`) cannot start on port 0: every peer address is needed before the first start. They keep their held reservations.
