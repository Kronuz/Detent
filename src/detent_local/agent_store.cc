#include "detent_local/agent_store.hh"
#include "change_file.hh"
#include "detent_local/layout.hh"
#include "live_record.hh"
#include "names.hh"
#include "segment_names.hh"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <sha256.h>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>

namespace detent::local {
namespace c = kronuz::io::completion;
namespace {
using Sha = std::array<char, 32>;
constexpr std::string_view table_magic = "DTTABLE1", selected = "selected", temporary = ".tmp-";
constexpr std::size_t table_header = 64, entry_fixed = 137, maximum_entries = 1u << 20;

void (*testing_hook)(void *, const char *) noexcept = nullptr;
void *testing_context = nullptr;
void primitive(const char *what) noexcept {
	if (testing_hook)
		testing_hook(testing_context, what);
}
Sha digest(std::string_view bytes) {
	SHA256 hash;
	hash.add(bytes.data(), bytes.size());
	Sha out{};
	hash.getHash(reinterpret_cast<unsigned char *>(out.data()));
	return out;
}
template <std::size_t N> bool nonzero(const std::array<char, N> &value) {
	return std::any_of(value.begin(), value.end(), [](char byte) { return byte != 0; });
}
void refuse(bool ok, const char *what) {
	if (!ok)
		throw TableRefused(std::string("agent store table: ") + what);
}
template <std::size_t N> void put(std::string &out, std::size_t &at, const std::array<char, N> &value) {
	std::copy(value.begin(), value.end(), out.begin() + at);
	at += N;
}
void put(std::string &out, std::size_t &at, std::uint64_t value, unsigned width) {
	write_integer(std::span<char>(out), at, value, width);
	at += width;
}
template <std::size_t N> void take(std::span<const char> in, std::size_t &at, std::array<char, N> &value) {
	std::copy_n(in.begin() + at, N, value.begin());
	at += N;
}
std::uint64_t take(std::span<const char> in, std::size_t &at, unsigned width) {
	const auto value = read_integer(in, at, width);
	at += width;
	return value;
}
std::string image_name(const GenerationId &id) { return std::string(detail::GenerationName(id, detail::GenerationFile::Image).view()); }
std::string read_name(const GenerationId &id) {
	return std::string(detail::GenerationName(id, detail::GenerationFile::ReaderMetadata).view());
}
std::string live_name(const GenerationId &id) { return std::string(detail::GenerationName(id, detail::GenerationFile::Live).view()); }
bool valid_name(std::string_view name) {
	return !name.empty() && name.size() <= 255 && name.find('\0') == std::string_view::npos;
}
} // namespace

void AgentStoreTesting::install(void (*hook)(void *, const char *) noexcept, void *context) noexcept {
	testing_hook = hook;
	testing_context = context;
}

const TableEntry *StoreTable::find(std::string_view name) const noexcept {
	auto at = std::lower_bound(entries.begin(), entries.end(), name,
							   [](const TableEntry &entry, std::string_view key) { return entry.name < key; });
	return at != entries.end() && at->name == name ? &*at : nullptr;
}

std::string encode_table(const StoreTable &table) {
	if (table.entries.size() > maximum_entries)
		throw std::length_error("agent store table entries");
	std::size_t size = table_header + 32;
	for (std::size_t i = 0; i < table.entries.size(); ++i) {
		const auto &entry = table.entries[i];
		if (!valid_name(entry.name) || (i && !(table.entries[i - 1].name < entry.name)) || !nonzero(entry.generation))
			throw std::invalid_argument("agent store table entry");
		size += entry_fixed + entry.name.size();
	}
	std::string out(size, '\0');
	std::size_t at = 0;
	std::copy(table_magic.begin(), table_magic.end(), out.begin());
	at = table_magic.size();
	put(out, at, agent_store_layout, 4);
	put(out, at, table.entries.size(), 4);
	put(out, at, table.sequence, 8);
	put(out, at, table.directory.cluster);
	put(out, at, table.directory.position, 8);
	put(out, at, table.profile.semantic_id, 4);
	put(out, at, table.profile.required_capabilities, 8);
	at = table_header;
	for (const auto &entry : table.entries) {
		put(out, at, entry.name.size(), 1);
		std::copy(entry.name.begin(), entry.name.end(), out.begin() + at);
		at += entry.name.size();
		put(out, at, entry.cursor.cluster);
		put(out, at, entry.cursor.incarnation);
		put(out, at, entry.cursor.position, 8);
		put(out, at, entry.slot, 4);
		put(out, at, entry.generation);
		put(out, at, entry.ordinal, 4);
		put(out, at, entry.root_length, 8);
		put(out, at, entry.root);
		put(out, at, entry.read);
	}
	put(out, at, digest(std::string_view(out).substr(0, at)));
	return out;
}

StoreTable decode_table(std::span<const char> in) {
	refuse(in.size() >= table_header + 32 && std::string_view(in.data(), 8) == table_magic, "header");
	const auto sum = digest({in.data(), in.size() - 32});
	refuse(std::equal(sum.begin(), sum.end(), in.end() - 32), "digest");
	std::size_t at = 8;
	refuse(take(in, at, 4) == agent_store_layout, "layout");
	const auto count = take(in, at, 4);
	refuse(count <= maximum_entries, "entry count");
	StoreTable table;
	table.sequence = take(in, at, 8);
	take(in, at, table.directory.cluster);
	table.directory.position = take(in, at, 8);
	table.profile.semantic_id = static_cast<std::uint32_t>(take(in, at, 4));
	table.profile.required_capabilities = take(in, at, 8);
	refuse(std::all_of(in.begin() + at, in.begin() + table_header, [](char byte) { return byte == 0; }), "padding");
	at = table_header;
	const auto end = in.size() - 32;
	std::set<std::uint32_t> slots;
	table.entries.reserve(count);
	for (std::uint64_t i = 0; i < count; ++i) {
		refuse(at < end, "truncated entry");
		const auto length = static_cast<std::size_t>(take(in, at, 1));
		refuse(length && end - at >= length + entry_fixed - 1, "truncated entry");
		TableEntry entry;
		entry.name.assign(in.data() + at, length);
		at += length;
		take(in, at, entry.cursor.cluster);
		take(in, at, entry.cursor.incarnation);
		entry.cursor.position = take(in, at, 8);
		entry.slot = static_cast<std::uint32_t>(take(in, at, 4));
		take(in, at, entry.generation);
		entry.ordinal = static_cast<std::uint32_t>(take(in, at, 4));
		entry.root_length = take(in, at, 8);
		take(in, at, entry.root);
		take(in, at, entry.read);
		refuse(valid_name(entry.name) && nonzero(entry.generation), "entry");
		refuse(table.entries.empty() || table.entries.back().name < entry.name, "entry order");
		refuse(slots.insert(entry.slot).second, "duplicate slot");
		table.entries.push_back(std::move(entry));
	}
	refuse(at == end, "trailing bytes");
	return table;
}

struct AgentStore::Impl {
	std::shared_ptr<c::PosixIO> io;
	Options options;
	std::unique_ptr<c::OwnerLock> lock;
	std::optional<detail::ChangeWriter> change;
	StoreTable table;
	std::vector<std::string> failed;
	std::map<std::string, std::vector<SegmentId>, std::less<>> segments; // what each entry's `.live` names
	std::set<std::string, std::less<>> reached;
	std::mt19937_64 random{std::random_device{}()};
	bool poisoned = false;

	int directory() const { return io->native_directory_handle(); }
	// The whole file, or nothing when it is absent or not a singly linked file of ours.
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
		std::size_t done = 0;
		while (done < bytes.size()) {
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
	// Whether an entry's `.image`, `.read` and `.live` verify against it and its packs and owner records
	// exist. `reach` gets the segments its `.live` names whenever that record decodes for the entry, so a
	// failed namespace's files stay protected from the orphan rule.
	bool verify(const TableEntry &entry, std::vector<SegmentId> &reach) {
		const auto image = slurp(image_name(entry.generation), entry.root_length);
		const auto read = slurp(read_name(entry.generation), 1u << 16);
		const auto live = slurp(live_name(entry.generation), detail::live_record_size(options.maximum_segments));
		if (!live)
			return false;
		detail::LiveRecord record;
		try {
			record = detail::decode_live_record(*live, options.maximum_segments);
		} catch (const std::invalid_argument &) {
			return false;
		}
		if (record.generation != entry.generation)
			return false;
		bool present = true;
		for (const auto &row : record.rows) {
			reach.push_back(row.segment);
			const detail::SegmentNames names(row.segment);
			const auto pack = io->inspect_entry(names.payload_name());
			present = present && pack && pack->footprint.kind == c::EntryKind::Regular &&
					  pack->footprint.logical_bytes == row.length && io->inspect_entry(names.owner_name());
		}
		return present && image && read && image->size() == entry.root_length && digest(*image) == entry.root &&
			   digest(*read) == entry.read && record.root_checksum == entry.root && record.read_digest == entry.read;
	}
	// The names the durable table reaches: each entry's generation files and its `.live` segments.
	void rebuild_reached() {
		reached.clear();
		for (const auto &entry : table.entries) {
			reached.insert(image_name(entry.generation));
			reached.insert(read_name(entry.generation));
			reached.insert(live_name(entry.generation));
			for (const auto &segment : segments[entry.name]) {
				const detail::SegmentNames names(segment);
				reached.emplace(names.payload_name());
				reached.emplace(names.owner_name());
			}
		}
	}
	void startup() {
		struct stat status{};
		if (::fstat(directory(), &status) != 0)
			throw std::system_error(errno, std::generic_category(), "agent store directory");
		if (status.st_uid != ::geteuid() || (status.st_mode & 0022))
			throw StoreRefused("agent store directory is not the agent's own");
		const bool fresh = !io->inspect_entry("owner.lock");
		lock = io->acquire_owner(c::OwnerLockOptions{c::OwnerLockMode::file,
													 fresh ? c::OwnerFile::create_exclusive : c::OwnerFile::open_existing});
		change.emplace(directory(), 0, status.st_uid, options.mode);
		io->sync_directory();
		primitive("startup barrier");
		std::vector<std::string> leftovers;
		for (auto cursor = io->scan_directory(); auto name = cursor->next();)
			if (name->starts_with(temporary))
				leftovers.push_back(std::move(*name));
		for (const auto &name : leftovers) {
			io->remove(name);
			primitive("remove temporary");
		}
		if (io->inspect_entry(selected)) {
			const auto bytes = slurp(selected, table_header + 32 + maximum_entries * (entry_fixed + 255));
			if (!bytes)
				throw StoreRefused("agent store table unreadable");
			table = decode_table(*bytes);
			if (table.profile != options.profile)
				throw StoreRefused("agent store table has another profile");
		} else
			table.profile = options.profile;
		std::size_t slots = 0;
		for (const auto &entry : table.entries) {
			slots = std::max<std::size_t>(slots, std::size_t(entry.slot) + 1);
			if (!verify(entry, segments[entry.name]))
				failed.push_back(entry.name);
		}
		rebuild_reached();
		change->reserve(slots);
		change->bump_all();
		primitive("bump change");
	}
};

AgentStore::AgentStore(std::shared_ptr<c::PosixIO> io, Options options) : impl_(std::make_unique<Impl>()) {
	if (!io || (options.mode != 0600 && options.mode != 0640 && options.mode != 0644))
		throw std::invalid_argument("agent store options");
	impl_->io = std::move(io);
	impl_->options = options;
	impl_->startup();
}
AgentStore::~AgentStore() = default;
const StoreTable &AgentStore::table() const noexcept { return impl_->table; }
std::span<const std::string> AgentStore::failed() const noexcept { return impl_->failed; }
std::uint64_t AgentStore::change_word(std::size_t index) const noexcept { return impl_->change->word(index); }

std::uint64_t AgentStore::publish(const DirectoryPosition &directory, std::span<const PreparedGeneration> cycle) {
	auto &s = *impl_;
	if (s.poisoned)
		throw std::logic_error("agent store needs a restart after a failed publication");
	struct Staged {
		std::string name, temporary, space;
		std::string_view bytes;
		Sha sum;
		bool owner;
		std::unique_ptr<c::File> file;
		bool reused = false;
	};
	struct Built {
		TableEntry entry;
		std::vector<SegmentId> segments;
	};
	// Validation: nothing is written for a cycle whose files disagree with their derived names.
	std::vector<Staged> files;
	std::vector<Built> built;
	std::set<std::string, std::less<>> names;
	for (const auto &generation : cycle) {
		if (!valid_name(generation.name) || !names.insert(generation.name).second)
			throw std::invalid_argument("agent store publication namespace");
		Built b;
		b.entry.name = generation.name;
		b.entry.cursor = generation.cursor;
		b.entry.ordinal = generation.ordinal;
		b.entry.generation = derive_generation(s.options.profile, generation.cursor.incarnation,
											   generation.cursor.position, generation.base, generation.ordinal);
		b.entry.root_length = generation.image.size();
		b.entry.root = digest(generation.image);
		b.entry.read = digest(generation.read);
		const auto record = detail::decode_live_record(generation.live, s.options.maximum_segments);
		if (record.generation != b.entry.generation || record.root_checksum != b.entry.root ||
			record.read_digest != b.entry.read)
			throw std::invalid_argument("agent store publication: .live does not bind its generation");
		std::map<SegmentId, const PreparedGeneration::Segment *> fresh;
		for (const auto &segment : generation.segments)
			if (!fresh.emplace(segment.id, &segment).second)
				throw std::invalid_argument("agent store publication: duplicate segment");
		for (const auto &row : record.rows) {
			const detail::SegmentNames segment_names(row.segment);
			b.segments.push_back(row.segment);
			const auto found = fresh.find(row.segment);
			if (found == fresh.end()) {
				if (!s.reached.contains(segment_names.payload_name()))
					throw std::invalid_argument("agent store publication: a base segment the table does not reach");
				continue;
			}
			const auto &segment = *found->second;
			if (segment.pack.size() != row.length || digest(segment.pack) != row.checksum)
				throw std::invalid_argument("agent store publication: a pack differs from its .live row");
			files.push_back({std::string(segment_names.owner_name()), {}, generation.name, segment.owner,
							 digest(segment.owner), true, {}});
			files.push_back({std::string(segment_names.payload_name()), {}, generation.name, segment.pack,
							 row.checksum, false, {}});
			fresh.erase(found);
		}
		if (!fresh.empty())
			throw std::invalid_argument("agent store publication: a segment its .live does not name");
		const auto id = b.entry.generation;
		files.push_back({image_name(id), {}, generation.name, generation.image, b.entry.root, false, {}});
		files.push_back({read_name(id), {}, generation.name, generation.read, b.entry.read, false, {}});
		files.push_back({live_name(id), {}, generation.name, generation.live, digest(generation.live), false, {}});
		built.push_back(std::move(b));
	}
	for (std::size_t i = 0; i < files.size(); ++i)
		for (std::size_t j = 0; j < i; ++j)
			if (files[i].name == files[j].name)
				throw std::invalid_argument("agent store publication: a name published twice");

	auto clean = [&] {
		for (auto &file : files)
			if (!file.temporary.empty())
				s.io->remove(file.temporary);
	};
	try {
		// Stage: every file under a `.tmp-` name, unsynced.
		for (auto &file : files) {
			file.temporary = s.temporary_name();
			file.file = s.io->create_exclusive(file.temporary);
			primitive("create temporary");
			for (std::size_t done = 0; done < file.bytes.size();)
				done += file.file->write_at(done, file.bytes.substr(done));
			primitive("write temporary");
		}
		// Link: linkat fails on an existing name. One the durable table reaches is reused when its bytes
		// verify; any other may hold bytes whose sync failed or never ran, so it is unlinked and relinked.
		for (auto &file : files) {
			for (;;) {
				if (::linkat(s.directory(), file.temporary.c_str(), s.directory(), file.name.c_str(), 0) == 0) {
					primitive("link");
					break;
				}
				if (errno == EINTR)
					continue;
				if (errno != EEXIST)
					throw std::system_error(errno, std::generic_category(), "agent store link");
				if (s.reached.contains(file.name)) {
					const auto existing = s.slurp(file.name, file.bytes.size());
					if (!existing || digest(*existing) != file.sum)
						throw ReachedNameMismatch(file.name, file.space);
					file.reused = true;
					primitive("verify reused name");
					break;
				}
				s.io->remove(file.name);
				primitive("unlink orphan");
			}
			s.io->remove(file.temporary);
			file.temporary.clear();
			primitive("unlink temporary");
		}
	} catch (const ReachedNameMismatch &) {
		clean();
		throw;
	} catch (...) {
		s.poisoned = true;
		throw;
	}
	try {
		std::vector<c::File *> group;
		for (auto &file : files)
			if (file.owner && !file.reused)
				group.push_back(file.file.get());
		s.io->sync_files_and_directory(group);
		primitive("sync owner records");
		group.clear();
		for (auto &file : files)
			if (!file.owner && !file.reused)
				group.push_back(file.file.get());
		s.io->sync_files_and_directory(group);
		primitive("sync payloads");

		StoreTable next = s.table;
		next.sequence = s.table.sequence + 1;
		next.directory = directory;
		std::uint32_t slots = 0;
		for (const auto &entry : next.entries)
			slots = std::max(slots, entry.slot + 1);
		std::vector<std::uint32_t> changed;
		for (auto &b : built) {
			auto at = std::lower_bound(next.entries.begin(), next.entries.end(), b.entry.name,
									   [](const TableEntry &entry, const std::string &key) { return entry.name < key; });
			if (at != next.entries.end() && at->name == b.entry.name) {
				b.entry.slot = at->slot;
				if (*at != b.entry)
					changed.push_back(at->slot);
				*at = b.entry;
			} else {
				b.entry.slot = slots++;
				changed.push_back(b.entry.slot);
				next.entries.insert(at, b.entry);
			}
		}
		const auto bytes = encode_table(next);
		const auto name = s.temporary_name();
		auto table = s.io->create_exclusive(name);
		primitive("create table");
		for (std::size_t done = 0; done < bytes.size();)
			done += table->write_at(done, std::string_view(bytes).substr(done));
		primitive("write table");
		c::File *sync[] = {table.get()};
		s.io->sync_files(sync);
		primitive("sync table");
		s.io->replace(name, selected);
		primitive("rename table");
		s.io->sync_directory();
		primitive("sync directory");
		s.change->reserve(slots);
		std::sort(changed.begin(), changed.end());
		s.change->publish(changed);
		primitive("change");

		s.table = std::move(next);
		for (auto &b : built) {
			s.segments[b.entry.name] = std::move(b.segments);
			std::erase(s.failed, b.entry.name);
		}
		s.rebuild_reached();
	} catch (...) {
		s.poisoned = true;
		throw;
	}
	return s.table.sequence;
}
} // namespace detent::local
