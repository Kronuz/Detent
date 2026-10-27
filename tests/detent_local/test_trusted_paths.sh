#!/bin/sh
set -eu
publisher_uid=$(id -u)
mkdir -p "$2"
if [ "$publisher_uid" -eq 0 ]; then
    exec "$1" "$publisher_uid" "$2"
fi
if command -v sudo >/dev/null 2>&1 && sudo -n true 2>/dev/null; then
    exec sudo -n "$1" "$publisher_uid" "$2"
fi
echo "Root-owned path fixtures require noninteractive sudo; skipped."
exit 77
