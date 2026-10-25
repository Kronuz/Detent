#!/usr/bin/env bash
set -Eeuo pipefail
trap 'printf "Managed fixture failed at line %s\n" "$LINENO" >&2' ERR
authority=$1
agent=$2
reader=$3
interpreter=$4
certificates=$5
delivery_mode=${6:-poll}
relay=${7:-}
isolated=${8:-}
image_format=${9:-flat-v1}
transition_to_flat=false
if [[ "$image_format" == paged-to-flat ]]; then
    image_format=paged-v2
    transition_to_flat=true
fi
check_format() {
    "$interpreter" "$(dirname "$0")/verify_local_format.py" "$1" "$2"
}
mkdir -p .scratch
root=$(mktemp -d "$PWD/.scratch/managed-delivery.XXXXXX")
authority_pid=
agent_pid=
relay_pid=
empty_agent_pid=
cleanup() {
	local result=$?
    for process in "$agent_pid" "$empty_agent_pid" "$relay_pid" "$authority_pid"; do
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
wait_relay_ready() {
    local deadline=$((SECONDS + 5))
    while ((SECONDS < deadline)); do
        if "$relay" --status --config "$root/relay.json" --runtime-dir "$root/relay-run" > "$root/relay-status.json" 2> "$root/relay-status-error.txt" &&
            grep -q '"ready":true' "$root/relay-status.json"; then return; fi
        if ! kill -0 "$relay_pid" 2>/dev/null; then cat "$1"; return 1; fi
        sleep 0.02
    done
    cat "$root/relay-status.json" "$root/relay-status-error.txt" "$1" >&2
    return 1
}
port=$("$interpreter" -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1])')
identity='"cluster":"63000000000000000000000000000000","group":"67000000000000000000000000000000","epoch":"65000000000000000000000000000000"'
writer_token=managed-writer-test-000000000000000000000000
reader_token=managed-reader-test-000000000000000000000000
printf '{%s,"directory":"authority-data","publisher_tls":{"certificate":"%s/one.pem","key":"%s/one.key","ca":"%s/ca.pem"},"credentials":[{"token":"%s","producer":"writer","namespaces":["ns","other"]},{"token":"%s","producer":"agent","principal":"agent-reader","publish":false,"namespaces":["ns"]}]}\n' \
    "$identity" "$certificates" "$certificates" "$certificates" "$writer_token" "$reader_token" > "$root/authority.json"
printf '{%s,"principal":"agent-reader","namespaces":["ns"],"token":"%s","port":%s,"ca":"%s/ca.pem","poll_interval_ms":10,"delivery_mode":"%s","image_format":"%s"}\n' \
    "$identity" "$reader_token" "$port" "$certificates" "$delivery_mode" "$image_format" > "$root/managed.json"
chmod 600 "$root/authority.json" "$root/managed.json"
if [[ -n "$relay" ]]; then
    relay_port=$("$interpreter" -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1])')
    relay_token=managed-relay-test-000000000000000000000000
    cp "$root/managed.json" "$root/upstream.json"
    printf '{"connections":64,"service_limits":{"opens":4,"polls":16,"closes":4,"sessions":64,"per_principal":64,"bootstrap_transfers":12,"catchup_transfers":4},"port":%s,"certificate":"%s/one.pem","private_key":"%s/one.key","ca":"%s/ca.pem","bundles":[{"source":"upstream.json","directory":"relay-generations","mode":"fresh"}],"credentials":[{"token":"%s","principal":"agent-reader","bundles":[0]}]}\n' \
        "$relay_port" "$certificates" "$certificates" "$certificates" "$relay_token" > "$root/relay.json"
    chmod 600 "$root/relay.json"
    "$interpreter" -c 'import json,sys; p=sys.argv[1]; data=json.load(open(p)); data["port"]=int(sys.argv[2]); data["token"]=sys.argv[3]; open(p,"w").write(json.dumps(data))' "$root/managed.json" "$relay_port" "$relay_token"
    if [[ "$isolated" == isolated ]]; then
        cp "$root/upstream.json" "$root/upstream-empty.json"
        "$interpreter" -c 'import json,sys; p=sys.argv[1]; data=json.load(open(p)); data["namespaces"]=[]; open(p,"w").write(json.dumps(data))' "$root/upstream-empty.json"
        empty_token=managed-empty-relay-test-000000000000000000
        "$interpreter" -c 'import json,sys; p=sys.argv[1]; data=json.load(open(p)); data["bundles"].append({"source":"upstream-empty.json","directory":"relay-empty-generations","mode":"fresh"}); data["credentials"].append({"token":sys.argv[2],"principal":"agent-reader","bundles":[1]}); open(p,"w").write(json.dumps(data))' "$root/relay.json" "$empty_token"
        cp "$root/managed.json" "$root/managed-empty.json"
        "$interpreter" -c 'import json,sys; p=sys.argv[1]; data=json.load(open(p)); data["namespaces"]=[]; data["token"]=sys.argv[2]; open(p,"w").write(json.dumps(data))' "$root/managed-empty.json" "$empty_token"
    fi
fi
policy_epoch=1
if [[ "$isolated" == isolated ]]; then
    policy_epoch=2
    "$interpreter" - "$root" <<'PYTHON'
import json
import pathlib
import sys
root = pathlib.Path(sys.argv[1])
for name in ("managed.json", "managed-empty.json", "upstream.json", "upstream-empty.json"):
    path = root / name
    data = json.loads(path.read_text())
    data["policy_epoch"] = 2
    path.write_text(json.dumps(data))
for name in ("authority.json", "relay.json"):
    path = root / name
    data = json.loads(path.read_text())
    for credential in data["credentials"]:
        if credential.get("principal") == "agent-reader":
            credential["policy_epoch"] = 2
    path.write_text(json.dumps(data))
PYTHON
fi
scope=$("$interpreter" -c 'import hashlib,sys; principal=b"agent-reader"; namespace=b"ns"; wire=b"detent-authorized-scope-v1"+len(principal).to_bytes(8,"big")+principal+int(sys.argv[1]).to_bytes(8,"big")+(1).to_bytes(8,"big")+len(namespace).to_bytes(8,"big")+namespace; print(hashlib.sha256(wire).hexdigest())' "$policy_epoch")
printf '{"managed_path":"managed.json","generation_dir":"generations","runtime_dir":"run","defaults_path":"defaults.json","generation_cluster":"63000000000000000000000000000000","generation_group":"67000000000000000000000000000000","generation_epoch":"65000000000000000000000000000000","generation_scope":"%s"}\n' "$scope" > "$root/agent.json"
if [[ "$isolated" == isolated ]]; then
    empty_scope=$("$interpreter" -c 'import hashlib,sys; p=b"agent-reader"; print(hashlib.sha256(b"detent-authorized-scope-v1"+len(p).to_bytes(8,"big")+p+int(sys.argv[1]).to_bytes(8,"big")+(0).to_bytes(8,"big")).hexdigest())' "$policy_epoch")
    cp "$root/agent.json" "$root/agent-empty.json"
    "$interpreter" -c 'import json,sys; p=sys.argv[1]; data=json.load(open(p)); data.update(managed_path="managed-empty.json",generation_dir="generations-empty",runtime_dir="run-empty",generation_scope=sys.argv[2]); open(p,"w").write(json.dumps(data))' "$root/agent-empty.json" "$empty_scope"
fi
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
if [[ -n "$relay" ]]; then
    "$relay" --config "$root/relay.json" --runtime-dir "$root/relay-run" > "$root/relay.log" 2>&1 &
    relay_pid=$!
    wait_relay_ready "$root/relay.log"
    "$interpreter" -c 'import json,sys; data=json.loads(next(line for line in open(sys.argv[1]) if line.startswith("{"))); native=data["disk_native_submitted"]; fallback=data["disk_fallback_submitted"]; peak=data["disk_maximum_outstanding"]; assert native+fallback > 0 and peak == 1; print(f"Relay disk originals: native={native}, fallback={fallback}, maximum_outstanding={peak}")' "$root/relay-status.json"
    if [[ "$isolated" == isolated ]]; then
        "$agent" --config "$root/agent-empty.json" --initialize-managed > "$root/agent-empty.log" 2>&1 &
        empty_agent_pid=$!
        for ((attempt=0;attempt<200;attempt++)); do
            if "$agent" --status --config "$root/agent-empty.json" 2>/dev/null | grep -q "durable revision=$first_revision "; then break; fi
            sleep 0.02
        done
        "$agent" --status --config "$root/agent-empty.json" | grep -q "durable revision=$first_revision "
        [[ $("$reader" get ns:feature --system --config "$root/agent-empty.json") == 7 ]]
        denied=$(curl -sS --max-time 5 --cacert "$certificates/ca.pem" --resolve "localhost:$relay_port:127.0.0.1" \
            -o "$root/cross-bundle.json" -w '%{http_code}' -H "Authorization: Bearer $relay_token" \
            --data "{\"version\":1,$identity,\"scope\":\"$empty_scope\",\"namespaces\":[],\"coding\":\"raw\",\"profile\":1}" \
            "https://localhost:$relay_port/v2/distribution/session/open")
        [[ "$denied" == 403 ]]
    fi
fi
"$agent" --config "$root/agent.json" --initialize-managed > "$root/agent.log" 2>&1 &
agent_pid=$!
wait_value() {
    local actual=
    for ((attempt=0;attempt<200;attempt++)); do
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
    for ((attempt=0;attempt<200;attempt++)); do
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
if [[ -n "$relay" ]]; then check_format "$root/relay-generations" "$image_format"; fi
[[ $("$reader" get ns:feature --system --config "$root/agent.json" --generation-scope 6600000000000000000000000000000000000000000000000000000000000000) == 7 ]]
[[ ! -e "$root/run/snapshot.shm" ]]
cp "$root/generations/storage.meta" "$root/storage-before"
post /v2/transactions "{$identity,\"generation\":1,\"sequence\":2,\"namespace\":\"other\",\"operations\":[{\"key\":\"other:hidden\",\"value\":{\"type\":\"int\",\"default\":99},\"expected_revision\":0,\"absent_namespace_revision\":0}]}" > "$root/progress.json"
progress_revision=$("$interpreter" -c 'import json,sys; print(json.load(open(sys.argv[1]))["revision"])' "$root/progress.json")
for ((attempt=0;attempt<200;attempt++)); do
    if "$agent" --status --config "$root/agent.json" | grep -q "durable revision=$progress_revision "; then break; fi
    sleep 0.02
done
"$agent" --status --config "$root/agent.json" | grep -q "durable revision=$progress_revision "
[[ $("$reader" get other:hidden --system --config "$root/agent.json" --default absent) == absent ]]
post /v2/transactions "{$identity,\"generation\":1,\"sequence\":3,\"namespace\":\"ns\",\"operations\":[{\"key\":\"ns:feature\",\"value\":null,\"expected_revision\":$first_revision}]}" > "$root/deleted.json"
deleted_revision=$("$interpreter" -c 'import json,sys; print(json.load(open(sys.argv[1]))["revision"])' "$root/deleted.json")
for ((attempt=0;attempt<200;attempt++)); do
    if "$agent" --status --config "$root/agent.json" > "$root/deleted-status.txt" &&
        grep -Fq "durable revision=$deleted_revision knob_count=0" "$root/deleted-status.txt"; then break; fi
    sleep 0.02
done
grep -Fq "durable revision=$deleted_revision knob_count=0" "$root/deleted-status.txt"
wait_value 7
if [[ "$isolated" == isolated ]]; then
    for ((attempt=0;attempt<200;attempt++)); do
        if "$agent" --status --config "$root/agent-empty.json" | grep -q "durable revision=$deleted_revision "; then break; fi
        sleep 0.02
    done
    "$agent" --status --config "$root/agent-empty.json" | grep -q "durable revision=$deleted_revision "
    [[ $("$reader" get ns:feature --system --config "$root/agent-empty.json") == 7 ]]
    kill -TERM "$empty_agent_pid"; wait "$empty_agent_pid"; empty_agent_pid=
fi
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
cmp "$root/storage-before" "$root/generations/storage.meta"
check_format "$root/generations" "$image_format"
if [[ "$transition_to_flat" == true ]]; then
    post /v2/transactions "{$identity,\"generation\":1,\"sequence\":4,\"namespace\":\"other\",\"operations\":[{\"key\":\"other:hidden\",\"value\":{\"type\":\"int\",\"default\":100},\"expected_revision\":$progress_revision}]}" > "$root/flat-forward.json"
    flat_revision=$("$interpreter" -c 'import json,sys; print(json.load(open(sys.argv[1]))["revision"])' "$root/flat-forward.json")
    for ((attempt=0;attempt<200;attempt++)); do
        if "$agent" --status --config "$root/agent.json" | grep -q "durable revision=$flat_revision "; then break; fi
        sleep 0.02
    done
    "$agent" --status --config "$root/agent.json" | grep -q "durable revision=$flat_revision "
    check_format "$root/generations" flat-v1
    [[ $("$reader" get ns:feature --system --config "$root/agent.json") == 7 ]]
    image_format=flat-v1
fi
kill -TERM "$agent_pid"; wait "$agent_pid"; agent_pid=
if [[ -n "$relay" ]]; then
    cp "$root/relay-generations/storage.meta" "$root/relay-storage-before"
    kill -TERM "$relay_pid"; wait "$relay_pid"; relay_pid=
    "$interpreter" -c 'import json,sys; p=sys.argv[1]; data=json.load(open(p)); [bundle.update(mode="existing") for bundle in data["bundles"]]; open(p,"w").write(json.dumps(data))' "$root/relay.json"
    "$relay" --config "$root/relay.json" --runtime-dir "$root/relay-run" > "$root/relay-restart.log" 2>&1 &
    relay_pid=$!
    wait_relay_ready "$root/relay-restart.log"
    cmp "$root/relay-storage-before" "$root/relay-generations/storage.meta"
    "$agent" --config "$root/agent.json" > "$root/agent-relay-restart.log" 2>&1 &
    agent_pid=$!
    wait_value 7
    deleted_namespace=$("$interpreter" -c 'import json,sys; print(json.load(open(sys.argv[1]))["namespace_revision"])' "$root/deleted.json")
    post /v2/transactions "{$identity,\"generation\":1,\"sequence\":4,\"namespace\":\"ns\",\"operations\":[{\"key\":\"ns:feature\",\"value\":{\"type\":\"int\",\"default\":84},\"expected_revision\":0,\"absent_namespace_revision\":$deleted_namespace}]}" > "$root/relay-new-value.json"
    wait_value 84
    denied=$(curl -sS --max-time 5 --cacert "$certificates/ca.pem" --resolve "localhost:$relay_port:127.0.0.1" \
        -o "$root/relay-denied.json" -w '%{http_code}' -H 'Authorization: Bearer invalid-relay-token-00000000000000000000' \
        --data '{}' "https://localhost:$relay_port/v2/distribution/session/open")
    [[ "$denied" == 403 ]]
    cp "$root/relay-generations/selected" "$root/relay-selected-before"
    kill -TERM "$authority_pid"; wait "$authority_pid"; authority_pid=
    "$interpreter" -c 'import json,sys; p=sys.argv[1]; data=json.load(open(p)); data["credentials"][1]["namespaces"]=["other"]; open(p,"w").write(json.dumps(data))' "$root/authority.json"
    "$authority" --authority "$root/authority.json" --port "$port" --runtime-dir "$root/authority-run" > "$root/authority-revoked.log" 2>&1 &
    authority_pid=$!
    for ((attempt=0;attempt<300;attempt++)); do
        if ! kill -0 "$relay_pid" 2>/dev/null; then break; fi
        sleep 0.02
    done
    if kill -0 "$relay_pid" 2>/dev/null; then exit 1; fi
    if wait "$relay_pid"; then exit 1; fi
    relay_pid=
    cmp "$root/relay-selected-before" "$root/relay-generations/selected"
    [[ $("$reader" get ns:feature --system --config "$root/agent.json") == 84 ]]
    printf 'TLS authority to relay to agent to reader, progress, deletion defaults, relay restart and upstream revocation passed\n'
    exit 0
fi
cp "$root/generations/selected" "$root/selected-before"
cp "$root/managed.json" "$root/managed-good.json"
"$interpreter" -c 'import json,sys; p=sys.argv[1]; data=json.load(open(p)); data["token"]="invalid-reader-token-000000000000000000000"; open(p,"w").write(json.dumps(data))' "$root/managed.json"
"$agent" --config "$root/agent.json" > "$root/agent-unauthorized.log" 2>&1 &
agent_pid=$!
for ((attempt=0;attempt<200;attempt++)); do
    if grep -Eq 'managed delivery failures|distribution session reset' "$root/agent-unauthorized.log"; then break; fi
    sleep 0.02
done
grep -Eq 'managed delivery failures|distribution session reset' "$root/agent-unauthorized.log"
[[ $("$reader" get ns:feature --system --config "$root/agent.json") == 7 ]]
for ((attempt=0;attempt<200;attempt++)); do
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
for ((attempt=0;attempt<200;attempt++)); do
    if post /v2/open "{$identity,\"expected_generation\":0,\"nonce\":\"62000000000000000000000000000000\"}" > "$root/reset-open.json" 2>/dev/null; then break; fi
    sleep 0.05
done
grep -q '"generation":1' "$root/reset-open.json"
post /v2/transactions "{$identity,\"generation\":1,\"sequence\":1,\"namespace\":\"ns\",\"operations\":[{\"key\":\"ns:feature\",\"value\":{\"type\":\"int\",\"default\":84},\"expected_revision\":0,\"absent_namespace_revision\":0}]}" > "$root/reset-value.json"
"$agent" --config "$root/agent.json" > "$root/agent-reset.log" 2>&1 &
agent_pid=$!
wait_ready "$root/agent-reset.log"
wait_value 84
cmp "$root/storage-before" "$root/generations/storage.meta"
kill -TERM "$agent_pid"; wait "$agent_pid"; agent_pid=
[[ $("$reader" get ns:feature --system --config "$root/agent.json") == 84 ]]
"$agent" --config "$root/agent.json" > "$root/agent-reset-restart.log" 2>&1 &
agent_pid=$!
wait_ready "$root/agent-reset-restart.log"
wait_value 84
cmp "$root/storage-before" "$root/generations/storage.meta"
kill -TERM "$agent_pid"; wait "$agent_pid"; agent_pid=
# A terminal post-readiness recovery failure must reach the service manager.
"$agent" --config "$root/agent.json" > "$root/agent-fatal.log" 2>&1 &
agent_pid=$!
wait_ready "$root/agent-fatal.log"
mv "$root/generations" "$root/displaced-generations"
mkdir "$root/generations"
chmod 700 "$root/generations"
post /v2/transactions "{$identity,\"generation\":1,\"sequence\":2,\"namespace\":\"other\",\"operations\":[{\"key\":\"other:hidden\",\"value\":{\"type\":\"int\",\"default\":100},\"expected_revision\":0,\"absent_namespace_revision\":0}]}" > "$root/fatal-progress.json"
for ((attempt=0;attempt<300;attempt++)); do
    if ! kill -0 "$agent_pid" 2>/dev/null; then break; fi
    sleep 0.02
done
if kill -0 "$agent_pid" 2>/dev/null; then exit 1; fi
wait_agent_failure "$agent_pid" "$root/agent-fatal.log"
agent_pid=
printf 'Authenticated delivery, identity binding, rejected credentials, progress, deletion defaults, stopped reads, trusted-reset restart and fatal service exit passed\n'

