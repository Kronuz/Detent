#!/bin/sh
set -eu
agent=$1
reader="$(dirname "$agent")/detent"
root=$2
mkdir -p "$root"
server_pid=
cleanup() {
    if test -n "$server_pid"; then kill "$server_pid" 2>/dev/null || true; wait "$server_pid" 2>/dev/null || true; fi
    rm -rf "$root"
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM
unset DETENT_RUNTIME_DIR DETENT_DEFAULTS_PATH DETENT_SOURCE_ADDRESS DETENT_SOURCE_HTTP DETENT_SOURCE_PATH
printf '{"schema_version":1,"knobs":{"test:value":{"type":"int","default":17}}}\n' > "$root/defaults.json"
printf '{"defaults_path":"defaults.json","runtime_dir":"run"}\n' > "$root/agent.json"
"$agent" --system --config "$root/agent.json" > "$root/output.log" 2>&1 &
server_pid=$!
i=0
until test "$("$reader" get test:value --system --config "$root/agent.json" 2>/dev/null || true)" = 17; do
    i=$((i+1)); test "$i" -lt 30; sleep 0.1
done
"$agent" --status --system --config "$root/agent.json"
test -r "$root/run/snapshot.shm"
kill "$server_pid"
wait "$server_pid"
server_pid=
if "$reader" get test:value --system --config "$root/agent.json"; then exit 1; fi
test "$("$reader" get test:value --system --config "$root/agent.json" --default 19)" = 19
