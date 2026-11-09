#!/bin/bash
# First-read gate (A15, reader.md T2 through the pre-S6 Client); see docs/perf/first-read-gate.md.
#   first-read-gate.sh prepare OUT   light job at the Dense commit with --ref BASELINE=<main>:
#       builds both trees with the pinned LLVM 23.1.2, Release, DETENT_READ_COUNTERS=OFF, exports
#       the 5M corpus as 1- and 1,000-namespace stores with each tree's own builder and prepares them.
#   first-read-gate.sh measure PREPARED OUT   exclusive job: tests/detent_system/first_read_gate.py.
set -euo pipefail
llvm=/usr/local/opt/llvm/bin
sdk=/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk
corpus=${DENSE_CORPUS:?DENSE_CORPUS names the 5M corpus}
trials=/Users/kronuz/code/Kronuz/Detent/.scratch/capacity-trials/5000000-restore-r12

prepare() {
	local out=$1 dense=$PWD
	mkdir -p "$out"
	"$llvm/clang++" --version | tee "$out/toolchain.txt"
	grep -q 'clang version 23.1.2' "$out/toolchain.txt"
	shasum -a 256 "$corpus" | tee "$out/corpus.sha256"
	openssl req -x509 -newkey rsa:2048 -nodes -days 2 -subj /CN=first-read-gate-ca \
		-keyout "$out/ca.key" -out "$out/ca.pem" 2>/dev/null
	for code in baseline dense; do
		local src version
		if [ "$code" = baseline ]; then src=${JOB_REF_BASELINE:?--ref BASELINE}; version=2; else src=$dense; version=3; fi
		mkdir -p "$out/$code"
		git -C "$src" rev-parse HEAD > "$out/$code/commit.txt"
		cat >> "$src/CMakeLists.txt" <<EOF

add_executable(first_read_export "$dense/tests/detent_system/first_read_export.cc")
target_include_directories(first_read_export PRIVATE "\${PROJECT_SOURCE_DIR}/src/detent_local" "\${PROJECT_SOURCE_DIR}/tests/detent_local")
target_link_libraries(first_read_export PRIVATE detent::local_publication)
EOF
		cmake -S "$src" -B "$src/.scratch/gate-build" -DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_CXX_COMPILER="$llvm/clang++" -DCMAKE_C_COMPILER="$llvm/clang" -DCMAKE_OSX_SYSROOT="$sdk" \
			-DDETENT_BUILD_AUTHORITY=OFF -DDETENT_BUILD_RELAY=OFF -DDETENT_BUILD_EXAMPLES=OFF \
			-DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_MANAGED_AGENT=ON -DDETENT_BUILD_TESTS=ON \
			-DDETENT_READ_COUNTERS=OFF
		grep -q '^CMAKE_CXX_COMPILER:FILEPATH=/usr/local/opt/llvm/bin/clang' "$src/.scratch/gate-build/CMakeCache.txt"
		grep -q '^DETENT_READ_COUNTERS:BOOL=OFF$' "$src/.scratch/gate-build/CMakeCache.txt"
		cmake --build "$src/.scratch/gate-build" -j4 \
			--target detent_system_prepared_client_probe detent-agent first_read_export
		git -C "$src" checkout -- CMakeLists.txt
		cp "$src/.scratch/gate-build/tests/detent_system/detent_system_prepared_client_probe" "$out/$code/prepared_client_probe"
		cp "$src/.scratch/gate-build/src/detent-agent" "$out/$code/detent-agent"
		for namespaces in 1 1000; do
			local store="$out/$code/store-${namespaces}ns" records=5000000
			[ "$namespaces" = 1000 ] && records=5099900
			"$src/.scratch/gate-build/first_read_export" "$corpus" "$store" "$namespaces" "$version" | tee "$out/$code/export-${namespaces}ns.json"
			"$out/$code/prepared_client_probe" --prepare "$store" "$records" 4294967296 4294967296 4294967296 4294967296
		done
		rm -rf "$src/.scratch/gate-build"
	done
	file "$out"/*/prepared_client_probe "$out"/*/detent-agent
}

case ${1:-} in
prepare) prepare "${2:?OUT}" ;;
measure)
	/usr/local/bin/python3 -I tests/detent_system/first_read_gate.py --prepared "${2:?PREPARED}" \
		--recipe "$trials/expected-values.bin" --managed "$trials/agent-0/managed.json" --output "${3:?OUT}"
	;;
*) echo "usage: $0 prepare OUT | measure PREPARED OUT" >&2; exit 2 ;;
esac
