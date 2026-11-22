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
// Every line printed is one JSON object.
#include "detent_relay/page_copy.hh"
#include "node.hh"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <libproc.h>
#include <mach/mach.h>
#include <malloc/malloc.h>
#include <mutex>
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
	double cpu_ms = 0, wall_ms = 0, encode_ms = 0, sync_ms = 0, digest_ms = 0;
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
	// One seal of `changes` uniform updates across every namespace, one build per namespace it touches.
	SealResult seal(std::size_t changes, std::mt19937_64 &random, int salt) {
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
		const auto digest = Clock::now();
		for (std::size_t i = 0; i < spaces_.size(); ++i)
			for (const auto &record : touched[i]) {
				const auto value = detent::authority::encode_definition(record.value);
				digest_.apply(record.key, record.revision - 1, value, false);
				digest_.apply(record.key, record.revision, value, true);
			}
		out.digest_ms = since_ms(digest);
		for (std::size_t i = 0; i < spaces_.size(); ++i)
			if (!touched[i].empty())
				build(spaces_[i], std::move(touched[i]), false, out);
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
				"\"digest_ms\":%.3f,\"written\":%llu,\"builds\":%llu,\"relocations\":%llu,\"heap_mb\":%.1f,"
				"\"resident_mb\":%.1f,\"peak_mb\":%.1f}\n",
				what, n, s.cpu_ms, s.wall_ms, s.encode_ms, s.sync_ms, s.digest_ms, (unsigned long long)s.written,
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
		digest.push_back(s.digest_ms);
		written += s.written;
		relocations += s.relocations;
	}
	std::printf("{\"step\":\"summary\",\"seals\":%zu,\"changes\":%zu,\"cpu_ms_p50\":%.3f,\"cpu_ms_p90\":%.3f,"
				"\"cpu_ms_max\":%.3f,\"wall_ms_p50\":%.3f,\"wall_ms_max\":%.3f,\"sync_ms_p50\":%.3f,"
				"\"digest_ms_p50\":%.3f,"
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
	// One group: every producer writes one uniform key.
	void group(std::mt19937_64 &random, std::size_t keys, int salt) {
		std::uniform_int_distribution<std::size_t> pick(0, keys - 1);
		for (auto &c : commands_) {
			c.mutations = {{"ns:feature_" + std::to_string(pick(random)), detent::KnobDef{salt}, {}, {}}};
			++c.sequence;
		}
		submit(commands_);
	}
	std::uint64_t seals() const noexcept { return node_->state().seal().ordinal; }
	// Runs after each service call: the inline arm builds there.
	std::function<void()> after_service;
	double worst_gap_ms = 0; // the longest interval between two service calls

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
					for (auto outcome : c->results)
						check(outcome.status == a::Status::Applied, "transaction rejected");
					completed_.erase(c);
					return;
				}
		}
		throw std::runtime_error("durable completion timeout");
	}
	void submit(const std::vector<a::Command> &commands) {
		const auto id = ++request_;
		await(id, [&] { return node_->propose(id, commands); });
	}
	a::NodeOptions options_;
	std::unique_ptr<a::Node> node_;
	std::vector<a::Command> commands_;
	std::vector<a::Completion> completed_;
	std::uint64_t tick_ = 0, request_ = 0;
	std::optional<Clock::time_point> last_;
};

// The builder thread: one seal per authority seal, in order, off the service loop.
class Builder {
  public:
	explicit Builder(Corpus &corpus) : corpus_(corpus), thread_([this] { run(); }) {}
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
			++wanted_;
		}
		wake_.notify_all();
	}
	// Waits for the posted seals; returns the wait's wall ms.
	double drain() {
		const auto start = Clock::now();
		std::unique_lock lock(mutex_);
		idle_.wait(lock, [&] { return done_ == wanted_; });
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

  private:
	void run() {
		std::mt19937_64 random(7);
		std::unique_lock lock(mutex_);
		for (;;) {
			wake_.wait(lock, [&] { return stop_ || done_ < wanted_; });
			if (stop_)
				return;
			const auto posted = Clock::now();
			lock.unlock();
			const auto s = corpus_.seal(a::LayoutPolicy{}.seal_transactions, random, int(++salt_));
			lock.lock();
			total_.cpu_ms += s.cpu_ms;
			total_.wall_ms += s.wall_ms;
			total_.sync_ms += s.sync_ms;
			total_.written += s.written;
			total_.builds += s.builds;
			total_.relocations += s.relocations;
			lag_.push_back(since_ms(posted));
			++done_;
			idle_.notify_all();
		}
	}
	Corpus &corpus_;
	std::mutex mutex_;
	std::condition_variable wake_, idle_;
	std::uint64_t wanted_ = 0, done_ = 0, salt_ = 0;
	bool stop_ = false;
	SealResult total_;
	std::vector<double> lag_;
	std::thread thread_;
};

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
		authority.worst_gap_ms = 0;
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
					"\"build_cpu_ms\":%.1f,\"build_wall_ms\":%.1f,\"build_lag_ms_max\":%.1f,\"drain_ms\":%.1f,"
					"\"process_cpu_ms\":%.1f,\"disk_written\":%llu,\"pages_written\":%llu,\"heap_mb\":%.1f,"
					"\"resident_mb\":%.1f,\"peak_mb\":%.1f}\n",
					arm.c_str(), i - 5, groups, groups * 64, run_ms, 1000.0 * double(groups * 64) / run_ms,
					percentile(latency, 0.5), percentile(latency, 0.9), percentile(latency, 0.99),
					percentile(latency, 1), authority.worst_gap_ms, (unsigned long long)(authority.seals() - seals0),
					(unsigned long long)build.builds, build.cpu_ms, build.wall_ms, percentile(lag, 1), drain_ms, cpu,
					(unsigned long long)written, (unsigned long long)build.written, heap_mb(), resident_mb(), peak_mb());
		std::fflush(stdout);
	}
	owned.reset();
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
		throw std::runtime_error("mode: seals or commit");
	} catch (const std::exception &error) {
		std::fprintf(stderr, "authority_pages_bench: %s\n", error.what());
		return 1;
	}
}
