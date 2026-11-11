// T7 on the agent store (namespace-delivery D8, D11): the disk is modelled from completion's sync seam,
// which records what each fsync and device flush made durable. A power cut after every primitive of a
// publication, a repair and a startup keeps only that; a process kill keeps every name and byte, whose
// restart then publishes again and is cut at every step. Each recovered store must open, reach only
// files that exist and verify, never revert a table it served, and publish again (no wedge).
#include "agent_store_fixture.hh"
#include <cstdio>
#include <fcntl.h>
#include <map>
#include <set>
#include <sys/stat.h>
#include <unistd.h>

using namespace fixture;
namespace c = kronuz::io::completion;
namespace {
const d::Profile profile{};

struct Inode {
	std::string staged, durable; // bytes an fsync staged, and bytes a device flush made durable
};
// The disk at one instant: the kernel's view (names, inodes and bytes) and what survives a power cut.
struct Disk {
	std::map<std::string, std::pair<int, std::string>> live; // name -> (inode, bytes)
	std::map<std::string, int> staged, durable;              // directory entries
	bool directory_staged = false;
	std::vector<Inode> inodes;
	std::uint64_t served = 0; // the newest table sequence a reader was told of
};
enum class Entries { Durable, All };
enum class Data { Truncate, Zeros };

// Seam recording. One recorded store at a time.
struct Recorder {
	std::filesystem::path root;
	std::pair<dev_t, ino_t> directory{};
	std::map<std::pair<dev_t, ino_t>, int> ids;
	Disk disk;
	std::vector<Disk> events;
	std::vector<std::string> labels;
} *g = nullptr;

bool outside(const std::string &name) { return name == "owner.lock" || name == "change"; }
std::pair<dev_t, ino_t> key(const struct stat &status) { return {status.st_dev, status.st_ino}; }
int fresh_inode() {
	g->disk.inodes.emplace_back();
	return int(g->disk.inodes.size()) - 1;
}
void scan() {
	std::map<std::string, std::pair<int, std::string>> live;
	for (const auto &name : listing(g->root)) {
		if (outside(name))
			continue;
		struct stat status{};
		check(::lstat((g->root / name).c_str(), &status) == 0);
		auto found = g->ids.find(key(status));
		// Every file the store creates starts as a new `.tmp-` name: a fresh inode even if the
		// filesystem reuses a freed inode number.
		if (found == g->ids.end() || (name.starts_with(".tmp-") && !g->disk.live.contains(name)))
			found = g->ids.insert_or_assign(key(status), fresh_inode()).first;
		live[name] = {found->second, slurp(g->root / name)};
	}
	g->disk.live = std::move(live);
}
void note(const char *label) {
	scan();
	g->events.push_back(g->disk);
	g->labels.emplace_back(label);
}
void hook(void *, const char *label) noexcept {
	if (g)
		note(label);
}
void device_flushed() {
	for (auto &inode : g->disk.inodes)
		inode.durable = inode.staged;
	if (g->disk.directory_staged)
		g->disk.durable = g->disk.staged;
}
int record_fsync(int fd) {
	struct stat status{};
	check(::fstat(fd, &status) == 0);
	scan();
	if (key(status) == g->directory) {
		g->disk.staged.clear();
		for (const auto &[name, entry] : g->disk.live)
			g->disk.staged[name] = entry.first;
		g->disk.directory_staged = true;
#ifndef __APPLE__
		g->disk.durable = g->disk.staged;
#endif
		note("fsync directory");
		return 0;
	}
	auto found = g->ids.find(key(status));
	check(found != g->ids.end());
	std::string bytes(std::size_t(status.st_size), '\0');
	check(::pread(fd, bytes.data(), bytes.size(), 0) == ssize_t(bytes.size()));
	auto &inode = g->disk.inodes[found->second];
	inode.staged = bytes;
#ifndef __APPLE__
	inode.durable = bytes;
#endif
	note("fsync file");
	return 0;
}
int skip_fsync(int) { return 0; }
#ifdef __APPLE__
int record_fcntl(int, int command) {
	check(command == F_FULLFSYNC || command == F_BARRIERFSYNC);
	device_flushed();
	note("device flush");
	return 0;
}
int skip_fcntl(int, int) { return 0; }
void record(c::PosixIO &io) { io.replace_sync_calls(&record_fsync, &record_fcntl); }
void skip(c::PosixIO &io) { io.replace_sync_calls(&skip_fsync, &skip_fcntl); }
#elif defined(__linux__)
int skip_writeback(int) { return 0; }
void record(c::PosixIO &io) { io.replace_sync_calls(&record_fsync, &skip_writeback); }
void skip(c::PosixIO &io) { io.replace_sync_calls(&skip_fsync, &skip_writeback); }
#else
void record(c::PosixIO &io) { io.replace_sync_calls(&record_fsync); }
void skip(c::PosixIO &io) { io.replace_sync_calls(&skip_fsync); }
#endif

void write_file(const std::filesystem::path &path, std::string_view bytes) {
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out.write(bytes.data(), std::streamsize(bytes.size()));
	out.close();
	check(bool(out) && ::chmod(path.c_str(), 0644) == 0);
}
void prepare(const std::filesystem::path &root) {
	std::filesystem::remove_all(root);
	std::filesystem::create_directories(root);
	check(::chmod(root.c_str(), 0700) == 0);
	write_file(root / "owner.lock", {});
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
void materialize(const std::filesystem::path &root, const std::map<std::string, std::string> &files) {
	prepare(root);
	for (const auto &[name, bytes] : files)
		write_file(root / name, bytes);
}
std::shared_ptr<c::PosixIO> unrecorded(const std::filesystem::path &root) {
	auto io = std::make_shared<c::PosixIO>(root, 0644);
	skip(*io);
	return io;
}

// One publication input: a cycle of generations, and its repair at the next ordinals.
struct Cycle {
	std::string label;
	std::vector<l::PreparedGeneration> generations;
	std::vector<l::PreparedGeneration> repair; // the same namespaces, ordinal + 1
};
struct Lineage {
	std::vector<l::StoreTable> tables; // every table published so far
	bool known(const l::StoreTable &table) const {
		return table.entries.empty() || std::find(tables.begin(), tables.end(), table) != tables.end();
	}
};
struct Counts {
	std::size_t cases = 0, distinct = 0, republished = 0, repaired = 0;
} counts;

// Publishes `cycle`, falling back to its repair when a reached name holds other bytes (D10).
void publish(l::AgentStore &store, const Cycle &cycle, Lineage &lineage) {
	try {
		store.publish({identity('c'), 1}, cycle.generations);
	} catch (const l::ReachedNameMismatch &) {
		++counts.repaired;
		store.publish({identity('c'), 1}, cycle.repair);
	}
	lineage.tables.push_back(store.table());
}
// Opens a recovered directory and checks T7's invariant, then (optionally) publishes `cycle` again.
// `corrupt` names the namespaces a run starts with failed (D10), until it republishes them.
std::set<std::string> g_corrupt;
void recovered(const std::filesystem::path &root, const Disk &disk, const Cycle *cycle, Lineage lineage,
			   const std::string &where) {
	++counts.cases;
	try {
		l::AgentStore store(unrecorded(root), {profile});
		const auto table = store.table();
		auto trusted = table;
		std::erase_if(trusted.entries, [&](const l::TableEntry &entry) {
			return std::find(store.failed().begin(), store.failed().end(), entry.name) != store.failed().end();
		});
		for (const auto &name : store.failed())
			check(g_corrupt.contains(name));
		check(table_verifies(root, trusted));
		check(table.sequence >= disk.served && lineage.known(table));
		for (const auto &name : listing(root))
			check(!name.starts_with(".tmp-"));
		if (cycle) {
			publish(store, *cycle, lineage);
			++counts.republished;
			check(store.failed().empty() && table_verifies(root, store.table()) &&
				  store.table().sequence == table.sequence + 1);
		}
	} catch (const std::exception &error) {
		throw std::runtime_error(where + ": " + error.what());
	}
}
std::string fingerprint(const std::map<std::string, std::string> &files) {
	std::string out;
	for (const auto &[name, bytes] : files) {
		if (name.starts_with(".tmp-"))
			continue;
		const auto sum = sha(bytes);
		out.append(name).append(sum.data(), sum.size());
	}
	return out;
}

struct Run {
	std::vector<Disk> events;
	std::vector<std::string> labels;
	Lineage lineage;
};
// Records a store opening `root` (whose disk the model already holds) and publishing `cycles` in order.
Run record_run(const std::filesystem::path &root, Disk start, std::span<const Cycle> cycles, Lineage lineage) {
	Recorder recorder;
	recorder.root = root;
	struct stat status{};
	check(::stat(root.c_str(), &status) == 0);
	recorder.directory = key(status);
	recorder.disk = std::move(start);
	g = &recorder;
	l::AgentStoreTesting::install(&hook, nullptr);
	// Map the files on disk to the model's inodes by name.
	for (const auto &name : listing(root)) {
		if (outside(name))
			continue;
		check(::lstat((root / name).c_str(), &status) == 0);
		recorder.ids[key(status)] = recorder.disk.live.at(name).first;
	}
	note("start");
	try {
		auto io = std::make_shared<c::PosixIO>(root, 0644);
		record(*io);
		l::AgentStore store(io, {profile});
		recorder.disk.served = store.table().sequence;
		lineage.tables.push_back(store.table());
		for (const auto &cycle : cycles) {
			publish(store, cycle, lineage);
			recorder.disk.served = store.table().sequence;
		}
	} catch (...) {
		l::AgentStoreTesting::install(nullptr, nullptr);
		g = nullptr;
		throw;
	}
	note("end");
	l::AgentStoreTesting::install(nullptr, nullptr);
	g = nullptr;
	return {std::move(recorder.events), std::move(recorder.labels), std::move(lineage)};
}
// A process kill: every name and byte stays; what was durable stays durable.
Disk after_kill(const Disk &disk) {
	Disk next;
	std::map<int, int> carried;
	auto carry = [&](int id) {
		auto [at, inserted] = carried.emplace(id, int(next.inodes.size()));
		if (inserted)
			next.inodes.push_back(disk.inodes[id]);
		return at->second;
	};
	for (const auto &[name, entry] : disk.live)
		next.live[name] = {carry(entry.first), entry.second};
	for (const auto &[name, id] : disk.durable)
		next.durable[name] = carry(id);
	for (const auto &[name, id] : disk.staged)
		next.staged[name] = carry(id);
	next.directory_staged = disk.directory_staged;
	next.served = disk.served;
	return next;
}
// Materializes a disk whose names are all present: one file per name (hard links become copies).
void materialize_live(const std::filesystem::path &root, Disk &disk) {
	prepare(root);
	std::map<int, int> copies;
	for (auto &[name, entry] : disk.live) {
		write_file(root / name, entry.second);
		if (!copies.emplace(entry.first, 0).second) {
			disk.inodes.push_back(disk.inodes[entry.first]);
			entry.first = int(disk.inodes.size()) - 1;
		}
	}
}

// Power cuts after every event of `run`; for each distinct cut, optionally a recovery that publishes
// `again`, recorded and cut in turn.
void matrix(const std::filesystem::path &scratch, const Run &run, const Cycle &again, bool nested,
			const std::string &name) {
	std::set<std::string> seen;
	for (std::size_t i = 0; i < run.events.size(); ++i) {
		for (auto entries : {Entries::Durable, Entries::All})
			for (auto data : {Data::Truncate, Data::Zeros}) {
				const auto files = after_power_cut(run.events[i], entries, data);
				if (!seen.insert(fingerprint(files)).second)
					continue;
				++counts.distinct;
				const auto where = name + " power cut after event " + std::to_string(i) + " (" + run.labels[i] + ")";
				const auto root = scratch / "cut";
				materialize(root, files);
				recovered(root, run.events[i], &again, run.lineage, where);
				if (!nested)
					continue;
				// Restart then republish over the reused names, cut at every step.
				materialize(root, files);
				Disk start;
				for (const auto &[file, bytes] : files) {
					start.inodes.push_back({bytes, bytes});
					start.live[file] = {int(start.inodes.size()) - 1, bytes};
					start.durable[file] = start.staged[file] = int(start.inodes.size()) - 1;
				}
				start.directory_staged = true;
				start.served = 0;
				Lineage lineage = run.lineage;
				const auto inner = record_run(root, start, std::span(&again, 1), lineage);
				for (std::size_t j = 0; j < inner.events.size(); ++j)
					for (auto inner_entries : {Entries::Durable, Entries::All}) {
						const auto cut = after_power_cut(inner.events[j], inner_entries, Data::Zeros);
						materialize(scratch / "inner", cut);
						recovered(scratch / "inner", inner.events[j], nullptr, inner.lineage,
								  where + ", restart, power cut after event " + std::to_string(j) + " (" +
									  inner.labels[j] + ")");
					}
			}
		if (!nested)
			continue;
		// A process kill here, then a restart that publishes `again`, cut at every step (D11's startup
		// barrier syncs no file: an unsynced table renamed before the kill would surface here, N6).
		auto killed = after_kill(run.events[i]);
		const auto root = scratch / "killed";
		materialize_live(root, killed);
		const auto inner = record_run(root, killed, std::span(&again, 1), run.lineage);
		for (std::size_t j = 0; j < inner.events.size(); ++j)
			for (auto entries : {Entries::Durable, Entries::All}) {
				const auto cut = after_power_cut(inner.events[j], entries, Data::Zeros);
				materialize(scratch / "inner", cut);
				recovered(scratch / "inner", inner.events[j], nullptr, inner.lineage,
						  name + " kill after event " + std::to_string(i) + " (" + run.labels[i] +
							  "), restart, power cut after event " + std::to_string(j) + " (" + inner.labels[j] + ")");
			}
	}
}

Cycle cycle_of(std::string label, std::vector<Generation> generations, std::vector<Generation> repair) {
	Cycle cycle{std::move(label), {}, {}};
	for (auto &g : generations)
		cycle.generations.push_back(std::move(g.prepared));
	for (auto &g : repair)
		cycle.repair.push_back(std::move(g.prepared));
	return cycle;
}
} // namespace

int main(int argc, char **argv) {
	check(argc == 2);
	const std::filesystem::path scratch = argv[1];
	std::filesystem::remove_all(scratch);
	std::filesystem::create_directories(scratch);
	try {
		// The first publication: two namespaces from full copies.
		const auto alpha = generation(profile, "alpha", cursor('a', 4), {}, 0, {}, 2, 'a');
		const auto beta = generation(profile, "beta", cursor('b', 9), {}, 0, {}, 1, 'b');
		const auto first = cycle_of("first", {alpha, beta},
									{generation(profile, "alpha", cursor('a', 4), {}, 1, {}, 2, 'a'),
									 generation(profile, "beta", cursor('b', 9), {}, 1, {}, 1, 'b')});
		// A second build of the same input with other bytes under the same derived names (a stale
		// `.live` from the first build names another `.read`): unlinked and relinked, or repaired.
		const auto other = cycle_of("first, rebuilt", {generation(profile, "alpha", cursor('a', 4), {}, 0, {}, 2, 'A'),
													   generation(profile, "beta", cursor('b', 9), {}, 0, {}, 1, 'B')},
									{generation(profile, "alpha", cursor('a', 4), {}, 1, {}, 2, 'A'),
									 generation(profile, "beta", cursor('b', 9), {}, 1, {}, 1, 'B')});
		// The next cycle: alpha catches up on its base (one base segment kept), gamma appears.
		const auto next = cycle_of("catch-up",
								   {generation(profile, "alpha", cursor('a', 7), alpha.id, 0, std::span(alpha.segments).first(1), 1, 'n'),
									generation(profile, "gamma", cursor('g', 2), {}, 0, {}, 1, 'g')},
								   {generation(profile, "alpha", cursor('a', 7), {}, 1, {}, 2, 'N'),
									generation(profile, "gamma", cursor('g', 2), {}, 1, {}, 1, 'g')});
		const auto root = scratch / "run";

		// 1. The first publication into a new store, then republishing the same and a rebuilt input.
		prepare(root);
		const auto run1 = record_run(root, {}, std::span(&first, 1), {});
		matrix(scratch, run1, first, true, "first");
		matrix(scratch, run1, other, true, "first, rebuilt");
		std::fprintf(stderr, "first: %zu events\n", run1.events.size());

		// 2. A catch-up cycle on a published store.
		prepare(root);
		auto base = record_run(root, {}, std::span(&first, 1), {});
		const auto published = base.events.back();
		Disk start;
		for (const auto &[name, entry] : published.live) {
			start.inodes.push_back({entry.second, entry.second});
			start.live[name] = {int(start.inodes.size()) - 1, entry.second};
			start.durable[name] = start.staged[name] = int(start.inodes.size()) - 1;
		}
		start.directory_staged = true;
		materialize(root, {});
		for (const auto &[name, entry] : start.live)
			write_file(root / name, entry.second);
		const auto run2 = record_run(root, start, std::span(&next, 1), base.lineage);
		matrix(scratch, run2, next, true, "catch-up");
		std::fprintf(stderr, "catch-up: %zu events\n", run2.events.size());

		// 3. A repair: alpha's reached `.read` is corrupt, so the store fails alpha and the cycle
		// republishes it at the next ordinal.
		materialize(root, {});
		for (const auto &[name, entry] : start.live)
			write_file(root / name, name == generation_file(alpha.id, ".read") ? std::string(192, 'z') : entry.second);
		Disk corrupt = start;
		for (auto &[name, entry] : corrupt.live)
			if (name == generation_file(alpha.id, ".read")) {
				entry.second = std::string(192, 'z');
				corrupt.inodes[entry.first] = {entry.second, entry.second};
			}
		const auto repair = cycle_of("repair", {generation(profile, "alpha", cursor('a', 4), {}, 0, {}, 2, 'a')},
									 {generation(profile, "alpha", cursor('a', 4), {}, 1, {}, 2, 'a')});
		{
			l::AgentStore probe(unrecorded(root), {profile});
			check(probe.failed().size() == 1);
		}
		materialize(root, {});
		for (const auto &[name, entry] : corrupt.live)
			write_file(root / name, entry.second);
		const auto run3 = record_run(root, corrupt, std::span(&repair, 1), base.lineage);
		check(counts.repaired > 0);
		g_corrupt = {"alpha"};
		matrix(scratch, run3, repair, true, "repair");
		g_corrupt.clear();
		std::fprintf(stderr, "repair: %zu events\n", run3.events.size());
	} catch (const std::exception &error) {
		std::fprintf(stderr, "test_agent_store_crash: %s\n", error.what());
		return 1;
	}
	std::fprintf(stderr, "T7: %zu cases (%zu distinct cuts), %zu republished, %zu repairs, 0 violations\n",
				 counts.cases, counts.distinct, counts.republished, counts.repaired);
	std::filesystem::remove_all(scratch);
	return 0;
}
