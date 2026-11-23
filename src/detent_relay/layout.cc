#include "detent_relay/layout.hh"
#include <algorithm>
#include <set>
#include <sha256.h>

namespace detent::relay {
namespace {
constexpr std::string_view table_magic = "DTMIRR01";
constexpr std::size_t table_header = 32, maximum_policy = 1u << 16, maximum_files = 1u << 16,
					  maximum_entries = 1u << 20;

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

Digest digest_of(std::string_view bytes) { return digest(bytes); }

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
	put(out, table.seal, 8);
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
	table.seal = r.integer(8);
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
} // namespace detent::relay
