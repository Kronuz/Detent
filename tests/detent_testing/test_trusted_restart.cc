// Trusted restart (namespace design D10, step S3): a cold start over a generation with a live record maps
// its packs without reading them, every read checks a page on first touch, and the scrub finds what no read
// touched. T5 runs over a 100K-knob store written by the agent's own publication.
#include "detent_distribution/retirement.hh"
#include "detent_local/generation.hh"
#include "detent_testing/generation_writer.hh"
#include "leased_paged_root.hh"
#include "live_record.hh"
#include "names.hh"
#include "pack_scrub.hh"
#include "recovery_context.hh"
#include "segment_names.hh"
#include "trusted_records.hh"
#include <algorithm>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <unistd.h>

namespace d = detent::distribution;
namespace l = detent::local;
namespace x = l::detail;
namespace fs = std::filesystem;


namespace {
void check(bool value, int line) {
	if (!value)
		throw std::runtime_error("trusted restart invariant at line " + std::to_string(line));
}
#define CHECK(value) check(value, __LINE__)

constexpr int knobs = 99990; // plus 10 more at revision 2: the parser caps a file at 100,000 knobs
// The value of knob i at a revision, or nothing when the revision lacks it.
std::optional<std::int64_t> expected(int revision, int i) {
	if (revision >= 3 && i % 3333 == 0)
		return {};
	if (i >= knobs + 10)
		return {};
	if (i >= knobs && revision < 2)
		return {};
	return std::int64_t(i) * 3 + 1 + (revision >= 2 && i % 2000 == 0 ? 1 : 0) + (revision >= 4 && i % 7 == 0 ? 5 : 0);
}
std::string name_space(int i) { return "n" + std::to_string(i % 10); }
std::string key(int i) { return "k" + std::to_string(i); }
std::string json(int revision) {
	std::string out = R"({"schema_version":1,"knobs":{)";
	bool first = true;
	for (int i = 0; i < knobs + 10; ++i)
		if (const auto value = expected(revision, i)) {
			out += (first ? "" : ",") + std::string("\"") + name_space(i) + ":" + key(i) +
				   R"(":{"type":"int","default":)" + std::to_string(*value) + "}";
			first = false;
		}
	return out + "}}";
}
struct Session {
	std::shared_ptr<d::StorageLedger> heap = std::make_shared<d::StorageLedger>(1ull << 30);
	std::shared_ptr<d::StorageLedger> mappings = std::make_shared<d::StorageLedger>(1ull << 32);
	d::RetirementRuntime runtime{heap, 1};
	d::RetirementDomain domain = runtime.domain(heap, 16384);
	std::shared_ptr<x::RecoveryMetrics> metrics = std::make_shared<x::RecoveryMetrics>();
	std::optional<l::RecoveredGeneration> generation;
	std::uint64_t digests_before = 0;
	void recover(const fs::path &directory, bool trusted_start = true) {
		l::ViewLimits limits;
		limits.retirement_domain = &domain;
		limits.mapping_account = mappings;
		limits.bytes = 1ull << 30;
		x::RecoveryContext context{domain.factory(), heap, mappings, limits, {}, {}};
		context.metrics = metrics;
		context.trusted_start = trusted_start;
		digests_before = l::paged::page_digests();
		generation.emplace(x::RecoveryEngine::recover(directory, context));
	}
	std::uint64_t digests() const { return l::paged::page_digests() - digests_before; }
	const x::SegmentRegistry &registry() const { return *x::RecoveredTesting::root(*generation).registry(); }
	~Session() {
		generation.reset();
		domain.close().wait();
	}
};
template <class F> bool throws(F &&f) {
	try {
		f();
	} catch (const std::exception &) {
		return true;
	}
	return false;
}
l::Descriptor selected(const fs::path &store) { return l::read_generation_selection(store).descriptor; }
fs::path live_path(const fs::path &store) {
	return store / std::string(x::GenerationName(selected(store).generation, x::GenerationFile::Live).view());
}
fs::path read_path(const fs::path &store) {
	return store /
		   std::string(x::GenerationName(selected(store).generation, x::GenerationFile::ReaderMetadata).view());
}
void flip(const fs::path &path, std::size_t at, char mask = 1) {
	const int fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
	CHECK(fd >= 0);
	char byte = 0;
	CHECK(::pread(fd, &byte, 1, at) == 1);
	byte = static_cast<char>(byte ^ mask);
	CHECK(::pwrite(fd, &byte, 1, at) == 1);
	CHECK(::close(fd) == 0);
}
struct Reads {
	std::uint64_t ok = 0, source_failures = 0, wrong = 0, other = 0;
	std::set<int> failed;
};
Reads read_all(const Session &session, int revision) {
	Reads reads;
	const auto &view = session.generation->view();
	for (int i = 0; i < knobs + 10; ++i) {
		const auto want = expected(revision, i);
		const auto got = view.get(name_space(i), key(i));
		if (got.has_value()) {
			if (want && got.value() == *want)
				++reads.ok;
			else
				++reads.wrong;
		} else if (got.error().category == detent::ErrorCategory::SourceFailure) {
			++reads.source_failures;
			reads.failed.insert(i);
		} else if (!want && got.error().category == detent::ErrorCategory::MissingKnob)
			++reads.ok;
		else
			++reads.other;
	}
	return reads;
}
void copy_store(const fs::path &from, const fs::path &to) {
	fs::remove_all(to);
	fs::copy(from, to, fs::copy_options::recursive);
}
// A trusted recovery equals the full recovery of the same generation, record for record.
void oracle(const fs::path &store, const fs::path &scratch, int revision) {
	Session trusted;
	trusted.recover(store);
	CHECK(trusted.metrics->trusted && trusted.metrics->segment_hash_bytes == 0 &&
		  trusted.metrics->root_hash_bytes == l::paged::root_bytes && trusted.digests() == 0);
	copy_store(store, scratch);
	fs::remove(live_path(scratch));
	Session full;
	full.recover(scratch, false);
	CHECK(!full.metrics->trusted && full.metrics->segment_hash_bytes > 0 && full.digests() > 0);
	const auto &a = trusted.registry(), &b = full.registry();
	CHECK(a.inputs().size() == b.inputs().size() && a.physical_bytes() == b.physical_bytes());
	for (std::size_t i = 0; i < a.inputs().size(); ++i) {
		CHECK(a.inputs()[i].ownership == b.inputs()[i].ownership);
		CHECK(a.reachability(i).pages == b.reachability(i).pages && a.reachability(i).bytes == b.reachability(i).bytes);
		CHECK(a.inputs()[i].pages && !b.inputs()[i].pages);
	}
	CHECK(std::equal(a.override_histogram().begin(), a.override_histogram().end(), b.override_histogram().begin()));
	const auto &ca = x::RecoveredTesting::root(*trusted.generation), &cb = x::RecoveredTesting::root(*full.generation);
	CHECK(ca.counts().bools == cb.counts().bools && ca.counts().ints == cb.counts().ints &&
		  ca.counts().strings == cb.counts().strings && ca.counts().overrides == cb.counts().overrides &&
		  ca.counts().maximum_overrides == cb.counts().maximum_overrides);
	CHECK(trusted.generation->view().size() == full.generation->view().size());
	// Every page is checked once on first touch, and not again.
	const auto before = l::paged::page_digests();
	auto first = read_all(trusted, revision);
	CHECK(first.ok == knobs + 10 && !first.wrong && !first.other && first.failed.empty());
	const auto touched = l::paged::page_digests() - before;
	CHECK(touched > 0);
	const auto again = l::paged::page_digests();
	auto second = read_all(trusted, revision);
	CHECK(second.ok == knobs + 10 && l::paged::page_digests() == again);
	std::cout << "revision " << revision << ": " << a.inputs().size() << " packs, " << touched
			  << " page digests at first touch, 0 at startup\n";
	fs::remove_all(scratch);
}
std::vector<fs::path> live_packs(const fs::path &store) {
	std::ifstream in(live_path(store), std::ios::binary);
	std::vector<char> bytes{std::istreambuf_iterator<char>(in), {}};
	std::vector<fs::path> packs;
	for (const auto &row : x::decode_live_record(bytes, 1u << 20).rows)
		packs.push_back(store / x::SegmentNames(row.segment).payload_name());
	return packs;
}
void t5(const fs::path &store, const fs::path &root, int revision) {
	const auto packs = live_packs(store);
	CHECK(packs.size() >= 10);
	std::map<fs::path, std::vector<char>> original;
	for (const auto &pack : packs) {
		std::ifstream in(pack, std::ios::binary);
		original[pack] = {std::istreambuf_iterator<char>(in), {}};
	}
	std::mt19937 random(2026);
	std::vector<std::pair<fs::path, std::size_t>> flips;
	std::set<fs::path> damaged;
	for (int n = 0; n < 100; ++n) {
		const auto &pack = packs[random() % packs.size()];
		const auto at = random() % original[pack].size();
		flips.emplace_back(pack, at);
		damaged.insert(pack);
	}
	auto damage = [&] {
		for (const auto &[pack, at] : flips)
			flip(pack, at, static_cast<char>(1 << (at % 8)));
	};
	auto repair = [&] {
		for (const auto &[pack, bytes] : original) {
			const int fd = ::open(pack.c_str(), O_RDWR | O_CLOEXEC);
			CHECK(fd >= 0 && ::pwrite(fd, bytes.data(), bytes.size(), 0) == ssize_t(bytes.size()));
			::close(fd);
		}
	};
	damage();
	{
		// Restart is ready: nothing was read or hashed.
		Session session;
		session.recover(store);
		CHECK(session.metrics->trusted && session.metrics->segment_hash_bytes == 0 && session.digests() == 0);
		const auto reads = read_all(session, revision);
		std::cout << "T5: " << reads.ok << " reads ok, " << reads.source_failures << " source failures, "
				  << reads.wrong << " wrong values\n";
		CHECK(reads.wrong == 0 && reads.other == 0 && reads.source_failures > 0 && reads.ok > 0);
		// A failed read fails again; it was never cached as verified.
		CHECK(read_all(session, revision).failed == reads.failed);
		// The scrub reports every damaged pack, touched or not, within one pass.
		x::PackScrub scrub(1'000'000'000);
		const auto t0 = x::PackScrub::Clock::now();
		scrub.begin(session.registry().inputs(), t0);
		scrub.step(t0 + std::chrono::seconds(1));
		CHECK(scrub.passes() == 1);
		std::set<fs::path> reported;
		for (const auto &segment : scrub.failures())
			reported.insert(store / x::SegmentNames(segment).payload_name());
		CHECK(reported == damaged);
	}
	{
		// The update path refuses to build on a damaged base, with the same source failure.
		detent::testing::GenerationWriter writer(store);
		bool refused = false;
		try {
			writer.publish_json(json(revision + 1));
		} catch (const std::exception &error) {
			refused = true;
			std::cout << "update over a damaged base: " << error.what() << "\n";
		}
		CHECK(refused);
	}
	// The refused update may have reached a durable generation that shares pages it never read, so the
	// selected revision is the old one or the new; either way no damaged page is served.
	const int after = int(selected(store).cursor.revision);
	CHECK(after == revision || after == revision + 1);
	{
		Session session;
		session.recover(store);
		const auto reads = read_all(session, after);
		std::cout << "after the refused update (revision " << after << "): " << reads.ok << " ok, "
				  << reads.source_failures << " source failures, " << reads.wrong << " wrong, " << reads.other
				  << " other\n";
		// A republication builds from the incoming snapshot, so the new revision carries no damaged page.
		CHECK(!reads.wrong && !reads.other && (after != revision || reads.source_failures > 0));
	}
	repair();
	{
		Session session;
		session.recover(store);
		const auto reads = read_all(session, after);
		CHECK(reads.ok == knobs + 10 && !reads.wrong && !reads.other && reads.failed.empty());
		x::PackScrub scrub(1'000'000'000);
		const auto t0 = x::PackScrub::Clock::now();
		scrub.begin(session.registry().inputs(), t0);
		scrub.step(t0 + std::chrono::seconds(1));
		CHECK(scrub.passes() == 1 && scrub.failures().empty());
	}
	{
		// The agent's own restart over the repaired store is ready and serves the same revision.
		detent::testing::GenerationWriter writer(store);
		CHECK(writer.revision() == std::uint64_t(after));
	}
	(void)root;
}
// A damaged table or record, or a missing one, refuses the store.
void refusals(const fs::path &store, const fs::path &scratch) {
	auto attempt = [&](const char *what, auto &&mutate, bool refused) {
		copy_store(store, scratch);
		mutate();
		Session session;
		const bool threw = throws([&] { session.recover(scratch); });
		std::cout << what << (threw ? ": refused\n" : ": opened\n");
		CHECK(threw == refused);
		if (!threw)
			CHECK(!session.metrics->trusted);
	};
	const auto live_size = fs::file_size(live_path(store));
	attempt("flip in selected", [&] { flip(scratch / "selected", 100); }, true);
	attempt("flip in .read", [&] { flip(read_path(scratch), 90); }, true);
	attempt("flip in .read digest", [&] { flip(read_path(scratch), 170); }, true);
	attempt("flip in .live histogram", [&] { flip(live_path(scratch), 200); }, true);
	attempt("flip in .live row", [&] { flip(live_path(scratch), x::live_header_bytes + 30); }, true);
	attempt("flip in .live digest", [&] { flip(live_path(scratch), live_size - 1); }, true);
	attempt("truncated .live", [&] { fs::resize_file(live_path(scratch), live_size - 72); }, true);
	attempt("missing .live", [&] { fs::remove(live_path(scratch)); }, true);
	attempt("missing .read", [&] { fs::remove(read_path(scratch)); }, true);
	// The agent's store refuses a generation without a live record, and a fresh publication repairs it.
	copy_store(store, scratch);
	fs::remove(live_path(scratch));
	CHECK(throws([&] { detent::testing::GenerationWriter refused(scratch); }));
	fs::remove_all(scratch);
	{
		detent::testing::GenerationWriter repaired(scratch);
		CHECK(repaired.publish_json(json(1)) == 1);
	}
	{
		Session session;
		session.recover(scratch);
		CHECK(session.metrics->trusted);
		const auto reads = read_all(session, 1);
		CHECK(reads.ok == knobs + 10 && !reads.wrong && !reads.other && reads.failed.empty());
	}
	// A live record of another generation is refused even though its own digest holds.
	attempt("live record swapped from an older generation",
			[&] {
				std::ifstream in(live_path(scratch), std::ios::binary);
				std::vector<char> bytes{std::istreambuf_iterator<char>(in), {}};
				auto record = x::decode_live_record(bytes, 1u << 20);
				record.generation[0] ^= 1;
				std::vector<char> out(bytes.size());
				x::encode_live_record(record, out);
				std::ofstream(live_path(scratch), std::ios::binary | std::ios::trunc).write(out.data(), out.size());
			},
			true);
}
} // namespace
int main() {
	const auto root = fs::current_path() / ".scratch" / ("trusted-restart-" + std::to_string(::getpid()));
	fs::remove_all(root);
	struct Cleanup {
		fs::path path;
		~Cleanup() { fs::remove_all(path); }
	} cleanup{root};
	const auto store = root / "store";
	{
		detent::testing::GenerationWriter writer(store);
		for (int revision = 1; revision <= 3; ++revision) {
			CHECK(writer.publish_json(json(revision)) == std::uint64_t(revision));
			oracle(store, root / "copy", revision);
		}
	}
	refusals(store, root / "copy");
	t5(store, root, 3);
	std::cout << "ok\n";
	return 0;
}
