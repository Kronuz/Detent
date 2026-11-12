// A3, T7 on the relay mirror (namespace-delivery D6 "Commit" and "Recovery", D7): the disk is modelled
// from completion's sync seam, which records what each fsync and device flush made durable. A power cut
// after every primitive of an adoption (fetch, stage, link, the sync group, the table, the collector), a
// repair by name and a startup keeps only that; a process kill keeps every name and byte, and its
// restart adopts again, cut at every step. Each recovered mirror must open with no namespace failed,
// reach only files that exist with the upstream's bytes, hold nothing else, never revert a table it
// served, and adopt the upstream's layout again to the uninterrupted mirror.
#include "mirror_fixture.hh"
#include <chrono>
#include <cstdio>
#include <fcntl.h>
#include <functional>
#include <unistd.h>

using namespace fixture;
namespace {
struct Inode {
	std::string staged, durable; // bytes an fsync staged, and bytes a device flush made durable
};
struct Disk {
	std::map<std::string, std::pair<int, std::string>> live; // name -> (inode, bytes)
	std::map<std::string, int> staged, durable;              // directory entries
	bool directory_staged = false;
	std::vector<Inode> inodes;
	std::uint64_t served = 0; // the newest table sequence an adoption returned
};
enum class Entries { Durable, All };
enum class Data { Truncate, Zeros };

struct Recorder {
	std::filesystem::path root;
	std::pair<dev_t, ino_t> directory{};
	std::map<std::pair<dev_t, ino_t>, int> ids;
	Disk disk;
	std::vector<Disk> events;
	std::vector<std::string> labels;
} *g = nullptr;

bool outside(const std::string &name) { return name == "owner.lock"; }
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

void materialize(const std::filesystem::path &root, const std::map<std::string, std::string> &files) {
	prepare(root);
	write_file(root / "owner.lock", {});
	for (const auto &[name, bytes] : files)
		write_file(root / name, bytes);
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
std::shared_ptr<c::PosixIO> unrecorded(const std::filesystem::path &root) {
	auto io = io_at(root);
	skip(*io);
	return io;
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

r::MirrorFetch upstream_fetch() {
	return [](const r::MirrorFile &f) { return bytes_of(f.name); };
}
// One step of a run: an adoption, or a read of one file (a repair when it rotted).
struct Step {
	std::string label;
	std::vector<r::NamespaceLayout> layouts;
	std::vector<std::string> dropped;
	std::optional<r::MirrorFile> read;
};
struct Lineage {
	std::vector<r::MirrorTable> tables;
	bool known(const r::MirrorTable &table) const {
		return table.entries.empty() || std::find(tables.begin(), tables.end(), table) != tables.end();
	}
};
struct Counts {
	std::size_t cases = 0, distinct = 0, readopted = 0;
} counts;
void apply(r::MirrorStore &store, const Step &step, Lineage &lineage) {
	if (step.read) {
		check(store.read(*step.read, upstream_fetch()) == bytes_of(step.read->name));
		return;
	}
	store.adopt(step.layouts, step.dropped, upstream_fetch());
	lineage.tables.push_back(store.table());
}

std::map<std::string, r::MirrorTable> g_outcomes;
// A cut left `files`: the mirror opens with nothing failed, holds exactly the reached files with the
// upstream's bytes, has not reverted the table it served, and adopts `again` to the uninterrupted mirror.
void recovered(const std::filesystem::path &root, const std::map<std::string, std::string> &files, std::uint64_t served,
			   const Step *again, const Lineage &lineage, const std::string &where) {
	++counts.cases;
	try {
		const auto key = fingerprint(files) + (again ? "|" + again->label : "");
		auto found = g_outcomes.find(key);
		if (found == g_outcomes.end()) {
			++counts.distinct;
			materialize(root, files);
			r::MirrorStore store(unrecorded(root), {});
			const auto opened = store.table();
			check(store.failed().empty());
			check(mirror_matches(root, opened));
			if (again) {
				store.adopt(again->layouts, again->dropped, upstream_fetch());
				++counts.readopted;
				check(store.table().entries == again->layouts && store.failed().empty() &&
					  mirror_matches(root, store.table()));
			}
			found = g_outcomes.emplace(key, opened).first;
		}
		check(found->second.sequence >= served && lineage.known(found->second));
	} catch (const std::exception &error) {
		throw std::runtime_error(where + ": " + error.what());
	}
}

struct Run {
	std::vector<Disk> events;
	std::vector<std::string> labels;
	Lineage lineage;
};
// Records a mirror opening `root` (whose disk the model already holds) and running `steps` in order.
Run record_run(const std::filesystem::path &root, Disk start, std::span<const Step> steps, Lineage lineage) {
	Recorder recorder;
	recorder.root = root;
	struct stat status{};
	check(::stat(root.c_str(), &status) == 0);
	recorder.directory = key(status);
	recorder.disk = std::move(start);
	g = &recorder;
	r::MirrorTesting::install(&hook, nullptr);
	for (const auto &name : listing(root)) {
		if (outside(name))
			continue;
		check(::lstat((root / name).c_str(), &status) == 0);
		recorder.ids[key(status)] = recorder.disk.live.at(name).first;
	}
	note("start");
	try {
		auto io = io_at(root);
		record(*io);
		r::MirrorStore store(io, {});
		recorder.disk.served = store.table().sequence;
		lineage.tables.push_back(store.table());
		for (const auto &step : steps) {
			apply(store, step, lineage);
			recorder.disk.served = store.table().sequence;
		}
	} catch (...) {
		r::MirrorTesting::install(nullptr, nullptr);
		g = nullptr;
		throw;
	}
	note("end");
	r::MirrorTesting::install(nullptr, nullptr);
	g = nullptr;
	return {std::move(recorder.events), std::move(recorder.labels), std::move(lineage)};
}
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
void materialize_live(const std::filesystem::path &root, Disk &disk) {
	prepare(root);
	write_file(root / "owner.lock", {});
	std::map<int, int> copies;
	for (auto &[name, entry] : disk.live) {
		write_file(root / name, entry.second);
		if (!copies.emplace(entry.first, 0).second) {
			disk.inodes.push_back(disk.inodes[entry.first]);
			entry.first = int(disk.inodes.size()) - 1;
		}
	}
}
std::string disk_key(const Disk &disk) {
	std::map<std::string, std::string> live, staged, durable, inodes;
	for (const auto &[name, entry] : disk.live) {
		live[name] = entry.second;
		inodes[name] = disk.inodes[entry.first].staged + "|" + disk.inodes[entry.first].durable;
	}
	for (const auto &[name, id] : disk.staged)
		staged[name] = disk.inodes[id].staged;
	for (const auto &[name, id] : disk.durable)
		durable[name] = disk.inodes[id].durable;
	return "kill#" + fingerprint(live) + "#" + fingerprint(staged) + "#" + fingerprint(durable) + "#" +
		   fingerprint(inodes) + (disk.directory_staged ? "#s" : "#");
}
// A disk whose every file is durable, as `files` holds them.
Disk durable_disk(const std::map<std::string, std::string> &files) {
	Disk start;
	for (const auto &[name, bytes] : files) {
		start.inodes.push_back({bytes, bytes});
		start.live[name] = {int(start.inodes.size()) - 1, bytes};
		start.durable[name] = start.staged[name] = int(start.inodes.size()) - 1;
	}
	start.directory_staged = true;
	return start;
}
// Power cuts after every event of `run`; for each distinct cut, a restart that adopts `again`, recorded
// and cut in turn; and a process kill after each event, whose restart adopts `again`, cut in turn.
void matrix(const std::filesystem::path &scratch, const Run &run, const Step &again, const std::string &name) {
	std::set<std::string> seen;
	for (std::size_t i = 0; i < run.events.size(); ++i) {
		const auto where = name + " power cut after event " + std::to_string(i) + " (" + run.labels[i] + ")";
		for (auto entries : {Entries::Durable, Entries::All})
			for (auto data : {Data::Truncate, Data::Zeros}) {
				const auto files = after_power_cut(run.events[i], entries, data);
				recovered(scratch / "cut", files, run.events[i].served, &again, run.lineage, where);
				if (!seen.insert(fingerprint(files)).second)
					continue;
				const auto root = scratch / "cut";
				materialize(root, files);
				auto start = durable_disk(files);
				const auto inner = record_run(root, start, std::span(&again, 1), run.lineage);
				for (std::size_t j = 0; j < inner.events.size(); ++j)
					for (auto inner_entries : {Entries::Durable, Entries::All}) {
						const auto cut = after_power_cut(inner.events[j], inner_entries, Data::Zeros);
						recovered(scratch / "inner", cut, inner.events[j].served, &again, inner.lineage,
								  where + ", restart, power cut after event " + std::to_string(j) + " (" +
									  inner.labels[j] + ")");
					}
			}
		if (!seen.insert(disk_key(run.events[i])).second)
			continue;
		auto killed = after_kill(run.events[i]);
		const auto root = scratch / "killed";
		materialize_live(root, killed);
		const auto inner = record_run(root, killed, std::span(&again, 1), run.lineage);
		for (std::size_t j = 0; j < inner.events.size(); ++j)
			for (auto entries : {Entries::Durable, Entries::All}) {
				const auto cut = after_power_cut(inner.events[j], entries, Data::Zeros);
				recovered(scratch / "inner", cut, inner.events[j].served, &again, inner.lineage,
						  name + " kill after event " + std::to_string(i) + " (" + run.labels[i] +
							  "), restart, power cut after event " + std::to_string(j) + " (" + inner.labels[j] + ")");
			}
	}
}
} // namespace

int main(int argc, char **argv) {
	check(argc == 2);
	const std::filesystem::path scratch = argv[1];
	std::filesystem::remove_all(scratch);
	std::filesystem::create_directories(scratch);
	const auto started = std::chrono::steady_clock::now();
	auto progress = [&](const char *what, std::size_t events) {
		std::fprintf(stderr, "%s: %zu events, %zu cases, %zu distinct, %.1f s\n", what, events, counts.cases, counts.distinct,
					 std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
	};
	try {
		Upstream up;
		up.add("app/flags");
		up.add("beta");
		up.add("gamma");
		up.advance({{"app/flags", 3}, {"beta", 2}, {"gamma", 1}});
		up.roll_up("app/flags");
		up.advance({{"app/flags", 2}, {"beta", 2}});
		up.roll_up("app/flags");
		up.advance({{"app/flags", 1}});
		const Step first{"first", up.layouts(), {}, {}};
		// The next seal: app/flags squashes and cuts, beta rolls up under a new policy, gamma is
		// dropped and delta appears: reused, fetched and collected files in one adoption.
		up.advance({{"app/flags", 2}, {"beta", 1}});
		up.roll_up("app/flags");
		up.squash("app/flags");
		up.cut("app/flags");
		up.roll_up("beta");
		up.policy = "policy-2";
		up.histories.erase("gamma");
		up.add("delta");
		up.advance({{"delta", 2}});
		const Step second{"second", up.layouts(), {"gamma"}, {}};
		const auto root = scratch / "run";

		// 1. The first adoption into a new mirror.
		prepare(root);
		write_file(root / "owner.lock", {});
		const auto run1 = record_run(root, {}, std::span(&first, 1), {});
		matrix(scratch, run1, first, "first");
		progress("first", run1.events.size());
		matrix(scratch, run1, second, "first, then second");
		progress("first, then second", run1.events.size());

		// 2. The second adoption on the adopted mirror: reuse, fetch, table, collector.
		prepare(root);
		write_file(root / "owner.lock", {});
		const auto base = record_run(root, {}, std::span(&first, 1), {});
		std::map<std::string, std::string> adopted;
		for (const auto &[name, entry] : base.events.back().live)
			adopted[name] = entry.second;
		materialize(root, adopted);
		const auto run2 = record_run(root, durable_disk(adopted), std::span(&second, 1), base.lineage);
		matrix(scratch, run2, second, "second");
		progress("second", run2.events.size());

		// 3. A repair: a reached block rotted; its first read fetches it by name and replaces it.
		const auto &rotted = first.layouts[0].files[1];
		auto corrupt = adopted;
		corrupt[r::mirror_file_name(rotted.name)][3] ^= 1;
		materialize(root, corrupt);
		const Step read{"repair", {}, {}, rotted};
		auto run3 = record_run(root, durable_disk(corrupt), std::span(&read, 1), base.lineage);
		// Cuts keep the rot or the repaired bytes; the recovered mirror opens either way, and adoption
		// then reads nothing, so the check reads the rotted name once more after recovery.
		std::size_t repaired = 0;
		for (std::size_t i = 0; i < run3.events.size(); ++i)
			for (auto entries : {Entries::Durable, Entries::All})
				for (auto data : {Data::Truncate, Data::Zeros}) {
					auto files = after_power_cut(run3.events[i], entries, data);
					materialize(scratch / "cut", files);
					r::MirrorStore store(unrecorded(scratch / "cut"), {});
					++counts.cases;
					check(store.failed().empty() && store.table() == base.lineage.tables.back());
					check(store.read(rotted, upstream_fetch()) == bytes_of(rotted.name));
					check(slurp(scratch / "cut" / r::mirror_file_name(rotted.name)) == bytes_of(rotted.name));
					std::erase_if(files, [](const auto &f) { return f.first.starts_with(".tmp-"); });
					repaired += files.at(r::mirror_file_name(rotted.name)) == bytes_of(rotted.name);
				}
		check(repaired > 0);
		progress("repair", run3.events.size());
	} catch (const std::exception &error) {
		std::fprintf(stderr, "test_relay_mirror_crash: %s\n", error.what());
		return 1;
	}
	std::fprintf(stderr, "T7 mirror: %zu cases (%zu distinct cuts), %zu re-adopted, 0 violations\n", counts.cases,
				 counts.distinct, counts.readopted);
	std::filesystem::remove_all(scratch);
	return 0;
}
