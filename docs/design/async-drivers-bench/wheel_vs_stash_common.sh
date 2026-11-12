# Sourced by wheel_vs_stash.sh and stash_niche.sh: builds the harness through cmake against the
# fetched libraries and prints the host and pins. A job pins the libraries with --ref wheel=...,
# stash=... (checkouts at $JOB_REF_WHEEL, $JOB_REF_STASH); without them, the commits
# CMakeLists.txt declares. $BUILD_DIR holds the build directories and logs.
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
build_root=${BUILD_DIR:-$here/build-wheel-vs-stash}
mkdir -p "$build_root"

# NAME FLAGS TARGET...: configures $build_root/NAME with C++ flags FLAGS and builds the targets;
# prints the pins and the build's file line.
build() {
	local name=$1 flags=$2 dir args=()
	shift 2
	dir=$build_root/$name
	[ -z "${JOB_REF_WHEEL:-}" ] || args+=("-DFETCHCONTENT_SOURCE_DIR_WHEEL=$JOB_REF_WHEEL")
	[ -z "${JOB_REF_STASH:-}" ] || args+=("-DFETCHCONTENT_SOURCE_DIR_STASH=$JOB_REF_STASH")
	cmake -S "$here" -B "$dir" -DBENCH_WHEEL_VS_STASH=ON -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_CXX_FLAGS_RELEASE="$flags" -DFETCHCONTENT_BASE_DIR="$build_root/deps" "${args[@]}" >/dev/null
	cmake --build "$dir" -j4 --target "$@" >/dev/null
	echo "pin wheel=$(git -C "$build_root/deps/wheel-src" rev-parse HEAD) stash=$(git -C "$build_root/deps/stash-src" rev-parse HEAD)"
	echo "flags=\"$flags\""
}

# The cmake target's binary in a build directory, and the wheel's paired-summary module.
bin() { echo "$build_root/$1/$2"; }
summary_module() { echo "$build_root/deps/wheel-src/bench/wheel_bench_summary.py"; }

host() {
	echo "host=$(uname -sm) cpu=\"$(sysctl -n machdep.cpu.brand_string 2>/dev/null || sed -n 's/^model name\t*: //p' /proc/cpuinfo | head -1)\" hw_threads=$(getconf _NPROCESSORS_ONLN) compiler=\"$(c++ --version | head -1)\""
}

# A round's header, with what else the host is doing; the summaries read it.
round() {
	local limit
	limit=$(pmset -g therm 2>/dev/null | awk '/CPU_Speed_Limit/ {print $3}')
	echo "round=$1 load=\"$(sysctl -n vm.loadavg 2>/dev/null || cut -d' ' -f1-3 /proc/loadavg)\" speed_limit=${limit:-na}"
}

# PROGRAM MODE: the sanitizer builds of the tests, each in its own directory.
sanitized_tests() {
	local program=$1 mode=$2
	build test-asan "-O1 -g -UNDEBUG -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all" "$program"
	build test-tsan "-O1 -g -UNDEBUG -fsanitize=thread" "$program"
	echo "sanitize=address,undefined"
	"$(bin test-asan "$program")" "$mode"
	echo "sanitize=thread"
	# Linux 6.x maps above TSan's address layout under full ASLR; one process runs without it
	if [ "$(uname -s)" = Linux ]; then setarch "$(uname -m)" -R "$(bin test-tsan "$program")" "$mode"; else "$(bin test-tsan "$program")" "$mode"; fi
}
