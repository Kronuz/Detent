#!/bin/sh
# Logs every fsync, fdatasync, F_FULLFSYNC and F_BARRIERFSYNC of one scenario through each PosixIO
# sync entry point (macOS), at Detent's old completion pin with default options and at the new pin
# with FlushMode::full, and diffs them.   run.sh OLD NEW OUT
set -u
HERE=$(cd "$(dirname "$0")" && pwd); OLD=$1; NEW=$2; O=$3
mkdir -p "$O"
src() { [ -d "$O/src-$1" ] || { mkdir -p "$O/src-$1" && git -C "$O/src-$1" init -q && git -C "$O/src-$1" fetch -q --depth 1 https://github.com/Kronuz/completion.git "$1" && git -C "$O/src-$1" checkout -q FETCH_HEAD; } || exit 1; echo "$O/src-$1"; }
b() { cmake -S "$HERE" -B "$O/$1" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCOMPLETION_SOURCE="$2" $3 >"$O/$1.cfg.log" 2>&1 && cmake --build "$O/$1" -j2 >"$O/$1.build.log" 2>&1 || { tail -30 "$O/$1.build.log" "$O/$1.cfg.log"; exit 1; }; }
old=$(src "$OLD") && new=$(src "$NEW") || exit 1
b new "$new" ""
b new-common "$new" "-DNO_SYNC_DATA=ON"
b old-default "$old" "-DOLD_API=ON -DOLD_DEFAULT=ON -DNO_SYNC_DATA=ON"
b old-ffo "$old" "-DOLD_API=ON -DNO_SYNC_DATA=ON"
L=$O/new/libinterpose.dylib
run() { DYLD_INSERT_LIBRARIES=$L "$O/$1/oracle_scenario" "$O/state" $2 2>"$O/$3.log" >/dev/null || { echo "$3 failed"; cat "$O/$3.log"; exit 1; }; }
run new full full-all; run new-common full full; run old-default "" old-default; run old-ffo "" old-ffo
grep -q '^SYS ' "$O/full.log" || { echo "the interposer logged nothing"; exit 1; }
for v in full-all old-default old-ffo full; do echo "$v: $(grep -c '^SYS ' "$O/$v.log") calls over $(grep -c '^MARK ' "$O/$v.log") steps"; done
echo "== old pin, Detent's default options, against new full (the steps both have)"
diff -u "$O/old-default.log" "$O/full.log"
echo "== old pin with full_flush_ordering against new full"
diff -u "$O/old-ffo.log" "$O/full.log"
# The differences each have a completion commit as cause: the expected diff is checked in.
diff -u "$O/old-default.log" "$O/full.log" | grep '^[-+]SYS' > "$O/delta.txt"
diff -u "$HERE/expected-delta.txt" "$O/delta.txt" && echo "ok   every difference is the expected one"
