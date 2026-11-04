> Status: CURRENT (2026-11-04) how `detentd --status` reads an authority's health, from `src/detentd/main.cc` and `include/detent_transport/https_client.hh`.

# Authority status probe

`detentd --status` asks a running instance for `GET /v1/health` and prints its pid and the body. It does not start a
server and does not take the process lock.

## Target

1. The address is the authority configuration's `publisher_bind` when `--authority CONFIG` is given, and `127.0.0.1`
   (`detent_cli::kLoopbackHost`) otherwise. An unspecified bind address (`0.0.0.0`, `::`) probes the loopback address
   of the same family. The port is `--port`, `DETENTD_PORT` or 8317.
2. With `publisher_tls` in the configuration the probe is HTTPS: it verifies the chain against the configured
   certificate authority and the name against `publisher_name` (`localhost` by default). Without it the probe is plain
   HTTP and the `Host` header is the address.

## Exchange

`detent::transport::https_health` and `detent::transport::http_health` share `detail::exchange_health`. Each connects
(and handshakes, for TLS), writes `GET /v1/health HTTP/1.1` with `Connection: close`, and feeds the reply to
`http_parser` in 4096-byte reads.

1. The body is bounded by `maximum_bytes` (65,536 by default). The bound applies to the bytes read and to the body
   appended, and an overrun throws `health response bound`.
2. A malformed reply throws `health response framing`. A status other than 200 throws `health endpoint failed`.
3. One `task::deadline` covers connect, handshake, write and read (two seconds by default); an expired deadline throws
   `health deadline`. A silent peer ends the probe at the deadline.

`--status` prints `not reachable: <reason>` and exits 1 on any exception, and exits 0 after printing the body
otherwise. It also prints `running (pid N)` first when the pid file named for the port holds a live lock.

## Tests

`test_authority_daemon` (`tests/detent_authority/test_daemon.sh`) runs `--status` without `--authority` against a plain
daemon and checks the body. `test_authority_transport_https` covers the TLS probe's name check, body bound and
deadline, and the plain probe's deadline.
