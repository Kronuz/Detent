#!/usr/bin/env bash
set -euo pipefail
authority=$1
agent=$2
reader=$3
interpreter=$4
certificates=$5
mkdir -p .scratch
root=$(mktemp -d "$PWD/.scratch/managed-delivery.XXXXXX")
authority_pid=
agent_pid=
cleanup() {
	local result=$?
    for process in "$agent_pid" "$authority_pid"; do
        if [[ -n "$process" ]]; then kill "$process" 2>/dev/null || true; wait "$process" 2>/dev/null || true; fi
    done
    if [[ "$result" != 0 ]]; then
        cat "$root/authority.log" "$root/agent.log" "$root/agent-restart.log" 2>/dev/null || true
        printf 'Failed managed fixture retained at %s\n' "$root" >&2
    else
        rm -rf "$root"
    fi
}
trap cleanup EXIT
port=$("$interpreter" -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1])')
identity='"cluster":"63000000000000000000000000000000","group":"67000000000000000000000000000000","epoch":"65000000000000000000000000000000"'
writer_token=managed-writer-test-000000000000000000000000
reader_token=managed-reader-test-000000000000000000000000
printf '{%s,"directory":"authority-data","publisher_tls":{"certificate":"%s/one.pem","key":"%s/one.key","ca":"%s/ca.pem"},"credentials":[{"token":"%s","producer":"writer","namespaces":["ns","other"]},{"token":"%s","producer":"agent","principal":"agent-reader","publish":false,"namespaces":["ns"]}]}\n' \
    "$identity" "$certificates" "$certificates" "$certificates" "$writer_token" "$reader_token" > "$root/authority.json"
printf '{%s,"principal":"agent-reader","namespaces":["ns"],"token":"%s","port":%s,"ca":"%s/ca.pem","poll_interval_ms":10}\n' \
    "$identity" "$reader_token" "$port" "$certificates" > "$root/managed.json"
chmod 600 "$root/authority.json" "$root/managed.json"
scope=$("$interpreter" -c 'import hashlib; principal=b"agent-reader"; namespace=b"ns"; wire=b"detent-authorized-scope-v1"+len(principal).to_bytes(8,"big")+principal+(1).to_bytes(8,"big")+(1).to_bytes(8,"big")+len(namespace).to_bytes(8,"big")+namespace; print(hashlib.sha256(wire).hexdigest())')
printf '{"managed_path":"managed.json","generation_dir":"generations","runtime_dir":"run","defaults_path":"defaults.json","generation_cluster":"63000000000000000000000000000000","generation_group":"67000000000000000000000000000000","generation_epoch":"65000000000000000000000000000000","generation_scope":"%s"}\n' "$scope" > "$root/agent.json"
printf '{"schema_version":1,"knobs":{"ns:feature":{"type":"int","default":7}}}\n' > "$root/defaults.json"
post() {
    curl -fsS --max-time 10 --cacert "$certificates/ca.pem" --resolve "localhost:$port:127.0.0.1" \
        -H "Authorization: Bearer $writer_token" -H 'Content-Type: application/json' --data "$2" "https://localhost:$port$1"
}
"$authority" --authority "$root/authority.json" --port "$port" --runtime-dir "$root/authority-run" > "$root/authority.log" 2>&1 &
authority_pid=$!
for ((attempt=0;attempt<200;attempt++)); do
    if post /v2/open "{$identity,\"expected_generation\":0,\"nonce\":\"61000000000000000000000000000000\"}" > "$root/open.json" 2>/dev/null; then break; fi
    sleep 0.05
done
grep -q '"generation":1' "$root/open.json"
post /v2/transactions "{$identity,\"generation\":1,\"sequence\":1,\"namespace\":\"ns\",\"operations\":[{\"key\":\"ns:feature\",\"value\":{\"type\":\"int\",\"default\":42},\"expected_revision\":0,\"absent_namespace_revision\":0}]}" > "$root/first.json"
first_revision=$("$interpreter" -c 'import json,sys; print(json.load(open(sys.argv[1]))["revision"])' "$root/first.json")
"$agent" --config "$root/agent.json" --initialize-managed > "$root/agent.log" 2>&1 &
agent_pid=$!
wait_value() {
    for ((attempt=0;attempt<200;attempt++)); do
        if [[ $("$reader" get ns:feature --config "$root/agent.json" 2>/dev/null) == "$1" ]]; then return; fi
        if ! kill -0 "$agent_pid" 2>/dev/null; then cat "$root/agent.log"; return 1; fi
        sleep 0.02
    done
    cat "$root/agent.log"; return 1
}
wait_value 42
[[ $("$reader" get ns:feature --config "$root/agent.json" --system --generation-scope 6600000000000000000000000000000000000000000000000000000000000000) == 7 ]]
[[ ! -e "$root/run/snapshot.shm" ]]
cp "$root/generations/storage.meta" "$root/storage-before"
post /v2/transactions "{$identity,\"generation\":1,\"sequence\":2,\"namespace\":\"other\",\"operations\":[{\"key\":\"other:hidden\",\"value\":{\"type\":\"int\",\"default\":99},\"expected_revision\":0,\"absent_namespace_revision\":0}]}" > "$root/progress.json"
progress_revision=$("$interpreter" -c 'import json,sys; print(json.load(open(sys.argv[1]))["revision"])' "$root/progress.json")
for ((attempt=0;attempt<200;attempt++)); do
    if "$agent" --status --config "$root/agent.json" | grep -q "durable revision=$progress_revision "; then break; fi
    sleep 0.02
done
"$agent" --status --config "$root/agent.json" | grep -q "durable revision=$progress_revision "
[[ $("$reader" get other:hidden --config "$root/agent.json" --default absent) == absent ]]
post /v2/transactions "{$identity,\"generation\":1,\"sequence\":3,\"namespace\":\"ns\",\"operations\":[{\"key\":\"ns:feature\",\"value\":null,\"expected_revision\":$first_revision}]}" > "$root/deleted.json"
wait_value 7
kill -KILL "$agent_pid"; wait "$agent_pid" 2>/dev/null || true; agent_pid=
[[ $("$reader" get ns:feature --config "$root/agent.json") == 7 ]]
if "$agent" --status --config "$root/agent.json" > "$root/stopped.txt"; then exit 1; fi
grep -q 'durable revision=' "$root/stopped.txt"
"$agent" --config "$root/agent.json" > "$root/agent-restart.log" 2>&1 &
agent_pid=$!
for ((attempt=0;attempt<200;attempt++)); do
    if grep -q 'managed local store ready' "$root/agent-restart.log"; then break; fi
    sleep 0.02
done
grep -q 'managed local store ready' "$root/agent-restart.log"
cmp "$root/storage-before" "$root/generations/storage.meta"
kill -TERM "$agent_pid"; wait "$agent_pid"; agent_pid=
cp "$root/generations/selected" "$root/selected-before"
cp "$root/managed.json" "$root/managed-good.json"
"$interpreter" -c 'import json,sys; p=sys.argv[1]; data=json.load(open(p)); data["token"]="invalid-reader-token-000000000000000000000"; open(p,"w").write(json.dumps(data))' "$root/managed.json"
"$agent" --config "$root/agent.json" > "$root/agent-unauthorized.log" 2>&1 &
agent_pid=$!
for ((attempt=0;attempt<200;attempt++)); do
    if grep -q 'managed delivery failures' "$root/agent-unauthorized.log"; then break; fi
    sleep 0.02
done
grep -q 'managed delivery failures' "$root/agent-unauthorized.log"
[[ $("$reader" get ns:feature --config "$root/agent.json") == 7 ]]
kill -TERM "$agent_pid"; wait "$agent_pid"; agent_pid=
cmp "$root/selected-before" "$root/generations/selected"
cp "$root/managed-good.json" "$root/managed.json"
"$interpreter" -c 'import json,sys; p=sys.argv[1]; data=json.load(open(p)); data["epoch"]="66000000000000000000000000000000"; open(p,"w").write(json.dumps(data))' "$root/managed.json"
if "$agent" --config "$root/agent.json" --daemonize > "$root/unconfigured-reset.txt" 2>&1; then exit 1; fi
grep -q 'identity transition is not configured' "$root/unconfigured-reset.txt"
cmp "$root/selected-before" "$root/generations/selected"
kill -TERM "$authority_pid"; wait "$authority_pid"; authority_pid=
identity=${identity/65000000000000000000000000000000/66000000000000000000000000000000}
"$interpreter" -c 'import json,sys; p=sys.argv[1]; data=json.load(open(p)); data["epoch"]="66000000000000000000000000000000"; data["directory"]="authority-data-reset"; open(p,"w").write(json.dumps(data))' "$root/authority.json"
"$interpreter" -c 'import json,sys; p=sys.argv[1]; data=json.load(open(p)); data["reset_from"]={"cluster":data["cluster"],"group":data["group"],"epoch":"65000000000000000000000000000000","scope":sys.argv[2]}; data["token"]=sys.argv[3]; open(p,"w").write(json.dumps(data))' "$root/managed.json" "$scope" "$reader_token"
"$interpreter" -c 'import json,sys; p=sys.argv[1]; data=json.load(open(p)); data["generation_epoch"]="66000000000000000000000000000000"; open(p,"w").write(json.dumps(data))' "$root/agent.json"
"$authority" --authority "$root/authority.json" --port "$port" --runtime-dir "$root/authority-run" > "$root/authority-reset.log" 2>&1 &
authority_pid=$!
for ((attempt=0;attempt<200;attempt++)); do
    if post /v2/open "{$identity,\"expected_generation\":0,\"nonce\":\"62000000000000000000000000000000\"}" > "$root/reset-open.json" 2>/dev/null; then break; fi
    sleep 0.05
done
grep -q '"generation":1' "$root/reset-open.json"
post /v2/transactions "{$identity,\"generation\":1,\"sequence\":1,\"namespace\":\"ns\",\"operations\":[{\"key\":\"ns:feature\",\"value\":{\"type\":\"int\",\"default\":84},\"expected_revision\":0,\"absent_namespace_revision\":0}]}" > "$root/reset-value.json"
"$agent" --config "$root/agent.json" > "$root/agent-reset.log" 2>&1 &
agent_pid=$!
wait_value 84
cmp "$root/storage-before" "$root/generations/storage.meta"
kill -TERM "$agent_pid"; wait "$agent_pid"; agent_pid=
[[ $("$reader" get ns:feature --config "$root/agent.json") == 84 ]]
"$agent" --config "$root/agent.json" > "$root/agent-reset-restart.log" 2>&1 &
agent_pid=$!
for ((attempt=0;attempt<200;attempt++)); do
    if grep -q 'managed local store ready' "$root/agent-reset-restart.log"; then break; fi
    sleep 0.02
done
grep -q 'managed local store ready' "$root/agent-reset-restart.log"
wait_value 84
cmp "$root/storage-before" "$root/generations/storage.meta"
kill -TERM "$agent_pid"; wait "$agent_pid"; agent_pid=
# A terminal post-readiness recovery failure must reach the service manager.
"$agent" --config "$root/agent.json" > "$root/agent-fatal.log" 2>&1 &
agent_pid=$!
for ((attempt=0;attempt<200;attempt++)); do
    if grep -q 'managed local store ready' "$root/agent-fatal.log"; then break; fi
    sleep 0.02
done
grep -q 'managed local store ready' "$root/agent-fatal.log"
mv "$root/generations" "$root/displaced-generations"
mkdir "$root/generations"
chmod 700 "$root/generations"
post /v2/transactions "{$identity,\"generation\":1,\"sequence\":2,\"namespace\":\"other\",\"operations\":[{\"key\":\"other:hidden\",\"value\":{\"type\":\"int\",\"default\":100},\"expected_revision\":0,\"absent_namespace_revision\":0}]}" > "$root/fatal-progress.json"
for ((attempt=0;attempt<300;attempt++)); do
    if ! kill -0 "$agent_pid" 2>/dev/null; then break; fi
    sleep 0.02
done
if kill -0 "$agent_pid" 2>/dev/null; then exit 1; fi
if wait "$agent_pid"; then exit 1; fi
agent_pid=
printf 'Authenticated delivery, identity binding, rejected credentials, progress, deletion defaults, stopped reads, trusted-reset restart and fatal service exit passed\n'

