// Replay every publication cut from completion's sync seam, then recover and republish.
// A selected generation must recover completely, and readiness must preserve it across a cut.
#include "applied_delta_fixture.hh"
#include "detent_local/disk_store.hh"
#include "paged_disk_fixture.hh"
#include "paged_maintenance_plan.hh"
#include <completion/native_completion.h>
#include <cstdlib>
#include <fcntl.h>
#include <map>
#include <source_location>
namespace c = kronuz::io::completion;

namespace {
void check(bool value, std::source_location where = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("power-cut invariant at line " + std::to_string(where.line()));
}
struct Inode {
	std::string content;                 // as written, volatile
	std::size_t durable = 0, staged = 0; // bytes a sync made durable, or (macOS) staged for the device flush
};
struct State {
	std::map<std::string, int> volatile_entries, durable_entries, staged_entries;
	std::vector<Inode> inodes;
	bool directory_staged = false;
};
enum class EntryMode { Durable, All };
enum class DataMode { Truncate, Zeros };

// Seam recording: the sync calls completion makes, applied to the model as they happen.
State *g_state = nullptr;
std::map<std::pair<dev_t, ino_t>, int> g_inode_ids;
std::pair<dev_t, ino_t> g_directory{};
std::size_t g_seam_files = 0, g_seam_directories = 0, g_seam_flushes = 0;
std::vector<State> *g_history = nullptr;
std::vector<std::string> *g_labels = nullptr;
std::vector<bool> *g_boundary = nullptr;

void note(std::string label, bool boundary = false) {
	g_history->push_back(*g_state);
	g_labels->push_back(std::move(label));
	g_boundary->push_back(boundary);
}
void file_synced(int id) {
	auto &inode = g_state->inodes[id];
	inode.staged = inode.content.size();
#ifndef __APPLE__
	inode.durable = inode.staged;
#endif
}
void directory_synced() {
	g_state->staged_entries = g_state->volatile_entries;
	g_state->directory_staged = true;
#ifndef __APPLE__
	g_state->durable_entries = g_state->volatile_entries;
#endif
}
void device_flushed() {
	for (auto &inode : g_state->inodes)
		inode.durable = inode.staged;
	if (g_state->directory_staged)
		g_state->durable_entries = g_state->staged_entries;
}
int record_fsync(int fd) {
	struct stat status{};
	check(::fstat(fd, &status) == 0);
	const std::pair<dev_t, ino_t> key{status.st_dev, status.st_ino};
	if (key == g_directory) {
		directory_synced();
		++g_seam_directories;
		note("fsync directory");
	} else if (auto found = g_inode_ids.find(key); found != g_inode_ids.end()) {
		file_synced(found->second);
		++g_seam_files;
		note("fsync file");
	}
	return ::fsync(fd);
}
#ifdef __APPLE__
int record_fcntl(int fd, int command) {
	check(command == F_FULLFSYNC || command == F_BARRIERFSYNC);
	device_flushed();
	++g_seam_flushes;
	note("device flush");
	return ::fcntl(fd, command);
}
void install(c::PosixIO &io) { io.replace_sync_calls(&record_fsync, &record_fcntl); }
#elif defined(__linux__)
int record_writeback(int fd) { return kronuz::journal::detail::default_writeback(fd); }
void install(c::PosixIO &io) { io.replace_sync_calls(&record_fsync, &record_writeback); }
#else
void install(c::PosixIO &io) { io.replace_sync_calls(&record_fsync); }
#endif

std::string slurp(const std::filesystem::path &path) {
	std::ifstream in(path, std::ios::binary);
	return {std::istreambuf_iterator<char>(in), {}};
}
bool outside_model(const std::string &name) { return name == "owner.lock" || name == "change"; }

// A directory holding one view of the state.
void materialize(const std::filesystem::path &root, const State &state, bool volatile_view, EntryMode entries,
				 DataMode data) {
	std::filesystem::remove_all(root);
	std::filesystem::create_directories(root);
	check(::chmod(root.c_str(), 0700) == 0);
	write(root / "owner.lock", {});
	std::map<std::string, int> names = volatile_view ? state.volatile_entries : state.durable_entries;
	if (!volatile_view && entries == EntryMode::All)
		for (const auto &[name, id] : state.volatile_entries)
			names[name] = id;
	for (const auto &[name, id] : names) {
		const auto &inode = state.inodes[id];
		std::string content = inode.content;
		if (!volatile_view) {
			const auto kept = std::min(inode.durable, content.size());
			if (data == DataMode::Truncate)
				content.resize(kept);
			else
				std::fill(content.begin() + kept, content.end(), '\0');
		}
		write(root / name, content);
	}
}
// The model after a restart on `materialize`d files: everything on disk is durable (power cut), or the
// volatile files are all there with only the previous durable subset durable (process kill).
State after_cut(const State &state, EntryMode entries, DataMode data) {
	State next;
	std::map<std::string, int> names = state.durable_entries;
	if (entries == EntryMode::All)
		for (const auto &[name, id] : state.volatile_entries)
			names[name] = id;
	for (const auto &[name, id] : names) {
		const auto &inode = state.inodes[id];
		std::string content = inode.content;
		const auto kept = std::min(inode.durable, content.size());
		if (data == DataMode::Truncate)
			content.resize(kept);
		else
			std::fill(content.begin() + kept, content.end(), '\0');
		next.inodes.push_back({content, content.size(), content.size()});
		next.volatile_entries[name] = next.durable_entries[name] = int(next.inodes.size()) - 1;
	}
	return next;
}
State after_kill(const State &state) {
	State next;
	std::map<int, int> carried;
	auto carry = [&](int id) {
		auto [position, inserted] = carried.emplace(id, int(next.inodes.size()));
		if (inserted)
			next.inodes.push_back(state.inodes[id]);
		return position->second;
	};
	for (const auto &[name, id] : state.volatile_entries)
		next.volatile_entries[name] = carry(id);
	for (const auto &[name, id] : state.durable_entries)
		next.durable_entries[name] = carry(id);
	return next;
}

struct Environment {
	Environment()
		: account(std::make_shared<d::StorageLedger>(64 * l::paged::segment_bytes)),
		  baseline(std::make_shared<d::StorageLedger>(l::paged::segment_bytes)), runtime(baseline, 1),
		  domain(runtime.domain(account, 64)) {
		options.limits = {64 * l::paged::segment_bytes, 100, 10};
		options.recovery_limits.retirement_domain = &domain;
		options.recovery_limits.mapping_account =
			std::make_shared<d::StorageLedger>(16 * l::paged::segment_bytes);
	}
	std::shared_ptr<d::StorageLedger> account, baseline;
	d::RetirementRuntime runtime;
	d::RetirementDomain domain;
	l::DiskStoreOptions options;
};
void drain(const std::shared_ptr<l::DiskStore> &store) {
	unsigned turns = 0;
	while (store->operation() || store->needs_work()) {
		auto job = store->operation();
		if (!job) {
			store->advance();
			continue;
		}
		check(++turns < 100000);
		job->submitted();
		check(job->complete(kronuz::journal::detail::execute_primitive(job->io(), job->request())));
		store->advance();
	}
	store->advance();
}
// Opens the directory as a restarted agent would and returns the selected descriptor after checking
// that the whole selected generation recovers.
l::Descriptor open_and_recover(const std::filesystem::path &root) {
	Environment env;
	auto store = l::make_disk_store(env.domain, root, std::make_shared<int>(1), env.options);
	drain(store);
	check(store->status() == l::StoreStatus::Ready);
	const auto selection = store->selection();
	check(selection.knowledge == l::SelectionKnowledge::Known && selection.descriptor);
	{
		auto recovered = l::recover_generation(root, env.options.recovery_limits);
		check(recovered.descriptor() == *selection.descriptor);
	}
	store->close();
	drain(store);
	check(store->drained());
	store.reset();
	env.domain.close().wait();
	return *selection.descriptor;
}

struct Run {
	std::vector<State> history;
	std::vector<std::string> labels;
	std::vector<bool> boundary;
	std::size_t startup_events = 0; // history entries before the publication began
	std::size_t ready = 0;          // the history entry at which the store reported ready
	l::Descriptor before{}, after{};
	l::SyncCounters publication_counters{}, startup_counters{};
	std::size_t seam_files = 0, seam_directories = 0, seam_flushes = 0;
	std::vector<c::Kind> trace;
	std::size_t groups = 0;
};
// Opens `root` (whose files `start` describes), publishes a maintenance rewrite as generation `next`,
// and records the history of the disk's state at every event.
Run publish(const std::filesystem::path &root, State start, const l::StorageIdentity &storage, char next_fill,
			std::size_t window) {
	Run run;
	State state = std::move(start);
	g_state = &state;
	g_history = &run.history;
	g_labels = &run.labels;
	g_boundary = &run.boundary;
	g_inode_ids.clear();
	g_seam_files = g_seam_directories = g_seam_flushes = 0;
	struct stat directory_status{};
	check(::stat(root.c_str(), &directory_status) == 0);
	g_directory = {directory_status.st_dev, directory_status.st_ino};
	for (const auto &[name, id] : state.volatile_entries) {
		struct stat status{};
		check(::stat((root / name).c_str(), &status) == 0);
		g_inode_ids[{status.st_dev, status.st_ino}] = id;
	}
	note("start", true);

	Environment env;
	env.options.sync_group_files = window;
	auto store = l::make_disk_store(env.domain, root, std::make_shared<int>(1), env.options);
	{
		auto job = store->operation();
		check(bool(job));
		auto *posix = dynamic_cast<c::PosixIO *>(&job->io());
		check(posix != nullptr);
		install(*posix);
	}
	std::map<const c::File *, int> file_ids;
	auto step = [&](bool publishing) {
		auto job = store->operation();
		if (!job) {
			store->advance();
			return;
		}
		const auto request = job->request();
		check(request.kind != c::Kind::Sync);
		job->submitted();
		auto result = kronuz::journal::detail::execute_primitive(job->io(), request);
		check(!result.error);
		if (publishing)
			run.trace.push_back(request.kind);
		switch (request.kind) {
		case c::Kind::Create:
		case c::Kind::CreateInto: {
			const std::string name(request.source);
			state.inodes.push_back({});
			const int id = int(state.inodes.size()) - 1;
			state.volatile_entries[name] = id;
			struct stat status{};
			check(::stat((root / name).c_str(), &status) == 0);
			g_inode_ids[{status.st_dev, status.st_ino}] = id;
			if (request.file)
				file_ids[request.file.get()] = id;
			note("create " + name);
			break;
		}
		case c::Kind::Write: {
			const int id = file_ids.at(request.file.get());
			auto &content = state.inodes[id].content;
			const auto end = request.offset + result.count;
			if (content.size() < end)
				content.resize(end);
			std::copy_n(request.bytes.begin(), result.count, content.begin() + request.offset);
			note("write");
			break;
		}
		case c::Kind::Replace: {
			const std::string from(request.source), to(request.destination);
			state.volatile_entries[to] = state.volatile_entries.at(from);
			state.volatile_entries.erase(from);
			note("replace " + from + " -> " + to);
			break;
		}
		case c::Kind::Remove:
			state.volatile_entries.erase(std::string(request.source));
			note("remove");
			break;
		case c::Kind::SyncFiles:
			++run.groups;
			break;
		default:
			break;
		}
		check(job->complete(std::move(result)));
		store->advance();
		note(std::string("complete ") + std::to_string(int(request.kind)), true);
	};
	// Startup, to ready.
	unsigned turns = 0;
	while (store->status() != l::StoreStatus::Ready) {
		check(++turns < 100000 && store->status() == l::StoreStatus::Starting);
		step(false);
	}
	run.ready = run.history.size() - 1;
	run.startup_events = run.history.size();
	run.startup_counters = store->sync_stats().startup;
	check(bool(store->selection().descriptor));
	run.before = *store->selection().descriptor;
	const auto selected_inode = state.volatile_entries.at("selected");
	check(state.durable_entries.at("selected") == selected_inode &&
		  state.inodes[selected_inode].durable == state.inodes[selected_inode].content.size());
	const auto startup_files = g_seam_files, startup_directories = g_seam_directories,
			   startup_flushes = g_seam_flushes;
	// Publish a maintenance rewrite of the selected generation.
	auto base = std::optional<l::RecoveredGeneration>(l::recover_generation(root, env.options.recovery_limits));
	l::GenerationId next;
	next.fill(next_fill);
	std::optional<l::detail::PagedPlan> plan;
	{
		l::detail::PagedMaintenanceBuilder builder(*base, env.domain.factory(), env.account, storage, next,
												   env.options.recovery_limits);
		while (!builder.done()) {
			l::ValidationBudget budget;
			builder.step(budget);
		}
		plan.emplace(builder.take());
	}
	run.after = plan->descriptor();
	check(store->publication_ready());
	const auto counters_before = store->sync_stats().total;
	store->begin(std::move(*plan));
	turns = 0;
	while (store->publication_status() == l::DiskPublicationStatus::Preparing) {
		check(++turns < 100000);
		step(true);
	}
	check(store->publication_status() == l::DiskPublicationStatus::Published);
	run.publication_counters = store->sync_stats().publication;
	check(store->sync_stats().total - counters_before == run.publication_counters);
	run.seam_files = g_seam_files - startup_files;
	run.seam_directories = g_seam_directories - startup_directories;
	run.seam_flushes = g_seam_flushes - startup_flushes;
	{
		auto recovered = l::recover_generation(root, env.options.recovery_limits);
		check(recovered.descriptor() == run.after);
		store->confirm_recovered(recovered);
	}
	store->close();
	drain(store);
	check(store->drained());
	store.reset();
	plan.reset();
	base.reset();
	env.domain.close().wait();
	g_state = nullptr;
	return run;
}
} // namespace

int main(int argc, char **argv) {
	const std::size_t stride = argc > 1 ? std::strtoul(argv[1], nullptr, 10) : 1;
	const auto directory = std::filesystem::current_path() / ".scratch" /
						   ("publication-power-cut-" + std::to_string(::getpid()));
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() {
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}
	} cleanup{directory};
	auto source = std::make_shared<const d::ValidatedSnapshot>(decode(fixture()));
	l::StorageIdentity storage{}, generation{};
	storage.fill('s');
	generation.fill('g');
	l::detail::PagedSnapshotBuilder builder(source, storage, generation);
	while (!builder.done()) {
		l::ValidationBudget budget;
		builder.step(budget);
	}
	auto image = builder.take();

	// The starting directory: one durable generation.
	const auto seed = directory / "seed";
	install(seed, image, storage);
	State start;
	for (const auto &entry : std::filesystem::directory_iterator(seed)) {
		const auto name = entry.path().filename().string();
		if (outside_model(name))
			continue;
		start.inodes.push_back({slurp(entry.path()), 0, 0});
		auto &inode = start.inodes.back();
		inode.durable = inode.staged = inode.content.size();
		start.volatile_entries[name] = start.durable_entries[name] = int(start.inodes.size()) - 1;
	}

	std::size_t cases = 0, requests = 0;
	// Cuts at every event, with every window size, in every mode.
	const std::pair<EntryMode, DataMode> modes[] = {{EntryMode::Durable, DataMode::Truncate},
													{EntryMode::Durable, DataMode::Zeros},
													{EntryMode::All, DataMode::Truncate},
													{EntryMode::All, DataMode::Zeros}};
	auto verify = [&](const Run &run, std::size_t index, const State &from, const std::filesystem::path &root,
					  const char *what) {
		const auto selected = open_and_recover(root);
		if (!(selected == run.before) && !(index >= run.startup_events && selected == run.after)) {
			std::cerr << what << " at event " << index << " selected a generation that is neither the one before the "
					  << "publication nor the one it publishes\n";
			check(false);
		}
		(void)from;
		++cases;
	};
	for (const std::size_t window : {std::size_t(1), std::size_t(2), std::size_t(128)}) {
		const auto root = directory / ("run-" + std::to_string(window));
		std::filesystem::remove_all(root);
		materialize(root, start, true, EntryMode::Durable, DataMode::Truncate);
		auto run = publish(root, start, storage, 'n', window);
		requests += run.trace.size();
		// Group syncs: owners and lease, then the payloads in windows of `window`, never a per-file sync.
		for (auto kind : run.trace)
			check(kind != c::Kind::Sync);
		check(run.publication_counters.groups == run.groups);
		const auto counters = run.publication_counters;
#ifdef __APPLE__
		check(counters.device_flushes == run.seam_flushes);
#else
		check(counters.device_flushes == run.seam_files + run.seam_directories);
#endif
		check(counters.files == run.seam_files && counters.directory_syncs == run.seam_directories);
		std::cout << "window " << window << ": " << run.trace.size() << " publication primitives, "
				  << run.history.size() << " events, files " << counters.files << ", groups " << counters.groups
				  << ", directory syncs " << counters.directory_syncs << ", device flushes "
				  << counters.device_flushes << '\n';
		for (std::size_t i = 0; i < run.history.size(); i += stride) {
			if (window == 128 && !run.boundary[i] && i % 3)
				continue;
			const auto &state = run.history[i];
			// The startup barrier: a restarted store reports ready only once what it serves is durable.
			if (i == run.ready) {
				const auto selected = state.volatile_entries.at("selected");
				check(state.durable_entries.at("selected") == selected &&
						state.inodes[selected].durable == state.inodes[selected].content.size());
			}
			// An owner record is durable before its payload, a lease before its generation's other files.
			for (const auto &[name, id] : state.durable_entries) {
				const auto stem = name.substr(0, name.rfind('.'));
				if (name.ends_with(".pack"))
					check(state.durable_entries.count(stem + ".owner"));
				for (const char *suffix : {".image", ".descriptor", ".live", ".read"})
					if (name.ends_with(suffix))
						check(state.durable_entries.count(stem + ".lease"));
			}
			const auto probe = directory / "probe";
			for (const auto &[entries, data] : modes) {
				materialize(probe, state, false, entries, data);
				verify(run, i, state, probe, run.labels[i].c_str());
			}
			if (run.boundary[i]) {
				materialize(probe, state, true, EntryMode::All, DataMode::Zeros);
				verify(run, i, state, probe, "kill");
			}
		}
		// Restart-then-republish cycles: cut or kill at a boundary, restart, publish again, cut again.
		if (window != 2)
			continue;
		unsigned cycle = 0;
		for (std::size_t i = 0; i < run.history.size(); ++i) {
			if (!run.boundary[i] || (i % (5 * stride) && !run.labels[i].starts_with("replace ")))
				continue;
			for (int variant = 0; variant < 3; ++variant) {
				State second = variant == 0   ? after_cut(run.history[i], EntryMode::Durable, DataMode::Truncate)
							   : variant == 1 ? after_cut(run.history[i], EntryMode::All, DataMode::Zeros)
											  : after_kill(run.history[i]);
				const auto again = directory / ("cycle-" + std::to_string(cycle++));
				materialize(again, second, true, EntryMode::Durable, DataMode::Truncate);
				auto rerun = publish(again, second, storage, 'o', 2);
				for (std::size_t j = 0; j < rerun.history.size(); j += 3) {
					const auto probe = directory / "probe";
					for (const auto &[entries, data] : {modes[0], modes[3]}) {
						materialize(probe, rerun.history[j], false, entries, data);
						verify(rerun, j, rerun.history[j], probe, "cycle");
					}
				}
				std::filesystem::remove_all(again);
			}
		}
	}
	std::cout << cases << " power-cut and restart cases over " << requests
			  << " publication primitives passed\n";
}
