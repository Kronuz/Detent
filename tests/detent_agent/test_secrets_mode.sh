#!/usr/bin/env bash
# detent-agent starts from detent-agent.json plus the one secrets file it names (managed_path), and
# refuses a secrets file that any group or other can access, or a link, before it opens anything.
# Usage: test_secrets_mode.sh DETENT_AGENT
set -uo pipefail
agent=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
mkdir -p .scratch
root=$(mktemp -d "$PWD/.scratch/secrets-mode.XXXXXX")
trap 'rm -rf "$root"' EXIT
printf '{"cluster":"x"}\n' > "$root/managed.json"
printf '{"managed_path":"managed.json","generation_dir":"generations","runtime_dir":"run"}\n' > "$root/detent-agent.json"
run() { "$agent" --config "$root/detent-agent.json" 2>&1; }
for mode in 640 604 660 666; do
    chmod "$mode" "$root/managed.json"
    out=$(run) && { echo "mode $mode: the agent started" >&2; exit 1; }
    grep -q 'no group or other access' <<< "$out" || { echo "mode $mode: refused for another reason: $out" >&2; exit 1; }
done
ln -s managed.json "$root/link.json"
printf '{"managed_path":"link.json","generation_dir":"generations","runtime_dir":"run"}\n' > "$root/link-agent.json"
chmod 600 "$root/managed.json"
out=$("$agent" --config "$root/link-agent.json" 2>&1) && { echo "link: the agent started" >&2; exit 1; }
grep -q 'no group or other access' <<< "$out" || { echo "link: refused for another reason: $out" >&2; exit 1; }
# Mode 0600 passes the mode check: the refusal now comes from the contents, not the mode.
out=$(run) && { echo "a malformed secrets file started the agent" >&2; exit 1; }
if grep -q 'no group or other access' <<< "$out"; then echo "mode 600 was refused as shared: $out" >&2; exit 1; fi
echo "agent secrets mode: group or other access and links refused, 0600 accepted by the mode check"
