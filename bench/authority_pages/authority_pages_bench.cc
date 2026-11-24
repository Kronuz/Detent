// Prices dense pages on the authority (docs/design/authority-pages.md) with the relay's own builder,
// detent::relay::build_generation (dense distribution D7.2): a full build, then one build over the
// previous generation per seal, the relay's relocation when dead bytes pass live ones.
//
//   authority_pages_bench seals DIR NAMESPACES KEYS SEALS CHANGES [write]
//     Cold-builds NAMESPACES namespaces of KEYS keys, then SEALS seals of CHANGES uniform updates
//     each; `write` also writes each new segment under DIR and syncs it (F_FULLFSYNC on macOS).
//   authority_pages_bench commit DIR NAMESPACES KEYS GROUPS ARM...
//     One durable single-voter authority (positions and history on, 100,000 keys) commits GROUPS
//     groups of 64 one-change transactions per ARM, back to back on one node. Each authority seal
//     (2,048 changes) triggers one builder seal of 2,048 uniform updates over the builder's corpus:
//     ARM none (today), thread (a builder thread, pages in memory), write (a builder thread that
//     also writes and syncs its segments) or inline (the build runs in the service loop).
//   authority_pages_bench paced DIR NAMESPACES KEYS SEAL_RATE ROUNDS GROUPS
//     The commit path beside builders held at SEAL_RATE by the clock, arms interleaved (paced()).
//   authority_pages_bench lag DIR NAMESPACES KEYS THREADS warm|cold INTERVAL RATE SECONDS BURST AT SPAN
//     Root lag of option 2's pass pool at an open-loop write rate (lag()).
// Every line printed is one JSON object.
#include "detent_relay/page_copy.hh"
#include "node.hh"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <ctime>
#include <deque>
#include <fcntl.h>
#include <filesystem>
#include <libproc.h>
#include <map>
#include <mach/mach.h>
#include <malloc/malloc.h>
#include <mutex>
#include <numeric>
#include <optional>
#include <random>
#include <set>
#include <sys/resource.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <completion/posix.h>
#include <sha256.h>

namespace d = detent::distribution;
namespace r = detent::relay;
namespace a = detent::authority;
using Clock = std::chrono::steady_clock;

namespace {
void check(bool valid, const char *message) {
	if (!valid)
		throw std::runtime_error(message);
}
double since_ms(Clock::time_point start) {
	return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
double thread_cpu_ms() {
	timespec t{};
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}
double process_cpu_ms() {
	rusage u{};
	getrusage(RUSAGE_SELF, &u);
	return (u.ru_utime.tv_sec + u.ru_stime.tv_sec) * 1e3 + (u.ru_utime.tv_usec + u.ru_stime.tv_usec) / 1e3;
}
double heap_mb() {
	malloc_statistics_t stats{};
	malloc_zone_statistics(nullptr, &stats);
	return double(stats.size_in_use) / (1 << 20);
}
double resident_mb() {
	mach_task_basic_info_data_t info{};
	mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
	task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count);
	return double(info.resident_size) / (1 << 20);
}
double peak_mb() {
	rusage u{};
	getrusage(RUSAGE_SELF, &u);
	return double(u.ru_maxrss) / (1 << 20); // bytes on macOS
}
// Bytes this process wrote to storage, as the kernel counts them.
std::uint64_t disk_written() {
	rusage_info_v2 info{};
	proc_pid_rusage(getpid(), RUSAGE_INFO_V2, reinterpret_cast<rusage_info_t *>(&info));
	return info.ri_diskio_byteswritten;
}
double percentile(std::vector<double> v, double p) {
	if (v.empty())
		return 0;
	std::sort(v.begin(), v.end());
	return v[std::min(v.size() - 1, std::size_t(p * double(v.size() - 1) + 0.5))];
}
d::Identity identity(char fill) {
	d::Identity out;
	out.fill(fill);
	return out;
}

// relay_window_bench's records: key k%07zu, an override on every seventh key.
detent::KnobDef definition(std::size_t key, int value) {
	detent::KnobDef out{std::int64_t(value)};
	if (key % 7 == 3)
		out.overrides.emplace("group-" + std::to_string(key % 5), std::int64_t(value * 3));
	return out;
}
// Option (d) as incremental hashing: a homomorphic multiset hash of the namespace's records
// (LtHash's shape: 1,024 16-bit lanes, each record expanded to 2 KiB by SHA-256 in counter mode),
// so a change subtracts the old record and adds the new one. Prices the authority's cost only.
struct RecordsDigest {
	std::array<std::uint16_t, 1024> lanes{};
	void apply(std::string_view key, std::uint64_t revision, const std::string &value, bool add) {
		unsigned char block[32];
		for (std::uint32_t i = 0; i < 64; ++i) {
			kronuz::sha256::hasher h;
			h.update(&i, sizeof i);
			h.update(&revision, sizeof revision);
			h.update(key);
			h.update(value);
			h.final(block);
			for (unsigned j = 0; j < 16; ++j) {
				const auto lane = std::uint16_t(block[2 * j] | block[2 * j + 1] << 8);
				auto &x = lanes[16 * i + j];
				x = add ? std::uint16_t(x + lane) : std::uint16_t(x - lane);
			}
		}
	}
};
struct Record {
	std::string key;
	std::uint64_t revision;
	detent::KnobDef value;
};
// One namespace's history for one build: a full copy at the head (cold) or the ring after the base.
class MemoryHistory final : public d::NamespaceHistory {
  public:
	MemoryHistory(d::NamespaceCursor head, std::uint64_t start, std::vector<Record> records, bool full)
		: head_(head), start_(start), records_(std::move(records)), full_(full) {}
	d::NamespaceCursor head() const noexcept override { return head_; }
	std::uint64_t cut() const noexcept override { return start_; }
	std::uint64_t ring_start() const noexcept override { return start_; }
	std::span<const d::HistorySpan> spans() const noexcept override { return {}; }
	std::unique_ptr<d::HistoryReader> read_span(std::size_t) const override {
		throw d::HistoryFailure("no spans");
	}
	std::unique_ptr<d::HistoryReader> read_ring(std::uint64_t after) const override {
		return std::make_unique<Reader>(full_ ? std::span<const Record>{} : std::span<const Record>(records_), after);
	}
	std::unique_ptr<d::HistoryReader> read_full_copy() const override {
		return std::make_unique<Reader>(full_ ? std::span<const Record>(records_) : std::span<const Record>{}, 0);
	}

  private:
	class Reader final : public d::HistoryReader {
	  public:
		Reader(std::span<const Record> records, std::uint64_t after) : records_(records), after_(after) {}
		const d::HistoryRecord *next() override {
			while (at_ < records_.size() && records_[at_].revision <= after_)
				++at_;
			if (at_ == records_.size())
				return nullptr;
			const auto &r = records_[at_++];
			current_ = {r.key, r.revision, &r.value};
			return &current_;
		}

	  private:
		std::span<const Record> records_;
		std::uint64_t after_;
		std::size_t at_ = 0;
		d::HistoryRecord current_;
	};
	d::NamespaceCursor head_;
	std::uint64_t start_;
	std::vector<Record> records_;
	bool full_;
};

struct SealResult {
	double cpu_ms = 0, wall_ms = 0, encode_ms = 0, sync_ms = 0, digest_cpu_ms = 0;
	std::uint64_t written = 0, builds = 0, relocations = 0;
};
// The builder's namespaces: their current generation each, as a voter that keeps only its head.
class Corpus {
  public:
	Corpus(std::filesystem::path dir, std::size_t spaces, std::size_t keys, bool write)
		: dir_(std::move(dir)), keys_(keys), write_(write) {
		for (std::size_t i = 0; i < spaces; ++i) {
			char name[16];
			std::snprintf(name, sizeof name, "app%03zu", i);
			spaces_.push_back({name, 0, nullptr, {}});
		}
	}
	std::size_t keys() const noexcept { return keys_ * spaces_.size(); }
	// The full build of every namespace: what a voter pays with no pages to start from.
	SealResult cold() {
		SealResult out;
		for (auto &s : spaces_) {
			std::vector<Record> records;
			records.reserve(keys_);
			for (std::size_t k = 0; k < keys_; ++k)
				records.push_back({key(s.name, k), k + 1, definition(k, int(k))});
			s.position = keys_;
			build(s, std::move(records), true, out);
		}
		return out;
	}
	std::size_t spaces() const noexcept { return spaces_.size(); }
	std::size_t keys_per_space() const noexcept { return keys_; }
	std::uint64_t position(std::size_t i) const noexcept { return spaces_[i].position; }
	// The full build of namespace i at position KEYS: a new leader's first pass over its state.
	SealResult cold_space(std::size_t i) {
		SealResult out;
		auto &s = spaces_[i];
		std::vector<Record> records;
		records.reserve(keys_);
		for (std::size_t k = 0; k < keys_; ++k)
			records.push_back({key(s.name, k), k + 1, definition(k, int(k))});
		s.position = keys_;
		build(s, std::move(records), true, out);
		return out;
	}
	// One pass over namespace i's head with `records` (revisions past it, ascending), up to their last.
	SealResult advance(std::size_t i, std::vector<Record> records) {
		SealResult out;
		auto &s = spaces_[i];
		s.position = records.back().revision;
		build(s, std::move(records), false, out);
		return out;
	}
	std::string record_key(std::size_t i, std::size_t k) const { return key(spaces_[i].name, k); }
	// One seal of `changes` uniform updates across every namespace, one build per namespace it touches,
	// on `threads` threads (each namespace builds on one).
	SealResult seal(std::size_t changes, std::mt19937_64 &random, int salt, std::size_t threads = 1) {
		SealResult out;
		std::vector<std::vector<Record>> touched(spaces_.size());
		std::uniform_int_distribution<std::size_t> pick(0, keys() - 1);
		for (std::size_t j = 0; j < changes; ++j) {
			const auto at = pick(random);
			auto &s = spaces_[at / keys_];
			const auto k = at % keys_;
			touched[at / keys_].push_back({key(s.name, k), ++s.position, definition(k, salt + int(k))});
		}
		// (d): each change removes its key's previous record and adds the new one.
		const auto digest = thread_cpu_ms();
		for (std::size_t i = 0; i < spaces_.size(); ++i)
			for (const auto &record : touched[i]) {
				const auto value = detent::authority::encode_definition(record.value);
				digest_.apply(record.key, record.revision - 1, value, false);
				digest_.apply(record.key, record.revision, value, true);
			}
		out.digest_cpu_ms = thread_cpu_ms() - digest;
		if (threads <= 1) {
			for (std::size_t i = 0; i < spaces_.size(); ++i)
				if (!touched[i].empty())
					build(spaces_[i], std::move(touched[i]), false, out);
			return out;
		}
		std::vector<SealResult> parts(threads);
		std::vector<std::thread> pool;
		for (std::size_t t = 0; t < threads; ++t)
			pool.emplace_back([&, t] {
				for (std::size_t i = t; i < spaces_.size(); i += threads)
					if (!touched[i].empty())
						build(spaces_[i], std::move(touched[i]), false, parts[t]);
			});
		for (auto &thread : pool)
			thread.join();
		for (const auto &p : parts) {
			out.cpu_ms += p.cpu_ms;
			out.wall_ms = std::max(out.wall_ms, p.wall_ms);
			out.encode_ms += p.encode_ms;
			out.sync_ms += p.sync_ms;
			out.written += p.written;
			out.builds += p.builds;
			out.relocations += p.relocations;
		}
		return out;
	}
	std::uint64_t page_bytes() const {
		std::uint64_t n = 0;
		for (const auto &s : spaces_)
			n += s.head->page_bytes();
		return n;
	}
	std::uint64_t physical_bytes() const {
		std::uint64_t n = 0;
		for (const auto &s : spaces_)
			n += s.head->physical_bytes();
		return n;
	}

  private:
	struct Space {
		std::string name;
		std::uint64_t position;
		std::shared_ptr<const r::Generation> head;
		std::set<std::string> files; // segment ids held on disk, when writing
	};
	static std::string key(const std::string &name, std::size_t k) {
		char text[16];
		std::snprintf(text, sizeof text, "k%07zu", k);
		return name + ":" + text;
	}
	static std::string id(const detent::local::detail::BuiltSegment &segment) {
		static const char hex[] = "0123456789abcdef";
		std::string out;
		for (unsigned char c : segment.ownership.segment) {
			out.push_back(hex[c >> 4]);
			out.push_back(hex[c & 15]);
		}
		return out;
	}
	void build(Space &s, std::vector<Record> records, bool full, SealResult &out) {
		const auto wall = Clock::now();
		const auto cpu = thread_cpu_ms();
		const auto base = s.head;
		const d::NamespaceCursor head{identity('C'), identity('i'), s.position};
		const auto start = full ? s.position : base->at().position;
		const MemoryHistory history(head, start, std::move(records), full);
		const d::NamespaceRequest request{s.name, &history, full ? std::nullopt : std::optional(base->at())};
		const auto encode = Clock::now();
		const auto body = d::encode_session_frames(std::nullopt, {&request, 1});
		out.encode_ms += since_ms(encode);
		auto built = r::build_generation(body, base);
		++out.builds;
		// The relay's space rule (D7.4): relocate once dead bytes pass live ones.
		if (base && 2 * built->page_bytes() < built->physical_bytes()) {
			built = r::relocate(*built);
			++out.relocations;
		}
		std::set<std::string> held;
		for (const auto &segment : built->segments()) {
			const auto name = id(segment);
			held.insert(name);
			if (s.files.count(name))
				continue;
			out.written += segment.ownership.length;
			if (write_)
				out.sync_ms += store(s.name + "-" + name, segment);
		}
		if (write_) {
			for (const auto &name : s.files)
				if (!held.count(name))
					std::filesystem::remove(dir_ / (s.name + "-" + name));
			const auto sync = Clock::now();
			const int fd = ::open(dir_.c_str(), O_RDONLY);
			check(fd >= 0 && ::fsync(fd) == 0 && ::close(fd) == 0, "directory sync");
			out.sync_ms += since_ms(sync);
		}
		s.files = std::move(held);
		s.head = std::move(built);
		out.cpu_ms += thread_cpu_ms() - cpu;
		out.wall_ms += since_ms(wall);
	}
	// Writes one segment and flushes it as the journal does on macOS; returns the sync's wall ms.
	double store(const std::string &name, const detent::local::detail::BuiltSegment &segment) {
		const auto bytes = segment.bytes->view();
		const int fd = ::open((dir_ / name).c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
		check(fd >= 0, "segment open");
		std::size_t done = 0;
		while (done < bytes.size()) {
			const auto n = ::write(fd, bytes.data() + done, bytes.size() - done);
			check(n > 0, "segment write");
			done += std::size_t(n);
		}
		const auto sync = Clock::now();
		check(::fcntl(fd, F_FULLFSYNC) == 0, "segment sync");
		const auto ms = since_ms(sync);
		check(::close(fd) == 0, "segment close");
		return ms;
	}
	std::filesystem::path dir_;
	std::size_t keys_;
	bool write_;
	std::vector<Space> spaces_;
	RecordsDigest digest_;
};

void print_seal(const char *what, std::size_t n, const SealResult &s) {
	std::printf("{\"step\":\"%s\",\"n\":%zu,\"cpu_ms\":%.3f,\"wall_ms\":%.3f,\"encode_ms\":%.3f,\"sync_ms\":%.3f,"
				"\"digest_cpu_ms\":%.3f,\"written\":%llu,\"builds\":%llu,\"relocations\":%llu,\"heap_mb\":%.1f,"
				"\"resident_mb\":%.1f,\"peak_mb\":%.1f}\n",
				what, n, s.cpu_ms, s.wall_ms, s.encode_ms, s.sync_ms, s.digest_cpu_ms, (unsigned long long)s.written,
				(unsigned long long)s.builds, (unsigned long long)s.relocations, heap_mb(), resident_mb(), peak_mb());
	std::fflush(stdout);
}

int seals(int argc, char **argv) {
	check(argc == 7 || (argc == 8 && std::string_view(argv[7]) == "write"),
		  "usage: seals DIR NAMESPACES KEYS SEALS CHANGES [write]");
	const std::filesystem::path dir = argv[2];
	const std::size_t spaces = std::stoull(argv[3]), keys = std::stoull(argv[4]), count = std::stoull(argv[5]),
					  changes = std::stoull(argv[6]);
	const bool write = argc == 8;
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir);
	std::printf("{\"mode\":\"seals\",\"namespaces\":%zu,\"keys\":%zu,\"seals\":%zu,\"changes\":%zu,\"write\":%s,"
				"\"heap_mb\":%.1f}\n",
				spaces, keys, count, changes, write ? "true" : "false", heap_mb());
	Corpus corpus(dir, spaces, keys, write);
	const auto heap0 = heap_mb();
	const auto disk0 = disk_written();
	const auto cold = corpus.cold();
	print_seal("cold", spaces, cold);
	std::printf("{\"step\":\"corpus\",\"page_bytes\":%llu,\"physical_bytes\":%llu,\"pages_heap_mb\":%.1f}\n",
				(unsigned long long)corpus.page_bytes(), (unsigned long long)corpus.physical_bytes(), heap_mb() - heap0);
	std::mt19937_64 random(20261121);
	std::vector<double> cpu, wall, sync, digest;
	std::uint64_t written = 0, relocations = 0;
	const auto disk1 = disk_written();
	for (std::size_t i = 0; i < count; ++i) {
		const auto s = corpus.seal(changes, random, int(1000 * (i + 1)));
		print_seal("seal", i + 1, s);
		cpu.push_back(s.cpu_ms);
		wall.push_back(s.wall_ms);
		sync.push_back(s.sync_ms);
		digest.push_back(s.digest_cpu_ms);
		written += s.written;
		relocations += s.relocations;
	}
	std::printf("{\"step\":\"summary\",\"seals\":%zu,\"changes\":%zu,\"cpu_ms_p50\":%.3f,\"cpu_ms_p90\":%.3f,"
				"\"cpu_ms_max\":%.3f,\"wall_ms_p50\":%.3f,\"wall_ms_max\":%.3f,\"sync_ms_p50\":%.3f,"
				"\"digest_cpu_ms_p50\":%.3f,"
				"\"written_per_seal\":%.0f,\"relocations\":%llu,\"page_bytes\":%llu,\"physical_bytes\":%llu,"
				"\"pages_heap_mb\":%.1f,\"disk_written_cold\":%llu,\"disk_written_seals\":%llu,\"peak_mb\":%.1f}\n",
				count, changes, percentile(cpu, 0.5), percentile(cpu, 0.9), percentile(cpu, 1), percentile(wall, 0.5),
				percentile(wall, 1), percentile(sync, 0.5), percentile(digest, 0.5), count ? double(written) / double(count) : 0.0,
				(unsigned long long)relocations, (unsigned long long)corpus.page_bytes(),
				(unsigned long long)corpus.physical_bytes(), heap_mb() - heap0, (unsigned long long)(disk1 - disk0),
				(unsigned long long)(disk_written() - disk1), peak_mb());
	std::filesystem::remove_all(dir);
	return 0;
}

// One durable single-voter authority with positions and history on, driven as the offline runtime
// drives it (tests/detent_authority/offline_runtime.hh): service, then its storage and history IO.
class Authority {
  public:
	explicit Authority(const std::filesystem::path &dir) {
		// The journal bootstraps only into an empty directory, so history sits beside it.
		for (const auto *sub : {"journal", "history"}) {
			std::filesystem::create_directories(dir / sub);
			::chmod((dir / sub).c_str(), 0700);
		}
		options_.context.cluster[0] = 'c';
		options_.context.group[0] = 'g';
		options_.context.epoch[0] = 'e';
		options_.configuration.cluster = options_.context.cluster;
		options_.configuration.configuration = options_.context.group;
		options_.configuration.local = 1;
		options_.configuration.voters = {1};
		node_ = std::make_unique<a::Node>(std::make_unique<kronuz::journal::PosixIO>(dir / "journal"), options_);
		node_->attach_history(std::make_shared<kronuz::journal::PosixIO>(dir / "history"));
		a::Identity storage{};
		storage[0] = 'b';
		node_->create(storage);
		for (unsigned i = 0; i < 10000 && !node_->worker().ready(); ++i)
			pump();
		check(node_->worker().ready(), "initialization timeout");
		node_->start();
		for (unsigned i = 0; i < 10000 && node_->worker().role() != kronuz::consensus::Role::Leader; ++i)
			pump();
		check(node_->worker().role() == kronuz::consensus::Role::Leader, "election timeout");
		a::AdministrativeCommand enable{options_.context, a::AdministrativeKind::EnablePositions, {}, {}, {}, {}};
		enable.nonce[0] = 's';
		await(++request_, [&] { return node_->propose_administrative(request_, enable); });
		commands_.resize(64);
		for (std::size_t i = 0; i < commands_.size(); ++i) {
			auto &c = commands_[i];
			c.context = options_.context;
			c.producer = "publisher" + std::to_string(i);
			c.kind = a::Kind::Open;
			c.nonce[0] = char(i + 1);
		}
		submit(commands_);
		for (auto &c : commands_) {
			c.kind = a::Kind::Transaction;
			c.nonce = {};
			c.generation = 1;
			c.name_space = "ns";
		}
	}
	// Seeds `count` keys, 128 per transaction.
	void load(std::size_t count) {
		values_.resize(count);
		std::iota(values_.begin(), values_.end(), std::int64_t(0));
		std::size_t key = 0;
		while (key < count) {
			std::vector<a::Command> batch;
			for (auto &c : commands_) {
				c.mutations.clear();
				for (unsigned i = 0; i < 128 && key < count; ++i, ++key)
					c.mutations.push_back({"ns:feature_" + std::to_string(key), detent::KnobDef{std::int64_t(key)}, {}, {}});
				if (c.mutations.empty())
					break;
				++c.sequence;
				batch.push_back(c);
			}
			submit(batch);
		}
	}
	// One group: every producer writes one uniform key, each a value no key has held, so none is
	// NoChange; seeded values are the key indexes, so group values start past them. ROOTS root
	// entries ride in the same group as mutations of its last transaction (a group holds at most 64),
	// about 62 B each (a key, a 32-byte override and two values: {namespace, incarnation, position,
	// root}).
	void group(std::mt19937_64 &random, std::size_t keys, int salt, std::size_t roots = 0) {
		std::uniform_int_distribution<std::size_t> pick(0, keys - 1);
		std::int64_t value = std::int64_t(values_.size()) + std::int64_t(salt) * std::int64_t(commands_.size());
		picked_.clear();
		for (auto &c : commands_) {
			const auto key = pick(random);
			picked_.push_back({key, value});
			c.mutations = {{"ns:feature_" + std::to_string(key), detent::KnobDef{value++}, {}, {}}};
			++c.sequence;
		}
		if (!roots) {
			submit(commands_);
		} else {
			auto batch = commands_;
			for (std::size_t j = 0; j < roots; ++j) {
				detent::KnobDef root{++root_value_};
				root.overrides.emplace(std::string(32, char('a' + j % 26)), root_value_);
				batch.back().mutations.push_back({"ns:root_" + std::to_string(j), std::move(root), {}, {}});
			}
			submit(batch);
		}
		for (const auto &[key, v] : picked_)
			values_[key] = v;
		picked_.clear();
	}
	std::uint64_t seals() const noexcept { return node_->state().seal().ordinal; }
	// Runs after each service call: the inline arm builds there.
	std::function<void()> after_service;
	double worst_gap_ms = 0; // the longest interval between two service calls
	// Starts a new gap measurement, so the next one never spans the pause before it.
	void reset_gap() {
		worst_gap_ms = 0;
		last_.reset();
	}

  private:
	void pump() {
		const auto now = Clock::now();
		if (last_)
			worst_gap_ms = std::max(worst_gap_ms, std::chrono::duration<double, std::milli>(now - *last_).count());
		last_ = now;
		node_->service({tick_ += 10, options_.timing.election_min});
		if (auto job = node_->worker().take_io_operation())
			kronuz::journal::drive_synchronously(*job);
		if (auto job = node_->take_history_operation())
			kronuz::journal::drive_synchronously(*job);
		for (const auto &action : node_->take_network_actions())
			check(!std::holds_alternative<kronuz::consensus::Send>(action), "unexpected peer traffic");
		check(!node_->worker().fenced(), "worker fenced");
		for (auto &c : node_->take_completions())
			completed_.push_back(std::move(c));
		if (after_service)
			after_service();
	}
	template <class Propose> void await(std::uint64_t id, Propose propose) {
		bool accepted = false;
		for (unsigned i = 0; i < 1000000; ++i) {
			if (!accepted) {
				const auto result = propose();
				check(result == kronuz::consensus::SubmitResult::Accepted ||
						  result == kronuz::consensus::SubmitResult::Busy ||
						  result == kronuz::consensus::SubmitResult::Pressure,
					  "proposal rejected");
				accepted = result == kronuz::consensus::SubmitResult::Accepted;
			}
			pump();
			for (auto c = completed_.begin(); c != completed_.end(); ++c)
				if (c->request == kronuz::consensus::RequestId{id}) {
					check(!c->rejection, "completion rejected");
					for (std::size_t k = 0; k < c->results.size(); ++k)
						if (c->results[k].status != a::Status::Applied)
							throw std::runtime_error("transaction " + std::to_string(k) + " rejected: status " +
													 std::to_string(int(c->results[k].status)) + rejection_detail(k));
					completed_.erase(c);
					return;
				}
		}
		throw std::runtime_error("durable completion timeout");
	}
	// Names the group mutation behind a rejected result: its key, its value and the key's value before.
	std::string rejection_detail(std::size_t k) const {
		if (k >= picked_.size())
			return {};
		const auto &[key, value] = picked_[k];
		return " (key " + std::to_string(key) + ", value " + std::to_string(value) + ", held " +
			   std::to_string(values_[key]) + ")";
	}
	void submit(const std::vector<a::Command> &commands) {
		const auto id = ++request_;
		await(id, [&] { return node_->propose(id, commands); });
	}
	a::NodeOptions options_;
	std::unique_ptr<a::Node> node_;
	std::vector<a::Command> commands_;
	std::int64_t root_value_ = std::int64_t(1) << 40; // past every key's value
	std::vector<a::Completion> completed_;
	std::vector<std::int64_t> values_;							  // each seeded key's value, by index
	std::vector<std::pair<std::size_t, std::int64_t>> picked_; // the group in flight
	std::uint64_t tick_ = 0, request_ = 0;
	std::optional<Clock::time_point> last_;
};

// The builder thread: one seal per authority seal, in order, off the service loop. Each seal's lag
// runs from its post to the end of its build, queueing behind earlier seals included; its build time
// is the build alone.
class Builder {
  public:
	explicit Builder(Corpus &corpus, std::size_t threads = 1)
		: corpus_(corpus), threads_(threads), thread_([this] { run(); }) {}
	~Builder() {
		{
			std::lock_guard lock(mutex_);
			stop_ = true;
		}
		wake_.notify_all();
		thread_.join();
	}
	void post() {
		{
			std::lock_guard lock(mutex_);
			queue_.push_back(Clock::now());
		}
		wake_.notify_all();
	}
	// Waits for the posted seals; returns the wait's wall ms.
	double drain() {
		const auto start = Clock::now();
		std::unique_lock lock(mutex_);
		idle_.wait(lock, [&] { return queue_.empty(); });
		return since_ms(start);
	}
	SealResult total() {
		std::lock_guard lock(mutex_);
		return total_;
	}
	std::vector<double> lag_ms() {
		std::lock_guard lock(mutex_);
		return lag_;
	}
	std::vector<double> build_ms() {
		std::lock_guard lock(mutex_);
		return build_;
	}

  private:
	void run() {
		std::mt19937_64 random(7);
		std::unique_lock lock(mutex_);
		for (;;) {
			wake_.wait(lock, [&] { return stop_ || !queue_.empty(); });
			if (stop_)
				return;
			const auto posted = queue_.front();
			lock.unlock();
			const auto start = Clock::now();
			const auto s = corpus_.seal(a::LayoutPolicy{}.seal_transactions, random, int(++salt_), threads_);
			const auto built = since_ms(start);
			lock.lock();
			total_.cpu_ms += s.cpu_ms;
			total_.wall_ms += s.wall_ms;
			total_.sync_ms += s.sync_ms;
			total_.written += s.written;
			total_.builds += s.builds;
			total_.relocations += s.relocations;
			lag_.push_back(since_ms(posted));
			build_.push_back(built);
			queue_.pop_front();
			idle_.notify_all();
		}
	}
	Corpus &corpus_;
	std::size_t threads_;
	std::mutex mutex_;
	std::condition_variable wake_, idle_;
	std::deque<Clock::time_point> queue_; // posted seals not yet built, by post time
	std::uint64_t salt_ = 0;
	bool stop_ = false;
	SealResult total_;
	std::vector<double> lag_, build_;
	std::thread thread_;
};

// Option 2's builder (PLAN D156): THREADS workers coalesce each namespace's pending changes into one
// pass, at most one pass per namespace per INTERVAL, and publish the root at the position the pass
// reached; a change's root lag runs from its arrival to the end of that pass. A cold pool (a new
// leader) full-builds each namespace first while changes keep arriving.
class PassPool {
  public:
	struct Sample {
		double at_s, lag_ms;
	};
	struct Totals {
		std::vector<Sample> samples;
		std::vector<double> pass_ms, pass_changes, cold_ms;
		double cpu_ms = 0;
		std::uint64_t passes = 0, relocations = 0, written = 0;
		double cold_done_ms = 0, caught_up_ms = 0; // the last namespace's cold build; its first pass after it
	};
	PassPool(Corpus &corpus, std::size_t threads, std::chrono::milliseconds interval, bool cold)
		: corpus_(corpus), interval_(interval), state_(corpus.spaces()), stats_(threads), t0_(Clock::now()) {
		for (std::size_t i = 0; i < state_.size(); ++i) {
			state_[i].next = cold ? corpus.keys_per_space() : corpus.position(i);
			state_[i].built = !cold;
		}
		for (std::size_t t = 0; t < threads; ++t)
			pool_.emplace_back([this, t] { work(stats_[t]); });
	}
	~PassPool() { finish(); }
	Clock::time_point t0() const noexcept { return t0_; }
	// One change to the key at index AT_KEY of the corpus, arrived at NOW.
	void arrive(std::size_t at_key, Clock::time_point now) {
		const auto keys = corpus_.keys_per_space();
		const auto i = at_key / keys, k = at_key % keys;
		auto &s = state_[i];
		std::lock_guard guard(s.mutex);
		++s.next;
		s.pending.records.push_back({corpus_.record_key(i, k), s.next, definition(k, int(s.next))});
		s.pending.arrived.push_back(now);
	}
	void notify() { wake_.notify_all(); }
	// Passes finished since the last call: the root entries the loop has to propose.
	std::uint64_t take_finished() {
		const auto now = finished_.load();
		const auto out = now - taken_;
		taken_ = now;
		return out;
	}
	// Builds what is pending, then stops the workers.
	void finish() {
		stopping_ = true;
		wake_.notify_all();
		for (auto &thread : pool_)
			thread.join();
		pool_.clear();
	}
	Totals totals() const {
		Totals out;
		for (const auto &st : stats_) {
			out.samples.insert(out.samples.end(), st.samples.begin(), st.samples.end());
			out.pass_ms.insert(out.pass_ms.end(), st.pass_ms.begin(), st.pass_ms.end());
			out.pass_changes.insert(out.pass_changes.end(), st.pass_changes.begin(), st.pass_changes.end());
			out.cold_ms.insert(out.cold_ms.end(), st.cold_ms.begin(), st.cold_ms.end());
			out.cpu_ms += st.cpu_ms;
			out.passes += st.passes;
			out.relocations += st.relocations;
			out.written += st.written;
		}
		for (const auto &s : state_) {
			out.cold_done_ms = std::max(out.cold_done_ms, s.built_ms);
			out.caught_up_ms = std::max(out.caught_up_ms, s.first_pass_ms);
		}
		return out;
	}

  private:
	struct Pending {
		std::vector<Record> records;
		std::vector<Clock::time_point> arrived;
	};
	struct Space {
		std::mutex mutex;
		Pending pending;
		std::uint64_t next = 0; // the last revision assigned
		bool busy = false, built = false;
		Clock::time_point last{};				// the start of its last pass
		double built_ms = -1, first_pass_ms = -1; // cold build done; first pass after it done
	};
	double ms_since_t0(Clock::time_point t) const { return std::chrono::duration<double, std::milli>(t - t0_).count(); }
	void work(Totals &out) {
		const auto spaces = state_.size();
		for (;;) {
			// Round robin from a shared cursor: the next namespace that is idle and has work.
			std::optional<std::size_t> pick;
			bool any_pending = false;
			{
				std::unique_lock guard(lock_);
				const auto now = Clock::now();
				for (std::size_t n = 0; n < spaces && !pick; ++n) {
					const auto i = (cursor_ + n) % spaces;
					std::lock_guard space_guard(state_[i].mutex);
					const bool work = !state_[i].built || !state_[i].pending.records.empty();
					any_pending |= work;
					if (work && !state_[i].busy && (!state_[i].built || stopping_ || now - state_[i].last >= interval_)) {
						state_[i].busy = true;
						pick = i;
						cursor_ = i + 1;
					}
				}
				if (!pick) {
					if (stopping_ && !any_pending)
						return;
					wake_.wait_for(guard, std::chrono::milliseconds(2));
					continue;
				}
			}
			const auto i = *pick;
			auto &s = state_[i];
			bool built;
			{
				std::lock_guard guard(s.mutex);
				built = s.built;
			}
			if (!built) {
				const auto begin = Clock::now();
				const auto r = corpus_.cold_space(i);
				out.cpu_ms += r.cpu_ms;
				out.cold_ms.push_back(since_ms(begin));
				std::lock_guard guard(s.mutex);
				s.built = true;
				s.built_ms = ms_since_t0(Clock::now());
				s.busy = false;
				continue;
			}
			Pending taken;
			const auto begin = Clock::now();
			{
				std::lock_guard guard(s.mutex);
				std::swap(taken, s.pending);
				s.last = begin;
			}
			const auto r = corpus_.advance(i, std::move(taken.records));
			const auto done = Clock::now();
			out.cpu_ms += r.cpu_ms;
			out.relocations += r.relocations;
			out.written += r.written;
			++out.passes;
			++finished_;
			out.pass_ms.push_back(since_ms(begin));
			out.pass_changes.push_back(double(taken.arrived.size()));
			for (const auto &arrived : taken.arrived)
				out.samples.push_back(
					{ms_since_t0(arrived) / 1e3, std::chrono::duration<double, std::milli>(done - arrived).count()});
			std::lock_guard guard(s.mutex);
			if (s.first_pass_ms < 0)
				s.first_pass_ms = ms_since_t0(done);
			s.busy = false;
		}
	}
	Corpus &corpus_;
	std::chrono::milliseconds interval_;
	std::vector<Space> state_;
	std::vector<Totals> stats_;
	Clock::time_point t0_;
	std::mutex lock_;
	std::condition_variable wake_;
	std::size_t cursor_ = 0;
	std::atomic<bool> stopping_{false};
	std::atomic<std::uint64_t> finished_{0};
	std::uint64_t taken_ = 0; // the loop's, read only by take_finished()
	std::vector<std::thread> pool_;
};

// Feeds POOL changes uniform over the corpus in 5 ms ticks from its t0: RATE/s, BURST/s from second AT
// for SPAN seconds, until second SECONDS or until STOP is set. Returns the count.
std::uint64_t feed(PassPool &pool, std::size_t total_keys, double rate, double seconds, double burst, double at,
				   double span, const std::atomic<bool> &stop) {
	std::mt19937_64 random(20261123);
	std::uniform_int_distribution<std::size_t> pick(0, total_keys - 1);
	double due = 0;
	std::uint64_t arrived = 0;
	auto tick = pool.t0();
	auto seconds_at = [&](Clock::time_point t) { return std::chrono::duration<double>(t - pool.t0()).count(); };
	while (seconds_at(tick) < seconds && !stop) {
		tick += std::chrono::milliseconds(5);
		std::this_thread::sleep_until(tick);
		const auto s = seconds_at(tick);
		due += 0.005 * (s >= at && s < at + span ? burst : rate);
		const auto now = Clock::now();
		for (; due >= 1; due -= 1, ++arrived)
			pool.arrive(pick(random), now);
		pool.notify();
	}
	return arrived;
}

int commit(int argc, char **argv) {
	check(argc >= 7, "usage: commit DIR NAMESPACES KEYS GROUPS ARM...");
	const std::filesystem::path dir = argv[2];
	const std::size_t spaces = std::stoull(argv[3]), keys = std::stoull(argv[4]), groups = std::stoull(argv[5]);
	constexpr std::size_t authority_keys = 100000;
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir / "pages");
	std::filesystem::create_directories(dir / "pages-write");
	// The arms that write and those that do not keep separate corpora, built alike.
	Corpus memory(dir / "pages", spaces, keys, false), stored(dir / "pages-write", spaces, keys, true);
	const auto cold = memory.cold();
	print_seal("cold", spaces, cold);
	print_seal("cold-write", spaces, stored.cold());
	auto owned = std::make_unique<Authority>(dir / "authority");
	auto &authority = *owned;
	authority.load(authority_keys);
	std::printf("{\"step\":\"ready\",\"authority_keys\":%zu,\"seals\":%llu,\"heap_mb\":%.1f,\"resident_mb\":%.1f}\n",
				authority_keys, (unsigned long long)authority.seals(), heap_mb(), resident_mb());
	std::fflush(stdout);
	std::mt19937_64 random(11);
	int salt = 0;
	for (int i = 6; i < argc; ++i) {
		const std::string arm = argv[i];
		check(arm == "none" || arm == "thread" || arm == "write" || arm == "inline", "arm: none, thread, write, inline");
		std::optional<Builder> builder;
		if (arm == "thread" || arm == "write")
			builder.emplace(arm == "write" ? stored : memory);
		std::mt19937_64 inline_random(13);
		SealResult inline_total;
		auto seen = authority.seals();
		authority.after_service = [&] {
			const auto now = authority.seals();
			for (; seen < now; ++seen) {
				if (builder)
					builder->post();
				else if (arm == "inline") {
					const auto s = memory.seal(a::LayoutPolicy{}.seal_transactions, inline_random, ++salt);
					inline_total.cpu_ms += s.cpu_ms;
					inline_total.wall_ms += s.wall_ms;
					inline_total.builds += s.builds;
					inline_total.written += s.written;
				}
			}
		};
		authority.reset_gap();
		const auto seals0 = authority.seals();
		const auto disk0 = disk_written();
		const auto cpu0 = process_cpu_ms();
		const auto start = Clock::now();
		std::vector<double> latency;
		for (std::size_t g = 0; g < groups; ++g) {
			const auto t = Clock::now();
			authority.group(random, authority_keys, ++salt);
			latency.push_back(since_ms(t));
		}
		const auto run_ms = since_ms(start);
		const auto drain_ms = builder ? builder->drain() : 0.0;
		const auto cpu = process_cpu_ms() - cpu0;
		const auto written = disk_written() - disk0;
		authority.after_service = nullptr;
		const auto build = builder ? builder->total() : inline_total;
		const auto lag = builder ? builder->lag_ms() : std::vector<double>{};
		std::printf("{\"step\":\"run\",\"arm\":\"%s\",\"round\":%d,\"groups\":%zu,\"transactions\":%zu,\"run_ms\":%.1f,"
					"\"tx_per_s\":%.1f,\"group_ms_p50\":%.3f,\"group_ms_p90\":%.3f,\"group_ms_p99\":%.3f,"
					"\"group_ms_max\":%.3f,\"worst_service_gap_ms\":%.3f,\"seals\":%llu,\"builds\":%llu,"
					"\"build_cpu_ms\":%.1f,\"build_wall_ms\":%.1f,\"lag_ms_max\":%.1f,\"build_ms_max\":%.1f,\"drain_ms\":%.1f,"
					"\"process_cpu_ms\":%.1f,\"disk_written\":%llu,\"pages_written\":%llu,\"heap_mb\":%.1f,"
					"\"resident_mb\":%.1f,\"peak_mb\":%.1f}\n",
					arm.c_str(), i - 5, groups, groups * 64, run_ms, 1000.0 * double(groups * 64) / run_ms,
					percentile(latency, 0.5), percentile(latency, 0.9), percentile(latency, 0.99),
					percentile(latency, 1), authority.worst_gap_ms, (unsigned long long)(authority.seals() - seals0),
					(unsigned long long)build.builds, build.cpu_ms, build.wall_ms, percentile(lag, 1),
					percentile(builder ? builder->build_ms() : std::vector<double>{}, 1), drain_ms, cpu,
					(unsigned long long)written, (unsigned long long)build.written, heap_mb(), resident_mb(), peak_mb());
		std::fflush(stdout);
	}
	owned.reset();
	std::filesystem::remove_all(dir);
	return 0;
}

// The commit path beside a builder held at a fixed seal rate (rc3: 2,000 / 2,048 = 0.98 seals/s),
// whatever the authority's own rate, with arms interleaved for paired deltas against the none blocks
// beside them. Each round is ten blocks of GROUPS groups, none alternating with thread, pool (one
// build per namespace on NAMESPACES threads), write, pass4 (option 2's pool: four threads, a pass per
// namespace every 250 ms, over changes arriving at SEAL_RATE x 2,048/s), pass4r (pass4 whose root
// entries, one per pass, ride the next group) and aa (a none labelled apart: the A/A); those six
// rotate by one per round.
int paced(int argc, char **argv) {
	check(argc == 8, "usage: paced DIR NAMESPACES KEYS SEAL_RATE ROUNDS GROUPS");
	const std::filesystem::path dir = argv[2];
	const std::size_t spaces = std::stoull(argv[3]), keys = std::stoull(argv[4]), rounds = std::stoull(argv[6]),
					  groups = std::stoull(argv[7]);
	const double seal_rate = std::stod(argv[5]);
	constexpr std::size_t authority_keys = 100000;
	const auto setup = Clock::now();
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir / "pages");
	std::filesystem::create_directories(dir / "pages-write");
	Corpus memory(dir / "pages", spaces, keys, false), stored(dir / "pages-write", spaces, keys, true);
	print_seal("cold", spaces, memory.cold());
	print_seal("cold-write", spaces, stored.cold());
	auto owned = std::make_unique<Authority>(dir / "authority");
	auto &authority = *owned;
	authority.load(authority_keys);
	std::printf("{\"step\":\"ready\",\"authority_keys\":%zu,\"seal_rate\":%.2f,\"setup_ms\":%.1f,\"heap_mb\":%.1f,"
				"\"resident_mb\":%.1f}\n",
				authority_keys, seal_rate, since_ms(setup), heap_mb(), resident_mb());
	std::fflush(stdout);
	std::mt19937_64 random(11);
	int salt = 0;
	const std::array<const char *, 6> others{"thread", "pool", "write", "pass4", "pass4r", "aa"};
	constexpr std::size_t pass_threads = 4;
	constexpr std::chrono::milliseconds pass_interval{250};
	const double change_rate = seal_rate * double(a::LayoutPolicy{}.seal_transactions);
	std::map<std::string, std::vector<double>> pooled, pooled_lag;
	const auto timed = Clock::now();
	for (std::size_t round = 0; round < rounds; ++round)
		for (std::size_t slot = 0; slot < 2 * others.size(); ++slot) {
			const std::string arm = slot % 2 == 0 ? "none" : others[(slot / 2 + round) % others.size()];
			std::optional<Builder> builder;
			if (arm == "thread" || arm == "write")
				builder.emplace(arm == "write" ? stored : memory);
			else if (arm == "pool")
				builder.emplace(memory, spaces);
			std::optional<PassPool> passes;
			std::atomic<bool> stop_feed{false};
			std::thread feeder;
			if (arm == "pass4" || arm == "pass4r") {
				passes.emplace(memory, pass_threads, pass_interval, false);
				feeder = std::thread([&] {
					feed(*passes, memory.keys(), change_rate, 1e9, 0, 0, 0, stop_feed);
				});
			}
			// Every arm counts the seals a builder would take; only builder arms post them.
			std::uint64_t posted = 0;
			authority.reset_gap();
			const auto disk0 = disk_written();
			const auto cpu0 = process_cpu_ms();
			const auto start = Clock::now();
			std::vector<double> latency;
			std::uint64_t root_entries = 0, root_backlog = 0;
			for (std::size_t g = 0; g < groups; ++g) {
				const auto t = Clock::now();
				// A transaction holds at most 128 mutations: the rest ride the next group.
				root_backlog += arm == "pass4r" ? passes->take_finished() : 0;
				const auto roots = std::min<std::uint64_t>(root_backlog, 120);
				root_backlog -= roots;
				root_entries += roots;
				authority.group(random, authority_keys, ++salt, roots);
				latency.push_back(since_ms(t));
				// Rounded to the nearest seal, so a block posts RATE x its length on average.
				for (const auto due = std::uint64_t(seal_rate * since_ms(start) / 1e3 + 0.5); posted < due; ++posted)
					if (builder)
						builder->post();
			}
			const auto run_ms = since_ms(start);
			const auto drained = Clock::now();
			SealResult build;
			std::vector<double> lag, build_ms;
			if (builder) {
				builder->drain();
				build = builder->total();
				lag = builder->lag_ms();
				build_ms = builder->build_ms();
			} else if (passes) {
				stop_feed = true;
				feeder.join();
				passes->finish();
				const auto totals = passes->totals();
				build.cpu_ms = totals.cpu_ms;
				build.builds = totals.passes;
				build.written = totals.written;
				for (const auto &sample : totals.samples)
					lag.push_back(sample.lag_ms);
				build_ms = totals.pass_ms;
			}
			const auto drain_ms = since_ms(drained);
			auto &all = pooled[arm];
			all.insert(all.end(), latency.begin(), latency.end());
			auto &all_lag = pooled_lag[arm];
			all_lag.insert(all_lag.end(), lag.begin(), lag.end());
			std::printf("{\"step\":\"block\",\"round\":%zu,\"slot\":%zu,\"arm\":\"%s\",\"groups\":%zu,\"run_ms\":%.1f,"
						"\"tx_per_s\":%.1f,\"group_ms_p50\":%.3f,\"group_ms_p90\":%.3f,\"group_ms_max\":%.3f,"
						"\"worst_service_gap_ms\":%.3f,\"seals\":%llu,\"builds\":%llu,\"build_cpu_ms\":%.1f,"
						"\"lag_ms_p50\":%.1f,\"lag_ms_p99\":%.1f,\"lag_ms_max\":%.1f,\"build_ms_max\":%.1f,\"drain_ms\":%.1f,"
						"\"process_cpu_ms\":%.1f,\"disk_written\":%llu,"
						"\"pages_written\":%llu,\"root_entries\":%llu,\"resident_mb\":%.1f}\n",
						round, slot, arm.c_str(), groups, run_ms, 1000.0 * double(groups * 64) / run_ms,
						percentile(latency, 0.5), percentile(latency, 0.9), percentile(latency, 1), authority.worst_gap_ms,
						(unsigned long long)posted, (unsigned long long)build.builds, build.cpu_ms, percentile(lag, 0.5),
						percentile(lag, 0.99), percentile(lag, 1), percentile(build_ms, 1), drain_ms, process_cpu_ms() - cpu0, (unsigned long long)(disk_written() - disk0),
						(unsigned long long)build.written, (unsigned long long)root_entries, resident_mb());
			std::fflush(stdout);
		}
	const auto timed_ms = since_ms(timed);
	for (const auto &[arm, latency] : pooled) {
		const auto &lag = pooled_lag[arm];
		std::printf("{\"step\":\"pooled\",\"arm\":\"%s\",\"groups\":%zu,\"group_ms_p50\":%.3f,\"group_ms_p99\":%.3f,"
					"\"group_ms_max\":%.3f,\"lag_samples\":%zu,\"lag_ms_p50\":%.1f,\"lag_ms_p99\":%.1f,\"lag_ms_max\":%.1f}\n",
					arm.c_str(), latency.size(), percentile(latency, 0.5), percentile(latency, 0.99),
					percentile(latency, 1), lag.size(), percentile(lag, 0.5), percentile(lag, 0.99), percentile(lag, 1));
	}
	const auto teardown = Clock::now();
	owned.reset();
	std::filesystem::remove_all(dir);
	std::printf("{\"step\":\"timing\",\"timed_ms\":%.1f,\"teardown_ms\":%.1f,\"peak_mb\":%.1f}\n", timed_ms,
				since_ms(teardown), peak_mb());
	return 0;
}

// Root lag at an open-loop write rate: changes arrive at RATE/s (BURST/s from second AT for SPAN
// seconds), uniform over the corpus, for SECONDS. A pool of THREADS builds each namespace's pending
// changes in one pass (coalescing), at most one pass per namespace per INTERVAL ms, and publishes its
// root at the position the pass reached; a change's lag is from its arrival to that root. START warm
// builds every namespace before the clock starts; START cold is a new leader: the pool cold-builds
// every namespace while changes arrive.
int lag(int argc, char **argv) {
	check(argc == 13, "usage: lag DIR NAMESPACES KEYS THREADS warm|cold INTERVAL RATE SECONDS BURST AT SPAN");
	const std::filesystem::path dir = argv[2];
	const std::size_t spaces = std::stoull(argv[3]), keys = std::stoull(argv[4]), threads = std::stoull(argv[5]);
	const std::string start_mode = argv[6];
	const auto interval = std::chrono::milliseconds(std::stoll(argv[7]));
	const double rate = std::stod(argv[8]), seconds = std::stod(argv[9]), burst = std::stod(argv[10]),
				 burst_at = std::stod(argv[11]), burst_span = std::stod(argv[12]);
	check(start_mode == "warm" || start_mode == "cold", "START: warm or cold");
	const bool cold = start_mode == "cold";
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir);
	Corpus corpus(dir, spaces, keys, false);
	const auto setup = Clock::now();
	if (!cold) {
		std::vector<std::thread> pool;
		for (std::size_t t = 0; t < threads; ++t)
			pool.emplace_back([&, t] {
				for (std::size_t i = t; i < spaces; i += threads)
					corpus.cold_space(i);
			});
		for (auto &thread : pool)
			thread.join();
	}
	std::printf("{\"step\":\"ready\",\"mode\":\"lag\",\"namespaces\":%zu,\"keys\":%zu,\"threads\":%zu,\"start\":\"%s\",\"interval_ms\":%lld,"
				"\"rate\":%.0f,\"seconds\":%.0f,\"burst\":%.0f,\"burst_at\":%.0f,\"burst_span\":%.0f,\"setup_ms\":%.1f,"
				"\"resident_mb\":%.1f,\"peak_mb\":%.1f}\n",
				spaces, keys, threads, start_mode.c_str(), (long long)interval.count(), rate, seconds, burst, burst_at, burst_span, since_ms(setup),
				resident_mb(), peak_mb());
	std::fflush(stdout);
	const auto cpu0 = process_cpu_ms();
	PassPool pool(corpus, threads, interval, cold);
	const std::atomic<bool> never{false};
	const auto arrived = feed(pool, spaces * keys, rate, seconds, burst, burst_at, burst_span, never);
	const auto arrivals_ms = since_ms(pool.t0());
	pool.finish();
	const auto total_ms = since_ms(pool.t0());
	const auto cpu = process_cpu_ms() - cpu0;
	const auto merged = pool.totals();
	// Per second of arrival: changes, lag p50, p99 and max.
	std::vector<std::vector<double>> by_second(std::size_t(seconds) + 1);
	std::vector<double> all;
	for (const auto &sample : merged.samples) {
		by_second[std::min(by_second.size() - 1, std::size_t(sample.at_s))].push_back(sample.lag_ms);
		all.push_back(sample.lag_ms);
	}
	for (std::size_t sec = 0; sec < by_second.size(); ++sec) {
		if (by_second[sec].empty())
			continue;
		std::printf("{\"step\":\"second\",\"at\":%zu,\"changes\":%zu,\"lag_ms_p50\":%.1f,\"lag_ms_p99\":%.1f,"
					"\"lag_ms_max\":%.1f}\n",
					sec, by_second[sec].size(), percentile(by_second[sec], 0.5), percentile(by_second[sec], 0.99),
					percentile(by_second[sec], 1));
	}
	std::printf("{\"step\":\"summary\",\"mode\":\"lag\",\"threads\":%zu,\"start\":\"%s\",\"interval_ms\":%lld,\"arrived\":%llu,"
				"\"arrivals_ms\":%.1f,\"total_ms\":%.1f,\"lag_ms_p50\":%.1f,\"lag_ms_p99\":%.1f,\"lag_ms_max\":%.1f,"
				"\"passes\":%llu,\"pass_ms_p50\":%.1f,\"pass_ms_max\":%.1f,\"pass_changes_p50\":%.0f,"
				"\"pass_changes_max\":%.0f,\"relocations\":%llu,\"build_cpu_ms\":%.1f,\"process_cpu_ms\":%.1f,"
				"\"cores\":%.2f,\"build_cores\":%.2f,\"cold_ms_p50\":%.1f,\"cold_ms_max\":%.1f,\"cold_done_ms\":%.1f,\"caught_up_ms\":%.1f,"
				"\"page_bytes\":%llu,\"resident_mb\":%.1f,\"peak_mb\":%.1f}\n",
				threads, start_mode.c_str(), (long long)interval.count(), (unsigned long long)arrived, arrivals_ms, total_ms, percentile(all, 0.5),
				percentile(all, 0.99), percentile(all, 1), (unsigned long long)merged.passes,
				percentile(merged.pass_ms, 0.5), percentile(merged.pass_ms, 1), percentile(merged.pass_changes, 0.5),
				percentile(merged.pass_changes, 1), (unsigned long long)merged.relocations, merged.cpu_ms, cpu,
				cpu / total_ms, merged.cpu_ms / total_ms, percentile(merged.cold_ms, 0.5), percentile(merged.cold_ms, 1), cold ? merged.cold_done_ms : 0.0,
				cold ? merged.caught_up_ms : 0.0, (unsigned long long)corpus.page_bytes(), resident_mb(), peak_mb());
	std::fflush(stdout);
	std::filesystem::remove_all(dir);
	return 0;
}
} // namespace

int main(int argc, char **argv) {
	try {
		check(argc >= 2, "usage: authority_pages_bench seals|commit ...");
		const std::string_view mode = argv[1];
		if (mode == "seals")
			return seals(argc, argv);
		if (mode == "commit")
			return commit(argc, argv);
		if (mode == "paced")
			return paced(argc, argv);
		if (mode == "lag")
			return lag(argc, argv);
		throw std::runtime_error("mode: seals, commit, paced or lag");
	} catch (const std::exception &error) {
		std::fprintf(stderr, "authority_pages_bench: %s\n", error.what());
		return 1;
	}
}
