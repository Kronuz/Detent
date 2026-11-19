#!/usr/bin/env bash
# usage, from the checkout root: a1.sh build|run [BASELINE_CHECKOUT]
# build (light, the job's --build): detentd and detent-relay (Release) here and, when given, at
# BASELINE_CHECKOUT. run (strict): counts each idle pair's wakeups, CPU and loop turns over 10 s,
# three rounds alternating head and baseline.
set -eu
mode=${1:?build or run}
shift
build() { # SOURCE BUILD
	cmake -S "$1" -B "$2" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
		-DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_OSX_SYSROOT= -DDETENT_BUILD_AUTHORITY=ON -DDETENT_BUILD_RELAY=ON \
		-DDETENT_BUILD_EXAMPLES=OFF -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_TESTS=OFF >/dev/null
	cmake --build "$2" -j4 --target detentd detent-relay >/dev/null
}
if [ "$mode" = build ]; then
	mkdir -p .scratch
	build . .scratch/a1-head
	/usr/bin/clang -O2 -o .scratch/a1-usage bench/idle-wakeups/usage.c
	if [ $# -ge 1 ]; then
		build "$1" .scratch/a1-base
	fi
	exit 0
fi
for round in 1 2 3; do
	/usr/local/bin/python3 bench/idle-wakeups/idle_loops.py --build .scratch/a1-head --usage .scratch/a1-usage \
		--label "head-$round"
	if [ $# -ge 1 ]; then
		/usr/local/bin/python3 bench/idle-wakeups/idle_loops.py --build .scratch/a1-base --usage .scratch/a1-usage \
			--label "main-$round"
	fi
done
