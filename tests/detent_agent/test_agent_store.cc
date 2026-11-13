// The agent store (namespace-delivery D8, W9, W11): one directory, the table, derived ids, `linkat`
// publication and verified reuse.
#include "agent_store_fixture.hh"
#include "change_file.hh"
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <system_error>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace fixture;
namespace c = kronuz::io::completion;
namespace {
const d::Profile profile{};
std::filesystem::path g_root;

std::filesystem::path fresh(const std::string &name) {
	const auto path = g_root / name;
	std::filesystem::remove_all(path);
	std::filesystem::create_directories(path);
	check(::chmod(path.c_str(), 0700) == 0);
	return path;
}
std::unique_ptr<l::AgentStore> open(const std::filesystem::path &root) {
	return std::make_unique<l::AgentStore>(std::make_shared<c::PosixIO>(root, 0644), l::AgentStore::Options{profile});
}
std::array<l::PreparedGeneration, 1> one(const Generation &g) { return {g.prepared}; }
ino_t inode(const std::filesystem::path &path) {
	struct stat status{};
	check(::lstat(path.c_str(), &status) == 0);
	return status.st_ino;
}
nlink_t links(const std::filesystem::path &path) {
	struct stat status{};
	check(::lstat(path.c_str(), &status) == 0);
	return status.st_nlink;
}
void overwrite(const std::filesystem::path &path, std::string_view bytes) {
	std::filesystem::remove(path);
	std::ofstream out(path, std::ios::binary);
	out.write(bytes.data(), std::streamsize(bytes.size()));
	out.close();
	check(::chmod(path.c_str(), 0644) == 0);
}
std::vector<std::string> expected_names(std::initializer_list<const Generation *> generations) {
	std::vector<std::string> names{"change", "owner.lock", "selected"};
	for (const auto *g : generations) {
		for (auto suffix : {".image", ".lease", ".live", ".read"})
			names.push_back(generation_file(g->id, suffix));
		for (const auto &segment : g->segments) {
			const l::detail::SegmentNames segment_names(segment.id);
			names.emplace_back(segment_names.owner_name());
			names.emplace_back(segment_names.payload_name());
		}
	}
	std::sort(names.begin(), names.end());
	names.erase(std::unique(names.begin(), names.end()), names.end());
	return names;
}

// W11, D8.1: ids derive from what was built, with no storage identity.
void derived_ids() {
	const l::GenerationId zero{}, base = l::derive_generation(profile, identity('i'), 7, zero, 0);
	const auto id = l::derive_generation(profile, identity('i'), 9, base, 0);
	check(id == l::derive_generation(profile, identity('i'), 9, base, 0));
	d::Profile other = profile;
	other.required_capabilities = 0;
	d::Profile semantic = profile;
	semantic.semantic_id = 2;
	for (const auto &variant :
		 {l::derive_generation(other, identity('i'), 9, base, 0), l::derive_generation(semantic, identity('i'), 9, base, 0),
		  l::derive_generation(profile, identity('j'), 9, base, 0), l::derive_generation(profile, identity('i'), 10, base, 0),
		  l::derive_generation(profile, identity('i'), 9, zero, 0), l::derive_generation(profile, identity('i'), 9, base, 1)})
		check(variant != id);
	// Pinned: two builds on any host name the same files, and the derivation never drifts silently.
	const auto pinned = l::generation_name(l::derive_generation(profile, identity('i'), 9, zero, 0));
	if (pinned != "generation-769d4f78b559a65597a3188c10bb87ad")
		throw std::runtime_error("derivation drifted: " + pinned);
	check(l::derive_segment(id, 0) == l::derive_segment(id, 0) && l::derive_segment(id, 0) != l::derive_segment(id, 1) &&
		  l::derive_segment(id, 0) != l::derive_segment(base, 0));
}

void table_codec() {
	l::StoreTable table;
	table.sequence = 5;
	table.directory = {identity('c'), 11};
	for (auto name : {"alpha", "beta", "gamma/delta"}) {
		l::TableEntry entry;
		entry.name = name;
		entry.cursor = cursor(name[0], 3);
		entry.slot = std::uint32_t(table.entries.size()) ^ 1;
		entry.generation = l::derive_generation(profile, entry.cursor.incarnation, 3, {}, 0);
		entry.ordinal = 2;
		entry.root_length = 320;
		entry.root = sha(name);
		entry.read = sha(std::string(name) + ".read");
		table.entries.push_back(entry);
	}
	const auto bytes = l::encode_table(table);
	check(l::decode_table(bytes) == table && table.find("beta") == &table.entries[1] && !table.find("b"));
	// One refusal for anything but a whole, current table.
	for (std::size_t at : {std::size_t(0), std::size_t(9), std::size_t(70), bytes.size() - 1}) {
		auto torn = bytes;
		torn[at] ^= 1;
		refuses<l::TableRefused>([&] { l::decode_table(torn); });
	}
	refuses<l::TableRefused>([&] { l::decode_table(std::string_view(bytes).substr(0, bytes.size() - 1)); });
	refuses<l::TableRefused>([&] { l::decode_table(std::string_view(bytes).substr(0, 64)); });
	auto resign = [](std::string bytes) {
		const auto sum = sha(std::string_view(bytes).substr(0, bytes.size() - 32));
		std::copy(sum.begin(), sum.end(), bytes.end() - 32);
		return bytes;
	};
	auto duplicate = table;
	duplicate.entries[1].slot = duplicate.entries[0].slot;
	refuses<l::TableRefused>([&] { l::decode_table(l::encode_table(duplicate)); });
	auto layout = bytes;
	layout[8] = 2;
	refuses<l::TableRefused>([&] { l::decode_table(resign(layout)); });
	auto unordered = bytes; // "alpha" and "beta" differ in their first byte; swap it to put beta first
	unordered[64 + 1] = 'c';
	refuses<l::TableRefused>([&] { l::decode_table(resign(unordered)); });
	check(l::decode_table(resign(bytes)) == table);
	refuses<std::invalid_argument>([&] {
		auto swapped = table;
		std::swap(swapped.entries[0], swapped.entries[1]);
		l::encode_table(swapped);
	});
}

// W9: the store is one directory holding `selected`, `change`, `owner.lock` and the derived files.
void one_directory() {
	const auto root = fresh("layout");
	auto store = open(root);
	check(listing(root) == std::vector<std::string>{"change", "owner.lock"});
	check(store->table().entries.empty() && store->table().sequence == 0 && store->failed().empty());
	refuses<std::exception>([&] { open(root); }); // owner.lock is held exclusively

	const auto a = generation(profile, "alpha", cursor('a', 4), {}, 0, {}, 2, 'a');
	const auto b = generation(profile, "beta", cursor('b', 9), {}, 0, {}, 1, 'b');
	const auto words = store->change_word(0);
	check(store->publish({identity('c'), 2}, std::array{b.prepared, a.prepared}) == 1);
	check(listing(root) == expected_names({&a, &b}));
	const auto table = l::decode_table(slurp(root / "selected"));
	check(table == store->table() && table.sequence == 1 && table.directory.position == 2);
	check(table.entries.size() == 2 && table.entries[0].name == "alpha" && table.entries[1].name == "beta");
	check(table.entries[0].generation == a.id && table.entries[0].cursor == a.prepared.cursor &&
		  table.entries[0].root == sha(a.prepared.image) && table.entries[0].read == sha(a.prepared.read) &&
		  table.entries[0].root_length == 320);
	// Slots in first-appearance order within the cycle; the store word and each changed slot move once.
	check(table.entries[1].slot == 0 && table.entries[0].slot == 1);
	check(store->change_word(0) == words + 1 && store->change_word(1) == 1 && store->change_word(2) == 1);
	check(table_verifies(root, table));
	for (const auto &name : listing(root))
		check(links(root / name) == 1);

	// A later namespace takes the next slot; an unchanged entry keeps its sequence.
	const auto c = generation(profile, "carrot", cursor('c', 1), {}, 0, {}, 1, 'c');
	check(store->publish({identity('c'), 3}, one(c)) == 2);
	check(store->table().find("carrot")->slot == 2 && store->change_word(1) == 1 && store->change_word(3) == 1);

	// Restart: the table reads back, every word moves once (reader.md D4 rule 2), `.tmp-` names go.
	std::vector<std::uint64_t> before;
	for (std::size_t i = 0; i < 4; ++i)
		before.push_back(store->change_word(i));
	store.reset();
	overwrite(root / ".tmp-leftover", "partial");
	store = open(root);
	check(store->table() == l::decode_table(slurp(root / "selected")) && store->failed().empty());
	for (std::size_t i = 0; i < 4; ++i)
		check(store->change_word(i) == before[i] + 1);
	check(!std::filesystem::exists(root / ".tmp-leftover"));

	// A table that does not decode refuses the store.
	store.reset();
	auto torn = slurp(root / "selected");
	torn[20] ^= 1;
	overwrite(root / "selected", torn);
	refuses<l::TableRefused>([&] { open(root); });
}

// W11, D8.3-4: files are linked from `.tmp-` names; a name the table reaches is reused when it verifies,
// any other relinked, and a mismatching name the table reaches is a repair at the next ordinal.
void publication() {
	const auto root = fresh("publication");
	auto store = open(root);
	const auto a = generation(profile, "alpha", cursor('a', 4), {}, 0, {}, 2, 'a');

	// An orphan a crash left at a derived name (unsynced bytes, here garbage) is unlinked and relinked.
	const auto orphan = root / generation_file(a.id, ".read");
	overwrite(orphan, std::string(192, 'x'));
	const auto pack = root / std::string(l::detail::SegmentNames(a.prepared.segments[0].id).payload_name());
	overwrite(pack, a.prepared.segments[0].pack.substr(0, 100));
	// One holding the right bytes is relinked too: no durable table reaches it, so its sync is unknown.
	const auto image = root / generation_file(a.id, ".image");
	overwrite(image, a.prepared.image);
	const auto stale = inode(image);
	store->publish({}, one(a));
	check(slurp(orphan) == a.prepared.read && slurp(pack) == a.prepared.segments[0].pack && inode(image) != stale);
	check(table_verifies(root, store->table()) && listing(root) == expected_names({&a}));

	// Republishing the same generation reuses every name: nothing is replaced.
	std::vector<ino_t> inodes;
	for (const auto &name : expected_names({&a}))
		if (name.starts_with("generation-") || name.starts_with("segment-"))
			inodes.push_back(inode(root / name));
	store->publish({}, one(a));
	std::size_t at = 0;
	for (const auto &name : expected_names({&a}))
		if (name.starts_with("generation-") || name.starts_with("segment-"))
			check(inode(root / name) == inodes[at++]);

	// A catch-up generation keeps one base segment and writes one; a base segment the table does not
	// reach is refused before anything is written.
	const auto next = generation(profile, "alpha", cursor('a', 6), a.id, 0, std::span(a.segments).first(1), 1, 'n');
	auto stray = a.segments[0];
	stray.id[0] ^= 1;
	const auto rebound = generation(profile, "alpha", cursor('a', 6), a.id, 0, std::span(&stray, 1), 1, 'n');
	const auto names = listing(root);
	refuses<std::invalid_argument>([&] { store->publish({}, one(rebound)); });
	check(listing(root) == names);
	store->publish({}, one(next));
	check(store->table().find("alpha")->generation == next.id && table_verifies(root, store->table()));

	// A `.live` that does not bind its generation is refused; nothing is written.
	const auto published = listing(root);
	auto unbound = generation(profile, "beta", cursor('b', 1), {}, 0, {}, 1, 'b');
	unbound.prepared.read[0] ^= 1;
	refuses<std::invalid_argument>([&] { store->publish({}, one(unbound)); });
	check(listing(root) == published);

	// Corruption on a name the table reaches fails that namespace at startup (D10); publishing the same
	// names refuses and leaves the table, and a repair at the next ordinal publishes new names.
	store.reset();
	const auto read = root / generation_file(next.id, ".read");
	overwrite(read, std::string(192, 'z'));
	store = open(root);
	check(store->failed().size() == 1 && store->failed()[0] == "alpha");
	const auto table = store->table();
	try {
		store->publish({}, one(next));
		check(false);
	} catch (const l::ReachedNameMismatch &mismatch) {
		check(mismatch.name == generation_file(next.id, ".read") && mismatch.space == "alpha");
	}
	check(store->table() == table && l::decode_table(slurp(root / "selected")) == table);
	for (const auto &name : listing(root))
		check(!name.starts_with(".tmp-"));
	const auto repair = generation(profile, "alpha", cursor('a', 6), {}, table.find("alpha")->ordinal + 1, {}, 2, 'r');
	check(repair.id != next.id);
	store->publish({}, one(repair));
	check(store->failed().empty() && store->table().find("alpha")->ordinal == 1 && table_verifies(root, store->table()));
	store.reset();
	store = open(root);
	check(store->failed().empty());
}

// The census the store keeps: every name but `.tmp-` ones, and one generation per `.lease`.
l::StoreUsage census(const std::filesystem::path &root) {
	l::StoreUsage usage;
	for (const auto &name : listing(root)) {
		check(!name.starts_with(".tmp-"));
		usage.bytes += std::filesystem::file_size(root / name);
		++usage.entries;
		usage.generations += name.starts_with("generation-") && name.ends_with(".lease");
	}
	return usage;
}
// A reader's shared lease on a generation, held until it goes out of scope.
struct Reader {
	explicit Reader(const std::filesystem::path &lease) : fd(::open(lease.c_str(), O_RDONLY | O_CLOEXEC)) {
		check(fd >= 0 && ::flock(fd, LOCK_SH | LOCK_NB) == 0);
	}
	~Reader() { ::close(fd); }
	Reader(const Reader &) = delete;
	int fd;
};

// A4: collect() removes every generation and segment the table does not reach, except a generation a
// reader leases and the segments its `.live` names; the census follows every cycle.
void collection() {
	const auto root = fresh("collection");
	auto store = open(root);
	check(store->usage() == census(root));
	const auto a = generation(profile, "alpha", cursor('a', 4), {}, 0, {}, 2, 'a');
	const auto b = generation(profile, "beta", cursor('b', 9), {}, 0, {}, 1, 'b');
	store->publish({}, std::array{a.prepared, b.prepared});
	check(store->usage() == census(root) && store->usage().generations == 2);
	check(store->collect() == l::StoreUsage{} && listing(root) == expected_names({&a, &b}));

	// alpha catches up keeping its first segment; its old generation and second segment are superseded.
	const auto next = generation(profile, "alpha", cursor('a', 6), a.id, 0, std::span(a.segments).first(1), 1, 'n');
	store->publish({}, one(next));
	check(store->usage() == census(root));
	const auto superseded = expected_names({&a, &b, &next});
	check(listing(root) == superseded);
	{
		// A reader still holds alpha's old generation: it stays, with every segment its `.live` names.
		Reader reader(root / generation_file(a.id, ".lease"));
		check(store->collect() == l::StoreUsage{} && listing(root) == superseded);
	}
	const auto before = store->usage();
	const auto removed = store->collect();
	check(listing(root) == expected_names({&b, &next}) && store->usage() == census(root));
	check(removed.generations == 1 && removed.entries == 4 + 2 && removed.bytes == before.bytes - store->usage().bytes);
	check(table_verifies(root, store->table()));

	// Orphans of a cut cycle (no lease, or a lease nobody holds) go too; names the store does not own stay.
	const auto stray = generation(profile, "gamma", cursor('g', 2), {}, 0, {}, 1, 'g');
	overwrite(root / generation_file(stray.id, ".image"), stray.prepared.image);
	overwrite(root / std::string(l::detail::SegmentNames(stray.prepared.segments[0].id).payload_name()), "partial");
	overwrite(root / "unrelated", "kept");
	store.reset();
	store = open(root);
	check(store->usage() == census(root));
	store->collect();
	auto kept = expected_names({&b, &next});
	kept.push_back("unrelated");
	std::sort(kept.begin(), kept.end());
	check(listing(root) == kept && store->usage() == census(root));
	store.reset();
	store = open(root);
	check(store->failed().empty() && table_verifies(root, store->table()));
}

// A cycle that derives an unreached generation a reader still leases (a revisited cursor) waits for it:
// LeaseHeld, nothing written; once the reader lets go the cycle relinks every name.
void held_lease() {
	const auto root = fresh("held-lease");
	auto store = open(root);
	const auto a = generation(profile, "alpha", cursor('a', 4), {}, 0, {}, 1, 'a');
	const auto b = generation(profile, "alpha", cursor('b', 4), {}, 0, {}, 1, 'b');
	store->publish({}, one(a));
	Reader reader(root / generation_file(a.id, ".lease"));
	store->publish({}, one(b));
	const auto names = listing(root);
	try {
		store->publish({}, one(a));
		check(false);
	} catch (const l::LeaseHeld &held) {
		check(held.name == generation_file(a.id, ".lease"));
	}
	check(listing(root) == names && store->table().find("alpha")->generation == b.id);
	::close(std::exchange(reader.fd, ::open(root.c_str(), O_RDONLY)));
	store->publish({}, one(a));
	check(store->table().find("alpha")->generation == a.id && table_verifies(root, store->table()) &&
		  store->usage() == census(root));
}

// A `change` cut short at its creation (a kill between create and size, a failed fchmod, or a power cut
// that kept its name but not its size) opens, with or without `selected` (D59, F1).
void short_change() {
	const auto root = fresh("short-change");
	overwrite(root / "change", "");
	auto store = open(root);
	check(std::filesystem::file_size(root / "change") == 8 && store->change_word(0) == 1);
	const auto a = generation(profile, "alpha", cursor('a', 4), {}, 0, {}, 1, 'a');
	store->publish({}, one(a));
	for (std::string_view bytes : {std::string_view(), std::string_view("abc")}) {
		const auto table = store->table();
		store.reset();
		overwrite(root / "change", bytes);
		store = open(root);
		check(store->table() == table && store->failed().empty() && std::filesystem::file_size(root / "change") == 16);
		store->publish({}, one(a));
	}
	// A ragged `change` past one word is no cut of the writer's, and still refuses the store.
	store.reset();
	overwrite(root / "change", std::string(12, '\0'));
	refuses<l::detail::ChangeRefused>([&] { open(root); });
}

// After a failed fsync the store restarts in the same boot, where the page cache still holds the bytes and
// a new fsync may report success. A name no durable table reaches is never reused: the pack whose fsync
// failed is relinked, never named by the table (D59, F2).
std::pair<dev_t, ino_t> g_failing{};
int failing_fsync(int fd) {
	struct stat status{};
	if (::fstat(fd, &status) == 0 && std::pair{status.st_dev, status.st_ino} == g_failing) {
		errno = EIO;
		return -1;
	}
	return ::fsync(fd);
}
#ifdef __APPLE__
int passing_fcntl(int fd, int command) { return ::fcntl(fd, command); }
void fail_syncs(c::PosixIO &io) { io.replace_sync_calls(&failing_fsync, &passing_fcntl); }
#elif defined(__linux__)
int passing_writeback(int) { return 0; }
void fail_syncs(c::PosixIO &io) { io.replace_sync_calls(&failing_fsync, &passing_writeback); }
#else
void fail_syncs(c::PosixIO &io) { io.replace_sync_calls(&failing_fsync); }
#endif
void failed_sync() {
	const auto root = fresh("failed-sync");
	const auto a = generation(profile, "alpha", cursor('a', 4), {}, 0, {}, 1, 'a');
	const auto pack = root / std::string(l::detail::SegmentNames(a.prepared.segments[0].id).payload_name());
	{
		auto io = std::make_shared<c::PosixIO>(root, 0644);
		fail_syncs(*io);
		l::AgentStore store(io, {profile});
		// The pack's `.tmp-` inode is the one linked at its name; fail its fsync when it comes.
		l::AgentStoreTesting::install(
			[](void *context, const char *what) noexcept {
				struct stat status{};
				if (std::string_view(what) == "link" && ::stat(static_cast<std::filesystem::path *>(context)->c_str(), &status) == 0)
					g_failing = {status.st_dev, status.st_ino};
			},
			const_cast<std::filesystem::path *>(&pack));
		refuses<std::system_error>([&] { store.publish({}, one(a)); });
		l::AgentStoreTesting::install(nullptr, nullptr);
		check(store.table().entries.empty() && std::filesystem::exists(pack));
	}
	const auto failed = g_failing;
	g_failing = {};
	check(failed.second != 0 && inode(pack) == failed.second);
	auto store = open(root);
	store->publish({}, one(a));
	check(inode(pack) != failed.second && slurp(pack) == a.prepared.segments[0].pack && table_verifies(root, store->table()));
}
} // namespace

int main(int argc, char **argv) {
	check(argc == 2);
	g_root = argv[1];
	std::filesystem::remove_all(g_root);
	std::filesystem::create_directories(g_root);
	try {
		derived_ids();
		table_codec();
		one_directory();
		publication();
		short_change();
		failed_sync();
		collection();
		held_lease();
	} catch (const std::exception &error) {
		std::fprintf(stderr, "test_agent_store: %s\n", error.what());
		return 1;
	}
	std::filesystem::remove_all(g_root);
	return 0;
}
