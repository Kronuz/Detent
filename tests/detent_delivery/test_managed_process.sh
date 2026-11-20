#!/usr/bin/env bash
set -Eeuo pipefail
source "$(dirname "$0")/../support/bound_port.sh"
trap 'printf "Managed fixture failed at line %s\n" "$LINENO" >&2' ERR
authority=$1
agent=$2
reader=$3
interpreter=$4
certificates=$5
delivery_mode=${6:-poll}
image_format=${7:-flat-v1}
transition_to_flat=false
if [[ "$image_format" == paged-to-flat ]]; then
    image_format=paged-v2
    transition_to_flat=true
fi
check_format() {
    "$interpreter" "$(dirname "$0")/verify_local_format.py" "$@"
}
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
        printf 'Managed fixture exit=%s at line %s\n' "$result" "${1:-unknown}" >&2
        for logfile in "$root"/*.log "$root"/*.txt; do
            [[ -f "$logfile" ]] || continue
            printf '=== %s ===\n' "${logfile##*/}" >&2
            tail -n 80 "$logfile" >&2 || true
        done
        printf 'Failed managed fixture retained at %s\n' "$root" >&2
    else
        rm -rf "$root"
    fi
}
trap 'cleanup "$LINENO"' EXIT
identity='"cluster":"63000000000000000000000000000000","group":"67000000000000000000000000000000","epoch":"65000000000000000000000000000000"'
writer_token=managed-writer-test-000000000000000000000000
reader_token=managed-reader-test-000000000000000000000000
printf '{%s,"directory":"authority-data","publisher_tls":{"certificate":"%s/one.pem","key":"%s/one.key","ca":"%s/ca.pem"},"credentials":[{"token":"%s","producer":"writer","namespaces":["ns","other"]},{"token":"%s","producer":"agent","principal":"agent-reader","publish":false,"namespaces":["ns"]}]}\n' \
    "$identity" "$certificates" "$certificates" "$certificates" "$writer_token" "$reader_token" > "$root/authority.json"
printf '{%s,"principal":"agent-reader","namespaces":["ns"],"token":"%s","port":0,"ca":"%s/ca.pem","poll_interval_ms":10,"delivery_mode":"%s","image_format":"%s"}\n' \
    "$identity" "$reader_token" "$certificates" "$delivery_mode" "$image_format" > "$root/managed.json"
chmod 600 "$root/authority.json" "$root/managed.json"
scope=$("$interpreter" -c 'import hashlib; principal=b"agent-reader"; namespace=b"ns"; wire=b"detent-authorized-scope-v1"+len(principal).to_bytes(8,"big")+principal+(1).to_bytes(8,"big")+(1).to_bytes(8,"big")+len(namespace).to_bytes(8,"big")+namespace; print(hashlib.sha256(wire).hexdigest())')
printf '{"managed_path":"managed.json","generation_dir":"generations","runtime_dir":"run","defaults_path":"defaults.json","generation_cluster":"63000000000000000000000000000000","generation_group":"67000000000000000000000000000000","generation_epoch":"65000000000000000000000000000000","generation_scope":"%s"}\n' "$scope" > "$root/agent.json"
"$interpreter" -c 'import json,sys; p=sys.argv[1]; data=json.load(open(p)); data["reader_validation"]="full"; open(p,"w").write(json.dumps(data))' "$root/agent.json"
printf '{"schema_version":1,"knobs":{"ns:feature":{"default":7}}}\n' > "$root/defaults.json"
set_port() {
    local value=$1 name
    shift
    for name in "$@"; do
        if [[ -f "$root/$name" ]]; then
            "$interpreter" -c 'import json,sys; p=sys.argv[1]; data=json.load(open(p)); data["port"]=int(sys.argv[2]); open(p,"w").write(json.dumps(data))' "$root/$name" "$value"
        fi
    done
}
post() {
    curl -fsS --max-time 10 --cacert "$certificates/ca.pem" --resolve "localhost:$port:127.0.0.1" \
        -H "Authorization: Bearer $writer_token" -H 'Content-Type: application/json' --data "$2" "https://localhost:$port$1"
}
"$authority" --authority "$root/authority.json" --port 0 --runtime-dir "$root/authority-run" > "$root/authority.log" 2>&1 &
authority_pid=$!
port=$(bound_port "$root/authority.log" "$authority_pid")
set_port "$port" managed.json
deadline=$((SECONDS + 30)); while (( SECONDS < deadline )); do
    if post /v2/open "{$identity,\"expected_generation\":0,\"nonce\":\"61000000000000000000000000000000\"}" > "$root/open.json" 2>/dev/null; then break; fi
    sleep 0.05
done
grep -q '"generation":1' "$root/open.json"
post /v2/transactions "{$identity,\"generation\":1,\"sequence\":1,\"namespace\":\"ns\",\"operations\":[{\"key\":\"ns:feature\",\"value\":{\"default\":42},\"expected_revision\":0,\"absent_namespace_revision\":0}]}" > "$root/first.json"
first_revision=$("$interpreter" -c 'import json,sys; print(json.load(open(sys.argv[1]))["revision"])' "$root/first.json")
"$agent" --config "$root/agent.json" --initialize-managed > "$root/agent.log" 2>&1 &
agent_pid=$!
wait_value() {
    local actual=
    deadline=$((SECONDS + 30)); while (( SECONDS < deadline )); do
        if actual=$("$reader" get ns:feature --system --config "$root/agent.json" 2>"$root/reader-get.log"); then
            if [[ "$actual" == "$1" ]]; then return; fi
        fi
        if ! kill -0 "$agent_pid" 2>/dev/null; then cat "$root/agent.log"; return 1; fi
        sleep 0.02
    done
    printf 'Reader expected=%s actual=%s\n' "$1" "$actual" >&2
    "$agent" --status --config "$root/agent.json" >&2 || true
    cat "$root/reader-get.log" "$root/agent.log" >&2
    return 1
}
wait_ready() {
    deadline=$((SECONDS + 30)); while (( SECONDS < deadline )); do
        if grep -q 'managed local store ready' "$1"; then return; fi
        sleep 0.02
    done
    cat "$1" >&2
    return 1
}
wait_agent_failure() {
    local status=0
    wait "$1" || status=$?
    if [[ "$status" != 1 ]]; then
        printf 'Expected agent error exit=1, observed=%s\n' "$status" >&2
        return 1
    fi
    grep -q '^detent: ' "$2"
}
wait_value 42
# Reader limits are independent of private delivery credentials.
"$interpreter" -c 'import json,sys; d=json.load(open(sys.argv[1])); d.pop("managed_path",None); d["reader_image_bytes"]=1; open(sys.argv[2],"w").write(json.dumps(d))' "$root/agent.json" "$root/reader-limits.json"
chmod 644 "$root/reader-limits.json"
[[ $("$reader" get ns:feature --system --config "$root/reader-limits.json") == 7 ]]
[[ $(DETENT_READER_IMAGE_BYTES=3221225472 "$reader" get ns:feature --system --config "$root/reader-limits.json") == 42 ]]
[[ $(DETENT_READER_IMAGE_BYTES=1 "$reader" get ns:feature --system --config "$root/reader-limits.json" --reader-image-bytes 3221225472) == 42 ]]
check_format "$root/generations" "$image_format"
[[ $("$reader" get ns:feature --system --config "$root/agent.json" --generation-scope 6600000000000000000000000000000000000000000000000000000000000000) == 7 ]]
check_format "$root/generations" mark > "$root/storage-before"
post /v2/transactions "{$identity,\"generation\":1,\"sequence\":2,\"namespace\":\"other\",\"operations\":[{\"key\":\"other:hidden\",\"value\":{\"default\":99},\"expected_revision\":0,\"absent_namespace_revision\":0}]}" > "$root/progress.json"
progress_revision=$("$interpreter" -c 'import json,sys; print(json.load(open(sys.argv[1]))["revision"])' "$root/progress.json")
deadline=$((SECONDS + 30)); while (( SECONDS < deadline )); do
    if "$agent" --status --config "$root/agent.json" | grep -q "durable revision=$progress_revision "; then break; fi
    sleep 0.02
done
"$agent" --status --config "$root/agent.json" | grep -q "durable revision=$progress_revision "
[[ $("$reader" get other:hidden --system --config "$root/agent.json" --default absent) == absent ]]
post /v2/transactions "{$identity,\"generation\":1,\"sequence\":3,\"namespace\":\"ns\",\"operations\":[{\"key\":\"ns:feature\",\"value\":null,\"expected_revision\":$first_revision}]}" > "$root/deleted.json"
deleted_revision=$("$interpreter" -c 'import json,sys; print(json.load(open(sys.argv[1]))["revision"])' "$root/deleted.json")
deadline=$((SECONDS + 30)); while (( SECONDS < deadline )); do
    if "$agent" --status --config "$root/agent.json" > "$root/deleted-status.txt" &&
        grep -Fq "durable revision=$deleted_revision knob_count=0" "$root/deleted-status.txt"; then break; fi
    sleep 0.02
done
grep -Fq "durable revision=$deleted_revision knob_count=0" "$root/deleted-status.txt"
wait_value 7
kill -KILL "$agent_pid"; wait "$agent_pid" 2>/dev/null || true; agent_pid=
[[ $("$reader" get ns:feature --system --config "$root/agent.json") == 7 ]]
if "$agent" --status --config "$root/agent.json" > "$root/stopped.txt"; then exit 1; fi
grep -q 'durable revision=' "$root/stopped.txt"
if "$agent" --status --config "$root/agent.json" --reader-image-bytes 1 > "$root/reader-low-status.txt"; then exit 1; fi
grep -q 'local generation unavailable:' "$root/reader-low-status.txt"
if "$agent" --status --config "$root/agent.json" --reader-image-bytes 3221225472 > "$root/reader-high-status.txt"; then exit 1; fi
grep -q 'durable revision=' "$root/reader-high-status.txt"
if [[ "$transition_to_flat" == true ]]; then
    "$interpreter" -c 'import json,sys; p=sys.argv[1]; data=json.load(open(p)); data["image_format"]="flat-v1"; open(p,"w").write(json.dumps(data))' "$root/managed.json"
fi
"$agent" --config "$root/agent.json" > "$root/agent-restart.log" 2>&1 &
agent_pid=$!
wait_ready "$root/agent-restart.log"
check_format "$root/generations" same-store "$root/storage-before"
check_format "$root/generations" "$image_format"
if [[ "$transition_to_flat" == true ]]; then
    post /v2/transactions "{$identity,\"generation\":1,\"sequence\":4,\"namespace\":\"other\",\"operations\":[{\"key\":\"other:hidden\",\"value\":{\"default\":100},\"expected_revision\":$progress_revision}]}" > "$root/flat-forward.json"
    flat_revision=$("$interpreter" -c 'import json,sys; print(json.load(open(sys.argv[1]))["revision"])' "$root/flat-forward.json")
    deadline=$((SECONDS + 30)); while (( SECONDS < deadline )); do
        if "$agent" --status --config "$root/agent.json" | grep -q "durable revision=$flat_revision "; then break; fi
        sleep 0.02
    done
    "$agent" --status --config "$root/agent.json" | grep -q "durable revision=$flat_revision "
    check_format "$root/generations" flat-v1
    [[ $("$reader" get ns:feature --system --config "$root/agent.json") == 7 ]]
    image_format=flat-v1
fi
kill -TERM "$agent_pid"; wait "$agent_pid"; agent_pid=
cp "$root/generations/selected" "$root/selected-before"
cp "$root/managed.json" "$root/managed-good.json"
"$interpreter" -c 'import json,sys; p=sys.argv[1]; data=json.load(open(p)); data["token"]="invalid-reader-token-000000000000000000000"; open(p,"w").write(json.dumps(data))' "$root/managed.json"
"$agent" --config "$root/agent.json" > "$root/agent-unauthorized.log" 2>&1 &
agent_pid=$!
deadline=$((SECONDS + 30)); while (( SECONDS < deadline )); do
    if grep -Eq 'managed delivery failures|distribution session reset' "$root/agent-unauthorized.log"; then break; fi
    sleep 0.02
done
grep -Eq 'managed delivery failures|distribution session reset' "$root/agent-unauthorized.log"
[[ $("$reader" get ns:feature --system --config "$root/agent.json") == 7 ]]
deadline=$((SECONDS + 30)); while (( SECONDS < deadline )); do
    if ! kill -0 "$agent_pid" 2>/dev/null; then break; fi
    sleep 0.02
done
if kill -0 "$agent_pid" 2>/dev/null; then exit 1; fi
wait_agent_failure "$agent_pid" "$root/agent-unauthorized.log"
agent_pid=
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
deadline=$((SECONDS + 30)); while (( SECONDS < deadline )); do
    if post /v2/open "{$identity,\"expected_generation\":0,\"nonce\":\"62000000000000000000000000000000\"}" > "$root/reset-open.json" 2>/dev/null; then break; fi
    sleep 0.05
done
grep -q '"generation":1' "$root/reset-open.json"
post /v2/transactions "{$identity,\"generation\":1,\"sequence\":1,\"namespace\":\"ns\",\"operations\":[{\"key\":\"ns:feature\",\"value\":{\"default\":84},\"expected_revision\":0,\"absent_namespace_revision\":0}]}" > "$root/reset-value.json"
"$agent" --config "$root/agent.json" > "$root/agent-reset.log" 2>&1 &
agent_pid=$!
wait_ready "$root/agent-reset.log"
wait_value 84
check_format "$root/generations" same-store "$root/storage-before"
kill -TERM "$agent_pid"; wait "$agent_pid"; agent_pid=
[[ $("$reader" get ns:feature --system --config "$root/agent.json") == 84 ]]
"$agent" --config "$root/agent.json" > "$root/agent-reset-restart.log" 2>&1 &
agent_pid=$!
wait_ready "$root/agent-reset-restart.log"
wait_value 84
check_format "$root/generations" same-store "$root/storage-before"
kill -TERM "$agent_pid"; wait "$agent_pid"; agent_pid=
# A terminal post-readiness recovery failure must reach the service manager.
"$agent" --config "$root/agent.json" > "$root/agent-fatal.log" 2>&1 &
agent_pid=$!
wait_ready "$root/agent-fatal.log"
mv "$root/generations" "$root/displaced-generations"
mkdir "$root/generations"
chmod 700 "$root/generations"
post /v2/transactions "{$identity,\"generation\":1,\"sequence\":2,\"namespace\":\"other\",\"operations\":[{\"key\":\"other:hidden\",\"value\":{\"default\":100},\"expected_revision\":0,\"absent_namespace_revision\":0}]}" > "$root/fatal-progress.json"
for ((attempt=0;attempt<300;attempt++)); do
    if ! kill -0 "$agent_pid" 2>/dev/null; then break; fi
    sleep 0.02
done
if kill -0 "$agent_pid" 2>/dev/null; then exit 1; fi
wait_agent_failure "$agent_pid" "$root/agent-fatal.log"
agent_pid=
printf 'Authenticated delivery, identity binding, rejected credentials, progress, deletion defaults, stopped reads, trusted-reset restart and fatal service exit passed\n'

