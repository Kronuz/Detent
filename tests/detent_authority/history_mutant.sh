#!/usr/bin/env bash
# usage, from the checkout root, in a light job: history_mutant.sh MUTANT
# Applies one history mutant (S5.2's seven, S5.3's six) to the source, builds the test that must kill it and runs it.
# Exits 0 when the test fails (the mutant is killed) and 1 when it passes or the mutant
# or its build does not apply, as `campaign job submit --fails-without` expects.
set -u
mutant=${1:?usage: history_mutant.sh MUTANT}
build=.scratch/history-mutant-build
case "$mutant" in
table-before-outputs) file=src/detent_authority/history.cc test=test_authority_history_power_cut
	old='		if (!step.written.empty())
			write.groups.push_back(std::move(step.written));
		kept(step);
		if (step.table) {'
	new='		if (step.table)
			write.groups.push_back({{std::string(table_name), encode(*step.table)}});
		if (!step.written.empty())
			write.groups.push_back(std::move(step.written));
		kept(step);
		if (step.table) {' ;;
collect-above-table) file=src/detent_authority/history.cc test=test_authority_history_power_cut
	old='	above_.push_back(seal_name(step.seal.index));
	for (const auto &o : step.seal.outputs)
		above_.push_back(o.name());'
	new='' ;;
trust-local-files) file=src/detent_authority/history.cc test=test_authority_history_power_cut
	old='	if (auto local = find(name); local && good(*local))'
	new='	if (auto local = find(name); local)' ;;
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
durable-at-seal) file=src/detent_authority/history.hh test=test_authority_history_power_cut
	old='return ready_ && durable_ >= index; }'
	new='return ready_ && layout_.table().index >= index; }' ;;
trust-rotten-table) file=src/detent_authority/history.cc test=test_authority_history_power_cut
	old='std::optional<Table> History::local_table() { return decoded(find(table_name), limits_); }'
	new='std::optional<Table> History::local_table() {
	auto bytes = find(table_name);
	return bytes ? std::optional(decode_table(*bytes, limits_)) : std::nullopt;
}' ;;
retained-bounds-only) file=src/detent_authority/node.cc test=test_authority_history_cluster
	old=',
			  std::tuple{"consensus_limits", o.consensus.log_entries, o.consensus.log_bytes}}'
	new='}' ;;
serve-any-name) file=src/detent_authority/history.cc test=test_authority_history_names
	old='	if (!history_name(name))
		return std::nullopt;
	auto file = io.open_reclaim_candidate_read_only(name);'
	new='	auto file = io.open_reclaim_candidate_read_only(name);' ;;
namespace-in-file-names) file=src/detent_authority/history_format.cc test=test_authority_history_names
	old='	return "block-" + namespace_token(n) + "-"'
	new='	return "block-" + std::string(n) + "-"' ;;
reader-serves-nothing) file=src/detent_authority/peer_history.cc test=test_authority_history_transport
	old='	return [io = std::move(io), bytes](std::string_view name) { return history::serve(*io, name, bytes); };'
	new='	return [](std::string_view) { return std::optional<std::string>(); };' ;;
*) echo "unknown mutant $mutant"; exit 1 ;;
esac
OLD="$old" NEW="$new" python3 - "$file" <<'EOF' || { echo "mutant does not apply"; exit 1; }
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
	-DDETENT_BUILD_EXAMPLES=OFF -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_TESTS=ON >/dev/null || { echo "configure failed"; exit 1; }
cmake --build "$build" -j4 --target "$test" >/dev/null || { echo "build failed"; exit 1; }
binary=$(find "$build" -name "$test" -type f -perm -u+x | head -n 1)
[ -n "$binary" ] || { echo "no $test"; exit 1; }
args=()
if [ "$test" = test_authority_history_transport ]; then
	# Its TLS fixture comes from the certificates test.
	ctest --test-dir "$build" -R '^authority_tls_certificates$' >/dev/null || { echo "no TLS fixture"; exit 1; }
	args=("$(dirname "$binary")/tls-fixture")
fi
"$binary" ${args[@]+"${args[@]}"}
status=$?
if [ "$status" -eq 0 ]; then echo "mutant $mutant survived $test"; exit 1; fi
echo "mutant $mutant killed by $test (status $status)"
exit 0
