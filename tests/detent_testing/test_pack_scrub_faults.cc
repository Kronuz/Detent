// The scrub's I/O failures (namespace S3b, K4): compiles the production pack_scrub.cc against wrappers of
// open, openat and pread that fail on cue. A failed directory open, pack open, read or short read is retried
// once: one failure then success leaves the pass clean, two report only that pack, and a directory that
// cannot be opened refuses the pass without reporting any pack. EMFILE, ENFILE and ENOMEM on a pack are not
// damage: the step throws and the next one resumes at that pack. Real RLIMIT_NOFILE exhaustion does the same.
#include "pack_scrub.hh"
#include "segment_names.hh"
#include "verified_pages.hh"
#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <random>
#include <sha256.h>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>

namespace {
enum class Kind { None, Directory, Open, Read, Short };
struct Fault {
	Kind kind = Kind::None;
	int remaining = 0, calls = 0, fd = -1;
	std::string target;
	int error = EIO;
} fault;
} // namespace

int scrub_fault_open(const char *path, int flags) {
	if (fault.kind == Kind::Directory) {
		++fault.calls;
		if (fault.remaining) {
			--fault.remaining;
			errno = EIO;
			return -1;
		}
	}
	return ::open(path, flags);
}
int scrub_fault_openat(int directory, const char *name, int flags) {
	const bool target = fault.target == name;
	if (target && fault.kind == Kind::Open) {
		++fault.calls;
		if (fault.remaining) {
			--fault.remaining;
			errno = fault.error;
			return -1;
		}
	}
	// Another pack can reuse the target's descriptor number once it is closed.
	const int fd = ::openat(directory, name, flags);
	fault.fd = target ? fd : -1;
	return fd;
}
ssize_t scrub_fault_pread(int fd, void *buffer, size_t count, off_t offset) {
	if (fd == fault.fd && offset == 0 && (fault.kind == Kind::Read || fault.kind == Kind::Short)) {
		++fault.calls;
		if (fault.remaining) {
			--fault.remaining;
			if (fault.kind == Kind::Read) {
				errno = fault.error;
				return -1;
			}
			return ::pread(fd, buffer, count - 1, offset);
		}
	}
	return ::pread(fd, buffer, count, offset);
}
#define open scrub_fault_open
#define openat scrub_fault_openat
#define pread scrub_fault_pread
#include "pack_scrub.cc"
#undef open
#undef openat
#undef pread

namespace l = detent::local;
namespace x = l::detail;
using Clock = x::PackScrub::Clock;
using namespace std::chrono_literals;
namespace {
void check(bool value, int line) {
	if (!value)
		throw std::runtime_error("pack scrub fault invariant at line " + std::to_string(line));
}
#define CHECK(value) check(value, __LINE__)

struct Pack {
	std::vector<char> bytes;
	l::SegmentOwnership ownership;
};
std::vector<Pack> synthetic(std::size_t count) {
	std::mt19937 random(2026);
	std::vector<Pack> packs(count);
	for (std::size_t i = 0; i != count; ++i) {
		auto &pack = packs[i];
		pack.bytes.resize(5000 + i * 7);
		for (auto &byte : pack.bytes)
			byte = static_cast<char>(random());
		pack.ownership.segment[0] = static_cast<char>(i + 1);
		pack.ownership.length = pack.bytes.size();
		SHA256 hash;
		hash.add(pack.bytes.data(), pack.bytes.size());
		hash.getHash(reinterpret_cast<unsigned char *>(pack.ownership.checksum.data()));
	}
	return packs;
}
std::vector<x::SegmentInput> materialize(const std::vector<Pack> &packs, const std::filesystem::path &directory) {
	std::filesystem::remove_all(directory);
	std::filesystem::create_directories(directory);
	std::vector<x::SegmentInput> inputs;
	for (const auto &pack : packs) {
		std::ofstream out(directory / x::SegmentNames(pack.ownership.segment).payload_name(), std::ios::binary);
		out.write(pack.bytes.data(), static_cast<std::streamsize>(pack.bytes.size()));
		inputs.push_back({pack.ownership, {pack.bytes.data(), pack.bytes.size()}});
	}
	return inputs;
}
// One case: `kind` fails once (transient) or on every call (persistent) for the second pack.
void fault_case(const std::filesystem::path &dir, Kind kind, bool persistent) {
	const auto packs = synthetic(3);
	const auto inputs = materialize(packs, dir);
	fault = {kind, persistent ? INT_MAX : 1, 0, -1,
			 std::string(x::SegmentNames(packs[1].ownership.segment).payload_name())};
	x::PackScrub scrub(1'000'000'000);
	const auto t0 = Clock::now();
	bool refused = false;
	try {
		scrub.begin(inputs, t0, dir);
		scrub.step(t0 + 1s);
	} catch (const std::system_error &) {
		refused = true;
	}
	const auto calls = fault.calls;
	fault = {};
	if (calls != 2)
		std::cerr << "fault kind " << int(kind) << (persistent ? " persistent" : " transient") << ": " << calls << " calls\n";
	CHECK(calls == 2);
	if (kind == Kind::Directory && persistent) {
		CHECK(refused && !scrub.running() && scrub.passes() == 0 && scrub.failures().empty());
		return;
	}
	CHECK(!refused && scrub.passes() == 1);
	if (persistent)
		CHECK(scrub.failures().size() == 1 && scrub.failures()[0] == packs[1].ownership.segment);
	else
		CHECK(scrub.failures().empty());
}
// Resource exhaustion on a pack is not damage: the step throws, the pass stays at that pack, reports nothing, and
// the next step finishes the pass clean.
void resource_case(const std::filesystem::path &dir, Kind kind, int error) {
	const auto packs = synthetic(3);
	const auto inputs = materialize(packs, dir);
	fault = {kind, 1, 0, -1, std::string(x::SegmentNames(packs[1].ownership.segment).payload_name()), error};
	x::PackScrub scrub(1'000'000'000);
	const auto t0 = Clock::now();
	scrub.begin(inputs, t0, dir);
	bool thrown = false;
	try {
		scrub.step(t0 + 1s);
	} catch (const std::system_error &failure) {
		thrown = failure.code().value() == error;
	}
	CHECK(thrown && scrub.running() && scrub.passes() == 0 && scrub.failures().empty());
	scrub.step(t0 + 2s);
	CHECK(fault.calls == 2);
	fault = {};
	CHECK(!scrub.running() && scrub.passes() == 1 && scrub.failures().empty());
}
struct Limit {
	rlimit saved{};
	Limit() {
		CHECK(::getrlimit(RLIMIT_NOFILE, &saved) == 0);
		rlimit none = saved;
		none.rlim_cur = 0;
		CHECK(::setrlimit(RLIMIT_NOFILE, &none) == 0);
	}
	~Limit() { ::setrlimit(RLIMIT_NOFILE, &saved); }
};
// Real descriptor exhaustion: a pack open is retried by the next step; a directory open refuses the pass.
void exhausted(const std::filesystem::path &dir) {
	const auto packs = synthetic(1);
	const auto inputs = materialize(packs, dir);
	x::PackScrub scrub(1'000'000'000);
	const auto t0 = Clock::now();
	scrub.begin(inputs, t0, dir);
	bool thrown = false;
	{
		Limit limit;
		try {
			scrub.step(t0 + 1s);
		} catch (const std::system_error &failure) {
			thrown = failure.code().value() == EMFILE;
		}
	}
	CHECK(thrown && scrub.running() && scrub.passes() == 0 && scrub.failures().empty());
	scrub.step(t0 + 2s);
	CHECK(scrub.passes() == 1 && scrub.failures().empty());
	bool refused = false;
	{
		Limit limit;
		try {
			scrub.begin(inputs, t0, dir);
		} catch (const std::system_error &) {
			refused = true;
		}
	}
	CHECK(refused && !scrub.running() && scrub.passes() == 1 && scrub.failures().empty());
}
} // namespace
int main() {
	const auto root =
		std::filesystem::current_path() / ".scratch" / ("pack-scrub-faults-" + std::to_string(::getpid()));
	std::filesystem::remove_all(root);
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() { std::filesystem::remove_all(path); }
	} cleanup{root};
	for (const auto kind : {Kind::Directory, Kind::Open, Kind::Read, Kind::Short})
		for (const bool persistent : {false, true})
			fault_case(root / "faults", kind, persistent);
	for (const auto kind : {Kind::Open, Kind::Read})
		for (const int error : {EMFILE, ENFILE, ENOMEM})
			resource_case(root / "resource", kind, error);
	exhausted(root / "exhausted");
	std::cout << "ok: 8 fault cases, 6 resource cases, descriptor exhaustion\n";
	return 0;
}
