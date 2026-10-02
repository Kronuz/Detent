#!/bin/sh
# Actual distributor processes: configuration, persistence, and precedence.
set -eu
distributor=$1
root=$2
curl=$3
mkdir -p "$root/config/detent"
unset DETENTD_PORT DETENTD_PERSIST_PATH DETENTD_REACTORS DETENTD_WORKERS
export XDG_CONFIG_HOME="$root/config" XDG_STATE_HOME="$root/state"
port=$((20000 + $$ % 20000))
server_pid=
stop_server() {
    if test -n "$server_pid"; then
        kill "$server_pid" 2>/dev/null || true
        wait "$server_pid" 2>/dev/null || true
        server_pid=
    fi
}
cleanup() { stop_server; rm -rf "$root"; }
trap cleanup EXIT
trap 'exit 1' HUP INT TERM
start_server() {
    "$distributor" "$@" > "$root/server.log" 2>&1 &
    server_pid=$!
    attempt=0
    until "$curl" -fs "http://127.0.0.1:$port/v1/health" > /dev/null 2>&1; do
        attempt=$((attempt + 1))
        if test "$attempt" -ge 50 || ! kill -0 "$server_pid" 2>/dev/null; then
            cat "$root/server.log"
            exit 1
        fi
        sleep 0.1
    done
}
expect_error() {
    if "$distributor" "$@" > "$root/error.log" 2>&1; then
        echo "Expected failure: $*"
        exit 1
    fi
}
printf '{"port":%s,"persist_path":"data/snapshot.json","reactors":1,"workers":1}\n' "$port" > "$root/config/detent/detentd.json"
start_server
"$curl" -fs -X PUT "http://127.0.0.1:$port/v1/snapshot" -d '{"schema_version":1,"version":1,"knobs":{"search:max_results":{"type":"int","default":500}}}' > /dev/null
stop_server
test -f "$root/config/detent/data/snapshot.json"
start_server
"$curl" -fs "http://127.0.0.1:$port/v1/snapshot" > "$root/restored.json"
cmp "$root/restored.json" "$root/config/detent/data/snapshot.json"
stop_server
# Config port is replaced by environment, then by CLI.
printf '{"port":%s}\n' "$((port + 1))" > "$root/config/detent/detentd.json"
export DETENTD_PORT=$port
start_server
stop_server
export DETENTD_PORT=$((port + 2))
start_server --port "$port"
stop_server
unset DETENTD_PORT
# Legacy positional syntax still overrides config and selects persistence.
start_server "$port" "$root/legacy/snapshot.json"
stop_server
# No persist_path: state directory is created automatically.
printf '{"port":%s}\n' "$port" > "$root/config/detent/detentd.json"
start_server
"$curl" -fs -X PUT "http://127.0.0.1:$port/v1/snapshot" -d '{"schema_version":1,"version":1,"knobs":{}}' > /dev/null
stop_server
test -f "$root/state/detent/snapshot.json"
printf '{"typo":1}\n' > "$root/config/detent/detentd.json"
expect_error
printf '{}\n' > "$root/config/detent/detentd.json"
expect_error --port 65536
expect_error --workers 0
expect_error --port
expect_error --unknown
expect_error --config "$root/missing.json"
# Status is read-only and background startup reports readiness/errors.
export XDG_RUNTIME_DIR="$root/session"
mkdir -p "$XDG_RUNTIME_DIR"
printf '{"port":%s,"reactors":1,"workers":1}\n' "$port" > "$root/config/detent/detentd.json"
expect_error --status
 test ! -e "$root/session/detent/detentd-$port.pid"
"$distributor" --daemonize
"$distributor" --status > "$root/status.txt"
 grep 'current_version' "$root/status.txt" > /dev/null
server_pid=$(cat "$root/session/detent/detentd-$port.pid")
kill "$server_pid"
attempt=0
while "$distributor" --status > /dev/null 2>&1; do
    attempt=$((attempt + 1)); test "$attempt" -lt 50; sleep 0.1
done
server_pid=
expect_error --system --daemonize
expect_error --status --daemonize
# A startup failure in the detached child must reach the parent.
expect_error --daemonize --persist-path "$root/config/detent/detentd.json/invalid"
