// The paced pack scrub (namespace design D10.2): pace against a fake and a real clock, and detection of
// single-byte flips in the packs of a real store written by the agent's own publication.
#include "detent_local/ownership.hh"
#include "detent_testing/generation_writer.hh"
#include "pack_scrub.hh"
#include "segment_names.hh"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <set>
#include <sha256.h>
#include <thread>
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
std::vector<char> slurp(const std::filesystem::path &path) {
	std::ifstream in(path, std::ios::binary);
	return {std::istreambuf_iterator<char>(in), {}};
}
// Each fake step grants exactly rate * dt bytes, so cumulative hashing is exact.
void fake_clock() {
	auto packs = synthetic(5, 40'000, 1);
	std::uint64_t total = 0;
	for (const auto &pack : packs)
		total += pack.bytes.size();
	const auto list = inputs(packs);
	constexpr std::uint64_t rate = 30'000;
	x::PackScrub scrub(rate, 4096);
	const auto t0 = Clock::now();
	scrub.begin(list, t0);
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
	scrub.begin(list, t0);
	std::uint64_t again = 0;
	for (int step = 1; scrub.running(); ++step)
		again += scrub.step(t0 + std::chrono::milliseconds(step * 500 + (step % 3) * 7));
	CHECK(again == total && scrub.passes() == 2);
	// A stalled caller is repaid at most one second of allowance.
	scrub.begin(list, t0);
	CHECK(scrub.step(t0 + 1h) == rate);
}
void failures_in_order() {
	auto packs = synthetic(6, 9000, 2);
	packs[1].bytes[17] ^= 1;
	packs[4].bytes.back() ^= 0x80;
	packs[5].ownership.checksum[0] ^= 1;
	const auto list = inputs(packs);
	x::PackScrub scrub(1'000'000'000, 1000);
	const auto t0 = Clock::now();
	scrub.begin(list, t0);
	scrub.step(t0 + 1s);
	CHECK(!scrub.running() && scrub.passes() == 1);
	CHECK(scrub.failures().size() == 3 && scrub.failures()[0] == packs[1].ownership.segment &&
		  scrub.failures()[1] == packs[4].ownership.segment && scrub.failures()[2] == packs[5].ownership.segment);
	scrub.begin({}, t0);
	CHECK(!scrub.running() && scrub.passes() == 2);
}
// The wall-clock pace stays within 10% of the configured rate over a whole pass.
void real_clock() {
	auto packs = synthetic(4, 1'500'000, 3);
	std::uint64_t total = 0;
	for (const auto &pack : packs)
		total += pack.bytes.size();
	const auto list = inputs(packs);
	const std::uint64_t rate = 2'400'000;
	x::PackScrub scrub(rate);
	const auto start = Clock::now();
	scrub.begin(list, start);
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
		scrub.begin(list, t0);
		scrub.step(t0 + 1s);
		CHECK(scrub.passes() == 1 && scrub.failures().empty());
	}
	// 100 single-byte flips at random offsets: every damaged pack is reported once, no other pack is.
	std::mt19937 random(7);
	std::set<l::StorageIdentity> damaged;
	for (int flip = 0; flip != 100; ++flip) {
		auto &pack = packs[random() % packs.size()];
		pack.bytes[random() % pack.bytes.size()] ^= static_cast<char>(1 << (random() % 8));
		damaged.insert(pack.ownership.segment);
	}
	const auto list = inputs(packs);
	x::PackScrub scrub(1'000'000'000);
	const auto t0 = Clock::now();
	scrub.begin(list, t0);
	scrub.step(t0 + 1s);
	CHECK(scrub.passes() == 1);
	const std::set<l::StorageIdentity> found(scrub.failures().begin(), scrub.failures().end());
	CHECK(found == damaged && scrub.failures().size() == damaged.size());
}
} // namespace
int main() {
	const auto root = std::filesystem::current_path() / ".scratch" / ("pack-scrub-" + std::to_string(::getpid()));
	std::filesystem::remove_all(root);
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() { std::filesystem::remove_all(path); }
	} cleanup{root};
	fake_clock();
	failures_in_order();
	real_clock();
	real_store(root);
	std::cout << "ok\n";
	return 0;
}
