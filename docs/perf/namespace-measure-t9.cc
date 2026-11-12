// T9 (namespace-measure): the agent's time to apply a block answer of x bytes at a prepared store
// against its time to build a full copy, on the dense corpus (DTCORP01, docs/design/dense-pages-model.py).
// Rules: docs/perf/namespace-measure.md. The answer is handed over as compressed fragments; the
// timed span runs from the first fragment byte to the store's Published, through the agent's own
// FragmentDecoder, Snapshot/DeltaDecoder, paged plan builders and DiskStore publication, the
// same calls as src/detent_delivery/coordinator.cc and src/detent_testing/generation_writer.cc.
//
//   namespace_measure_t9 full CORPUS STORE           a full copy into an empty STORE (left prepared)
//   namespace_measure_t9 delta CORPUS STORE F SEED   a block answer touching a fraction F of the
//                                                    definitions (5% deletes) at STORE, which holds
//                                                    the corpus (made by `full`)
// One JSON line per run on stdout.
#include "detent_distribution/framing.hh"
#include "detent_distribution/messagepack.hh"
#include "detent_distribution/protocol.hh"
#include "detent_distribution/wire_encoder.hh"
#include "detent_local/disk_store.hh"
#include "managed_recovery.hh"
#include "paged_delta_plan.hh"
#include "paged_limits.hh"
#include "paged_snapshot_plan.hh"
#include <completion/native_completion.h>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <sys/resource.h>
#include <sys/stat.h>

namespace d = detent::distribution;
namespace l = detent::local;
namespace m = d::messagepack;
using Clock = std::chrono::steady_clock;

namespace {
constexpr std::size_t segment = l::paged::segment_bytes;
constexpr std::uint8_t kind_definition = 2, kind_namespace = 3;

std::uint64_t be(const char *p, int n) {
	std::uint64_t v = 0;
	for (int i = 0; i < n; ++i)
		v = (v << 8) | static_cast<unsigned char>(p[i]);
	return v;
}

struct Corpus {
	std::string data;
	std::uint64_t count = 0;
	const char *key_offsets = nullptr, *keys = nullptr, *body_offsets = nullptr, *bodies = nullptr,
			   *kinds = nullptr;
	std::string_view key(std::uint64_t i) const {
		const auto a = be(key_offsets + 4 * i, 4), b = be(key_offsets + 4 * (i + 1), 4);
		return {keys + a, b - a};
	}
	std::string_view body(std::uint64_t i) const {
		const auto a = be(body_offsets + 4 * i, 4), b = be(body_offsets + 4 * (i + 1), 4);
		return {bodies + a, b - a};
	}
	std::uint8_t kind(std::uint64_t i) const { return static_cast<std::uint8_t>(kinds[i]); }
};

Corpus load(const std::filesystem::path &path) {
	Corpus c;
	std::ifstream in(path, std::ios::binary);
	std::ostringstream s;
	s << in.rdbuf();
	c.data = std::move(s).str();
	const char *p = c.data.data();
	if (c.data.size() < 32 || std::memcmp(p, "DTCORP01", 8) != 0)
		throw std::runtime_error("not a DTCORP01 corpus: " + path.string());
	c.count = be(p + 8, 8);
	const auto key_bytes = be(p + 16, 8), body_bytes = be(p + 24, 8);
	c.key_offsets = p + 32;
	c.keys = c.key_offsets + 4 * (c.count + 1);
	c.body_offsets = c.keys + key_bytes;
	c.bodies = c.body_offsets + 4 * (c.count + 1);
	c.kinds = c.bodies + body_bytes;
	if (std::size_t(c.kinds + c.count - p) != c.data.size())
		throw std::runtime_error("corpus length mismatch");
	return c;
}

// The corpus as distribution records: namespaces (W keys) and definitions (K<ns>\0<local>\0).
struct Def {
	std::uint32_t ns = 0;
	std::string name; // "ns:local"
	std::uint64_t revision = 0;
	std::string_view body;
};
struct Records {
	std::vector<std::string> scope;
	std::vector<std::uint64_t> revisions;
	std::vector<Def> defs;
	std::uint64_t cursor_revision = 0;
};

Records records(const Corpus &c) {
	Records r;
	for (std::uint64_t i = 0; i < c.count; ++i)
		if (c.kind(i) == kind_namespace) {
			const auto k = c.key(i);
			r.scope.emplace_back(k.substr(1, k.size() - 2));
			r.revisions.push_back(be(c.body(i).data(), 8));
			r.cursor_revision = std::max(r.cursor_revision, r.revisions.back());
		}
	r.defs.reserve(c.count);
	for (std::uint64_t i = 0; i < c.count; ++i) {
		if (c.kind(i) != kind_definition)
			continue;
		const auto k = c.key(i);
		const auto zero = k.find('\0', 1);
		const auto ns = k.substr(1, zero - 1);
		const auto at = std::lower_bound(r.scope.begin(), r.scope.end(), ns);
		if (at == r.scope.end() || *at != ns)
			throw std::runtime_error("definition outside every namespace");
		Def def;
		def.ns = std::uint32_t(at - r.scope.begin());
		def.name = std::string(ns) + ":" + std::string(k.substr(zero + 1, k.size() - zero - 2));
		def.body = c.body(i);
		def.revision = be(def.body.data(), 8);
		r.defs.push_back(std::move(def));
	}
	return r;
}

// A corpus body is the retired two-kind paged definition (DTCORP01 predates the one-knob type):
// revision u64, kind u8 (1 boolean, 2 integer), default bool u8, rollout u8 (255 none), pad, override
// count u32, default int i64, then rows of key length u16, kind u8 (0 forced, 1 percentage), value u8, key.
// The wire form is the one-knob definition: a bool is default 0 or 1 with rollout value 1.
// Appends it; `flip` changes the default (an update).
void definition(std::string &out, std::string_view body, bool flip, std::uint64_t &overrides,
				std::uint64_t &strings) {
	const auto *bytes = body.data();
	const auto kind = static_cast<unsigned char>(bytes[8]);
	if (kind == 2) {
		m::array(out, 4);
		m::signed_integer(out, std::int64_t(be(bytes + 16, 8)) + (flip ? 1 : 0));
		out.push_back('\xc0');
		out.push_back('\xc0');
		m::array(out, 0);
		return;
	}
	const auto rollout = static_cast<unsigned char>(bytes[10]);
	const auto count = be(bytes + 12, 4);
	bool percentage = rollout != 255;
	std::vector<std::pair<std::string_view, std::pair<bool, std::uint8_t>>> rows;
	std::size_t offset = 24;
	for (std::uint64_t i = 0; i < count; ++i) {
		const auto length = be(bytes + offset, 2);
		rows.push_back({{bytes + offset + 4, std::size_t(length)},
						{bytes[offset + 2] != 0, static_cast<std::uint8_t>(bytes[offset + 3])}});
		percentage |= bytes[offset + 2] != 0;
		offset += 4 + length;
	}
	m::array(out, 4);
	m::signed_integer(out, (bytes[9] != 0) != flip ? 1 : 0);
	if (rollout != 255)
		m::unsigned_integer(out, rollout);
	else
		out.push_back('\xc0');
	if (percentage)
		m::signed_integer(out, 1);
	else
		out.push_back('\xc0');
	m::array(out, count);
	for (const auto &[key, row] : rows) {
		const detent::GroupOverride value =
			row.first ? detent::GroupOverride{detent::GroupRollout{row.second}} : detent::GroupOverride{std::int64_t(row.second)};
		d::detail::wire::override_row(out, key, value);
		++overrides;
		strings += key.size();
	}
}

void put_cursor(std::string &out, const d::Cursor &value) {
	m::array(out, 5);
	for (const auto *field : {&value.cluster, &value.group, &value.restore_epoch})
		m::string(out, std::string_view(field->data(), field->size()), true);
	m::string(out, std::string_view(value.scope.data(), value.scope.size()), true);
	m::unsigned_integer(out, value.revision);
}

d::Cursor cursor_at(std::uint64_t revision) {
	d::Cursor c;
	c.cluster.fill('c');
	c.group.fill('g');
	c.restore_epoch.fill('e');
	c.scope.fill('s');
	c.revision = revision;
	return c;
}

// The full copy's body: generation_writer.cc's snapshot shape with the corpus's revisions.
std::string snapshot_wire(const Records &r, const d::Cursor &cursor) {
	std::string body;
	std::uint64_t overrides = 0, strings = 0;
	for (const auto &ns : r.scope)
		strings += ns.size();
	body.reserve(r.defs.size() * 64);
	m::array(body, r.defs.size());
	for (const auto &def : r.defs) {
		m::array(body, 4);
		m::unsigned_integer(body, def.ns);
		m::string(body, def.name);
		m::unsigned_integer(body, def.revision);
		strings += def.name.size();
		definition(body, def.body, false, overrides, strings);
	}
	std::string out;
	m::array(out, 7);
	m::unsigned_integer(out, 1);
	m::unsigned_integer(out, 1);
	m::array(out, 2);
	m::unsigned_integer(out, 1);
	m::unsigned_integer(out, 1);
	put_cursor(out, cursor);
	m::array(out, 4);
	m::unsigned_integer(out, r.scope.size());
	m::unsigned_integer(out, r.defs.size());
	m::unsigned_integer(out, overrides);
	m::unsigned_integer(out, strings);
	m::array(out, r.scope.size());
	for (std::size_t i = 0; i < r.scope.size(); ++i) {
		m::array(out, 2);
		m::string(out, r.scope[i]);
		m::unsigned_integer(out, r.revisions[i]);
	}
	out += body;
	return out;
}

struct Answer {
	std::string wire;
	std::uint64_t keys = 0, deletes = 0;
};

// A block answer as one transaction (R, R + 1]: a uniform sample of the definitions, each once,
// 5% of them deleted, the rest set to their flipped default.
Answer delta_wire(const Records &r, const d::Cursor &base, double fraction, std::uint64_t seed) {
	auto target = base;
	++target.revision;
	std::mt19937_64 random(seed);
	std::uniform_real_distribution<double> unit(0.0, 1.0);
	Answer a;
	std::string body;
	std::uint64_t overrides = 0, strings = 0;
	std::vector<bool> touched(r.scope.size());
	std::vector<std::pair<const Def *, bool>> chosen;
	chosen.reserve(std::size_t(double(r.defs.size()) * fraction) + 16);
	for (const auto &def : r.defs) {
		if (fraction < 1.0 && unit(random) >= fraction)
			continue;
		const bool remove = unit(random) < 0.05;
		chosen.emplace_back(&def, remove);
		touched[def.ns] = true;
	}
	m::array(body, chosen.size());
	for (const auto &[def, remove] : chosen) {
		m::array(body, 4);
		m::unsigned_integer(body, def->ns);
		m::string(body, def->name);
		strings += def->name.size();
		m::unsigned_integer(body, remove ? 1 : 0);
		if (remove) {
			body.push_back('\xc0');
			++a.deletes;
		} else
			definition(body, def->body, true, overrides, strings);
	}
	a.keys = chosen.size();
	std::uint64_t namespaces = 0;
	for (std::size_t i = 0; i < r.scope.size(); ++i)
		if (touched[i])
			++namespaces, strings += r.scope[i].size();
	auto &out = a.wire;
	m::array(out, 7);
	m::unsigned_integer(out, 1);
	m::unsigned_integer(out, 2);
	m::array(out, 2);
	m::unsigned_integer(out, 1);
	m::unsigned_integer(out, 1);
	put_cursor(out, base);
	put_cursor(out, target);
	m::array(out, 4);
	m::unsigned_integer(out, 1);
	m::unsigned_integer(out, chosen.size());
	m::unsigned_integer(out, overrides);
	m::unsigned_integer(out, strings);
	m::array(out, 1);
	m::array(out, 3);
	m::unsigned_integer(out, target.revision);
	m::array(out, namespaces);
	for (std::size_t i = 0; i < r.scope.size(); ++i)
		if (touched[i]) {
			m::array(out, 2);
			m::string(out, r.scope[i]);
			m::unsigned_integer(out, target.revision);
		}
	out += body;
	return a;
}

// Today's framing: 64 KiB fragments, zstd-compressed (encode_fragment with compression).
struct Framed {
	std::string bytes;
	std::uint64_t fragments = 0;
};
Framed frame(d::UpdateKind kind, const std::string &wire) {
	Framed f;
	d::Identity update{};
	update.fill('u');
	for (std::uint64_t offset = 0; offset < wire.size(); offset += d::fragment_bytes) {
		d::Fragment fragment;
		fragment.kind = kind;
		fragment.update = update;
		fragment.offset = offset;
		fragment.total = wire.size();
		fragment.final = offset + d::fragment_bytes >= wire.size();
		fragment.bytes = wire.substr(offset, d::fragment_bytes);
		f.bytes += d::encode_fragment(fragment, true);
		++f.fragments;
	}
	return f;
}

double cpu_seconds() {
	rusage u{};
	getrusage(RUSAGE_SELF, &u);
	return double(u.ru_utime.tv_sec + u.ru_stime.tv_sec) + double(u.ru_utime.tv_usec + u.ru_stime.tv_usec) / 1e6;
}
std::uint64_t peak_rss() {
	rusage u{};
	getrusage(RUSAGE_SELF, &u);
#ifdef __APPLE__
	return std::uint64_t(u.ru_maxrss);
#else
	return std::uint64_t(u.ru_maxrss) * 1024;
#endif
}
double seconds(Clock::duration v) { return std::chrono::duration<double>(v).count(); }

d::Limits wire_limits(std::shared_ptr<d::StorageLedger> heap) {
	d::Limits limits;
	limits.bytes = 4ull << 30;
	limits.string_bytes = 2ull << 30;
	limits.records = 8'000'000;
	limits.storage_bytes = 12ull << 30;
	limits.storage_account = std::move(heap);
	return limits;
}

// Hands the framed answer to the agent's fragment decoder and the update decoder `feed`.
template <class Feed> void unframe(const Framed &framed, d::UpdateKind kind, std::uint64_t total, Feed feed,
								   std::shared_ptr<d::StorageLedger> heap) {
	d::FragmentDecoder fragments(kind, total, true, heap);
	std::span<const char> input(framed.bytes.data(), framed.bytes.size());
	while (!input.empty()) {
		d::WorkBudget budget{1 << 20, 1 << 16};
		fragments.feed(input, budget);
		while (auto piece = fragments.take()) {
			auto bytes = piece->bytes();
			std::span<const char> body(bytes.data(), bytes.size());
			while (!body.empty()) {
				d::WorkBudget b{1 << 20, 1 << 16};
				feed(body, b);
			}
		}
	}
	fragments.finish();
}

struct Store {
	std::filesystem::path path;
	std::shared_ptr<d::StorageLedger> baseline = std::make_shared<d::StorageLedger>(segment);
	std::shared_ptr<d::StorageLedger> heap = std::make_shared<d::StorageLedger>(48ull << 30);
	d::RetirementRuntime runtime{baseline, 1};
	d::RetirementDomain domain = runtime.domain(heap, 4096);
	l::DiskStoreOptions options;
	std::shared_ptr<l::DiskStore> disk;
	l::StorageIdentity identity{};
	double open_seconds = 0;

	explicit Store(std::filesystem::path p) : path(std::move(p)) {
		const auto start = Clock::now();
		std::filesystem::create_directories(path);
		::chmod(path.c_str(), 0700);
		options.mode = std::filesystem::exists(path / "storage.meta") ? l::StoreOpenMode::ExistingManaged
																	   : l::StoreOpenMode::FreshEmpty;
		options.limits = {256ull << 30, 10'000'000, 1000};
		options.recovery_limits = l::ViewLimits::prepared();
		options.recovery_limits.retirement_domain = &domain;
		options.recovery_limits.mapping_account = std::make_shared<d::StorageLedger>(64ull << 30);
		disk = l::make_disk_store(domain, path, std::make_shared<int>(1), options);
		drive();
		if (disk->status() != l::StoreStatus::Ready)
			throw std::runtime_error("store did not open: " + path.string());
		std::array<char, l::storage_identity_bytes> bytes{};
		std::ifstream in(path / "storage.meta", std::ios::binary);
		in.read(bytes.data(), bytes.size());
		identity = l::decode_storage_identity(bytes);
		open_seconds = seconds(Clock::now() - start);
	}
	~Store() {
		disk->close();
		try {
			drive();
		} catch (...) {
		}
		disk.reset();
		domain.close().wait();
	}
	void drive() {
		for (std::uint64_t turns = 0;; ++turns) {
			if (turns > 1'000'000'000)
				throw std::runtime_error("store made no progress");
			disk->advance();
			auto operation = disk->operation();
			if (!operation) {
				if (disk->needs_work())
					continue;
				break;
			}
			operation->submitted();
			if (!operation->complete(
					kronuz::journal::detail::execute_primitive(operation->io(), operation->request())))
				throw std::runtime_error("store completion refused");
		}
		if (auto error = disk->error())
			std::rethrow_exception(error);
	}
	l::GenerationId generation(std::uint64_t seed) {
		std::mt19937_64 random(seed);
		l::GenerationId g{};
		for (auto &byte : g)
			byte = static_cast<char>(random());
		return g;
	}
	std::optional<l::RecoveredGeneration> actual() {
		if (!disk->selection().descriptor)
			return std::nullopt;
		return l::detail::ManagedRecovery::recover(path, l::detail::ManagedRecovery::capture(*disk));
	}
	// Publishes the plan; returns seconds until Published.
	double publish(l::detail::PagedPlan plan) {
		const auto start = Clock::now();
		disk->begin(std::move(plan));
		drive();
		if (disk->publication_status() != l::DiskPublicationStatus::Published)
			throw std::runtime_error("publication was refused");
		return seconds(Clock::now() - start);
	}
};

struct Timing {
	double wall = 0, cpu = 0, decode = 0, plan = 0, publish = 0;
};

void report(std::ostream &out, const char *arm, double fraction, const Answer &a, std::uint64_t x_raw,
			const Framed &framed, double encode_cpu, double encode_wall, const Timing &t, double open, double recover,
			const l::SyncStats &sync) {
	out << "{\"arm\":\"" << arm << "\",\"fraction\":" << fraction << ",\"keys\":" << a.keys
		<< ",\"deletes\":" << a.deletes << ",\"x_raw\":" << x_raw << ",\"x_framed\":" << framed.bytes.size()
		<< ",\"fragments\":" << framed.fragments << ",\"encode_cpu_s\":" << encode_cpu
		<< ",\"encode_wall_s\":" << encode_wall << ",\"apply_wall_s\":" << t.wall << ",\"apply_cpu_s\":" << t.cpu
		<< ",\"decode_s\":" << t.decode << ",\"plan_s\":" << t.plan << ",\"publish_s\":" << t.publish
		<< ",\"store_open_s\":" << open << ",\"recover_s\":" << recover
		<< ",\"sync\":{\"files\":" << sync.publication.files
		<< ",\"directories\":" << sync.publication.directory_syncs
		<< ",\"flushes\":" << sync.publication.device_flushes << ",\"groups\":" << sync.publication.groups << "}"
		<< ",\"peak_rss\":" << peak_rss() << "}" << std::endl;
}
} // namespace

int main(int argc, char **argv) {
	try {
		if (argc < 4)
			throw std::invalid_argument("usage: full CORPUS STORE | delta CORPUS STORE FRACTION SEED");
		const std::string mode = argv[1];
		const std::filesystem::path corpus_path = argv[2], store_path = argv[3];
		const bool full = mode == "full";
		if (!full && (mode != "delta" || argc != 6))
			throw std::invalid_argument("usage: full CORPUS STORE | delta CORPUS STORE FRACTION SEED");
		const double fraction = full ? 1.0 : std::stod(argv[4]);
		const std::uint64_t seed = full ? 0 : std::stoull(argv[5]);
		if (!(fraction > 0 && fraction <= 1))
			throw std::invalid_argument("fraction in (0, 1]");
		if (full && std::filesystem::exists(store_path))
			throw std::invalid_argument("full needs a new store directory");
		// The answer, encoded and framed as a relay would send it; the corpus is released before timing.
		std::vector<std::string> scope;
		d::Cursor cursor;
		Answer a;
		Framed framed;
		std::uint64_t x_raw = 0;
		double encode_cpu = 0, encode_wall = 0;
		{
			const auto load_start = Clock::now();
			const auto corpus = load(corpus_path);
			const auto recs = records(corpus);
			std::cerr << "corpus " << recs.defs.size() << " definitions, " << recs.scope.size()
					  << " namespaces, loaded in " << seconds(Clock::now() - load_start) << " s\n";
			cursor = cursor_at(recs.cursor_revision);
			scope = recs.scope;
			const auto e0 = Clock::now();
			const auto c0 = cpu_seconds();
			if (full) {
				a.wire = snapshot_wire(recs, cursor);
				a.keys = recs.defs.size();
			} else
				a = delta_wire(recs, cursor, fraction, seed);
			framed = frame(full ? d::UpdateKind::Snapshot : d::UpdateKind::Delta, a.wire);
			encode_cpu = cpu_seconds() - c0;
			encode_wall = seconds(Clock::now() - e0);
			x_raw = a.wire.size();
			std::string().swap(a.wire);
		}
		Store store(store_path);
		std::optional<l::RecoveredGeneration> actual;
		double recover = 0;
		if (!full) {
			const auto selected = store.disk->selection().descriptor;
			if (!selected || selected->cursor.revision != cursor.revision)
				throw std::runtime_error("store does not hold the corpus at its revision");
			const auto r0 = Clock::now();
			actual = store.actual();
			recover = seconds(Clock::now() - r0);
		}
		Timing t;
		const auto start = Clock::now();
		const auto cpu = cpu_seconds();
		std::optional<l::detail::PagedPlan> plan;
		if (full) {
			d::SnapshotDecoder decoder(cursor, scope, wire_limits(store.heap));
			unframe(framed, d::UpdateKind::Snapshot, x_raw,
					[&](std::span<const char> &in, d::WorkBudget &b) { decoder.feed(in, b); }, store.heap);
			auto snapshot = std::make_shared<const d::ValidatedSnapshot>(decoder.finish());
			t.decode = seconds(Clock::now() - start);
			const auto planned = Clock::now();
			l::detail::PagedSnapshotPlanBuilder builder(snapshot, std::nullopt, store.disk->directory_identity(),
														store.identity, store.generation(1), store.domain.factory(),
														store.heap, store.options.recovery_limits);
			while (!builder.done()) {
				l::ValidationBudget budget;
				builder.step(budget);
			}
			plan.emplace(builder.take());
			t.plan = seconds(Clock::now() - planned);
		} else {
			d::DeltaDecoder decoder(cursor, scope, wire_limits(store.heap), d::UpdateKind::Delta);
			unframe(framed, d::UpdateKind::Delta, x_raw,
					[&](std::span<const char> &in, d::WorkBudget &b) { decoder.feed(in, b); }, store.heap);
			auto source = std::make_shared<const d::ValidatedDelta>(decoder.finish());
			t.decode = seconds(Clock::now() - start);
			const auto planned = Clock::now();
			const auto bounds = l::detail::paged_limits(store.options.recovery_limits, l::paged::root_bytes);
			l::detail::PagedDeltaPlanBuilder builder(*actual, source, store.generation(seed + 2),
													 store.domain.factory(), store.heap, bounds);
			while (!builder.done()) {
				l::ValidationBudget budget;
				builder.step(budget);
			}
			plan.emplace(builder.take());
			t.plan = seconds(Clock::now() - planned);
		}
		t.publish = store.publish(std::move(*plan));
		t.wall = seconds(Clock::now() - start);
		t.cpu = cpu_seconds() - cpu;
		report(std::cout, full ? "full" : "delta", fraction, a, x_raw, framed, encode_cpu, encode_wall, t,
			   store.open_seconds, recover, store.disk->sync_stats());
	} catch (const std::exception &e) {
		std::cerr << "namespace_measure_t9: " << e.what() << '\n';
		return 1;
	}
}
