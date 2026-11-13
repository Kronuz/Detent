#!/usr/bin/env bash
# usage, from the checkout root, in a light job: history_mutant.sh MUTANT
# Applies one S5.2 mutant to the source, builds the test that must kill it and runs it.
# Exits 1 when the test fails (the mutant is killed), 0 when it passes or the mutant or
# its build does not apply, so a `--fails-without` job succeeds only on a real kill.
set -u
mutant=${1:?usage: history_mutant.sh MUTANT}
build=.scratch/history-mutant-build
case "$mutant" in
table-before-outputs) file=src/detent_authority/history.cc test=test_authority_history_power_cut
	old='		directory_.publish(step.written);
		if (step.table) {
			directory_.publish({{std::string(table_name), encode(*step.table)}});'
	new='		if (step.table)
			directory_.publish({{std::string(table_name), encode(*step.table)}});
		directory_.publish(step.written);
		if (step.table) {' ;;
collect-above-table) file=src/detent_authority/history.cc test=test_authority_history_power_cut
	old='		if (name.starts_with("seal-") && seal_index(name) > durable_.index) {'
	new='		if (false) {' ;;
trust-local-files) file=src/detent_authority/history.cc test=test_authority_history_power_cut
	old='	if (auto local = directory_.find(name); local && good(*local))'
	new='	if (auto local = directory_.find(name); local)' ;;
eager-eviction) file=src/detent_authority/change_history.cc test=test_authority_change_history
	old=' &&
			   p.live[p.head]->revision <= std::max(p.durable, p.dead))'
	new=')' ;;
checkpoint-between-seals) file=src/detent_authority/node.cc test=test_authority_history_cluster
	old='	if (!state_.positions_enabled())
		return &state_;'
	new='	return &state_;' ;;
cut-at-twice-the-cost) file=src/detent_authority/history_layout.cc test=test_authority_history_layout
	old='			if (Wide{younger} * p.alpha_den >= budget)'
	new='			if (Wide{younger} * p.alpha_den >= 2 * budget)' ;;
ring-by-group-revision) file=src/detent_authority/change_history.cc test=test_authority_history_cluster
	old='		if (!record || record->revision != entry_->position)'
	new='		if (!record || record->revision != entry_->revision)' ;;
*) echo "unknown mutant $mutant"; exit 0 ;;
esac
OLD="$old" NEW="$new" python3 - "$file" <<'EOF' || { echo "mutant does not apply"; exit 0; }
import os, sys
path = sys.argv[1]
text = open(path).read()
old, new = os.environ["OLD"], os.environ["NEW"]
if text.count(old) != 1:
    sys.exit(1)
open(path, "w").write(text.replace(old, new))
EOF
git diff --stat
trap 'git checkout -- "$file"' EXIT
cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang \
	-DCMAKE_OSX_SYSROOT= -DDETENT_BUILD_AUTHORITY=ON -DDETENT_BUILD_RELAY=ON -DDETENT_BUILD_MANAGED_AGENT=ON \
	-DDETENT_BUILD_EXAMPLES=OFF -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_TESTS=ON >/dev/null || { echo "configure failed"; exit 0; }
cmake --build "$build" -j4 --target "$test" >/dev/null || { echo "build failed"; exit 0; }
binary=$(find "$build" -name "$test" -type f -perm -u+x | head -n 1)
[ -n "$binary" ] || { echo "no $test"; exit 0; }
"$binary"
status=$?
if [ "$status" -eq 0 ]; then echo "mutant $mutant survived $test"; exit 0; fi
echo "mutant $mutant killed by $test (status $status)"
exit 1
