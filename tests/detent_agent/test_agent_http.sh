#!/bin/sh
# Real CLI + distributor: persistent address config, no baked defaults.
set -eu
agent=$1
reader="$(dirname "$agent")/detent"
distributor=$2
root=$3
curl=$4
mkdir -p "$root/config/detent" "$root/session"
unset DETENT_DEFAULTS_PATH DETENT_SOURCE_HTTP DETENT_SOURCE_ADDRESS DETENT_SOURCE_PATH DETENT_RUNTIME_DIR DETENT_POLL_INTERVAL_MS
export XDG_CONFIG_HOME="$root/config" XDG_RUNTIME_DIR="$root/session" TMPDIR="$root"
port=$((20000 + $$ % 20000))
printf '{"source_address":"127.0.0.1:%s","poll_interval_ms":10}\n' "$port" > "$root/config/detent/agent.json"
"$distributor" "$port" "$root/snapshot.json" --reactors 1 --workers 1 > "$root/distributor.log" 2>&1 &
server_pid=$!
stop_agent() {
    if test -f "$root/session/detent/agent.pid"; then
        pid=$(cat "$root/session/detent/agent.pid")
        kill "$pid" 2>/dev/null || true
        attempt=0
        while "$agent" --status > /dev/null 2>&1; do
            attempt=$((attempt + 1)); test "$attempt" -lt 50; sleep 0.1
        done
    fi
}
cleanup() {
    result=$?
    if test "$result" -ne 0 && test -f "$root/session/detent/agent.log"; then
        head -c 65536 "$root/session/detent/agent.log" >&2
    fi
    stop_agent || true
    kill "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
    rm -rf "$root"
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM
attempt=0
until "$curl" -fs "http://127.0.0.1:$port/v1/health" > /dev/null; do
    attempt=$((attempt + 1))
    test "$attempt" -lt 50
    kill -0 "$server_pid"
    sleep 0.1
done
"$curl" -fs -X PUT "http://127.0.0.1:$port/v1/snapshot" -d '{"schema_version":1,"version":1,"knobs":{"search:max_results":{"type":"int","default":500}}}' > /dev/null
# Run from a different directory to prove startup is independent of cwd.
cd "$root"
test "$("$reader" get search:max_results)" = 500
"$agent" --status
"$curl" -fs -X PUT "http://127.0.0.1:$port/v1/snapshot" -d '{"schema_version":1,"version":2,"knobs":{"search:max_results":{"type":"int","default":501}}}' > /dev/null
attempt=0
until test "$("$reader" get search:max_results)" = 501; do
    attempt=$((attempt + 1))
    test "$attempt" -lt 50
    sleep 0.1
done
stop_agent
# An address flag replaces a conflicting lower-priority file source.
printf '{"source_path":"absent.json"}\n' > "$root/config/detent/agent.json"
test "$("$reader" get search:max_results --source-address "127.0.0.1:$port")" = 501
stop_agent
test "$("$reader" get search:max_results --source-http "127.0.0.1:$port")" = 501
stop_agent
# Environment address overrides source_path in config, too.
export DETENT_SOURCE_ADDRESS="127.0.0.1:$port"
test "$("$reader" get search:max_results)" = 501
stop_agent
