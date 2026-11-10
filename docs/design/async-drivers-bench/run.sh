#!/usr/bin/env bash
# Builds the async-drivers measurements against the library commits a job pins and runs them.
#   run.sh idle_cost [groups]       per-idle-group bytes, descriptors and threads
#   run.sh driver_cost [section]    frames, resumes, wheels, steps, idle CPU
#   run.sh channel_layout           where a follower's SnapshotChannel bytes go
#   run.sh wheel_test|waiter_test|blocking_entry|reader_test [args]   the protocol tests and probes
#   run.sh peers_cost [voters...]   today's per-group transport (OpenSSL 3 or later at $OPENSSL_ROOT)
#   run.sh conn_cost [connections]  one idle peer connection, today's and D10's (the same OpenSSL)
# SANITIZE=address,undefined or SANITIZE=thread builds with those sanitizers (own build dir).
# A campaign job pins the libraries with --ref consensus=..., journal=..., completion=...,
# task=... (checkouts at $JOB_REF_<NAME>); without them, the commits CMakeLists.txt declares.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
program=${1:?program}
shift
sanitize=${SANITIZE:-}
transport=
case $program in peers_cost | conn_cost) transport=-transport ;; esac
build=${BUILD_DIR:-$here/build${sanitize:+-$(printf '%s' "$sanitize" | tr ',' '-')}$transport}
args=()
openssl_root=${OPENSSL_ROOT:-/usr/local/opt/openssl@4}  # the one whose headers /usr/local/include holds
[ -d "$openssl_root" ] || openssl_root=/usr/local/opt/openssl@3
[ -n "$transport" ] && args+=("-DBENCH_TRANSPORT=ON" "-DOPENSSL_ROOT_DIR=$openssl_root")
if [ -n "$sanitize" ]; then
	args+=("-DCMAKE_CXX_FLAGS=-fsanitize=$sanitize -fno-omit-frame-pointer -fno-sanitize-recover=all")
	echo "sanitize=$sanitize"
fi
for name in consensus journal completion task; do
	upper=$(printf '%s' "$name" | tr '[:lower:]' '[:upper:]')
	var=JOB_REF_$upper
	if [ -n "${!var:-}" ]; then
		args+=("-DFETCHCONTENT_SOURCE_DIR_$upper=${!var}")
		echo "pin $name=$(git -C "${!var}" rev-parse HEAD 2>/dev/null || echo "${!var}")"
	fi
done
cmake -S "$here" -B "$build" -DCMAKE_BUILD_TYPE=Release ${args[@]+"${args[@]}"} >/dev/null
cmake --build "$build" -j4 --target "$program" >/dev/null
echo "host=$(uname -sm) compiler=$(c++ --version | head -1)"
file "$build/$program"
export BENCH_DIR="$build/data"
mkdir -p "$BENCH_DIR"
if [ -n "$transport" ]; then  # a CA and one mutual-TLS leaf per node, as consensus's fixture makes them
	certs="$build/certs"
	mkdir -p "$certs"
	ossl() { "$openssl_root/bin/openssl" "$@" >/dev/null 2>&1; }
	(cd "$certs"
	ossl req -x509 -newkey rsa:2048 -nodes -days 1 -subj /CN=Bench-CA -keyout ca.key -out ca.pem \
		-addext basicConstraints=critical,CA:TRUE -addext keyUsage=critical,keyCertSign,cRLSign -addext subjectKeyIdentifier=hash
	printf 'basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\nextendedKeyUsage=serverAuth,clientAuth\nsubjectAltName=DNS:localhost\nsubjectKeyIdentifier=hash\nauthorityKeyIdentifier=keyid,issuer\n' >leaf.ext
	for n in 1 2 3 4 5; do
		ossl req -new -newkey rsa:2048 -nodes -subj "/CN=n$n" -keyout "n$n.key" -out "n$n.csr"
		ossl x509 -req -in "n$n.csr" -CA ca.pem -CAkey ca.key -CAcreateserial -days 1 -extfile leaf.ext -out "n$n.pem"
	done)
	set -- "$certs" "$@"
fi
echo "start=$(date -u +%FT%TZ)"
"$build/$program" "$@"
echo "end=$(date -u +%FT%TZ)"
rm -rf "$BENCH_DIR"
