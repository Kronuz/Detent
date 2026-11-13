#include "detent_relay/mirror.hh"
#include <algorithm>
#include <cerrno>
#include <map>
#include <random>
#include <set>
#include <sha256.h>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>

namespace detent::relay {
namespace c = kronuz::io::completion;
namespace {
constexpr std::string_view table_magic = "DTMIRR01", table_name = "mirror", owner_name = "owner.lock",
						   temporary = ".tmp-";
constexpr std::size_t table_header = 32, maximum_policy = 1u << 16, maximum_files = 1u << 16,
					  maximum_entries = 1u << 20;

void (*testing_hook)(void *, const char *) noexcept = nullptr;
void *testing_context = nullptr;
void primitive(const char *what) noexcept {
	if (testing_hook)
		testing_hook(testing_context, what);
}
Digest digest(std::string_view bytes) {
	SHA256 hash;
	hash.add(bytes.data(), bytes.size());
	Digest out{};
	hash.getHash(reinterpret_cast<unsigned char *>(out.data()));
	return out;
}
bool valid_name(std::string_view name) {
	return !name.empty() && name.size() <= 255 && name.find('\0') == std::string_view::npos;
}
void put(std::string &out, std::uint64_t value, unsigned width) {
	for (unsigned i = 0; i < width; ++i)
		out.push_back(char((value >> (8 * i)) & 0xff));
}
template <std::size_t N> void put(std::string &out, const std::array<char, N> &value) {
	out.append(value.data(), N);
}
void put(std::string &out, std::string_view bytes, unsigned width) {
	put(out, bytes.size(), width);
	out.append(bytes);
}
void refuse(bool ok, const char *what) {
	if (!ok)
		throw MirrorRefused(std::string("relay mirror table: ") + what);
}
struct Reader {
	std::span<const char> in;
	std::size_t at = 0;
	std::uint64_t integer(unsigned width) {
		refuse(in.size() - at >= width, "truncated");
		std::uint64_t value = 0;
		for (unsigned i = 0; i < width; ++i)
			value |= std::uint64_t(static_cast<unsigned char>(in[at + i])) << (8 * i);
		at += width;
		return value;
	}
	std::string bytes(std::size_t size) {
		refuse(in.size() - at >= size, "truncated");
		std::string out(in.data() + at, size);
		at += size;
		return out;
	}
	template <std::size_t N> void array(std::array<char, N> &value) {
		refuse(in.size() - at >= N, "truncated");
		std::copy_n(in.begin() + at, N, value.begin());
		at += N;
	}
};
bool is_tier(MirrorFile::Kind kind) { return kind == MirrorFile::Kind::Block || kind == MirrorFile::Kind::Section; }
} // namespace

void MirrorTesting::install(void (*hook)(void *, const char *) noexcept, void *context) noexcept {
	testing_hook = hook;
	testing_context = context;
}

std::uint64_t NamespaceLayout::end() const noexcept {
	for (auto at = files.rbegin(); at != files.rend(); ++at)
		if (is_tier(at->kind))
			return at->last;
	return cut;
}

const NamespaceLayout *MirrorTable::find(std::string_view name) const noexcept {
	auto at = std::lower_bound(entries.begin(), entries.end(), name,
							   [](const NamespaceLayout &entry, std::string_view key) { return entry.name < key; });
	return at != entries.end() && at->name == name ? &*at : nullptr;
}

std::string mirror_file_name(std::string_view upstream_name) {
	static constexpr char hex[] = "0123456789abcdef";
	const auto sum = digest(upstream_name);
	std::string name = "m-";
	for (std::size_t i = 0; i < 16; ++i) {
		name.push_back(hex[static_cast<unsigned char>(sum[i]) >> 4]);
		name.push_back(hex[static_cast<unsigned char>(sum[i]) & 15]);
	}
	return name;
}

void check_layout(const NamespaceLayout &layout) {
	auto fail = [](const char *what) { throw std::invalid_argument(std::string("relay mirror layout: ") + what); };
	if (!valid_name(layout.name) || layout.policy.size() > maximum_policy || layout.files.size() > maximum_files)
		fail("namespace");
	std::set<std::string_view> names;
	auto previous = MirrorFile::Kind::Base;
	std::uint64_t end = layout.cut;
	for (std::size_t i = 0; i < layout.files.size(); ++i) {
		const auto &file = layout.files[i];
		if (!valid_name(file.name) || !names.insert(file.name).second)
			fail("file name");
		if (file.kind == MirrorFile::Kind::Base) {
			if (i != 0 || file.first != 0 || file.last != layout.cut)
				fail("the base is first, at the cut");
			continue;
		}
		if (file.kind < previous || (file.kind == MirrorFile::Kind::Section && previous == MirrorFile::Kind::Copy))
			fail("tier order");
		if (is_tier(file.kind)) {
			if (file.first != end + 1 || file.last < file.first)
				fail("tiers are not contiguous");
			end = file.last;
		} else if (i + 1 != layout.files.size() || file.first != 0 || file.last != end)
			fail("the copy is at s, last");
		previous = file.kind;
	}
}

std::string encode_mirror_table(const MirrorTable &table) {
	if (table.entries.size() > maximum_entries)
		throw std::length_error("relay mirror table entries");
	std::string out(table_magic);
	put(out, mirror_layout, 4);
	put(out, table.entries.size(), 4);
	put(out, table.sequence, 8);
	out.resize(table_header, '\0');
	std::set<std::string> names;
	for (std::size_t i = 0; i < table.entries.size(); ++i) {
		const auto &entry = table.entries[i];
		check_layout(entry);
		if (i && !(table.entries[i - 1].name < entry.name))
			throw std::invalid_argument("relay mirror table entry order");
		put(out, entry.name, 1);
		put(out, entry.incarnation);
		put(out, entry.seal, 8);
		put(out, entry.ordinal, 8);
		put(out, entry.cut, 8);
		put(out, entry.policy, 4);
		put(out, entry.files.size(), 4);
		for (const auto &file : entry.files) {
			if (!names.insert(mirror_file_name(file.name)).second)
				throw std::invalid_argument("relay mirror table: a file name repeats");
			put(out, std::uint64_t(file.kind), 1);
			put(out, file.first, 8);
			put(out, file.last, 8);
			put(out, file.size, 8);
			put(out, file.digest);
			put(out, file.name, 1);
		}
	}
	put(out, digest(out));
	return out;
}

MirrorTable decode_mirror_table(std::span<const char> in) {
	refuse(in.size() >= table_header + 32 && std::string_view(in.data(), 8) == table_magic, "header");
	const auto sum = digest({in.data(), in.size() - 32});
	refuse(std::equal(sum.begin(), sum.end(), in.end() - 32), "digest");
	Reader r{in.first(in.size() - 32), 8};
	refuse(r.integer(4) == mirror_layout, "layout");
	const auto count = r.integer(4);
	refuse(count <= maximum_entries, "entry count");
	MirrorTable table;
	table.sequence = r.integer(8);
	refuse(std::all_of(in.begin() + r.at, in.begin() + table_header, [](char byte) { return byte == 0; }), "padding");
	r.at = table_header;
	std::set<std::string> names;
	for (std::uint64_t i = 0; i < count; ++i) {
		NamespaceLayout entry;
		entry.name = r.bytes(r.integer(1));
		r.array(entry.incarnation);
		entry.seal = r.integer(8);
		entry.ordinal = r.integer(8);
		entry.cut = r.integer(8);
		const auto policy = r.integer(4);
		refuse(policy <= maximum_policy, "policy");
		entry.policy = r.bytes(policy);
		const auto files = r.integer(4);
		refuse(files <= maximum_files, "file count");
		for (std::uint64_t j = 0; j < files; ++j) {
			MirrorFile file;
			const auto kind = r.integer(1);
			refuse(kind >= 1 && kind <= 4, "file kind");
			file.kind = MirrorFile::Kind(kind);
			file.first = r.integer(8);
			file.last = r.integer(8);
			file.size = r.integer(8);
			r.array(file.digest);
			file.name = r.bytes(r.integer(1));
			refuse(names.insert(mirror_file_name(file.name)).second, "a file name repeats");
			entry.files.push_back(std::move(file));
		}
		try {
			check_layout(entry);
		} catch (const std::invalid_argument &) {
			refuse(false, "entry");
		}
		refuse(table.entries.empty() || table.entries.back().name < entry.name, "entry order");
		table.entries.push_back(std::move(entry));
	}
	refuse(r.at == r.in.size(), "trailing bytes");
	return table;
}

struct MirrorStore::Impl {
	std::shared_ptr<c::PosixIO> io;
	Options options;
	std::unique_ptr<c::OwnerLock> lock;
	MirrorTable table;
	std::vector<std::string> failed;
	// Mirror name -> the file the durable table reaches under it.
	std::map<std::string, const MirrorFile *, std::less<>> reached;
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
				reached.emplace(mirror_file_name(file.name), &file);
		failed.clear();
		for (const auto &entry : table.entries)
			for (const auto &file : entry.files)
				if (missing.contains(mirror_file_name(file.name))) {
					failed.push_back(entry.name);
					break;
				}
	}
	// Fetches `file` and checks it against the layout.
	std::string fetch(const MirrorFile &file, const MirrorFetch &from) {
		auto bytes = from(file);
		if (bytes.size() != file.size || digest(bytes) != file.digest)
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
		for (const auto &[name, file] : reached)
			if (!holds(name, *file))
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

AdoptStats MirrorStore::adopt(std::span<const NamespaceLayout> layouts, std::span<const std::string> dropped,
							  const MirrorFetch &from) {
	auto &s = *impl_;
	if (s.poisoned)
		throw std::logic_error("relay mirror needs a restart after a failed adoption");
	// Validation: the next table, whole, before anything is fetched or written.
	MirrorTable next = s.table;
	next.sequence = s.table.sequence + 1;
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
				if (*found->second != file)
					throw FetchRefused("relay mirror: the upstream changed an immutable file: " + file.name);
				if (!s.missing.contains(name)) {
					++stats.reused_files;
					continue;
				}
			}
			wanted.push_back({&file, std::move(name), {}, {}});
		}
	if (wanted.empty() && next.entries == s.table.entries)
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
			bytes = s.fetch(*want.file, from);
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
	if (found == s.reached.end() || *found->second != file)
		throw std::invalid_argument("relay mirror read: a file the table does not reach: " + file.name);
	if (!s.missing.contains(name)) {
		auto bytes = s.slurp(name, file.size);
		if (bytes && bytes->size() == file.size && s.verified.contains(name))
			return std::move(*bytes);
		if (bytes && bytes->size() == file.size && digest(*bytes) == file.digest) {
			s.verified.insert(name);
			primitive("verify");
			return std::move(*bytes);
		}
	}
	// Repair (D6 Recovery): the same bytes by name from the upstream, replacing the name durably.
	auto bytes = s.fetch(file, from);
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
