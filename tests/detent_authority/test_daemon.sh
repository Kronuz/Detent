#!/usr/bin/env bash
set -euo pipefail
binary=$1
interpreter=$2
mkdir -p .scratch
root=$(mktemp -d "$PWD/.scratch/authority-daemon.XXXXXX")
pid=
cleanup() {
    if [[ -n "$pid" ]]; then kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true; fi
    rm -rf "$root"
}
trap cleanup EXIT
# The OS chooses an available test port; this is unrelated to the default port.
port=$("$interpreter" -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1])')
identity='"cluster":"63000000000000000000000000000000","group":"67000000000000000000000000000000","epoch":"65000000000000000000000000000000"'
token=authority-test-token-000000000000000000000000
printf '{%s,"directory":"data","credentials":[{"token":"%s","producer":"writer","namespaces":["ns"]}]}\n' "$identity" "$token" > "$root/config.json"
chmod 600 "$root/config.json"
start() {
    "$binary" --authority "$root/config.json" --port "$port" --runtime-dir "$root/runtime" --workers 4 --reactors 1 > "$root/daemon.log" 2>&1 &
    pid=$!
    for ((i=0;i<200;i++)); do
        if curl -fsS --max-time 1 "http://127.0.0.1:$port/v1/health" > "$root/health.json" 2>/dev/null; then return; fi
        if ! kill -0 "$pid" 2>/dev/null; then cat "$root/daemon.log"; return 1; fi
        sleep 0.05
    done
    cat "$root/daemon.log"; return 1
}
post() {
    curl -fsS --max-time 10 -H "Authorization: Bearer $token" -H 'Content-Type: application/json' --data "$2" "http://127.0.0.1:$port$1"
}
start
for ((i=0;i<200;i++)); do
    if post /v2/open "{$identity,\"expected_generation\":0,\"nonce\":\"61000000000000000000000000000000\"}" > "$root/open.json" 2>/dev/null && rg -q '"status":"applied"' "$root/open.json"; then break; fi
    sleep 0.05
done
rg -q '"generation":1' "$root/open.json"
transaction="{$identity,\"generation\":1,\"sequence\":1,\"namespace\":\"ns\",\"operations\":[{\"key\":\"ns:feature\",\"value\":{\"type\":\"int\",\"default\":42},\"expected_revision\":0,\"absent_namespace_revision\":0}]}"
post /v2/transactions "$transaction" > "$root/ack.json"
rg -q '"status":"applied"' "$root/ack.json"
"$binary" --status --port "$port" --runtime-dir "$root/runtime" > "$root/status.txt"
rg -q 'authority|revision' "$root/status.txt"
kill -KILL "$pid"
wait "$pid" 2>/dev/null || true
pid=
start
for ((i=0;i<200;i++)); do
    if post /v2/transactions "$transaction" > "$root/retry.json" 2>/dev/null && rg -q '"status":"applied"' "$root/retry.json"; then break; fi
    sleep 0.05
done
cmp "$root/ack.json" "$root/retry.json"
post /v2/read "{$identity,\"namespace\":\"ns\",\"keys\":[\"ns:feature\"]}" > "$root/read.json"
rg -q '"default":42' "$root/read.json"
foreign=${identity/65000000000000000000000000000000/66000000000000000000000000000000}
code=$(curl -sS --max-time 10 -o "$root/foreign.json" -w '%{http_code}' -H "Authorization: Bearer $token" --data "{$foreign,\"generation\":1,\"sequence\":1}" "http://127.0.0.1:$port/v2/result")
[[ "$code" == 400 ]]
printf 'Authority daemon ACK, kill, recovery, retry, read, status and epoch fencing passed\n'
