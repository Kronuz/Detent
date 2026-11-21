// DTPAGE03 publication artifacts, and their publication through DiskStore: the grouped sync order
// (D11), sync counters that match completion's sync seam, and T7's power cuts over Dense generations.
#include "applied_delta_fixture.hh"
#include "live_record.hh"
#include "managed_recovery.hh"
#include "paged_delta_plan.hh"
#include "paged_disk_fixture.hh"
#include "paged_publication_artifacts.hh"
#include "reader_metadata.hh"
#include "store_core.hh"
#include <chrono>
#include <completion/posix.h>
#include <fcntl.h>
#include <iterator>
#include <map>
#include <set>
#include <thread>
template <class Error, class Call> void rejects(Call call) {
	try {
		call();
	} catch (const Error &) {
		return;
	}
	throw std::runtime_error("invalid paged artifact operation accepted");
}
template <class Test> void eventually(Test test) {
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (!test()) {
		require(std::chrono::steady_clock::now() < deadline);
		std::this_thread::yield();
	}
}

// T7 over DTPAGE03 (namespace-delivery D8, D11): the disk is modelled from completion's sync seam as in
// tests/detent_agent/test_agent_store_crash.cc. A power cut or a kill after every primitive of a store's
// startup, a Dense publication and its collection must leave a store that opens Ready, selects the base or
// the published generation (the published one once served), reads its exact state, and publishes again;
// each restart's publication over a cut's or a kill's leftovers is itself cut at every step, and a cut
// after a kill drops whatever the kill left unsynced.
namespace power_cut {
namespace c = kronuz::io::completion;
namespace x = l::detail;
using Key = std::pair<dev_t, ino_t>;
struct Inode {
	std::string staged, durable; // bytes an fsync staged, and bytes a device flush made durable
};
struct Disk {
	std::map<std::string, std::pair<int, std::string>> live; // name -> (inode, bytes)
	std::map<std::string, int> staged, durable;              // directory entries
	bool directory_staged = false;
	std::vector<Inode> inodes;
	bool served = false; // the publication settled: its generation must survive
};
struct Seam {
	std::uint64_t files = 0, directories = 0, flushes = 0;
};
enum class Entries { Durable, All };
enum class Data { Truncate, Zeros };
struct Recorder {
	std::filesystem::path root;
	Key directory{};
	std::map<Key, int> ids;
	Disk disk;
	std::vector<Disk> events;
	std::vector<std::string> labels, trace; // trace: labels, and each fsynced file's name
	Seam seam;
} *g = nullptr;

Key key(const struct stat &status) { return {status.st_dev, status.st_ino}; }
std::string slurp(const std::filesystem::path &path) {
	std::ifstream in(path, std::ios::binary);
	return {std::istreambuf_iterator<char>(in), {}};
}
void scan() {
	std::map<std::string, std::pair<int, std::string>> live;
	for (const auto &entry : std::filesystem::directory_iterator(g->root)) {
		const auto name = entry.path().filename().string();
		if (name == "owner.lock")
			continue;
		struct stat status{};
		require(::lstat(entry.path().c_str(), &status) == 0);
		auto found = g->ids.find(key(status));
		// Every file the store creates starts as a new `.tmp-` name: a fresh inode.
		if (found == g->ids.end() || (name.starts_with(".tmp-") && !g->disk.live.contains(name))) {
			g->disk.inodes.emplace_back();
			found = g->ids.insert_or_assign(key(status), int(g->disk.inodes.size()) - 1).first;
		}
		// `change` words never decide a recovery; the model keeps its length alone.
		live[name] = {found->second,
					  name == "change" ? std::string(std::size_t(status.st_size), '\0') : slurp(entry.path())};
	}
	g->disk.live = std::move(live);
}
void note(std::string label) {
	scan();
	g->events.push_back(g->disk);
	g->trace.push_back(label);
	g->labels.push_back(std::move(label));
}
void primitive(void *, const char *label) noexcept {
	if (g)
		note(label);
}
int record_fsync(int fd) {
	if (!g)
		return 0;
	struct stat status{};
	require(::fstat(fd, &status) == 0);
	scan();
	if (key(status) == g->directory) {
		g->disk.staged.clear();
		for (const auto &[name, entry] : g->disk.live)
			g->disk.staged[name] = entry.first;
		g->disk.directory_staged = true;
#ifndef __APPLE__
		g->disk.durable = g->disk.staged;
#endif
		++g->seam.directories;
		note("fsync directory");
		return 0;
	}
	const auto found = g->ids.find(key(status));
	require(found != g->ids.end());
	std::string bytes(std::size_t(status.st_size), '\0');
	require(::pread(fd, bytes.data(), bytes.size(), 0) == ssize_t(bytes.size()));
	auto &inode = g->disk.inodes[found->second];
	inode.staged = bytes;
#ifndef __APPLE__
	inode.durable = bytes;
#endif
	++g->seam.files;
	for (const auto &[name, entry] : g->disk.live)
		if (entry.first == found->second)
			g->trace.push_back("file " + name);
	note("fsync file");
	return 0;
}
int skip_fsync(int) { return 0; }
#ifdef __APPLE__
int record_fcntl(int, int command) {
	if (!g)
		return 0;
	require(command == F_FULLFSYNC || command == F_BARRIERFSYNC);
	for (auto &inode : g->disk.inodes)
		inode.durable = inode.staged;
	if (g->disk.directory_staged)
		g->disk.durable = g->disk.staged;
	++g->seam.flushes;
	note("device flush");
	return 0;
}
int skip_fcntl(int, int) { return 0; }
void attach(c::PosixIO &io) noexcept {
	if (g)
		io.replace_sync_calls(&record_fsync, &record_fcntl);
	else
		io.replace_sync_calls(&skip_fsync, &skip_fcntl);
}
#elif defined(__linux__)
int skip_writeback(int) { return 0; }
void attach(c::PosixIO &io) noexcept { io.replace_sync_calls(g ? &record_fsync : &skip_fsync, &skip_writeback); }
#else
void attach(c::PosixIO &io) noexcept { io.replace_sync_calls(g ? &record_fsync : &skip_fsync); }
#endif
// The counters the seam implies: on macOS a device flush is its own call, elsewhere every fsync flushes.
void matches(const l::SyncCounters &counters, const Seam &seam, std::uint64_t groups) {
#ifdef __APPLE__
	const auto flushes = seam.flushes;
#else
	const auto flushes = seam.files + seam.directories;
#endif
	require(counters.files == seam.files && counters.directory_syncs == seam.directories &&
			counters.device_flushes == flushes && counters.groups == groups);
}
Seam operator-(const Seam &a, const Seam &b) {
	return {a.files - b.files, a.directories - b.directories, a.flushes - b.flushes};
}

void put(const std::filesystem::path &path, std::string_view bytes) {
	write(path, std::span<const char>(bytes.data(), bytes.size()));
}
// What a power cut leaves: durable names (or every name, when metadata reached the disk first) holding
// only durable bytes (cut short, or zero-filled to their size).
std::map<std::string, std::string> after_power_cut(const Disk &disk, Entries entries, Data data) {
	std::map<std::string, int> names = disk.durable;
	if (entries == Entries::All)
		for (const auto &[name, entry] : disk.live)
			names[name] = entry.first;
	std::map<int, std::size_t> sizes;
	for (const auto &[name, entry] : disk.live)
		sizes[entry.first] = entry.second.size();
	std::map<std::string, std::string> files;
	for (const auto &[name, id] : names) {
		auto bytes = disk.inodes[id].durable;
		if (data == Data::Zeros && sizes.contains(id) && sizes[id] > bytes.size())
			bytes.resize(sizes[id], '\0');
		files[name] = std::move(bytes);
	}
	return files;
}
// A process kill: every name and byte stays.
std::map<std::string, std::string> after_kill(const Disk &disk) {
	std::map<std::string, std::string> files;
	for (const auto &[name, entry] : disk.live)
		files[name] = entry.second;
	return files;
}
void materialize(const std::filesystem::path &root, const std::map<std::string, std::string> &files) {
	std::filesystem::remove_all(root);
	std::filesystem::create_directories(root);
	require(::chmod(root.c_str(), 0700) == 0);
	put(root / "owner.lock", {});
	for (const auto &[name, bytes] : files)
		put(root / name, bytes);
}
// Materializes a kill's disk for a restart and returns its model: what was unsynced stays unsynced. Names
// linked to one inode become copies, each with that inode's sync state.
Disk materialize_killed(const std::filesystem::path &root, Disk disk) {
	materialize(root, after_kill(disk));
	std::set<int> taken;
	for (auto &[name, entry] : disk.live) {
		if (taken.insert(entry.first).second)
			continue;
		auto inode = disk.inodes[entry.first];
		disk.inodes.push_back(std::move(inode));
		const int copy = int(disk.inodes.size()) - 1;
		for (auto *entries : {&disk.staged, &disk.durable})
			if (const auto at = entries->find(name); at != entries->end() && at->second == entry.first)
				at->second = copy;
		entry.first = copy;
	}
	return disk;
}
std::string fingerprint(const std::map<std::string, std::string> &files) {
	std::string out;
	for (const auto &[name, bytes] : files) {
		if (name.starts_with(".tmp-"))
			continue;
		const auto sum = trusted_records::sha(bytes);
		out.append(name).push_back('\0');
		out.append(sum.data(), sum.size());
	}
	return out;
}
// A kill's disk up to `.tmp-` names, which startup removes unread: its names, and what is staged and
// durable of each.
std::string kill_key(const Disk &disk) {
	std::map<std::string, std::string> live, staged, durable, inodes;
	for (const auto &[name, entry] : disk.live) {
		live[name] = entry.second;
		inodes[name] = disk.inodes[entry.first].staged + "|" + disk.inodes[entry.first].durable;
	}
	for (const auto &[name, id] : disk.staged)
		staged[name] = disk.inodes[id].staged;
	for (const auto &[name, id] : disk.durable)
		durable[name] = disk.inodes[id].durable;
	return fingerprint(live) + "#" + fingerprint(staged) + "#" + fingerprint(durable) + "#" + fingerprint(inodes) +
		   (disk.directory_staged ? "#s" : "#");
}

// One store's world: its account, retirement runtime and options; settled empty when it ends.
struct World {
	std::shared_ptr<d::StorageLedger> account = std::make_shared<d::StorageLedger>(64 * l::paged::segment_bytes);
	std::shared_ptr<d::StorageLedger> baseline = std::make_shared<d::StorageLedger>(l::paged::segment_bytes);
	d::RetirementRuntime runtime{baseline, 1};
	d::RetirementDomain domain = runtime.domain(account, 64);
	std::uint64_t initial = account->used();
	l::DiskStoreOptions options;
	World() {
		options.limits = {64 * l::paged::segment_bytes, 100, 10};
		options.recovery_limits.retirement_domain = &domain;
		options.recovery_limits.mapping_account = std::make_shared<d::StorageLedger>(16 * l::paged::segment_bytes);
	}
	void settle() {
		domain.close().wait();
		require(account->used() == initial);
	}
};
void drive(const std::shared_ptr<l::DiskStore> &store) {
	for (unsigned turns = 0;; ++turns) {
		require(turns < 100000);
		store->advance();
		auto work = store->work();
		if (!work)
			break;
		work->run();
	}
}
std::shared_ptr<l::DiskStore> open(World &world, const std::filesystem::path &root) {
	auto store = l::make_disk_store(world.domain, root, std::make_shared<int>(1), world.options);
	drive(store);
	return store;
}
void close(std::shared_ptr<l::DiskStore> &store) {
	store->close();
	drive(store);
	require(store->drained());
	store.reset();
}
// The next generation over what the open store selects, planned on its own recovery as the agent plans:
// a delta, or a progress plan reusing a base segment.
struct Planned {
	std::optional<l::RecoveredGeneration> base;
	std::optional<x::PagedPlan> plan;
};
void plan(Planned &out, World &world, const std::filesystem::path &root, const l::DiskStore &store, bool progress) {
	out.base.emplace(x::ManagedRecovery::recover(root, x::ManagedRecovery::capture(store)));
	if (progress) {
		auto delta = std::make_shared<const d::ValidatedDelta>(parse_delta(true, 2, "app", world.account));
		x::PagedPlanBuilder builder(*out.base, delta, world.domain);
		while (!builder.done()) {
			l::ValidationBudget budget;
			builder.step(budget);
		}
		out.plan.emplace(builder.take());
	} else {
		x::PagedDeltaPlanBuilder builder(*out.base, parse_delta(false, 2, "app", world.account), world.domain);
		while (!builder.done()) {
			l::ValidationBudget budget;
			builder.step(budget);
		}
		out.plan.emplace(builder.take());
	}
}
// The two generations a cut may leave selected, and the state each must read.
struct Lineage {
	l::Descriptor base, wanted;
	l::paged::Digest base_state, wanted_state;
	bool progress = false;
};
// D11's order from "begin" to "published": the owner records and the lease, one group; the payloads,
// one group; the table, synced alone; its rename; the directory; `change`. On macOS each sync is one
// device flush.
void grouped(const std::vector<std::string> &trace, const std::set<std::string> &owners,
			 const std::set<std::string> &payloads) {
	static constexpr std::string_view ends[] = {"sync owner records", "sync payloads", "sync table",
												"rename table",       "sync directory", "change"};
	std::vector<std::set<std::string>> files(6);
	std::vector<unsigned> directories(6), flushes(6);
	auto at = std::find(trace.begin(), trace.end(), "begin");
	require(at != trace.end());
	std::size_t stage = 0;
	for (++at; at != trace.end() && *at != "published"; ++at) {
		if (stage < 6 && *at == ends[stage]) {
			++stage;
			continue;
		}
		require(stage < 6);
		if (at->starts_with("file "))
			require(files[stage].insert(at->substr(5)).second);
		directories[stage] += *at == "fsync directory";
		flushes[stage] += *at == "device flush";
	}
	require(stage == 6);
	require(files[0] == owners && files[1] == payloads && files[2].size() == 1 &&
			files[2].begin()->starts_with(".tmp-") && files[3].empty() && files[4].empty() && files[5].empty());
	require(directories == std::vector<unsigned>{1, 1, 0, 0, 1, 0});
#ifdef __APPLE__
	require(flushes == std::vector<unsigned>{1, 1, 1, 0, 1, 0});
#endif
}

struct Counted {
	l::SyncCounters counters;
	Seam seam;
	std::uint64_t groups = 0;
};
struct Run {
	std::vector<Disk> events;
	std::vector<std::string> labels, trace;
	std::set<std::string> owners, payloads; // the publication's two groups
	Lineage lineage;
	std::vector<Counted> synced; // startup's and the publication's counters, and the seam's
	l::SyncCounters total;
	Seam seam;
	// The counters and the order, checked after the run's cuts so a durability defect meets T7 first.
	void check() const {
		for (const auto &each : synced)
			matches(each.counters, each.seam, each.groups);
		require(total.files == seam.files && total.directory_syncs == seam.directories);
		grouped(trace, owners, payloads);
	}
};
// Records a store opening `root` (whose files `start` models), publishing the next generation and
// collecting its base, with the counters `Run::check` compares against the seam.
Run record(const std::filesystem::path &root, Disk start, bool progress) {
	Recorder recorder;
	recorder.root = root;
	struct stat status{};
	require(::stat(root.c_str(), &status) == 0);
	recorder.directory = key(status);
	recorder.disk = std::move(start);
	for (const auto &entry : std::filesystem::directory_iterator(root)) {
		if (entry.path().filename() == "owner.lock")
			continue;
		require(::lstat(entry.path().c_str(), &status) == 0);
		recorder.ids[key(status)] = recorder.disk.live.at(entry.path().filename().string()).first;
	}
	g = &recorder;
	l::AgentStoreTesting::install(&primitive, nullptr);
	struct Uninstall {
		~Uninstall() {
			l::AgentStoreTesting::install(nullptr, nullptr);
			g = nullptr;
		}
	} uninstall;
	note("start");
	Run run;
	World world;
	{
		auto before = recorder.seam;
		auto store = open(world, root);
		require(store->status() == l::StoreStatus::Ready);
		run.synced.push_back({store->sync_stats().startup, recorder.seam - before, 0});
		Planned planned;
		plan(planned, world, root, *store, progress);
		run.lineage = {planned.base->descriptor(), planned.plan->descriptor(), {}, {}, progress};
		run.lineage.base_state = x::PreparedReader::open(root, ::getuid(), world.options.recovery_limits).state_commitment;
		run.lineage.wanted_state = l::paged::state_commitment(planned.plan->root());
		require(store->selection().descriptor == run.lineage.base);
		const auto id = run.lineage.wanted.generation;
		auto name = [&](x::GenerationFile kind) { return std::string(x::GenerationName(id, kind).view()); };
		run.owners = {name(x::GenerationFile::Lease)};
		run.payloads = {name(x::GenerationFile::Image), name(x::GenerationFile::ReaderMetadata),
						name(x::GenerationFile::Live)};
		for (const auto &segment : planned.plan->segments()) {
			const x::SegmentNames names(segment.ownership.segment);
			run.owners.emplace(names.owner_name());
			run.payloads.emplace(names.payload_name());
		}
		before = recorder.seam;
		recorder.trace.emplace_back("begin");
		store->begin(std::move(*planned.plan));
		drive(store);
		require(store->publication_status() == l::DiskPublicationStatus::Published &&
				store->selection().descriptor == run.lineage.wanted);
		run.synced.push_back({store->sync_stats().publication, recorder.seam - before, 2});
		recorder.disk.served = true;
		note("published");
		// Confirmed, the store collects what the new table no longer reaches.
		auto recovered = x::ManagedRecovery::recover(root, x::ManagedRecovery::capture(*store));
		store->confirm_recovered(recovered);
		drive(store);
		require(store->status() == l::StoreStatus::Ready);
		run.total = store->sync_stats().total;
		close(store);
		note("closed");
		run.seam = recorder.seam;
	}
	world.settle();
	run.events = std::move(recorder.events);
	run.labels = std::move(recorder.labels);
	run.trace = std::move(recorder.trace);
	return run;
}

struct Counts {
	std::size_t cases = 0, distinct = 0, republished = 0, nested = 0, killed = 0;
} counts;
std::map<std::string, bool> seen; // each distinct case, and whether it selected the base
struct Outcome {
	bool fresh = false, base = false;
};
// A cut left `files`: the store opens Ready on the base or the published generation (the published one
// once `served`) and reads its state; on the base, it publishes the next generation again. Returns
// whether the case was new and whether it selected the base.
Outcome recovered(const std::filesystem::path &root, const std::map<std::string, std::string> &files, bool served,
				  const Lineage &lineage, bool republish, const std::string &where) {
	++counts.cases;
	const auto [at, fresh] =
		seen.try_emplace(fingerprint(files) + (lineage.progress ? "|p" : "|d") + (republish ? "|r" : ""), false);
	if (!fresh)
		return {false, at->second};
	++counts.distinct;
	try {
		materialize(root, files);
		bool base = false;
		{
			World world;
			auto store = open(world, root);
			require(store->status() == l::StoreStatus::Ready);
			const auto selected = store->selection().descriptor;
			require(selected == lineage.wanted || (!served && selected == lineage.base));
			base = selected == lineage.base;
			{
				const auto prepared = x::PreparedReader::open(root, ::getuid(), world.options.recovery_limits);
				require(prepared.descriptor == *selected &&
						prepared.state_commitment == (base ? lineage.base_state : lineage.wanted_state));
			}
			close(store);
			world.settle();
		}
		if (republish && base) {
			World world;
			{
				auto store = open(world, root);
				Planned planned;
				plan(planned, world, root, *store, lineage.progress);
				require(planned.plan->descriptor() == lineage.wanted);
				store->begin(std::move(*planned.plan));
				drive(store);
				require(store->publication_status() == l::DiskPublicationStatus::Published &&
						store->selection().descriptor == lineage.wanted);
				const auto prepared = x::PreparedReader::open(root, ::getuid(), world.options.recovery_limits);
				require(prepared.state_commitment == lineage.wanted_state);
				close(store);
			}
			world.settle();
			++counts.republished;
		}
		at->second = base;
		return {true, base};
	} catch (const std::exception &error) {
		throw std::runtime_error(where + ": " + error.what());
	}
}
// A store whose files are all durable, as a restart finds them.
Disk durable(const std::filesystem::path &root) {
	Disk disk;
	for (const auto &entry : std::filesystem::directory_iterator(root)) {
		const auto name = entry.path().filename().string();
		if (name == "owner.lock")
			continue;
		const auto bytes = slurp(entry.path());
		disk.inodes.push_back({bytes, bytes});
		disk.live[name] = {int(disk.inodes.size()) - 1, bytes};
		disk.durable[name] = disk.staged[name] = int(disk.inodes.size()) - 1;
	}
	disk.directory_staged = true;
	return disk;
}
// A restart over a cut's or a kill's leftovers (`start` models them) republishes the same generation; that
// run is cut at every step.
void restart(const std::filesystem::path &scratch, const Run &run, const Disk &start, const std::string &where) {
	Run inner;
	try {
		inner = record(scratch / "restart", start, run.lineage.progress);
	} catch (const std::exception &error) {
		throw std::runtime_error(where + ", restart: " + error.what());
	}
	require(inner.lineage.base == run.lineage.base && inner.lineage.wanted == run.lineage.wanted);
	for (std::size_t j = 0; j < inner.events.size(); ++j)
		for (auto entries : {Entries::Durable, Entries::All})
			recovered(scratch / "cut", after_power_cut(inner.events[j], entries, Data::Zeros), inner.events[j].served,
					  inner.lineage, false,
					  where + ", restart, power cut after event " + std::to_string(j) + " (" + inner.labels[j] + ")");
	inner.check();
}
// Cuts and kills after every event of `run`; a new cut or kill on the base restarts and republishes.
void matrix(const std::filesystem::path &scratch, const Run &run, const std::string &name) {
	std::set<std::string> kills;
	for (std::size_t i = 0; i < run.events.size(); ++i) {
		const auto where = name + " after event " + std::to_string(i) + " (" + run.labels[i] + ")";
		const auto &event = run.events[i];
		const auto killed = after_kill(event);
		const auto kill = recovered(scratch / "cut", killed, event.served, run.lineage, true, where + ", kill");
		// A kill whose bytes are all durable restarts as the cuts below do; any other carries its unsynced
		// bytes into the restart, and the restart's cuts drop them.
		const bool unsynced = fingerprint(killed) != fingerprint(after_power_cut(event, Entries::Durable, Data::Truncate));
		if (kill.base && unsynced && kills.insert(kill_key(event)).second) {
			++counts.killed;
			restart(scratch, run, materialize_killed(scratch / "restart", event), where + ", kill");
		}
		for (auto entries : {Entries::Durable, Entries::All})
			for (auto data : {Data::Truncate, Data::Zeros}) {
				const auto files = after_power_cut(event, entries, data);
				const auto cut = recovered(scratch / "cut", files, event.served, run.lineage, true, where + ", power cut");
				if (!cut.fresh || !cut.base)
					continue;
				++counts.nested;
				materialize(scratch / "restart", files);
				restart(scratch, run, durable(scratch / "restart"), where + ", power cut");
			}
	}
}
void run(const std::filesystem::path &scratch, const x::PagedImage &image) {
	x::DiskStoreTesting::install(&attach);
	struct Uninstall {
		~Uninstall() { x::DiskStoreTesting::install(nullptr); }
	} uninstall;
	const auto started = std::chrono::steady_clock::now();
	for (bool progress : {false, true}) {
		const auto root = scratch / "run";
		std::filesystem::remove_all(root);
		install(root, image);
		const auto recorded = record(root, durable(root), progress);
		matrix(scratch, recorded, progress ? "progress" : "delta");
		recorded.check();
	}
	std::fprintf(stderr,
				 "T7 over DTPAGE03: %zu cases, %zu distinct, %zu republished, %zu nested runs after cuts, %zu after "
				 "kills, %.1f s\n",
				 counts.cases, counts.distinct, counts.republished, counts.nested, counts.killed,
				 std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
	require(counts.republished > 0 && counts.nested > 0 && counts.killed > 0);
}
} // namespace power_cut

int main() {
	const auto directory =
		std::filesystem::current_path() / ".scratch" / ("paged-artifacts-" + std::to_string(::getpid()));
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() {
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}
	} cleanup{directory};
	auto source = std::make_shared<const d::ValidatedSnapshot>(decode(fixture()));
	l::GenerationId generation{}, next{};
	generation.fill('g');
	next.fill('n');
	l::detail::PagedSnapshotBuilder builder(source, generation);
	while (!builder.done()) {
		l::ValidationBudget budget;
		builder.step(budget);
	}
	auto image = builder.take();
	install(directory, image);
	auto recovered = l::recover_generation(directory);
	for (bool progress : {false, true}) {
		for (bool pressure : {false, true}) {
			auto account = std::make_shared<d::StorageLedger>(16 * l::paged::segment_bytes);
			auto baseline = std::make_shared<d::StorageLedger>(l::paged::segment_bytes);
			d::RetirementRuntime runtime(baseline, 1);
			auto domain = runtime.domain(account, 8);
			const auto initial = account->used();
			{
				std::optional<l::detail::PagedPlan> plan;
				if (progress) {
					auto delta =
						std::make_shared<const d::ValidatedDelta>(parse_delta(true, 2, "app", account));
					l::detail::PagedPlanBuilder construction(recovered, delta, domain);
					while (!construction.done()) {
						l::ValidationBudget budget;
						construction.step(budget);
					}
					plan.emplace(construction.take());
				} else {
					l::detail::PagedDeltaPlanBuilder construction(recovered, parse_delta(false, 2, "app", account), domain);
					while (!construction.done()) {
						l::ValidationBudget budget;
						construction.step(budget);
					}
					plan.emplace(construction.take());
				}
				// The builder's work and input owners retire on the runtime thread; the plan is left.
				eventually([&] { return domain.residents() == 1; });
				next = l::next_generation(recovered.descriptor(), plan->descriptor().cursor, plan->descriptor().profile,
										  false)
						   .id;
				auto artifacts = domain.factory().make<l::detail::PagedPublicationArtifacts>(account);
				if (pressure) {
					auto admission = account->admit(account->limit() - account->used());
					rejects<d::StoragePressure>([&] { artifacts->populate(*plan); });
					rejects<std::logic_error>([&] { artifacts->artifacts(); });
					require(plan->descriptor().generation == next);
				} else {
					artifacts->populate(*plan);
					const auto rows = artifacts->artifacts();
					require(artifacts->reused().size() == (progress ? 1 : 0));
					// Owner records, packs, then the image, `.live` and `.read`: the agent store's files.
					const auto segments = (rows.size() - 3) / 2;
					require(plan->segments().size() == segments && rows.size() == 2 * segments + 3);
					for (std::size_t i = 0; i < segments; ++i) {
						require(rows[i].filename().ends_with(".owner"));
						require(rows[segments + i].filename().ends_with(".pack"));
					}
					const auto &image_row = rows[2 * segments], &live_row = rows[2 * segments + 1], &read_row = rows.back();
					require(image_row.filename().ends_with(".image") && live_row.filename().ends_with(".live") &&
							read_row.filename().ends_with(".read"));
					require(artifacts->usage().entries == rows.size() && artifacts->usage().generations == 1);
					const auto metadata =
					    l::detail::decode_reader_metadata({read_row.bytes().data(), read_row.bytes().size()});
					require(metadata.generation == next &&
					        metadata.root_checksum == plan->descriptor().checksum &&
					        metadata.counts.records == plan->counts().records &&
					        metadata.physical_bytes == artifacts->target_physical_bytes());
					// `.live` binds the generation, its root and the whole `.read`, and rows every segment it reaches.
					const auto live = l::detail::decode_live_record({live_row.bytes().data(), live_row.bytes().size()}, 1024);
					require(live.generation == next && live.root_checksum == plan->descriptor().checksum &&
							live.read_digest == l::detail::reader_metadata_digest({read_row.bytes().data(), read_row.bytes().size()}) &&
							live.rows.size() == segments + artifacts->reused().size());
					artifacts->attach(std::move(*plan));
					rejects<std::logic_error>([&] { plan->root(); });
				}
			}
			domain.close().wait();
			require(account->used() == initial);
		}
	}
	power_cut::run(directory / "power-cut", image);
}
