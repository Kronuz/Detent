#!/usr/bin/env bash
# T4(7) (namespace-measure): completion_sync_probe at completion:ab0f551 on APFS or in the local
# Linux VM, 1 KiB files. Rules: docs/perf/namespace-measure.md.
#
#   namespace-measure-sync.sh build                    configure and build the probe (light)
#   namespace-measure-sync.sh apfs SIZE:ROUNDS ...     run on this host's APFS (exclusive)
#   namespace-measure-sync.sh vm SIZE:ROUNDS ...       build and run in the VM, with flush counts
#
# Needs $JOB_REF_COMPLETION (a checkout of completion:ab0f551). The VM is reached by $VM_SSH_KEY,
# $VM_PORT and $VM_USER (defaults: strata's VM, port 18772).
set -euo pipefail
mode=$1
shift
here=$(cd "$(dirname "$0")" && pwd)
src=${JOB_REF_COMPLETION:?JOB_REF_COMPLETION names completion:ab0f551}
build=${SYNC_BUILD:-$PWD/.scratch/namespace-measure-sync/build}
echo "completion $(git -C "$src" rev-parse HEAD)"

case $mode in
build)
	mkdir -p "$(dirname "$build")"
	cmake -S "$src" -B "$build" -DCMAKE_BUILD_TYPE=Release -DCOMPLETION_BUILD_BENCH=ON \
		-DCOMPLETION_ASIO=OFF -DBUILD_TESTING=OFF >"$build.configure.log" 2>&1 || {
		cat "$build.configure.log"
		exit 1
	}
	cmake --build "$build" --target completion_sync_probe -j4
	file "$build/completion_sync_probe"
	;;
apfs)
	probe=$build/completion_sync_probe
	file "$probe"
	dir=$PWD/.scratch/namespace-measure-sync/data
	mkdir -p "$dir"
	sw_vers | tr '\n' ' '
	echo
	df -h "$dir" | tail -1
	mount | grep " on $(df "$dir" | tail -1 | awk '{print $NF}') " || true
	ulimit -n 10240
	for spec in "$@"; do
		echo "== ${spec%%:*} files of 1 KB, ${spec##*:} rounds"
		start=$(date +%s)
		"$probe" "$dir" "${spec%%:*}" "${spec##*:}" 1
		echo "elapsed $(($(date +%s) - start)) s"
	done
	;;
vm)
	key=${VM_SSH_KEY:-$HOME/code/Kronuz/strata/.scratch/linux-vm/ssh_key}
	known=$(dirname "$key")/known_hosts
	ssh_=(ssh -i "$key" -o UserKnownHostsFile="$known" -o BatchMode=yes -p "${VM_PORT:-18772}"
		"${VM_USER:-strata}@127.0.0.1")
	stage=$PWD/.scratch/namespace-measure-sync/vm-src
	rm -rf "$stage" && mkdir -p "$stage/inc"
	cp -R "$src/include/." "$stage/inc/"
	for d in "$build"/_deps/*-src/include; do cp -R "$d/." "$stage/inc/"; done
	cp "$src/bench/probe.h" "$src/bench/sync_probe.cc" "$here/namespace-measure-flush.cc" "$stage/"
	(cd "$stage" && shasum -a 256 sync_probe.cc probe.h namespace-measure-flush.cc)
	remote=namespace-measure-sync
	"${ssh_[@]}" "rm -rf $remote && mkdir -p $remote"
	tar -C "$stage" -cf - . | "${ssh_[@]}" "tar -C $remote -xf -"
	"${ssh_[@]}" "cd $remote && uname -r && g++ --version | head -1 &&
		g++ -std=c++20 -O2 -DNDEBUG -Iinc -I. sync_probe.cc -o sync_probe -pthread &&
		g++ -std=c++20 -O2 -DNDEBUG -Iinc -I. namespace-measure-flush.cc -o flush -pthread &&
		file sync_probe flush && findmnt -no FSTYPE,OPTIONS -T . && mkdir -p data"
	for spec in "$@"; do
		n=${spec%%:*} r=${spec##*:}
		echo "== $n files of 1 KB, $r rounds"
		"${ssh_[@]}" "cd $remote && ulimit -n 10240 && start=\$(date +%s) &&
			./sync_probe data $n $r 1 && ./flush data $n $r 1 && echo elapsed \$((\$(date +%s) - start)) s"
	done
	"${ssh_[@]}" "rm -rf $remote"
	;;
*)
	echo "unknown mode $mode" >&2
	exit 2
	;;
esac
