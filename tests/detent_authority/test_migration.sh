#!/usr/bin/env bash
set -euo pipefail
binary=$1
interpreter=$2
mkdir -p .scratch
root=$(mktemp -d "$PWD/.scratch/authority-import.XXXXXX")
trap 'rm -rf "$root"' EXIT
cat > "$root/config.json" <<'JSON'
{"cluster":"63000000000000000000000000000000","group":"67000000000000000000000000000000","epoch":"65000000000000000000000000000000","directory":"data","credentials":[{"token":"offline-import-test-token-0000000000000000","producer":"publisher","namespaces":["ns"]}]}
JSON
chmod 600 "$root/config.json"
printf '{"schema_version":1,"version":999,"knobs":{' > "$root/legacy.json"
for ((i=0;i<300;i++)); do
    if ((i)); then printf ',' >> "$root/legacy.json"; fi
    printf '"ns:key%d":{"type":"int","default":%d}' "$i" "$i" >> "$root/legacy.json"
done
printf '}}\n' >> "$root/legacy.json"
printf 'invalid JSON' > "$root/invalid.json"
if "$binary" import "$root/config.json" "$root/invalid.json"; then exit 1; fi
[[ ! -e "$root/data" ]]
"$binary" import "$root/config.json" "$root/legacy.json"
if "$binary" import "$root/config.json" "$root/legacy.json"; then exit 1; fi
"$binary" export "$root/config.json" "$root/export.json" 1000
"$interpreter" -c 'import json,sys; source=json.load(open(sys.argv[1])); result=json.load(open(sys.argv[2])); assert source["knobs"] == result["knobs"]; assert result["version"] == 1000' "$root/legacy.json" "$root/export.json"
if "$binary" export "$root/config.json" "$root/export.json" 1001; then exit 1; fi
printf 'Bounded import, semantic verification, recovery and explicit export passed\n'
