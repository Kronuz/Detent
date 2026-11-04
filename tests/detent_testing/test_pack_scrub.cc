// The paced pack scrub (namespace design D10.2): pace against a fake and a real clock, detection of
// single-byte flips in the packs of a real store written by the agent's own publication, reading the file
// and not a mapping (SR3), and withdrawing the verified pages of a damaged pack (SR4).
#include "detent_local/ownership.hh"
#include "detent_testing/generation_writer.hh"
#include "pack_scrub.hh"
#include "segment_names.hh"
#include "verified_pages.hh"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <set>
#include <sha256.h>
#include <thread>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace l = detent::local;
namespace x = l::detail;
using Clock = x::PackScrub::Clock;
using namespace std::chrono_literals;
namespace {
void check(bool value, int line) {
	if (!value)
		throw std::runtime_error("pack scrub invariant at line " + std::to_string(line));
}
#define CHECK(value) check(value, __LINE__)

struct Pack {
	std::vector<char> bytes;
	l::SegmentOwnership ownership;
};
std::array<char, 32> sha(const std::vector<char> &bytes) {
	SHA256 hash;
	hash.add(bytes.data(), bytes.size());
	std::array<char, 32> out{};
	hash.getHash(reinterpret_cast<unsigned char *>(out.data()));
	return out;
}
std::vector<x::SegmentInput> inputs(const std::vector<Pack> &packs) {
	std::vector<x::SegmentInput> out;
	for (const auto &pack : packs)
		out.push_back({pack.ownership, {pack.bytes.data(), pack.bytes.size()}});
	return out;
}
std::vector<Pack> synthetic(std::size_t count, std::size_t size, unsigned seed) {
	std::mt19937 random(seed);
	std::vector<Pack> packs(count);
	for (std::size_t i = 0; i != count; ++i) {
		auto &pack = packs[i];
		pack.bytes.resize(size + i * 7);
		for (auto &byte : pack.bytes)
			byte = static_cast<char>(random());
		pack.ownership.segment[0] = static_cast<char>(i + 1);
		pack.ownership.length = pack.bytes.size();
		pack.ownership.checksum = sha(pack.bytes);
	}
	return packs;
}
// Writes each pack under its segment name, as the store does, so the scrub reads files.
void materialize(const std::vector<Pack> &packs, const std::filesystem::path &directory) {
	std::filesystem::remove_all(directory);
	std::filesystem::create_directories(directory);
	for (const auto &pack : packs) {
		std::ofstream out(directory / x::SegmentNames(pack.ownership.segment).payload_name(), std::ios::binary);
		out.write(pack.bytes.data(), static_cast<std::streamsize>(pack.bytes.size()));
	}
}
std::filesystem::path pack_path(const std::filesystem::path &directory, const Pack &pack) {
	return directory / x::SegmentNames(pack.ownership.segment).payload_name();
}
void flip_in_file(const std::filesystem::path &directory, const Pack &pack, std::size_t at, char mask) {
	const auto path = pack_path(directory, pack);
	const int fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
	CHECK(fd >= 0);
	char byte = 0;
	CHECK(::pread(fd, &byte, 1, static_cast<off_t>(at)) == 1);
	byte = static_cast<char>(byte ^ mask);
	CHECK(::pwrite(fd, &byte, 1, static_cast<off_t>(at)) == 1);
	CHECK(::close(fd) == 0);
}
std::vector<char> slurp(const std::filesystem::path &path) {
	std::ifstream in(path, std::ios::binary);
	return {std::istreambuf_iterator<char>(in), {}};
}
// Each fake step grants exactly rate * dt bytes, so cumulative hashing is exact.
void fake_clock(const std::filesystem::path &dir) {
	auto packs = synthetic(5, 40'000, 1);
	materialize(packs, dir);
	std::uint64_t total = 0;
	for (const auto &pack : packs)
		total += pack.bytes.size();
	const auto list = inputs(packs);
	constexpr std::uint64_t rate = 30'000;
	x::PackScrub scrub(rate, 4096);
	const auto t0 = Clock::now();
	scrub.begin(list, t0, dir);
	CHECK(scrub.running() && scrub.step(t0) == 0);
	std::uint64_t seen = 0;
	for (int second = 1; scrub.running(); ++second) {
		seen += scrub.step(t0 + std::chrono::seconds(second));
		CHECK(seen == std::min<std::uint64_t>(total, rate * second));
		CHECK(scrub.bytes_hashed() == seen);
	}
	CHECK(seen == total && scrub.passes() == 1 && scrub.failures().empty());
	CHECK(scrub.step(t0 + 100s) == 0);
	// Half-second steps and an uneven cadence hash the same total.
	scrub.begin(list, t0, dir);
	std::uint64_t again = 0;
	for (int step = 1; scrub.running(); ++step)
		again += scrub.step(t0 + std::chrono::milliseconds(step * 500 + (step % 3) * 7));
	CHECK(again == total && scrub.passes() == 2);
	// A stalled caller is repaid at most one second of allowance.
	scrub.begin(list, t0, dir);
	CHECK(scrub.step(t0 + 1h) == rate);
}
void failures_in_order(const std::filesystem::path &dir) {
	auto packs = synthetic(6, 9000, 2);
	packs[1].bytes[17] ^= 1;
	packs[4].bytes.back() ^= 0x80;
	packs[5].ownership.checksum[0] ^= 1;
	materialize(packs, dir);
	const auto list = inputs(packs);
	x::PackScrub scrub(1'000'000'000, 1000);
	const auto t0 = Clock::now();
	scrub.begin(list, t0, dir);
	scrub.step(t0 + 1s);
	CHECK(!scrub.running() && scrub.passes() == 1);
	CHECK(scrub.failures().size() == 3 && scrub.failures()[0] == packs[1].ownership.segment &&
		  scrub.failures()[1] == packs[4].ownership.segment && scrub.failures()[2] == packs[5].ownership.segment);
	scrub.begin({}, t0, dir);
	CHECK(!scrub.running() && scrub.passes() == 2);
}
// Damage is reported once per pass, never accumulated, and a new pack set starts clean (architect probe P1).
void failures_per_pass(const std::filesystem::path &dir) {
	auto packs = synthetic(2, 4096, 5);
	packs[1].bytes[100] ^= 1;
	materialize(packs, dir);
	const auto both = inputs(packs);
	const auto good_only = std::vector<x::SegmentInput>{both[0]};
	x::PackScrub scrub(1'000'000'000);
	auto at = Clock::now();
	for (std::uint64_t pass = 1; pass <= 3; ++pass) {
		scrub.begin(both, at, dir);
		CHECK(scrub.failures().empty());
		at += 1s;
		scrub.step(at);
		CHECK(scrub.passes() == pass && scrub.failures().size() == 1 &&
			  scrub.failures()[0] == packs[1].ownership.segment);
	}
	scrub.begin(good_only, at, dir);
	CHECK(scrub.failures().empty());
	at += 1s;
	scrub.step(at);
	CHECK(scrub.passes() == 4 && scrub.failures().empty());
	// A pass cut short by a new begin reports nothing from the abandoned pass.
	x::PackScrub slow(100'000);
	slow.begin(both, at, dir);
	slow.step(at + 1s);
	CHECK(slow.passes() == 1 && slow.failures().size() == 1);
	slow.begin(both, at, dir);
	slow.step(at + 10ms);
	CHECK(slow.running() && slow.failures().empty());
}
// The wall-clock pace stays within 10% of the configured rate over a whole pass.
void real_clock(const std::filesystem::path &dir) {
	auto packs = synthetic(4, 1'500'000, 3);
	materialize(packs, dir);
	std::uint64_t total = 0;
	for (const auto &pack : packs)
		total += pack.bytes.size();
	const auto list = inputs(packs);
	const std::uint64_t rate = 2'400'000;
	x::PackScrub scrub(rate);
	const auto start = Clock::now();
	scrub.begin(list, start, dir);
	while (scrub.running()) {
		scrub.step(Clock::now());
		std::this_thread::sleep_for(2ms);
	}
	const std::chrono::duration<double> elapsed = Clock::now() - start;
	const double measured = double(total) / elapsed.count();
	std::cout << "real clock: " << total << " bytes in " << elapsed.count() << " s = " << measured
			  << " B/s for " << rate << "\n";
	CHECK(measured > 0.9 * double(rate) && measured < 1.1 * double(rate));
	CHECK(scrub.failures().empty());
}
std::vector<Pack> read_store(const std::filesystem::path &store) {
	std::vector<Pack> packs;
	for (const auto &entry : std::filesystem::directory_iterator(store)) {
		const auto name = entry.path().filename().string();
		if (!name.starts_with("segment-") || !name.ends_with(".owner"))
			continue;
		const auto record = slurp(entry.path());
		Pack pack;
		pack.ownership = l::decode_segment_ownership(record);
		const x::SegmentNames names(pack.ownership.segment);
		pack.bytes = slurp(store / names.payload_name());
		CHECK(pack.bytes.size() == pack.ownership.length);
		packs.push_back(std::move(pack));
	}
	std::sort(packs.begin(), packs.end(), [](const Pack &a, const Pack &b) {
		return a.ownership.segment < b.ownership.segment;
	});
	return packs;
}
void real_store(const std::filesystem::path &root) {
	std::string json = R"({"schema_version":1,"knobs":{)";
	for (int i = 0; i != 20000; ++i)
		json += (i ? "," : "") + std::string("\"t:k") + std::to_string(i) + R"(":{"type":"int","default":)" +
				std::to_string(i * 7919) + "}";
	json += "}}";
	detent::testing::GenerationWriter writer(root / "store");
	CHECK(writer.publish_json(json) == 1);
	auto packs = read_store(writer.store());
	CHECK(packs.size() >= 3);
	{
		const auto list = inputs(packs);
		x::PackScrub scrub(1'000'000'000);
		const auto t0 = Clock::now();
		scrub.begin(list, t0, writer.store());
		scrub.step(t0 + 1s);
		CHECK(scrub.passes() == 1 && scrub.failures().empty());
	}
	// 100 single-byte flips at random offsets: every damaged pack is reported once, no other pack is.
	std::mt19937 random(7);
	std::set<l::StorageIdentity> damaged;
	for (int flip = 0; flip != 100; ++flip) {
		const auto &pack = packs[random() % packs.size()];
		flip_in_file(writer.store(), pack, random() % pack.bytes.size(), static_cast<char>(1 << (random() % 8)));
		damaged.insert(pack.ownership.segment);
	}
	const auto list = inputs(packs);
	x::PackScrub scrub(1'000'000'000);
	const auto t0 = Clock::now();
	scrub.begin(list, t0, writer.store());
	scrub.step(t0 + 1s);
	CHECK(scrub.passes() == 1);
	const std::set<l::StorageIdentity> found(scrub.failures().begin(), scrub.failures().end());
	CHECK(found == damaged && scrub.failures().size() == damaged.size());
}
// A pack that is missing, shorter, longer, a directory or unreadable is damaged, not skipped; a store
// directory that cannot be opened reports every pack.
void unreadable_packs(const std::filesystem::path &dir) {
	auto packs = synthetic(6, 5000, 8);
	materialize(packs, dir);
	std::filesystem::remove(pack_path(dir, packs[1]));
	std::filesystem::resize_file(pack_path(dir, packs[2]), packs[2].bytes.size() - 1);
	std::filesystem::resize_file(pack_path(dir, packs[3]), packs[3].bytes.size() + 1);
	std::filesystem::remove(pack_path(dir, packs[4]));
	std::filesystem::create_directory(pack_path(dir, packs[4]));
	const auto list = inputs(packs);
	x::PackScrub scrub(1'000'000'000);
	const auto t0 = Clock::now();
	scrub.begin(list, t0, dir);
	scrub.step(t0 + 1s);
	CHECK(scrub.passes() == 1 && scrub.failures().size() == 4);
	for (std::size_t i = 0; i != 4; ++i)
		CHECK(scrub.failures()[i] == packs[i + 1].ownership.segment);
	scrub.begin(list, t0, dir / "missing");
	scrub.step(t0 + 1s);
	CHECK(scrub.passes() == 2 && scrub.failures().size() == packs.size());
}
// A pack file replaced by one with the same bytes is clean and keeps its verified pages; a pack that
// mismatches or is gone withdraws them (SR4).
void withdraws_verified_pages(const std::filesystem::path &dir) {
	auto packs = synthetic(3, 8000, 9);
	materialize(packs, dir);
	auto account = std::make_shared<detent::distribution::StorageLedger>(1 << 20);
	std::vector<x::VerifiedPages> bits;
	bits.reserve(packs.size());
	for (const auto &pack : packs) {
		bits.emplace_back(pack.bytes.size(), account);
		for (std::size_t at = 0; at < pack.bytes.size(); at += x::VerifiedPages::granule_bytes)
			bits.back().set(at);
	}
	auto set_bits = [&](std::size_t i) {
		std::size_t count = 0;
		for (std::size_t at = 0; at < packs[i].bytes.size(); at += x::VerifiedPages::granule_bytes)
			count += bits[i].test(at);
		return count;
	};
	const auto before = set_bits(0);
	CHECK(before > 0 && set_bits(1) > 0 && set_bits(2) > 0);
	auto list = inputs(packs);
	for (std::size_t i = 0; i != packs.size(); ++i)
		list[i].pages = &bits[i];
	std::filesystem::remove(pack_path(dir, packs[0]));
	{
		std::ofstream out(pack_path(dir, packs[0]), std::ios::binary);
		out.write(packs[0].bytes.data(), static_cast<std::streamsize>(packs[0].bytes.size()));
	}
	flip_in_file(dir, packs[1], 123, 4);
	std::filesystem::remove(pack_path(dir, packs[2]));
	x::PackScrub scrub(1'000'000'000);
	const auto t0 = Clock::now();
	scrub.begin(list, t0, dir);
	scrub.step(t0 + 1s);
	CHECK(scrub.failures().size() == 2 && scrub.failures()[0] == packs[1].ownership.segment &&
		  scrub.failures()[1] == packs[2].ownership.segment);
	CHECK(set_bits(0) == before && set_bits(1) == 0 && set_bits(2) == 0);
}
} // namespace
int main() {
	const auto root = std::filesystem::current_path() / ".scratch" / ("pack-scrub-" + std::to_string(::getpid()));
	std::filesystem::remove_all(root);
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() { std::filesystem::remove_all(path); }
	} cleanup{root};
	fake_clock(root / "fake");
	failures_in_order(root / "order");
	failures_per_pass(root / "per-pass");
	real_clock(root / "clock");
	unreadable_packs(root / "unreadable");
	withdraws_verified_pages(root / "bits");
	real_store(root);
	std::cout << "ok\n";
	return 0;
}
