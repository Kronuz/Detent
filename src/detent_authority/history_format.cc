#include "history_format.hh"
#include "detent/validation.hh"
#include <algorithm>
#include <array>
#include <charconv>
#include <sha256.h>
#include <stdexcept>

namespace detent::authority::history {
namespace {
constexpr std::uint32_t tombstone = 0xffffffffu;
void require(bool condition, const char *message) {
	if (!condition)
		throw std::invalid_argument(message);
}
struct Writer {
	std::string bytes;
	void raw(std::string_view s) { bytes.append(s); }
	void word(std::uint64_t n, unsigned width = 8) {
		for (unsigned i = 0; i < width; ++i)
			bytes.push_back(static_cast<char>(n >> (i * 8)));
	}
	void octet(unsigned n) { bytes.push_back(static_cast<char>(n)); }
	void text(std::string_view s) {
		word(s.size());
		raw(s);
	}
	void identity(const Identity &id) { raw({id.data(), id.size()}); }
	void digest(const Digest &d) { raw({reinterpret_cast<const char *>(d.data()), d.size()}); }
};
struct Reader {
	std::string_view bytes;
	std::string_view raw(std::size_t n) {
		require(n <= bytes.size(), "truncated history file");
		auto s = bytes.substr(0, n);
		bytes.remove_prefix(n);
		return s;
	}
	std::uint64_t word(unsigned width = 8) {
		auto s = raw(width);
		std::uint64_t n = 0;
		for (unsigned i = 0; i < width; ++i)
			n |= std::uint64_t{static_cast<unsigned char>(s[i])} << (8 * i);
		return n;
	}
	unsigned octet() { return static_cast<unsigned char>(raw(1)[0]); }
	std::string_view text(std::size_t cap) {
		auto n = word();
		require(n <= cap, "history string bound");
		return raw(static_cast<std::size_t>(n));
	}
	Identity identity() {
		Identity id{};
		auto s = raw(id.size());
		std::copy(s.begin(), s.end(), id.begin());
		return id;
	}
	Digest digest() {
		Digest d{};
		auto s = raw(d.size());
		std::copy(s.begin(), s.end(), reinterpret_cast<char *>(d.data()));
		return d;
	}
	void end() { require(bytes.empty(), "trailing history bytes"); }
};
bool valid_namespace(std::string_view name) {
	return !name.empty() && name.size() < 200 && is_valid_knob_name(std::string(name) + ":x");
}
void write_changes(Writer &w, const Changes &changes) {
	w.word(changes.size());
	for (const auto &c : changes) {
		w.word(c.key.size(), 4);
		w.raw(c.key);
		w.word(c.revision);
		w.word(c.definition ? c.definition->size() : tombstone, 4);
		if (c.definition)
			w.raw(*c.definition);
	}
}
// Keys sorted, unique and in the namespace; revisions in [low, high]; definitions canonical.
Changes read_changes(Reader &r, std::string_view name_space, Revision low, Revision high, bool tombstones,
					 const Limits &limits) {
	auto count = r.word();
	require(count <= limits.keys + (tombstones ? std::uint64_t{limits.keys} * 64 : 0), "history change count");
	Changes changes;
	changes.reserve(std::min<std::uint64_t>(count, 65536));
	for (std::uint64_t i = 0; i < count; ++i) {
		Change c;
		auto length = r.word(4);
		require(length <= 200, "history key bound");
		c.key = std::string(r.raw(length));
		c.revision = r.word();
		auto size = r.word(4);
		require(is_valid_knob_name(c.key) && std::string_view(c.key).substr(0, c.key.find(':')) == name_space &&
					(changes.empty() || changes.back().key < c.key) && c.revision >= low && c.revision <= high,
				"invalid history change");
		if (size == tombstone)
			require(tombstones, "tombstone in a base");
		else {
			require(size <= limits.definition_bytes, "history definition bound");
			c.definition = std::string(r.raw(size));
			require(encode_definition(decode_definition(*c.definition, limits), limits) == *c.definition,
					"noncanonical history definition");
		}
		changes.push_back(std::move(c));
	}
	return changes;
}
void write_block(Writer &w, const TableBlock &b) {
	w.word(b.first);
	w.word(b.last);
	w.word(b.bytes);
	w.digest(b.digest);
	w.word(b.seal);
	w.octet(b.carried);
}
TableBlock read_block(Reader &r) {
	TableBlock b;
	b.first = r.word();
	b.last = r.word();
	b.bytes = r.word();
	b.digest = r.digest();
	b.seal = r.word();
	auto carried = r.octet();
	require(carried <= 1, "invalid table flag");
	b.carried = carried;
	return b;
}
// Appends the SHA-256 of everything written so far.
std::string sealed(Writer &w) {
	w.digest(sha256(w.bytes));
	return std::move(w.bytes);
}
std::string_view verified(std::string_view bytes) {
	require(bytes.size() >= 32, "truncated history file");
	auto body = bytes.substr(0, bytes.size() - 32);
	auto expected = sha256(body);
	require(std::equal(expected.begin(), expected.end(),
					   reinterpret_cast<const unsigned char *>(bytes.data() + body.size())),
			"history file digest mismatch");
	return body;
}
} // namespace

std::uint64_t bytes(const Changes &changes) noexcept {
	std::uint64_t total = 0;
	for (const auto &c : changes)
		total += c.size();
	return total;
}
Changes merge(const Changes &older, const Changes &younger, bool drop_tombstones) {
	Changes out;
	out.reserve(older.size() + younger.size());
	auto keep = [&](const Change &c) {
		if (!drop_tombstones || c.definition)
			out.push_back(c);
	};
	auto o = older.begin(), y = younger.begin();
	while (o != older.end() || y != younger.end()) {
		if (y == younger.end() || (o != older.end() && o->key < y->key))
			keep(*o++);
		else {
			if (o != older.end() && o->key == y->key)
				++o;
			keep(*y++);
		}
	}
	return out;
}
Changes trim(const Changes &changes, Revision after) {
	Changes out;
	for (const auto &c : changes)
		if (c.revision > after)
			out.push_back(c);
	return out;
}
Digest sha256(std::string_view bytes) {
	Digest d{};
	SHA256 sha;
	sha.add(bytes.data(), bytes.size());
	sha.getHash(d.data());
	return d;
}
std::string hex(const Identity &identity) {
	constexpr char digits[] = "0123456789abcdef";
	std::string out;
	for (unsigned char c : identity) {
		out.push_back(digits[c >> 4]);
		out.push_back(digits[c & 15]);
	}
	return out;
}
std::string namespace_token(std::string_view n) {
	const auto digest = sha256(n);
	Identity token{};
	std::copy_n(digest.begin(), token.size(), token.begin());
	return hex(token);
}
std::string block_name(std::string_view n, const Identity &i, Revision first, Revision last) {
	return "block-" + namespace_token(n) + "-" + hex(i) + "-" + std::to_string(first) + "-" + std::to_string(last);
}
std::string base_name(std::string_view n, const Identity &i, Revision position) {
	return "base-" + namespace_token(n) + "-" + hex(i) + "-" + std::to_string(position);
}
std::string seal_name(std::uint64_t index) {
	constexpr char digits[] = "0123456789abcdef";
	std::string out = "seal-";
	for (int shift = 60; shift >= 0; shift -= 4)
		out.push_back(digits[(index >> shift) & 15]);
	return out;
}
namespace {
bool lower_hex(std::string_view text, std::size_t size) noexcept {
	return text.size() == size && std::all_of(text.begin(), text.end(), [](char c) {
			   return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
		   });
}
bool decimal(std::string_view text) noexcept {
	std::uint64_t value = 0;
	auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
	return !text.empty() && error == std::errc{} && end == text.data() + text.size() &&
		   (text.size() == 1 || text.front() != '0');
}
} // namespace
bool history_name(std::string_view name) noexcept {
	if (name == table_name)
		return true;
	if (name.starts_with("seal-"))
		return lower_hex(name.substr(5), 16);
	std::array<std::string_view, 5> fields;
	std::size_t count = 0;
	for (std::size_t start = 0;;) {
		if (count == fields.size())
			return false;
		auto dash = name.find('-', start);
		fields[count++] = name.substr(start, dash - start);
		if (dash == std::string_view::npos)
			break;
		start = dash + 1;
	}
	const bool block = count == 5 && fields[0] == "block" && decimal(fields[3]) && decimal(fields[4]);
	const bool base = count == 4 && fields[0] == "base" && decimal(fields[3]);
	return (block || base) && lower_hex(fields[1], 32) && lower_hex(fields[2], 32);
}
std::string Output::name() const {
	return base ? base_name(name_space, incarnation, last) : block_name(name_space, incarnation, first, last);
}

std::string encode_block(const Block &b) {
	require(b.first && b.first <= b.last, "invalid block range");
	Writer w;
	w.raw("DTABLK01");
	w.text(b.name_space);
	w.identity(b.incarnation);
	w.word(b.first);
	w.word(b.last);
	write_changes(w, b.changes);
	return std::move(w.bytes);
}
std::string encode_base(const Block &b) {
	require(!b.first && std::all_of(b.changes.begin(), b.changes.end(), [](auto &c) { return c.definition; }),
			"invalid base");
	Writer w;
	w.raw("DTABAS01");
	w.text(b.name_space);
	w.identity(b.incarnation);
	w.word(b.last);
	write_changes(w, b.changes);
	return std::move(w.bytes);
}
Block decode_block(std::string_view bytes, const Limits &limits) {
	Reader r{bytes};
	require(r.raw(8) == "DTABLK01", "not a history block");
	Block b;
	b.name_space = std::string(r.text(199));
	b.incarnation = r.identity();
	b.first = r.word();
	b.last = r.word();
	require(valid_namespace(b.name_space) && b.first && b.first <= b.last, "invalid block header");
	b.changes = read_changes(r, b.name_space, b.first, b.last, true, limits);
	r.end();
	return b;
}
Block decode_base(std::string_view bytes, const Limits &limits) {
	Reader r{bytes};
	require(r.raw(8) == "DTABAS01", "not a history base");
	Block b;
	b.name_space = std::string(r.text(199));
	b.incarnation = r.identity();
	b.last = r.word();
	require(valid_namespace(b.name_space), "invalid base header");
	b.changes = read_changes(r, b.name_space, 1, b.last, false, limits);
	r.end();
	return b;
}

std::string encode(const SealFile &s) {
	Writer w;
	w.raw("DTASEAL1");
	w.word(s.index);
	w.word(s.ordinal);
	w.word(s.previous);
	w.raw(encode_policy(s.policy));
	w.word(s.namespaces.size());
	for (const auto &n : s.namespaces) {
		w.text(n.name);
		w.identity(n.incarnation);
		w.word(n.directory);
		w.word(n.origin);
		w.word(n.position);
		w.word(n.live);
		w.octet((n.dropped ? 1 : 0) | (n.first_base ? 2 : 0) | (n.section ? 4 : 0));
		if (n.section)
			w.text(*n.section);
	}
	w.word(s.carried.size());
	for (const auto &c : s.carried)
		w.text(c);
	w.word(s.outputs.size());
	for (const auto &o : s.outputs) {
		w.octet(o.base);
		w.text(o.name_space);
		w.identity(o.incarnation);
		w.word(o.first);
		w.word(o.last);
		w.word(o.bytes);
		w.digest(o.digest);
	}
	return sealed(w);
}
SealFile decode_seal(std::string_view bytes, const Limits &limits) {
	Reader r{verified(bytes)};
	require(r.raw(8) == "DTASEAL1", "not a seal file");
	SealFile s;
	s.index = r.word();
	s.ordinal = r.word();
	s.previous = r.word();
	s.policy = decode_policy(r.raw(88));
	require(s.index && (s.ordinal ? s.previous < s.index && s.previous : !s.previous), "invalid seal header");
	auto count = r.word();
	require(count <= limits.namespaces * 2, "seal namespace count");
	const auto body = std::size_t{limits.state_bytes} * 2 + 4096;
	for (std::uint64_t i = 0; i < count; ++i) {
		SealNamespace n;
		n.name = std::string(r.text(199));
		n.incarnation = r.identity();
		n.directory = r.word();
		n.origin = r.word();
		n.position = r.word();
		n.live = r.word();
		auto flags = r.octet();
		require(flags < 8 && valid_namespace(n.name) && n.origin <= n.position &&
					(s.namespaces.empty() || s.namespaces.back().name < n.name),
				"invalid seal namespace");
		n.dropped = flags & 1;
		n.first_base = flags & 2;
		if (flags & 4)
			n.section = std::string(r.text(body));
		s.namespaces.push_back(std::move(n));
	}
	count = r.word();
	require(count <= limits.namespaces, "seal carried count");
	for (std::uint64_t i = 0; i < count; ++i)
		s.carried.emplace_back(r.text(body));
	count = r.word();
	require(count <= std::uint64_t{limits.namespaces} * 64, "seal output count");
	for (std::uint64_t i = 0; i < count; ++i) {
		Output o;
		auto base = r.octet();
		require(base <= 1, "invalid seal output");
		o.base = base;
		o.name_space = std::string(r.text(199));
		o.incarnation = r.identity();
		o.first = r.word();
		o.last = r.word();
		o.bytes = r.word();
		o.digest = r.digest();
		require(valid_namespace(o.name_space) && (o.base ? !o.first : o.first && o.first <= o.last),
				"invalid seal output");
		s.outputs.push_back(std::move(o));
	}
	r.end();
	return s;
}

TableNamespace *Table::find(std::string_view name) {
	auto it = std::lower_bound(namespaces.begin(), namespaces.end(), name,
							   [](const TableNamespace &n, std::string_view k) { return n.name < k; });
	return it != namespaces.end() && it->name == name ? &*it : nullptr;
}
const TableNamespace *Table::find(std::string_view name) const { return const_cast<Table *>(this)->find(name); }
std::string encode(const Table &t) {
	Writer w;
	w.raw("DTALAY01");
	w.word(t.index);
	w.word(t.ordinal);
	w.raw(encode_policy(t.policy));
	w.word(t.namespaces.size());
	for (const auto &n : t.namespaces) {
		w.text(n.name);
		w.identity(n.incarnation);
		w.word(n.directory);
		w.word(n.origin);
		w.word(n.position);
		w.word(n.live);
		w.word(n.carries);
		w.octet(n.touched);
		write_block(w, n.base);
		w.word(n.blocks.size());
		for (const auto &b : n.blocks)
			write_block(w, b);
		w.word(n.level0.size());
		for (const auto &b : n.level0)
			write_block(w, b);
	}
	return sealed(w);
}
Table decode_table(std::string_view bytes, const Limits &limits) {
	Reader r{verified(bytes)};
	require(r.raw(8) == "DTALAY01", "not a layout table");
	Table t;
	t.index = r.word();
	t.ordinal = r.word();
	t.policy = decode_policy(r.raw(88));
	auto count = r.word();
	require(t.index && count <= limits.namespaces, "invalid layout table header");
	for (std::uint64_t i = 0; i < count; ++i) {
		TableNamespace n;
		n.name = std::string(r.text(199));
		n.incarnation = r.identity();
		n.directory = r.word();
		n.origin = r.word();
		n.position = r.word();
		n.live = r.word();
		n.carries = r.word();
		auto touched = r.octet();
		n.base = read_block(r);
		require(touched <= 1 && valid_namespace(n.name) && n.origin <= n.position && !n.base.first &&
					n.base.last >= n.origin && n.base.last <= n.position &&
					(t.namespaces.empty() || t.namespaces.back().name < n.name),
				"invalid layout namespace");
		n.touched = touched;
		// Blocks, then sections, tile (p, s] without a gap.
		auto next = n.base.last + 1;
		for (auto *list : {&n.blocks, &n.level0}) {
			auto blocks = r.word();
			require(blocks <= 4096, "layout block count");
			for (std::uint64_t j = 0; j < blocks; ++j) {
				auto b = read_block(r);
				require(b.first == next && b.last >= b.first && b.last <= n.position &&
							(list == &n.level0 ? b.seal != 0 : !b.seal && !b.carried),
						"layout blocks do not tile");
				next = b.last + 1;
				list->push_back(b);
			}
		}
		require(next == n.position + 1, "layout blocks do not reach the seal");
		t.namespaces.push_back(std::move(n));
	}
	r.end();
	return t;
}
} // namespace detent::authority::history
