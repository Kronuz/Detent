#include "detent_relay/mirror.hh"
#include <algorithm>
#include <cerrno>
#include <map>
#include <random>
#include <set>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>

namespace detent::relay {
namespace c = kronuz::io::completion;
namespace {
constexpr std::string_view table_name = "mirror", owner_name = "owner.lock", temporary = ".tmp-";

void (*testing_hook)(void *, const char *) noexcept = nullptr;
void *testing_context = nullptr;
void primitive(const char *what) noexcept {
	if (testing_hook)
		testing_hook(testing_context, what);
}
} // namespace

void MirrorTesting::install(void (*hook)(void *, const char *) noexcept, void *context) noexcept {
	testing_hook = hook;
	testing_context = context;
}

struct MirrorStore::Impl {
	std::shared_ptr<c::PosixIO> io;
	Options options;
	std::unique_ptr<c::OwnerLock> lock;
	MirrorTable table;
	std::vector<std::string> failed;
	// Mirror name -> the file the durable table reaches under it, and its namespace.
	struct Reach {
		std::string_view name_space;
		const MirrorFile *file;
	};
	std::map<std::string, Reach, std::less<>> reached;
	std::set<std::string, std::less<>> missing, verified;
	std::mt19937_64 random{std::random_device{}()};
	bool poisoned = false;

	int directory() const { return io->native_directory_handle(); }
	std::optional<std::string> slurp(std::string_view name, std::uint64_t limit) {
		std::unique_ptr<c::File> file;
		try {
			file = io->open_existing_read_only(name);
		} catch (const std::exception &) {
			return std::nullopt;
		}
		const auto size = file->size();
		if (size > limit)
			return std::nullopt;
		std::string bytes(size, '\0');
		for (std::size_t done = 0; done < bytes.size();) {
			const auto got = file->read_at(done, std::span<char>(bytes).subspan(done));
			if (!got)
				return std::nullopt;
			done += got;
		}
		return bytes;
	}
	std::string temporary_name() {
		static constexpr char hex[] = "0123456789abcdef";
		std::string name(temporary);
		auto value = random();
		for (int i = 0; i < 16; ++i, value >>= 4)
			name.push_back(hex[value & 15]);
		return name;
	}
	bool holds(std::string_view name, const MirrorFile &file) {
		const auto entry = io->inspect_entry(name);
		return entry && entry->footprint.kind == c::EntryKind::Regular && entry->footprint.logical_bytes == file.size;
	}
	void rebuild() {
		reached.clear();
		for (const auto &entry : table.entries)
			for (const auto &file : entry.files)
				reached.emplace(mirror_file_name(file.name), Reach{entry.name, &file});
		failed.clear();
		for (const auto &entry : table.entries)
			for (const auto &file : entry.files)
				if (missing.contains(mirror_file_name(file.name))) {
					failed.push_back(entry.name);
					break;
				}
	}
	// Fetches `file` and checks it against the layout.
	std::string fetch(std::string_view name_space, const MirrorFile &file, const MirrorFetch &from) {
		auto bytes = from(name_space, file);
		if (bytes.size() != file.size || digest_of(bytes) != file.digest)
			throw FetchRefused("relay mirror: fetched bytes differ from the layout: " + file.name);
		return bytes;
	}
	std::unique_ptr<c::File> stage(std::string &name, std::string_view bytes) {
		name = temporary_name();
		auto file = io->create_exclusive(name);
		primitive("create temporary");
		for (std::size_t done = 0; done < bytes.size();)
			done += file->write_at(done, bytes.substr(done));
		primitive("write temporary");
		return file;
	}
	void publish_table(const MirrorTable &next) {
		const auto bytes = encode_mirror_table(next);
		std::string name;
		auto file = stage(name, bytes);
		c::File *sync[] = {file.get()};
		io->sync_files(sync);
		primitive("sync table");
		io->replace(name, table_name);
		primitive("rename table");
		io->sync_directory();
		primitive("sync directory");
	}
	void startup() {
		struct stat status{};
		if (::fstat(directory(), &status) != 0)
			throw std::system_error(errno, std::generic_category(), "relay mirror directory");
		if (status.st_uid != ::geteuid() || (status.st_mode & 0022))
			throw MirrorRefused("relay mirror directory is not the relay's own");
		const bool fresh = !io->inspect_entry(owner_name);
		lock = io->acquire_owner(
			c::OwnerLockOptions{c::OwnerLockMode::file, fresh ? c::OwnerFile::create_exclusive : c::OwnerFile::open_existing});
		io->sync_directory();
		primitive("startup barrier");
		if (io->inspect_entry(table_name)) {
			const auto bytes = slurp(table_name, options.maximum_table);
			if (!bytes)
				throw MirrorRefused("relay mirror table unreadable");
			table = decode_mirror_table(*bytes);
		}
		rebuild();
		for (const auto &[name, reach] : reached)
			if (!holds(name, *reach.file))
				missing.insert(name);
		rebuild();
		std::vector<std::string> leftovers;
		for (auto cursor = io->scan_directory(); auto name = cursor->next();)
			if (*name != table_name && *name != owner_name && !reached.contains(*name))
				leftovers.push_back(std::move(*name));
		for (const auto &name : leftovers) {
			io->remove(name);
			primitive(name.starts_with(temporary) ? "remove temporary" : "remove orphan");
		}
	}
};

MirrorStore::MirrorStore(std::shared_ptr<c::PosixIO> io, Options options) : impl_(std::make_unique<Impl>()) {
	if (!io || (options.mode != 0600 && options.mode != 0640 && options.mode != 0644))
		throw std::invalid_argument("relay mirror options");
	impl_->io = std::move(io);
	impl_->options = options;
	impl_->startup();
}
MirrorStore::~MirrorStore() = default;
const MirrorTable &MirrorStore::table() const noexcept { return impl_->table; }
std::span<const std::string> MirrorStore::failed() const noexcept { return impl_->failed; }

AdoptStats MirrorStore::adopt(std::uint64_t seal, std::span<const NamespaceLayout> layouts,
							  std::span<const std::string> dropped, const MirrorFetch &from) {
	auto &s = *impl_;
	if (s.poisoned)
		throw std::logic_error("relay mirror needs a restart after a failed adoption");
	if (seal < s.table.seal)
		throw std::invalid_argument("relay mirror adoption: a seal older than the table's");
	// Validation: the next table, whole, before anything is fetched or written.
	MirrorTable next = s.table;
	next.sequence = s.table.sequence + 1;
	next.seal = seal;
	std::set<std::string_view> touched;
	auto locate = [&](std::string_view name) {
		return std::lower_bound(next.entries.begin(), next.entries.end(), name,
								[](const NamespaceLayout &entry, std::string_view key) { return entry.name < key; });
	};
	for (const auto &name : dropped) {
		if (!touched.insert(name).second)
			throw std::invalid_argument("relay mirror adoption: a namespace twice");
		if (auto at = locate(name); at != next.entries.end() && at->name == name)
			next.entries.erase(at);
	}
	for (const auto &layout : layouts) {
		check_layout(layout);
		if (!touched.insert(layout.name).second)
			throw std::invalid_argument("relay mirror adoption: a namespace twice");
		auto at = locate(layout.name);
		if (at != next.entries.end() && at->name == layout.name)
			*at = layout;
		else
			next.entries.insert(at, layout);
	}
	struct Wanted {
		std::string_view name_space;
		const MirrorFile *file;
		std::string name, temporary;
		std::unique_ptr<c::File> handle;
	};
	std::vector<Wanted> wanted;
	std::set<std::string> names;
	AdoptStats stats;
	for (const auto &entry : next.entries)
		for (const auto &file : entry.files) {
			auto name = mirror_file_name(file.name);
			if (!names.insert(name).second)
				throw std::invalid_argument("relay mirror adoption: a file name repeats");
			if (const auto found = s.reached.find(name); found != s.reached.end()) {
				if (*found->second.file != file)
					throw FetchRefused("relay mirror: the upstream changed an immutable file: " + file.name);
				if (!s.missing.contains(name)) {
					++stats.reused_files;
					continue;
				}
			}
			wanted.push_back({entry.name, &file, std::move(name), {}, {}});
		}
	if (wanted.empty() && next.entries == s.table.entries && next.seal == s.table.seal)
		return stats; // the durable table already holds this layout
	auto clean = [&] {
		for (auto &want : wanted)
			if (!want.temporary.empty())
				s.io->remove(want.temporary);
	};
	// Fetch and stage, one file in memory at a time. A failed or refused fetch publishes nothing and
	// leaves the mirror as it was; only an I/O error needs a restart.
	for (auto &want : wanted) {
		std::string bytes;
		try {
			bytes = s.fetch(want.name_space, *want.file, from);
		} catch (...) {
			clean();
			throw;
		}
		try {
			want.handle = s.stage(want.temporary, bytes);
		} catch (...) {
			s.poisoned = true;
			throw;
		}
		++stats.fetched_files;
		stats.fetched_bytes += bytes.size();
	}
	try {
		// Link. An existing name may be one the durable table reaches (failed at startup), so its
		// replacement is synced before it is renamed over it, as a repair is.
		for (auto &want : wanted) {
			for (;;) {
				if (::linkat(s.directory(), want.temporary.c_str(), s.directory(), want.name.c_str(), 0) == 0) {
					primitive("link");
					s.io->remove(want.temporary);
					primitive("unlink temporary");
					break;
				}
				if (errno == EINTR)
					continue;
				if (errno != EEXIST)
					throw std::system_error(errno, std::generic_category(), "relay mirror link");
				c::File *sync[] = {want.handle.get()};
				s.io->sync_files(sync);
				primitive("sync replacement");
				s.io->replace(want.temporary, want.name);
				primitive("rename replacement");
				break;
			}
			want.temporary.clear();
		}
		if (!wanted.empty()) {
			std::vector<c::File *> group;
			for (auto &want : wanted)
				group.push_back(want.handle.get());
			s.io->sync_files_and_directory(group);
			primitive("sync files");
		}
		s.publish_table(next);
		const auto before = s.reached;
		s.table = std::move(next);
		for (auto &want : wanted) {
			s.missing.erase(want.name);
			s.verified.insert(want.name);
		}
		s.rebuild();
		// Collector: files the durable table no longer reaches. A cut before the unlinks leaves orphans,
		// which startup removes.
		for (const auto &[name, file] : before)
			if (!s.reached.contains(name)) {
				s.io->remove(name); // a file missing since startup is no error
				s.missing.erase(name);
				s.verified.erase(name);
				++stats.collected_files;
				primitive("collect");
			}
	} catch (...) {
		s.poisoned = true;
		throw;
	}
	return stats;
}

std::string MirrorStore::read(const MirrorFile &file, const MirrorFetch &from) {
	auto &s = *impl_;
	if (s.poisoned)
		throw std::logic_error("relay mirror needs a restart after a failed adoption");
	const auto name = mirror_file_name(file.name);
	const auto found = s.reached.find(name);
	if (found == s.reached.end() || *found->second.file != file)
		throw std::invalid_argument("relay mirror read: a file the table does not reach: " + file.name);
	if (!s.missing.contains(name)) {
		auto bytes = s.slurp(name, file.size);
		if (bytes && bytes->size() == file.size && s.verified.contains(name))
			return std::move(*bytes);
		if (bytes && bytes->size() == file.size && digest_of(*bytes) == file.digest) {
			s.verified.insert(name);
			primitive("verify");
			return std::move(*bytes);
		}
	}
	// Repair (D6 Recovery): the same bytes by name from the upstream, replacing the name durably.
	auto bytes = s.fetch(found->second.name_space, file, from);
	try {
		std::string temporary_name;
		auto handle = s.stage(temporary_name, bytes);
		c::File *sync[] = {handle.get()};
		s.io->sync_files(sync);
		primitive("sync repair");
		s.io->replace(temporary_name, name);
		primitive("rename repair");
		s.io->sync_directory();
		primitive("sync directory");
	} catch (...) {
		s.poisoned = true;
		throw;
	}
	s.missing.erase(name);
	s.verified.insert(name);
	s.rebuild();
	return bytes;
}
} // namespace detent::relay
