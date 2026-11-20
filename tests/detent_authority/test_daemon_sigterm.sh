#!/usr/bin/env bash
# A SIGTERM under load: requests in flight finish whole, no response is cut short, and the daemon
# exits 0 within --shutdown-timeout-ms.
set -euo pipefail
binary=$1
bound=${2:-10000}
mkdir -p .scratch
root=$(mktemp -d "$PWD/.scratch/authority-sigterm.XXXXXX")
pid=
cleanup() {
    jobs -p | xargs kill 2>/dev/null || true
    if [[ -n "$pid" ]]; then kill -KILL "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true; fi
    rm -rf "$root"
}
trap cleanup EXIT
source "$(dirname "$0")/../support/bound_port.sh"
identity='"cluster":"63000000000000000000000000000000","group":"67000000000000000000000000000000","epoch":"65000000000000000000000000000000"'
printf '{%s,"directory":"data","credentials":[{"token":"t000000000000000000000000000000000000000","producer":"writer","namespaces":["ns"]}]}\n' "$identity" > "$root/config.json"
chmod 600 "$root/config.json"
"$binary" --authority "$root/config.json" --port 0 --runtime-dir "$root/runtime" --reactors 2 \
    --shutdown-timeout-ms "$bound" > "$root/daemon.log" 2>&1 &
pid=$!
port=$(bound_port "$root/daemon.log" "$pid")
for ((i=0;i<200;i++)); do
    curl -fsS --max-time 1 "http://127.0.0.1:$port/v1/health" 2>/dev/null | grep -q '"ready":true' && break
    sleep 0.05
done
# Eight clients, each a loop of health requests; every curl's exit status goes to its own file.
for ((c=0;c<8;c++)); do
    (while :; do
        curl -sS --max-time 5 -o /dev/null "http://127.0.0.1:$port/v1/health" 2>/dev/null && echo 0 >> "$root/c$c" || echo $? >> "$root/c$c"
    done) &
done
sleep 1.5
started=$SECONDS
kill -TERM "$pid"
status=0; wait "$pid" || status=$?
elapsed=$((SECONDS - started))
pid=
jobs -p | xargs kill 2>/dev/null || true
wait 2>/dev/null || true
if [[ "$status" != 0 ]]; then echo "SIGTERM: the daemon exited with status $status" >&2; cat "$root/daemon.log" >&2; exit 1; fi
if ((elapsed * 1000 > bound)); then echo "SIGTERM: the stop took ${elapsed} s, over the ${bound} ms bound" >&2; exit 1; fi
ok=$(cat "$root"/c* | grep -c '^0$' || true)
# A cut-short response is curl 18 (partial file) or 52 (empty reply); refusing or resetting a new
# connection after the stop is 7 or 56.
torn=$(cat "$root"/c* | grep -Ec '^(18|52)$' || true)
if ((ok < 8)); then echo "SIGTERM: only $ok requests completed" >&2; exit 1; fi
if ((torn)); then echo "SIGTERM: $torn responses were cut short" >&2; exit 1; fi
printf 'authority SIGTERM under load: %d requests completed, none cut short, exit 0 in %d s\n' "$ok" "$elapsed"
