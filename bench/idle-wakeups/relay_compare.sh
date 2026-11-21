#!/usr/bin/env bash
# usage, from the checkout root: relay_compare.sh build|run [BASELINE_CHECKOUT]
#   build (light, a job's --build): the relay test's programs (Release, tests on) here and at BASELINE_CHECKOUT.
#   run: relay_sample.py over ctest's RELAY_TEST (default test_relay_service), RELAY_ROUNDS (default 2)
#        rounds alternating head and baseline.
set -eu
mode=${1:?build or run}
shift
here=$(cd "$(dirname "$0")" && pwd)
test=${RELAY_TEST:-test_relay_service}
build() { # SOURCE BUILD
	cmake -S "$1" -B "$2" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
		-DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_OSX_SYSROOT= -DDETENT_BUILD_AUTHORITY=ON -DDETENT_BUILD_RELAY=ON \
		-DDETENT_BUILD_MANAGED_AGENT=OFF -DDETENT_BUILD_EXAMPLES=OFF -DDETENT_BUILD_BENCHMARKS=OFF \
		-DDETENT_BUILD_TESTS=ON >/dev/null
	cmake --build "$2" -j4 --target detent-authority detent-relay test_relay_probe >/dev/null
}
run() { # BUILD LABEL
	echo "== $2"
	/usr/local/bin/python3 "$here/relay_sample.py" "$1" -- \
		ctest --test-dir "$1" --output-on-failure -R "^($test|authority_tls_certificates)\$" --timeout 330 || true
}
case $mode in
build)
	mkdir -p .scratch
	build . .scratch/relay-head
	[ $# -ge 1 ] && build "$1" .scratch/relay-base
	exit 0 ;;
run)
	for round in $(seq 1 "${RELAY_ROUNDS:-2}"); do
		run "$PWD/.scratch/relay-head" "head-$round"
		[ $# -ge 1 ] && run "$PWD/.scratch/relay-base" "base-$round"
	done ;;
*)
	echo "unknown mode $mode" >&2
	exit 2 ;;
esac
