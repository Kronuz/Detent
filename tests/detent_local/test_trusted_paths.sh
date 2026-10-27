#!/bin/sh
set -eu
publisher_uid=${3:-$(id -u)}
mkdir -p "$2"
if [ "$(id -u)" -eq 0 ]; then
    if [ "$(uname -s)" = FreeBSD ]; then
        mountpoint="$2/freebsd-acl-$$"
        mkdir "$mountpoint"
        device=''
        mounted=false
        cleanup() {
            if "$mounted"; then umount "$mountpoint"; fi
            if [ -n "$device" ]; then mdconfig -d -u "$device"; fi
            rmdir "$mountpoint"
        }
        trap cleanup 0
        trap 'exit 1' HUP INT TERM
        device=$(mdconfig -a -t swap -s 32m)
        newfs -U "/dev/$device" >/dev/null
        tunefs -a enable "/dev/$device"
        mount "/dev/$device" "$mountpoint"
        mounted=true
        "$1" "$publisher_uid" "$mountpoint"
        exit
    fi
    exec "$1" "$publisher_uid" "$2"
fi
if command -v sudo >/dev/null 2>&1 && sudo -n true 2>/dev/null; then
    if [ "$(uname -s)" = FreeBSD ]; then
        exec sudo -n sh "$0" "$1" "$2" "$publisher_uid"
    fi
    exec sudo -n "$1" "$publisher_uid" "$2"
fi
echo "Root-owned path fixtures require noninteractive sudo; skipped."
exit 77
