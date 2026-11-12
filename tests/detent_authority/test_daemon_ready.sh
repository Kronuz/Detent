#!/usr/bin/env bash
# detentd reports ready only once a leader accepts /v2/open: --daemonize returns after it, and
# --status / /v1/health never say "ready":true before it.
set -euo pipefail
binary=$1
mkdir -p .scratch
root=$(mktemp -d "$PWD/.scratch/authority-ready.XXXXXX")
pid=
cleanup() {
    if [[ -n "$pid" ]]; then kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true; fi
    pkill -TERM -f "$root/" 2>/dev/null || true
    rm -rf "$root"
}
trap cleanup EXIT
source "$(dirname "$0")/../support/bound_port.sh"
identity='"cluster":"63000000000000000000000000000000","group":"67000000000000000000000000000000","epoch":"65000000000000000000000000000000"'
token=authority-test-token-000000000000000000000000
config() { # NAME: a private authority configuration with its own data directory
    printf '{%s,"directory":"%s-data","credentials":[{"token":"%s","producer":"writer","namespaces":["ns"]}]}\n' "$identity" "$1" "$token" > "$root/$1.json"
    chmod 600 "$root/$1.json"
}

# A1: the open right after --daemonize returns succeeds on its first attempt, with no retry.
config daemon
"$binary" --authority "$root/daemon.json" --port 0 --runtime-dir "$root/runtime" --reactors 1 --daemonize > "$root/start.txt"
port=$(bound_port "$root/runtime/detentd-0.log" $$)
code=$(curl -sS --max-time 10 -o "$root/open.json" -w '%{http_code}' -H "Authorization: Bearer $token" \
    -H 'Content-Type: application/json' --data "{$identity,\"expected_generation\":0,\"nonce\":\"61000000000000000000000000000000\"}" \
    "http://127.0.0.1:$port/v2/open") || code=failed
if [[ "$code" != 200 ]] || ! grep -Eq '"status":"applied"' "$root/open.json" || ! grep -Eq '"generation":1' "$root/open.json"; then
    echo "READINESS: open right after --daemonize returned $code: $(cat "$root/open.json" 2>/dev/null)" >&2
    exit 1
fi

# A2: --status reports ready only with a leader (single voter: role 2).
"$binary" --status --port "$port" --runtime-dir "$root/runtime" > "$root/status.txt"
if ! grep -Eq '"ready":true' "$root/status.txt" || ! grep -Eq '"role":2' "$root/status.txt"; then
    echo "READINESS: --status after --daemonize: $(cat "$root/status.txt")" >&2
    exit 1
fi
daemon=$(sed -n -E 's/^running \(pid ([0-9]+)\)$/\1/p' "$root/status.txt")
[[ -n "$daemon" ]] && kill -TERM "$daemon"

# A2: from the first reachable health answer, no sample says ready without a leader.
config foreground
"$binary" --authority "$root/foreground.json" --port 0 --runtime-dir "$root/foreground-runtime" --reactors 1 > "$root/foreground.log" 2>&1 &
pid=$!
port=$(bound_port "$root/foreground.log" "$pid")
early=0
for ((i=0;i<1000;i++)); do
    if health=$(curl -fsS --max-time 1 "http://127.0.0.1:$port/v1/health" 2>/dev/null); then
        if grep -Eq '"ready":true' <<< "$health"; then
            grep -Eq '"role":2' <<< "$health" || { echo "READINESS: ready:true without a leader: $health" >&2; exit 1; }
            printf 'ready:true seen after %d samples without it, role 2\n' "$early"
            break
        fi
        early=$((early + 1))
    fi
    sleep 0.01
done
grep -Eq '"ready":true' <<< "$health" || { echo "READINESS: never ready: $health" >&2; exit 1; }
printf 'detentd readiness waits for a leader: open after --daemonize and health passed\n'
