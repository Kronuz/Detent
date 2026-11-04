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
#include "verified_pages.hh"
#include <algorithm>
#include <fcntl.h>
#include <fstream>
#include <cstdlib>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <sys/stat.h>
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

// 99,990 knobs plus 10 more at revision 2 (the parser caps a file at 100,000); sanitizer builds shrink it.
const int knobs = std::getenv("DETENT_TRUSTED_RESTART_KNOBS") ? std::atoi(std::getenv("DETENT_TRUSTED_RESTART_KNOBS"))
																: 99990;
// The value of knob i at a revision, or nothing when the revision lacks it.
std::optional<std::int64_t> expected(int revision, int i) {
	if (revision == 100) { // after the delta of delta_changes
		const auto value = expected(3, i);
		if (!value || i % 10 != 3)
			return value;
		if (i % 500 == 3)
			return {};
		return i % 50 == 3 ? std::optional(*value + 1000) : value;
	}
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
// Decodes the record, lets `change` edit it, re-encodes it with a valid digest and writes it back.
template <class F> void rewrite_live(const fs::path &path, F &&change) {
	std::ifstream in(path, std::ios::binary);
	std::vector<char> bytes{std::istreambuf_iterator<char>(in), {}};
	in.close();
	auto record = x::decode_live_record(bytes, 1u << 20);
	change(record);
	std::vector<char> out(x::live_record_size(record.rows.size()));
	x::encode_live_record(record, out);
	std::ofstream(path, std::ios::binary | std::ios::trunc).write(out.data(), out.size());
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
// T5's damage: 100 single-byte flips at random offsets of random packs of the selected generation.
struct Damage {
	std::vector<fs::path> packs;
	std::map<fs::path, std::vector<char>> original;
	std::vector<std::pair<fs::path, std::size_t>> flips;
	std::set<fs::path> damaged;
	explicit Damage(const fs::path &store) : packs(live_packs(store)) {
		CHECK(packs.size() >= 10);
		for (const auto &pack : packs) {
			std::ifstream in(pack, std::ios::binary);
			original[pack] = {std::istreambuf_iterator<char>(in), {}};
		}
		std::mt19937 random(2026);
		for (int n = 0; n < 100; ++n) {
			const auto &pack = packs[random() % packs.size()];
			const auto at = random() % original[pack].size();
			flips.emplace_back(pack, at);
			damaged.insert(pack);
		}
	}
	void apply() const {
		for (const auto &[pack, at] : flips)
			flip(pack, at, static_cast<char>(1 << (at % 8)));
	}
	void restore() const {
		for (const auto &[pack, bytes] : original) {
			const int fd = ::open(pack.c_str(), O_RDWR | O_CLOEXEC);
			CHECK(fd >= 0 && ::pwrite(fd, bytes.data(), bytes.size(), 0) == ssize_t(bytes.size()));
			::close(fd);
		}
	}
};
// The packs a pass over the session's registry reports, read from the files in `store`.
std::set<fs::path> scrub_reports(const Session &session, const fs::path &store) {
	x::PackScrub scrub(1'000'000'000);
	const auto t0 = x::PackScrub::Clock::now();
	scrub.begin(session.registry().inputs(), t0, store);
	scrub.step(t0 + std::chrono::seconds(1));
	CHECK(scrub.passes() == 1);
	std::set<fs::path> reported;
	for (const auto &segment : scrub.failures())
		reported.insert(store / x::SegmentNames(segment).payload_name());
	return reported;
}
void t5(const fs::path &store, const fs::path &root, int revision) {
	const Damage damage_set(store);
	const auto &packs = damage_set.packs;
	const auto &damaged = damage_set.damaged;
	auto damage = [&] { damage_set.apply(); };
	auto repair = [&] { damage_set.restore(); };
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
		CHECK(scrub_reports(session, store) == damaged);
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
		CHECK(scrub_reports(session, store).empty());
	}
	{
		// The agent's own restart over the repaired store is ready and serves the same revision.
		detent::testing::GenerationWriter writer(store);
		CHECK(writer.revision() == std::uint64_t(after));
	}
	(void)root;
}
void delta_changes(std::map<std::string, std::int64_t> &upserts, std::vector<std::string> &removals);
// SR3 and SR4: the scrub hashes the files, so 40 bytes rewritten after the packs were mapped are reported, and
// the report withdraws the verified pages of those packs: later reads verify again and never return a wrong
// value, while the packs nobody damaged keep their verified pages.
void scrub_disk(const fs::path &store, int revision) {
	const auto packs = live_packs(store);
	Session session;
	session.recover(store);
	const auto clean = read_all(session, revision);
	CHECK(clean.ok == knobs + 10 && !clean.wrong && !clean.other && clean.failed.empty());
	std::mt19937 random(2027);
	std::set<fs::path> rewritten;
	std::map<fs::path, std::vector<char>> original;
	for (int n = 0; n < 40; ++n) {
		const auto &pack = packs[random() % packs.size()];
		if (!original.count(pack)) {
			std::ifstream in(pack, std::ios::binary);
			original[pack] = {std::istreambuf_iterator<char>(in), {}};
		}
		const int fd = ::open(pack.c_str(), O_RDWR | O_CLOEXEC);
		CHECK(fd >= 0);
		const auto at = random() % original[pack].size();
		const char byte = static_cast<char>(random() | 1);
		CHECK(::pwrite(fd, &byte, 1, at) == 1);
		::close(fd);
		rewritten.insert(pack);
	}
	// Until the scrub reports, verified pages stay verified: the reads hash nothing.
	const auto idle = l::paged::page_digests();
	(void)read_all(session, revision);
	CHECK(l::paged::page_digests() == idle);
	CHECK(scrub_reports(session, store) == rewritten);
	const auto withdrawn = l::paged::page_digests();
	const auto after = read_all(session, revision);
	CHECK(!after.wrong && !after.other);
	CHECK(l::paged::page_digests() > withdrawn);
	std::cout << "SR3/SR4: " << rewritten.size() << " packs rewritten, reported from the files; after the report "
			  << after.ok << " ok, " << after.source_failures << " source failures, " << after.wrong
			  << " wrong; " << (l::paged::page_digests() - withdrawn) << " page digests to verify again\n";
	// The packs nobody damaged keep serving without a hash.
	std::size_t untouched_bits = 0, cleared_bits = 0;
	const auto inputs = session.registry().inputs();
	for (const auto &input : inputs) {
		const auto path = store / x::SegmentNames(input.ownership.segment).payload_name();
		for (std::size_t at = 0; input.pages && at < input.bytes.size(); at += x::VerifiedPages::granule_bytes)
			(rewritten.count(path) ? cleared_bits : untouched_bits) += input.pages->test(at);
	}
	CHECK(untouched_bits > 0);
	for (const auto &[pack, bytes] : original) {
		const int fd = ::open(pack.c_str(), O_RDWR | O_CLOEXEC);
		CHECK(fd >= 0 && ::pwrite(fd, bytes.data(), bytes.size(), 0) == ssize_t(bytes.size()));
		::close(fd);
	}
}
// Namespace design D10.3: T5's damage heals with no operator action beyond the repair the agent requests. The
// scrub reports, a repair publishes at the head's own revision, every read is right afterwards, the damaged
// packs are collected and the scrub runs clean; a delta that crossed a damaged page no longer fails.
void heal(const fs::path &store, int revision) {
	const Damage damage(store);
	const auto old_generation = selected(store);
	const std::set<fs::path> old_packs(damage.packs.begin(), damage.packs.end());
	damage.apply();
	std::vector<std::string> scope;
	for (int ns = 0; ns < 10; ++ns)
		scope.push_back("n" + std::to_string(ns));
	std::map<std::string, std::int64_t> upserts;
	std::vector<std::string> removals;
	delta_changes(upserts, removals);
	auto pinned_session = std::make_unique<Session>();
	auto &pinned = *pinned_session;
	pinned.recover(store);
	const auto before = read_all(pinned, revision);
	CHECK(before.ok > 0 && before.source_failures > 0 && !before.wrong && !before.other);
	CHECK(scrub_reports(pinned, store) == damage.damaged);
	{
		detent::testing::GenerationWriter writer(store);
		// The delta crosses a damaged page and fails first touch, every time.
		for (int attempt = 0; attempt < 2; ++attempt)
			CHECK(throws([&] { writer.publish_delta(scope, "n3", upserts, removals); }));
		CHECK(selected(store) == old_generation);
		CHECK(writer.repair_json(json(revision)) == std::uint64_t(revision));
	}
	// A repair publishes a fresh generation at the same cursor; a snapshot shares no pack with its base.
	const auto repaired = selected(store);
	CHECK(repaired.generation != old_generation.generation && repaired.cursor == old_generation.cursor);
	const auto new_packs = live_packs(store);
	CHECK(new_packs.size() >= 10);
	for (const auto &pack : new_packs)
		CHECK(!old_packs.count(pack));
	{
		// Every read is right, and the scrub is clean.
		Session session;
		session.recover(store);
		const auto reads = read_all(session, revision);
		CHECK(reads.ok == knobs + 10 && !reads.wrong && !reads.other && reads.failed.empty());
		CHECK(scrub_reports(session, store).empty());
		// The reader that mapped the damaged generation keeps what it could read and never turns wrong.
		const auto during = read_all(pinned, revision);
		CHECK(!during.wrong && !during.other && during.failed == before.failed && during.ok == before.ok);
	}
	// The collector removes the damaged packs once nothing reaches them: a reader still holding the damaged
	// generation keeps it; when it lets go, the next start finishes the collection.
	{
		detent::testing::GenerationWriter writer(store);
		writer.collect();
		for (const auto &pack : old_packs)
			CHECK(fs::exists(pack));
	}
	pinned_session.reset();
	{
		detent::testing::GenerationWriter writer(store);
		writer.collect();
		for (const auto &pack : old_packs)
			CHECK(!fs::exists(pack));
		for (const auto &pack : new_packs)
			CHECK(fs::exists(pack));
		// The delta that failed now publishes, and the next revision is right.
		CHECK(writer.publish_delta(scope, "n3", upserts, removals) == std::uint64_t(revision + 1));
	}
	Session session;
	session.recover(store);
	const auto reads = read_all(session, 100);
	CHECK(reads.ok == knobs + 10 && !reads.wrong && !reads.other);
	std::cout << "heal: " << damage.damaged.size() << " damaged packs of " << old_packs.size() << ", "
			  << before.source_failures << " source failures before; after the repair "
			  << "0, " << new_packs.size() << " new packs, the damaged ones collected, the delta published\n";
}
// A repair of an undamaged head publishes too: the content is unchanged and a fresh generation is written.
void repair_unchanged(const fs::path &store, int revision) {
	const auto before = selected(store);
	const auto old_packs = live_packs(store);
	{
		detent::testing::GenerationWriter writer(store);
		CHECK(writer.repair_json(json(revision)) == std::uint64_t(revision));
	}
	const auto after = selected(store);
	CHECK(after.generation != before.generation && after.cursor == before.cursor);
	for (const auto &pack : live_packs(store))
		CHECK(!std::count(old_packs.begin(), old_packs.end(), pack));
	Session session;
	session.recover(store);
	const auto reads = read_all(session, revision);
	CHECK(reads.ok == knobs + 10 && !reads.wrong && !reads.other);
	CHECK(scrub_reports(session, store).empty());
	std::cout << "repair of an undamaged head: new generation, same cursor, " << reads.ok << " reads ok\n";
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
			[&] { rewrite_live(live_path(scratch), [](x::LiveRecord &record) { record.generation[0] ^= 1; }); }, true);
	// A digest-valid record whose derived state disagrees with the root or `.read` is refused (security review SR1).
	attempt("live pages inflated (sum differs from root.pages)",
			[&] { rewrite_live(live_path(scratch), [](x::LiveRecord &record) { record.rows[0].live.pages += 1000; }); },
			true);
	attempt("live bytes shifted (sum differs from root.bytes)",
			[&] {
				rewrite_live(live_path(scratch), [](x::LiveRecord &record) { --record.rows[0].live.bytes; });
			},
			true);
	attempt("histogram forged (records and a bucket)",
			[&] {
				rewrite_live(live_path(scratch), [](x::LiveRecord &record) {
					record.histogram[0] += 7;
					record.histogram[200] += 1;
				});
			},
			true);
	attempt("histogram moved to a bucket (override count differs from .read)",
			[&] {
				rewrite_live(live_path(scratch), [](x::LiveRecord &record) {
					--record.histogram[0];
					++record.histogram[3];
				});
			},
			true);
	// A FIFO where the record belongs is refused at once; a blocking open would hang the start.
	attempt("live record replaced by a FIFO",
			[&] {
				fs::remove(live_path(scratch));
				CHECK(::mkfifo(live_path(scratch).c_str(), 0600) == 0);
				::alarm(60);
			},
			true);
	::alarm(0);
}
// Security review SR2: a digest-valid `.live` whose rows are not the set the root reaches. Two shapes over a
// store of one value changed between two revisions, so the packs of both are on disk with equal lengths.
std::string small_json(int bump) {
	std::string out = R"({"schema_version":1,"knobs":{)";
	for (int i = 0; i < 30000; ++i)
		out += (i ? "," : "") + std::string("\"n") + std::to_string(i % 10) + ":k" + std::to_string(i) +
			   R"(":{"type":"int","default":)" + std::to_string(i * 3 + 1 + (i == 7 ? bump : 0)) + "}";
	return out + "}}";
}
void stale_rows(const fs::path &root) {
	const auto store = root / "stale";
	std::vector<char> first_live;
	{
		detent::testing::GenerationWriter writer(store);
		CHECK(writer.publish_json(small_json(0)) == 1);
		std::ifstream in(live_path(store), std::ios::binary);
		first_live.assign(std::istreambuf_iterator<char>(in), {});
		CHECK(writer.publish_json(small_json(1)) == 2);
	}
	const auto first = x::decode_live_record(first_live, 1u << 20);
	const auto scratch = root / "stale-copy";
	auto prepare = [&] {
		copy_store(store, scratch);
		for (const auto &row : first.rows)
			CHECK(fs::exists(scratch / x::SegmentNames(row.segment).payload_name()));
	};
	auto reads_of = [&](const char *what) {
		Session session;
		session.recover(scratch);
		std::uint64_t ok = 0, failed = 0;
		for (int i = 0; i < 30000; ++i) {
			const auto got = session.generation->view().get("n" + std::to_string(i % 10), "k" + std::to_string(i));
			if (got.has_value())
				++ok;
			else if (got.error().category == detent::ErrorCategory::SourceFailure)
				++failed;
			else
				CHECK(false);
		}
		std::cout << what << ": opened, " << ok << " ok, " << failed << " source failures\n";
		return failed;
	};
	// Shape 1: the second revision's header over the first revision's rows. The tree's segment is not a row.
	prepare();
	rewrite_live(live_path(scratch), [&](x::LiveRecord &record) { record.rows = first.rows; });
	{
		Session session;
		CHECK(throws([&] { session.recover(scratch); }));
		std::cout << "current header over the first revision's rows: refused\n";
	}
	// Shape 2: the second revision's rows with one non-tree row swapped for a first-revision row of equal length.
	prepare();
	std::ifstream image(scratch / std::string(x::GenerationName(selected(scratch).generation, x::GenerationFile::Image).view()),
						std::ios::binary);
	std::vector<char> root_bytes{std::istreambuf_iterator<char>(image), {}};
	const auto tree = l::paged::decode_root(root_bytes).tree.segment;
	bool swapped = false;
	rewrite_live(live_path(scratch), [&](x::LiveRecord &record) {
		for (auto &row : record.rows)
			if (row.segment != tree && !swapped)
				for (const auto &old : first.rows)
					if (old.length == row.length && !swapped) {
						row.segment = old.segment;
						row.checksum = old.checksum;
						swapped = true;
					}
		std::sort(record.rows.begin(), record.rows.end(), [](const x::LiveRow &a, const x::LiveRow &b) {
			return std::memcmp(a.segment.data(), b.segment.data(), 16) < 0;
		});
	});
	CHECK(swapped);
	CHECK(reads_of("rows with a swapped non-tree segment") > 0);
}
// The knobs after the delta of `delta_changes`: namespace n3 loses every 500th knob and bumps every 50th.
void delta_changes(std::map<std::string, std::int64_t> &upserts, std::vector<std::string> &removals) {
	for (int i = 3; i < knobs + 10; i += 10) {
		const auto value = expected(3, i);
		if (!value)
			continue;
		if (i % 500 == 3)
			removals.push_back("n3:k" + std::to_string(i));
		else if (i % 50 == 3)
			upserts["n3:k" + std::to_string(i)] = *value + 1000;
	}
}
void run_mode(const std::string &mode, const fs::path &root) {
	const auto store = root / "store";
	{
		detent::testing::GenerationWriter writer(store);
		for (int revision = 1; revision <= (mode == "refusals" ? 1 : 3); ++revision) {
			CHECK(writer.publish_json(json(revision)) == std::uint64_t(revision));
			if (mode == "oracle")
				oracle(store, root / "copy", revision);
		}
		if (mode == "oracle") {
			// The reused-row branch of the publication at scale: a delta and a compaction over the 100K store,
			// each compared with the walk of the generation it wrote (architect finding A4/C3).
			std::map<std::string, std::int64_t> upserts;
			std::vector<std::string> removals;
			delta_changes(upserts, removals);
			std::vector<std::string> scope;
			for (int ns = 0; ns < 10; ++ns)
				scope.push_back("n" + std::to_string(ns));
			CHECK(writer.publish_delta(scope, "n3", upserts, removals) == 4);
			oracle(store, root / "copy", 100);
			writer.compact(3);
			CHECK(writer.revision() == 4);
			oracle(store, root / "copy", 100);
		}
	}
	if (mode == "refusals") {
		refusals(store, root / "copy");
		stale_rows(root);
	}
	if (mode == "t5")
		t5(store, root, 3);
	if (mode == "scrub-disk")
		scrub_disk(store, 3);
	if (mode == "heal") {
		repair_unchanged(store, 3);
		heal(store, 3);
	}
}
} // namespace
int main(int argc, char **argv) {
	const auto root = fs::current_path() / ".scratch" / ("trusted-restart-" + std::to_string(::getpid()));
	fs::remove_all(root);
	struct Cleanup {
		fs::path path;
		~Cleanup() { fs::remove_all(path); }
	} cleanup{root};
	const std::string mode = argc > 1 ? argv[1] : "all";
	CHECK(mode == "all" || mode == "oracle" || mode == "refusals" || mode == "t5" || mode == "scrub-disk" ||
		  mode == "heal");
	for (const auto *each : {"oracle", "refusals", "t5", "scrub-disk", "heal"})
		if (mode == "all" || mode == each)
			run_mode(each, root / each);
	std::cout << "ok\n";
	return 0;
}
