#!/bin/bash
# usage: t7-timing.sh [LIMIT_SECONDS]     (run from the checkout root, in a light job)
# Times T7 (Release) at the head ("new") against the same test as of Detent:e32cdb8 ("old", whose seam
# and probe stores issue real fsync and F_FULLFSYNC), interleaved old new old new. Before each run it
# samples F_FULLFSYNC latency in the run directory; 120 s into each run, and at LIMIT (then kills it),
# it records `sample`'s top-of-stack summary. Passes iff every new run passes.
set -u
limit=${1:-1200}
old=e32cdb8fa654
build=.scratch/build-t7-timing
cut=tests/detent_local/test_publication_power_cut.cc
out=$PWD/.scratch/t7-timing
rm -rf "$out"; mkdir -p "$out"
trap 'git checkout -q -- tests' EXIT
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_OSX_SYSROOT= \
  -DDETENT_BUILD_AUTHORITY=OFF -DDETENT_BUILD_RELAY=OFF -DDETENT_BUILD_MANAGED_AGENT=ON \
  -DDETENT_BUILD_EXAMPLES=OFF -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_TESTS=ON >/dev/null || exit 125
bin=$build/tests/detent_local/test_local_publication_power_cut
cmake --build "$build" -j4 --target test_local_publication_power_cut >/dev/null || exit 125
cp "$bin" "$out/new"
git show "$old:$cut" >"$cut" || exit 125
cmake --build "$build" -j4 --target test_local_publication_power_cut >/dev/null || exit 125
cp "$bin" "$out/old"
git checkout -q -- tests
flush_latency() {
  /usr/bin/python3 -I - "$1" <<'PY'
import fcntl, os, statistics, sys, time
path = os.path.join(sys.argv[1], "flush.probe")
fd = os.open(path, os.O_RDWR | os.O_CREAT, 0o600)
ts = []
for _ in range(30):
    os.pwrite(fd, b"x" * 4096, 0)
    t = time.perf_counter()
    if hasattr(fcntl, "F_FULLFSYNC"):
        fcntl.fcntl(fd, fcntl.F_FULLFSYNC)
    else:
        os.fsync(fd)
    ts.append((time.perf_counter() - t) * 1000)
os.close(fd)
os.unlink(path)
ts.sort()
print("flush ms median %.1f p90 %.1f max %.1f" % (statistics.median(ts), ts[26], ts[-1]))
PY
}
stacks() { # pid label
  if command -v sample >/dev/null; then
    sample "$1" 3 -file "$out/$2.sample" >/dev/null 2>&1
    echo "--- $2: top of stack"
    awk '/Sort by top of stack/{p=1} p' "$out/$2.sample" | head -14
  fi
}
status=0
for round in 1 2; do
  for which in old new; do
    run=$out/run-$which-$round
    mkdir -p "$run"
    echo "=== $which r$round: $(flush_latency "$run"); load $(sysctl -n vm.loadavg 2>/dev/null || cut -d' ' -f1-3 /proc/loadavg)"
    start=$(date +%s)
    (cd "$run" && exec "$out/$which") >"$run/stdout" 2>"$run/stderr" &
    pid=$!
    sampled=0 killed=0
    while kill -0 "$pid" 2>/dev/null; do
      now=$(($(date +%s) - start))
      if [ "$sampled" -eq 0 ] && [ "$now" -ge 120 ]; then stacks "$pid" "$which-$round-120s"; sampled=1; fi
      if [ "$now" -ge "$limit" ]; then
        stacks "$pid" "$which-$round-limit"; kill -9 "$pid"; killed=1; break
      fi
      sleep 5
    done
    wait "$pid"; rc=$?
    secs=$(($(date +%s) - start))
    echo "$which r$round: rc=$rc killed=$killed seconds=$secs; $(tail -1 "$run/stdout")"
    grep '^progress: window' "$run/stderr"
    tail -1 "$run/stderr"
    echo "after: $(flush_latency "$run")"
    if [ "$which" = new ] && [ "$rc" -ne 0 ]; then status=1; cat "$run/stderr" | tail -5; fi
    rm -rf "$run"/.scratch
  done
done
exit $status
