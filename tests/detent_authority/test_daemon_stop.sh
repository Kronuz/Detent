#!/usr/bin/env bash
# While --daemonize waits for the node's own leader, a SIGTERM must not signal ready to the parent,
# and a runtime failure must reach the parent as its cause, well before the 30 s startup timeout.
set -euo pipefail
binary=$1
mkdir -p .scratch
root=$(mktemp -d "$PWD/.scratch/authority-stop.XXXXXX")
cleanup() {
    pkill -KILL -f "$root/" 2>/dev/null || true
    chmod -R u+rwx "$root" 2>/dev/null || true
    rm -rf "$root"
}
trap cleanup EXIT
source "$(dirname "$0")/../support/bound_port.sh"
identity='"cluster":"63000000000000000000000000000000","group":"67000000000000000000000000000000","epoch":"65000000000000000000000000000000"'
token=authority-test-token-000000000000000000000000
config() { # NAME: a private authority configuration with its own data directory
    printf '{%s,"directory":"%s-data","credentials":[{"token":"%s","producer":"writer","namespaces":["ns"]}]}\n' "$identity" "$root/$1" "$token" > "$root/$1.json"
    chmod 600 "$root/$1.json"
}
# start NAME: --daemonize in the background; sets parent, daemon (the pid in the pidfile) once it listens.
start() {
    config "$1"
    "$binary" --authority "$root/$1.json" --port 0 --runtime-dir "$root/$1-runtime" --reactors 1 --daemonize \
        > "$root/$1.out" 2> "$root/$1.err" &
    parent=$!
    until [[ -e "$root/$1-runtime/detentd-0.log" ]]; do kill -0 "$parent" 2>/dev/null || break; sleep 0.02; done
    bound_port "$root/$1-runtime/detentd-0.log" "$parent" > /dev/null
    daemon=$(cat "$root/$1-runtime/detentd-0.pid")
}
# finish NAME: the parent's exit status, which must come well before the 30 s startup timeout.
finish() {
    local deadline=$((SECONDS + 20))
    while state=$(ps -o stat= -p "$parent" 2>/dev/null) && [[ -n "$state" && "$state" != Z* ]]; do
        if ((SECONDS >= deadline)); then echo "STARTUP: the parent was still waiting after 20 s: $(cat "$root/$1.err")" >&2; exit 1; fi
        sleep 0.05
    done
    status=0; wait "$parent" || status=$?
}

# A6: a SIGTERM during the leader wait (held stopped until the signal is pending) is not "ready".
for ((attempt=1; attempt<=3; attempt++)); do
    start "stop$attempt"
    kill -STOP "$daemon"; kill -TERM "$daemon"; kill -CONT "$daemon"
    finish "stop$attempt"
    if [[ "$status" != 0 ]]; then break; fi
done
if [[ "$status" == 0 ]] || ! grep -q 'stopped before startup completed' "$root/stop$attempt.err"; then
    echo "STARTUP: --daemonize reported ready after a SIGTERM (status $status): $(cat "$root/stop$attempt.err")" >&2
    exit 1
fi

# A6: a runtime failure after admission and before a leader is reported to the parent with its cause.
start failure
chmod 000 "$root/failure-data"
finish failure
if [[ "$status" == 0 ]] || ! grep -q 'authority stopped before a leader was elected' "$root/failure.err"; then
    echo "STARTUP: a runtime failure did not reach the parent (status $status): $(cat "$root/failure.err")" >&2
    exit 1
fi
printf 'detentd --daemonize: SIGTERM and a runtime failure during the leader wait are reported, not ready\n'
