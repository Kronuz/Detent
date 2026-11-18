#include "detent_local/store_table.hh"
#include "detent_local/derived_ids.hh"
#include "detent_local/layout.hh"
#include <algorithm>
#include <set>
#include <sha256.h>

namespace detent::local {
namespace {
using Sha = std::array<char, 32>;
constexpr std::string_view table_magic = "DTTABLE1";
constexpr std::size_t table_header = 64, entry_fixed = 137, maximum_entries = 1u << 20;
Sha digest(std::string_view bytes) {
	SHA256 hash;
	hash.add(bytes.data(), bytes.size());
	Sha out{};
	hash.getHash(reinterpret_cast<unsigned char *>(out.data()));
	return out;
}
bool valid_name(std::string_view name) {
	return !name.empty() && name.size() <= 255 && name.find('\0') == std::string_view::npos;
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
} // namespace

std::uint64_t table_bytes(std::uint64_t entries) noexcept { return table_header + 32 + entries * (entry_fixed + 255); }
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

} // namespace detent::local
