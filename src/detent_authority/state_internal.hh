// State's codec primitives and value helpers shared by state.cc, state_codec.cc,
// state_application.cc, state_administrative.cc and state_image.cc.
#pragma once
#include "state.hh"
#include "deferred_values.hh"
#include "detent/utf8.hh"
#include <limits>
#include <stdexcept>
#include <string_view>

namespace detent::authority {
namespace detail {
constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
inline void require(bool condition, const char *message) {
	if (!condition) {
		throw std::invalid_argument(message);
	}
}
struct Writer {
	std::string bytes;
	std::size_t limit;
	void raw(std::string_view s) {
		require(s.size() <= limit - bytes.size(), "authority encoding bound");
		bytes.append(s);
	}
	void number(std::uint64_t n) {
		char b[8];
		for (unsigned i = 0; i < 8; ++i) {
			b[i] = static_cast<char>(n >> (i * 8));
		}
		raw({b, 8});
	}
	void flag(bool b) {
		char c = b ? 1 : 0;
		raw({&c, 1});
	}
	void text(std::string_view s) {
		number(s.size());
		raw(s);
	}
	void identity(Identity id) { raw({id.data(), id.size()}); }
	void optional(std::optional<Revision> n) {
		flag(n.has_value());
		if (n) {
			number(*n);
		}
	}
};
struct Incomplete : std::invalid_argument {
	Incomplete() : std::invalid_argument("truncated authority encoding") {}
};
struct Reader {
	std::string_view bytes;
	std::string_view raw(std::size_t n) {
		if (n > bytes.size())
			throw Incomplete{};
		auto s = bytes.substr(0, n);
		bytes.remove_prefix(n);
		return s;
	}
	std::uint64_t number() {
		auto s = raw(8);
		std::uint64_t n = 0;
		for (unsigned i = 0; i < 8; ++i) {
			n |= static_cast<std::uint64_t>(static_cast<unsigned char>(s[i])) << (8 * i);
		}
		return n;
	}
	bool flag() {
		auto b = static_cast<unsigned char>(raw(1)[0]);
		require(b <= 1, "invalid authority flag");
		return b != 0;
	}
	std::string_view text_view(std::size_t cap) {
		auto n = number();
		require(n <= cap, "authority string bound");
		return raw(static_cast<std::size_t>(n));
	}
	std::string text(std::size_t cap) { return std::string(text_view(cap)); }
	Identity identity() {
		Identity id{};
		auto s = raw(16);
		std::copy(s.begin(), s.end(), id.begin());
		return id;
	}
	std::optional<Revision> optional() { return flag() ? std::optional<Revision>{number()} : std::nullopt; }
	void end() { require(bytes.empty(), "trailing authority bytes"); }
};
using detent::valid_utf8;

// Presence bits of a definition header: rollout_pct, rollout_value, nonempty overrides.
constexpr unsigned has_rollout = 1, has_value = 2, has_overrides = 4;
inline unsigned presence(const KnobDef &def) {
	return (def.rollout_pct ? has_rollout : 0) | (def.rollout_value ? has_value : 0) |
		   (def.overrides.empty() ? 0 : has_overrides);
}
inline void octet(Writer &w, unsigned value) {
	char c = static_cast<char>(value);
	w.raw({&c, 1});
}
inline unsigned octet(Reader &r) { return static_cast<unsigned char>(r.raw(1)[0]); }
// Writes the header: default, presence byte, then the fields it names.
inline void definition_header(Writer &w, const KnobDef &def, const Limits &limits) {
	require(!definition_error(def), "invalid definition shape");
	require(def.overrides.size() <= limits.overrides, "override count bound");
	const auto bits = presence(def);
	w.number(std::bit_cast<std::uint64_t>(def.default_value));
	octet(w, bits);
	if (bits & has_rollout)
		octet(w, *def.rollout_pct);
	if (bits & has_value)
		w.number(std::bit_cast<std::uint64_t>(*def.rollout_value));
	if (bits & has_overrides)
		w.number(def.overrides.size());
}
inline void override_row(Writer &w, std::string_view key, const GroupOverride &value) {
	require(is_valid_group_key(key) && valid_utf8(key, false), "invalid group key");
	w.text(key);
	if (auto forced = std::get_if<std::int64_t>(&value)) {
		octet(w, 0);
		w.number(std::bit_cast<std::uint64_t>(*forced));
	} else {
		auto pct = std::get<GroupRollout>(value).rollout_pct;
		require(pct <= 100, "invalid group rollout");
		octet(w, 1);
		octet(w, pct);
	}
}
// Reads the header into `def` and returns the override count that follows.
inline std::uint64_t definition_header(Reader &r, KnobDef &def, const Limits &limits) {
	def.default_value = std::bit_cast<std::int64_t>(r.number());
	const auto bits = octet(r);
	require(bits <= (has_rollout | has_value | has_overrides), "invalid definition presence");
	if (bits & has_rollout) {
		auto pct = octet(r);
		require(pct <= 100, "invalid rollout");
		def.rollout_pct = static_cast<std::uint8_t>(pct);
	}
	if (bits & has_value)
		def.rollout_value = std::bit_cast<std::int64_t>(r.number());
	std::uint64_t count = 0;
	if (bits & has_overrides) {
		count = r.number();
		require(count && count <= limits.overrides, "override count bound");
	}
	return count;
}
inline GroupOverride override_value(Reader &r) {
	const auto kind = octet(r);
	require(kind <= 1, "invalid override kind");
	if (!kind)
		return std::bit_cast<std::int64_t>(r.number());
	auto pct = octet(r);
	require(pct <= 100, "invalid group rollout");
	return GroupRollout{static_cast<std::uint8_t>(pct)};
}
inline void definition(Writer &w, const KnobDef &def, const Limits &limits) {
	auto start = w.bytes.size();
	definition_header(w, def, limits);
	for (const auto &[key, value] : def.overrides)
		override_row(w, key, value);
	require(w.bytes.size() - start <= limits.definition_bytes, "definition byte bound");
}
inline KnobDef definition(Reader &r, const Limits &limits) {
	auto start = r.bytes.size();
	KnobDef result;
	auto count = definition_header(r, result, limits);
	std::string previous;
	for (std::uint64_t i = 0; i < count; ++i) {
		auto key = r.text(128);
		require(is_valid_group_key(key) && valid_utf8(key, false) && (i == 0 || previous < key),
				"invalid or unordered group key");
		previous = key;
		auto value = override_value(r);
		result.overrides.emplace(std::move(key), value);
	}
	require(!definition_error(result), "invalid definition shape");
	require(start - r.bytes.size() <= limits.definition_bytes, "definition byte bound");
	return result;
}
inline void encode_context(Writer &w, Context c) {
	w.identity(c.cluster);
	w.identity(c.group);
	w.identity(c.epoch);
}
inline Context decode_context(Reader &r) { return {r.identity(), r.identity(), r.identity()}; }
inline std::size_t footprint(std::string_view key, const KnobDef &value, Limits limits) {
	Writer w{{}, limits.request_bytes};
	definition(w, value, limits);
	return key.size() + w.bytes.size() + 16;
}
inline void write_result(Writer &w, Result r) {
	w.number(static_cast<std::uint8_t>(r.status));
	w.number(r.revision);
	w.number(r.namespace_revision);
	w.number(r.generation);
	w.number(r.sequence);
}
inline Result read_result(Reader &r) {
	auto status = r.number();
	require(status >= 1 && status <= static_cast<unsigned>(Status::Exhausted), "invalid receipt status");
	return {static_cast<Status>(status), r.number(), r.number(), r.number(), r.number()};
}
inline void write_policy(Writer &w, const LayoutPolicy &p) {
	for (auto n : {p.seal_transactions, p.seal_bytes, p.period, p.carry_bytes, p.carry_count, p.ratio,
				   p.alpha_num, p.alpha_den, p.cap, p.log_entries, p.log_bytes})
		w.number(n);
}
inline LayoutPolicy read_policy(Reader &r) {
	LayoutPolicy p;
	for (auto *n : {&p.seal_transactions, &p.seal_bytes, &p.period, &p.carry_bytes, &p.carry_count, &p.ratio,
					&p.alpha_num, &p.alpha_den, &p.cap, &p.log_entries, &p.log_bytes})
		*n = r.number();
	validate(p);
	return p;
}
// The ten replicated limits in their encoded order; a restart may raise the three marked.
struct LimitField {
	const char *name;
	std::size_t Limits::*field;
	bool raisable;
};
constexpr LimitField limit_fields[] = {
	{"keys", &Limits::keys, true},
	{"namespaces", &Limits::namespaces, false},
	{"producers", &Limits::producers, false},
	{"mutations", &Limits::mutations, false},
	{"request_bytes", &Limits::request_bytes, false},
	{"state_bytes", &Limits::state_bytes, true},
	{"image_bytes", &Limits::image_bytes, true},
	{"definition_bytes", &Limits::definition_bytes, false},
	{"overrides", &Limits::overrides, false},
	{"results", &Limits::results, false},
};
inline void write_limits(Writer &w, const Limits &l) {
	for (const auto &f : limit_fields)
		w.number(l.*f.field);
}
// The profile an entry or image records, admitted under `configured`, whose layout it takes.
inline Limits read_limits(Reader &r, const Limits &configured) {
	Limits recorded = configured;
	for (const auto &f : limit_fields)
		recorded.*f.field = static_cast<std::size_t>(r.number());
	admit_limits(recorded, configured);
	return recorded;
}
inline std::shared_ptr<const KnobDef> move_definition(detail::DeferredValues<KnobDef> &arena, KnobDef &source,
											   std::size_t bytes) {
	auto value = arena.create(bytes);
	value->default_value = source.default_value;
	value->rollout_pct = source.rollout_pct;
	value->rollout_value = source.rollout_value;
	value->overrides.swap(source.overrides);
	return value;
}
inline std::size_t stored_footprint(std::string_view key, const Record &record) {
	return key.size() + detail::DeferredValues<KnobDef>::encoded_size(*record.value) + 16;
}
inline std::shared_ptr<Producer> copy_producer(detail::DeferredValues<Producer> &arena, const Producer &source) {
	auto value = arena.create(41 + source.receipts.size() * 80);
	*value = source;
	return value;
}
} // namespace detail
using namespace detail;
} // namespace detent::authority
