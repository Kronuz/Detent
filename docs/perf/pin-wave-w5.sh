#!/bin/bash
# The pin wave's input classes (quest pin-wave, A5): the M1 identity drivers (tools/w5) built into
# the old tree and the new one, each against its own pins, run, and compared class by class.
#   pin-wave-w5.sh OUT     light, in the new tree's checkout with --ref OLD=<old commit>
# Exits 1 when the snapshot JSON (w5_detent) or the wire frames and persisted images (w5_store)
# differ; the other classes are reported, each difference to be traced to a library commit.
set -uo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
mkdir -p "${1:?OUT}" && out=$(cd "$1" && pwd)
for arm in old new; do
	if [ $arm = old ]; then src=${JOB_REF_OLD:?--ref OLD=<old commit>}; else src=$root; fi
	build=$src/.scratch/pin-wave-w5
	git -C "$src" rev-parse HEAD | tee "$out/commit-$arm.txt"
	cmake -S "$src" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
		-DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_OSX_SYSROOT= -DDETENT_BUILD_AUTHORITY=ON -DDETENT_BUILD_RELAY=ON \
		-DDETENT_BUILD_MANAGED_AGENT=ON -DDETENT_BUILD_EXAMPLES=OFF -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_TESTS=OFF \
		-DCMAKE_PROJECT_INCLUDE="$root/tools/w5/include.cmake" > "$out/configure-$arm.log" || { tail -30 "$out/configure-$arm.log"; exit 2; }
	cmake --build "$build" -j4 || exit 2
	ln -sfn "$build" "$out/build-$arm"
done
"$root/tools/w5/run_both.sh" "$out/build-old" "$out/build-new" "$out/run" || exit 2
"$root/tools/w5/identity_report.sh" "$out/run" "$out/report"
for f in "$out"/report/identity-*.txt; do echo "== $(basename "$f")"; head -40 "$f"; done
python3 -I "$root/tools/w5/compare.py" "$out/run/old/w5_detent.txt" "$out/run/new/w5_detent.txt" > /dev/null &&
	diff -r "$out/run/old/store" "$out/run/new/store" > /dev/null &&
	echo "ok   snapshot JSON, wire frames and persisted images are identical"
