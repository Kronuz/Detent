#include "state.hh"
#include "deferred_values.hh"
#include "detent/validation.hh"
#include "prepared.hh"
#include <algorithm>
#include <bit>
#include <limits>
#include <map>
#include <set>
#include <sha256.h>
#include <stdexcept>

namespace detent::authority {
namespace {
constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
void require(bool condition, const char *message) {
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
bool valid_utf8(std::string_view s, bool reject_zero = true) {
	for (std::size_t i = 0; i < s.size();) {
		auto c = static_cast<unsigned char>(s[i++]);
		if (c < 0x80) {
			if (!c && reject_zero) {
				return false;
			}
			continue;
		}
		unsigned count;
		std::uint32_t value, minimum;
		if (c >= 0xc2 && c <= 0xdf) {
			count = 1;
			value = c & 31;
			minimum = 0x80;
		} else if (c >= 0xe0 && c <= 0xef) {
			count = 2;
			value = c & 15;
			minimum = 0x800;
		} else if (c >= 0xf0 && c <= 0xf4) {
			count = 3;
			value = c & 7;
			minimum = 0x10000;
		} else {
			return false;
		}
		if (count > s.size() - i) {
			return false;
		}
		for (unsigned j = 0; j < count; ++j) {
			auto b = static_cast<unsigned char>(s[i++]);
			if ((b & 0xc0) != 0x80) {
				return false;
			}
			value = (value << 6) | (b & 63);
		}
		if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) {
			return false;
		}
	}
	return true;
}
void definition(Writer &w, const KnobDef &def, const Limits &limits) {
	auto start = w.bytes.size();
	w.flag(is_bool(def));
	if (auto integer = std::get_if<IntDef>(&def)) {
		w.number(std::bit_cast<std::uint64_t>(integer->default_value));
	} else {
		auto &flag = std::get<BoolDef>(def);
		w.flag(flag.default_value);
		w.optional(flag.rollout_pct);
		require(!flag.rollout_pct || *flag.rollout_pct <= 100, "invalid rollout");
		require(flag.overrides.size() <= limits.overrides, "override count bound");
		w.number(flag.overrides.size());
		for (const auto &[key, value] : flag.overrides) {
			require(is_valid_group_key(key) && valid_utf8(key, false), "invalid group key");
			w.text(key);
			w.flag(std::holds_alternative<bool>(value));
			if (auto forced = std::get_if<bool>(&value)) {
				w.flag(*forced);
			} else {
				auto pct = std::get<GroupRollout>(value).rollout_pct;
				require(pct <= 100, "invalid group rollout");
				w.number(pct);
			}
		}
	}
	require(w.bytes.size() - start <= limits.definition_bytes, "definition byte bound");
}
KnobDef definition(Reader &r, const Limits &limits) {
	auto start = r.bytes.size();
	if (!r.flag()) {
		return IntDef{std::bit_cast<std::int64_t>(r.number())};
	}
	BoolDef result;
	result.default_value = r.flag();
	auto pct = r.optional();
	require(!pct || *pct <= 100, "invalid rollout");
	if (pct) {
		result.rollout_pct = static_cast<std::uint8_t>(*pct);
	}
	auto count = r.number();
	require(count <= limits.overrides, "override count bound");
	std::string previous;
	for (std::uint64_t i = 0; i < count; ++i) {
		auto key = r.text(128);
		require(is_valid_group_key(key) && valid_utf8(key, false) && (i == 0 || previous < key),
				"invalid or unordered group key");
		previous = key;
		if (r.flag()) {
			result.overrides.emplace(std::move(key), r.flag());
		} else {
			auto p = r.number();
			require(p <= 100, "invalid group rollout");
			result.overrides.emplace(std::move(key), GroupRollout{static_cast<std::uint8_t>(p)});
		}
	}
	require(start - r.bytes.size() <= limits.definition_bytes, "definition byte bound");
	return result;
}
void encode_context(Writer &w, Context c) {
	w.identity(c.cluster);
	w.identity(c.group);
	w.identity(c.epoch);
}
Context decode_context(Reader &r) { return {r.identity(), r.identity(), r.identity()}; }
std::size_t footprint(std::string_view key, const KnobDef &value, Limits limits) {
	Writer w{{}, limits.request_bytes};
	definition(w, value, limits);
	return key.size() + w.bytes.size() + 16;
}
void write_result(Writer &w, Result r) {
	w.number(static_cast<std::uint8_t>(r.status));
	w.number(r.revision);
	w.number(r.namespace_revision);
	w.number(r.generation);
	w.number(r.sequence);
}
Result read_result(Reader &r) {
	auto status = r.number();
	require(status >= 1 && status <= static_cast<unsigned>(Status::Exhausted), "invalid receipt status");
	return {static_cast<Status>(status), r.number(), r.number(), r.number(), r.number()};
}
void write_limits(Writer &w, Limits l) {
	for (auto n : {l.keys, l.namespaces, l.producers, l.mutations, l.request_bytes, l.state_bytes,
				   l.image_bytes, l.definition_bytes, l.overrides, l.results}) {
		w.number(n);
	}
}
} // namespace

std::string semantic_profile(Limits limits) {
	Writer writer{{}, 80};
	write_limits(writer, limits);
	return std::move(writer.bytes);
}

void validate(const Command &c, Limits l) {
	require(c.context.cluster != Identity{} && c.context.group != Identity{} && c.context.epoch != Identity{},
			"missing authority identity");
	require(!c.producer.empty() && c.producer.size() <= 128 && valid_utf8(c.producer), "invalid producer");
	if (c.kind == Kind::Open) {
		require(c.nonce != Identity{} && !c.sequence && c.name_space.empty() && !c.expected_namespace &&
					c.mutations.empty(),
				"invalid open command");
		return;
	}
	if (c.kind == Kind::Close) {
		require(c.generation && !c.sequence && c.nonce == Identity{} && c.name_space.empty() &&
					!c.expected_namespace && c.mutations.empty(),
				"invalid close command");
		return;
	}
	require(c.kind == Kind::Transaction && c.generation && c.sequence && c.nonce == Identity{},
			"invalid transaction identity");
	require(!c.name_space.empty() && c.name_space.size() < 200 && is_valid_knob_name(c.name_space + ":x"),
			"invalid namespace");
	require(!c.mutations.empty() && c.mutations.size() <= l.mutations, "mutation count bound");
	std::set<std::string_view> names;
	for (const auto &op : c.mutations) {
		require(is_valid_knob_name(op.key) && op.key.substr(0, op.key.find(':')) == c.name_space &&
					names.insert(op.key).second,
				"invalid or duplicate transaction key");
		require(op.absence_revision.has_value() == (op.expected && *op.expected == 0),
				"absent precondition requires namespace revision");
		if (op.value) {
			Writer w{{}, l.request_bytes};
			definition(w, *op.value, l);
		}
	}
}
std::string encode(const Command &c, Limits l) {
	validate(c, l);
	Writer w{{}, l.request_bytes};
	w.raw("DTACMD01");
	encode_context(w, c.context);
	w.number(static_cast<std::uint8_t>(c.kind));
	w.text(c.producer);
	w.number(c.generation);
	w.number(c.sequence);
	w.identity(c.nonce);
	w.text(c.name_space);
	w.optional(c.expected_namespace);
	std::vector<const Mutation *> operations;
	for (auto &op : c.mutations) {
		operations.push_back(&op);
	}
	std::sort(operations.begin(), operations.end(), [](auto a, auto b) { return a->key < b->key; });
	w.number(operations.size());
	for (auto op : operations) {
		w.text(op->key);
		w.optional(op->expected);
		w.optional(op->absence_revision);
		w.flag(op->value.has_value());
		if (op->value) {
			definition(w, *op->value, l);
		}
	}
	return std::move(w.bytes);
}
static Command command_prefix(Reader &r) {
	require(r.raw(8) == "DTACMD01", "unsupported authority command");
	Command c;
	c.context = decode_context(r);
	auto kind = r.number();
	require(kind >= 1 && kind <= 3, "unknown command kind");
	c.kind = static_cast<Kind>(kind);
	c.producer = r.text(128);
	c.generation = r.number();
	c.sequence = r.number();
	c.nonce = r.identity();
	c.name_space = r.text(199);
	c.expected_namespace = r.optional();
	return c;
}
Command decode(std::string_view bytes, Limits l) {
	require(bytes.size() <= l.request_bytes, "command byte bound");
	Reader r{bytes};
	auto c = command_prefix(r);
	auto count = r.number();
	require(count <= l.mutations, "mutation count bound");
	std::string previous;
	for (std::uint64_t i = 0; i < count; ++i) {
		Mutation op;
		op.key = r.text(200);
		require(i == 0 || previous < op.key, "unordered mutation keys");
		previous = op.key;
		op.expected = r.optional();
		op.absence_revision = r.optional();
		if (r.flag()) {
			op.value = definition(r, l);
		}
		c.mutations.push_back(std::move(op));
	}
	r.end();
	validate(c, l);
	return c;
}
Digest digest(const Command &c, Limits l) {
	auto bytes = encode(c, l);
	SHA256 sha;
	sha.add(bytes.data(), bytes.size());
	Digest result{};
	sha.getHash(result.data());
	return result;
}

struct CommandEncoder::Impl {
	enum class Phase { Validate, Order, Header, Mutation, Overrides, Cleanup, Complete };
	const Command &command;
	Limits limits;
	Writer writer;
	std::map<std::string_view, const Mutation *> ordered;
	decltype(ordered)::const_iterator mutation{};
	decltype(BoolDef::overrides)::const_iterator group{};
	const BoolDef *flag = nullptr;
	std::size_t row = 0, definition_start = 0;
	Phase phase = Phase::Validate;
	bool failed = false, consumed = false;
	Impl(const Command &c, Limits l) : command(c), limits(l), writer{{}, l.request_bytes} {
		require(l.mutations && l.mutations <= 4096 && l.overrides <= 4096 && l.request_bytes >= 256 &&
					l.request_bytes <= 16 * 1024 * 1024 && l.definition_bytes >= 9 &&
					l.definition_bytes <= l.request_bytes,
				"command work profile bound");
		writer.bytes.reserve(l.request_bytes);
	}
	static bool claim(CommandWorkBudget &b, std::size_t bytes) {
		if (!b.records || b.bytes < bytes)
			return false;
		--b.records;
		b.bytes -= bytes;
		return true;
	}
	void end_definition() {
		require(writer.bytes.size() - definition_start <= limits.definition_bytes, "definition byte bound");
		++mutation;
		flag = nullptr;
		phase = Phase::Mutation;
	}
	bool clear(CommandWorkBudget &b) {
		while (!ordered.empty()) {
			if (!b.receipts || b.bytes < 128)
				return false;
			--b.receipts;
			b.bytes -= 128;
			ordered.erase(ordered.begin());
		}
		return true;
	}
};
CommandEncoder::CommandEncoder(const Command &command, Limits limits)
	: impl_(std::make_unique<Impl>(command, limits)) {}
CommandEncoder::~CommandEncoder() = default;
CommandEncoder::CommandEncoder(CommandEncoder &&) noexcept = default;
bool CommandEncoder::done() const noexcept {
	return impl_ && !impl_->failed && !impl_->consumed && impl_->phase == Impl::Phase::Complete;
}
void CommandEncoder::step(CommandWorkBudget &budget) {
	require(impl_ && !impl_->failed && !impl_->consumed, "inactive command encoder");
	auto &d = *impl_;
	const auto &c = d.command;
	auto &w = d.writer;
	using Phase = Impl::Phase;
	try {
		while (d.phase != Phase::Complete)
			switch (d.phase) {
			case Phase::Validate:
				if (!Impl::claim(budget, 2048))
					return;
				require(c.context.cluster != Identity{} && c.context.group != Identity{} &&
							c.context.epoch != Identity{},
						"missing authority identity");
				require(!c.producer.empty() && c.producer.size() <= 128 && valid_utf8(c.producer),
						"invalid producer");
				if (c.kind == Kind::Open || c.kind == Kind::Close)
					validate(c, d.limits);
				else {
					require(c.kind == Kind::Transaction && c.generation && c.sequence &&
								c.nonce == Identity{},
							"invalid transaction identity");
					require(!c.name_space.empty() && c.name_space.size() < 200 &&
								is_valid_knob_name(c.name_space + ":x"),
							"invalid namespace");
					require(!c.mutations.empty() && c.mutations.size() <= d.limits.mutations,
							"mutation count bound");
				}
				d.phase = Phase::Order;
				break;
			case Phase::Order:
				if (d.row == c.mutations.size()) {
					d.mutation = d.ordered.begin();
					d.phase = Phase::Header;
					break;
				}
				if (!Impl::claim(budget, 512))
					return;
				{
					const auto &op = c.mutations[d.row++];
					require(is_valid_knob_name(op.key) &&
								std::string_view(op.key).substr(0, op.key.find(':')) == c.name_space &&
								d.ordered.emplace(op.key, &op).second,
							"invalid or duplicate transaction key");
					require(op.absence_revision.has_value() == (op.expected && *op.expected == 0),
							"absent precondition requires namespace revision");
				}
				break;
			case Phase::Header:
				if (!Impl::claim(budget, 2048))
					return;
				w.raw("DTACMD01");
				encode_context(w, c.context);
				w.number(static_cast<std::uint8_t>(c.kind));
				w.text(c.producer);
				w.number(c.generation);
				w.number(c.sequence);
				w.identity(c.nonce);
				w.text(c.name_space);
				w.optional(c.expected_namespace);
				w.number(d.ordered.size());
				d.phase = Phase::Mutation;
				break;
			case Phase::Mutation:
				if (d.mutation == d.ordered.end()) {
					d.phase = Phase::Cleanup;
					break;
				}
				if (!Impl::claim(budget, 1024))
					return;
				{
					const auto &op = *d.mutation->second;
					w.text(op.key);
					w.optional(op.expected);
					w.optional(op.absence_revision);
					w.flag(op.value.has_value());
					if (!op.value) {
						++d.mutation;
						break;
					}
					d.definition_start = w.bytes.size();
					w.flag(is_bool(*op.value));
					if (auto integer = std::get_if<IntDef>(&*op.value)) {
						w.number(std::bit_cast<std::uint64_t>(integer->default_value));
						d.end_definition();
						break;
					}
					d.flag = &std::get<BoolDef>(*op.value);
					require(!d.flag->rollout_pct || *d.flag->rollout_pct <= 100, "invalid rollout");
					require(d.flag->overrides.size() <= d.limits.overrides, "override count bound");
					w.flag(d.flag->default_value);
					w.optional(d.flag->rollout_pct);
					w.number(d.flag->overrides.size());
					require(w.bytes.size() - d.definition_start <= d.limits.definition_bytes,
							"definition byte bound");
					d.group = d.flag->overrides.begin();
					d.phase = Phase::Overrides;
				}
				break;
			case Phase::Overrides:
				if (d.group == d.flag->overrides.end()) {
					d.end_definition();
					break;
				}
				if (!Impl::claim(budget, 512))
					return;
				{
					const auto &[key, value] = *d.group;
					require(is_valid_group_key(key) && valid_utf8(key, false), "invalid group key");
					w.text(key);
					w.flag(std::holds_alternative<bool>(value));
					if (auto forced = std::get_if<bool>(&value))
						w.flag(*forced);
					else {
						auto pct = std::get<GroupRollout>(value).rollout_pct;
						require(pct <= 100, "invalid group rollout");
						w.number(pct);
					}
					require(w.bytes.size() - d.definition_start <= d.limits.definition_bytes,
							"definition byte bound");
					++d.group;
				}
				break;
			case Phase::Cleanup:
				if (!d.clear(budget))
					return;
				d.phase = Phase::Complete;
				break;
			case Phase::Complete:
				break;
			}
	} catch (...) {
		d.failed = true;
		throw;
	}
}
std::string CommandEncoder::take() {
	require(done(), "incomplete command encoder");
	impl_->consumed = true;
	return std::move(impl_->writer.bytes);
}
EncodedCommand CommandEncoder::take_encoded() {
	require(done(), "incomplete command encoder");
	return {take(), impl_->command.context, impl_->limits};
}
bool CommandEncoder::discard(CommandWorkBudget &budget) {
	if (!impl_)
		return true;
	impl_->failed = true;
	if (!impl_->clear(budget))
		return false;
	std::string{}.swap(impl_->writer.bytes);
	impl_->consumed = true;
	return true;
}

struct CommandDecoder::Impl {
	enum class Phase { Header, Mutation, Overrides, Hash, Complete };
	std::string_view original;
	Reader reader;
	Limits limits;
	std::optional<Context> expected_context;
	Command command;
	Mutation pending;
	std::vector<std::size_t> sizes;
	std::size_t count = 0, overrides = 0, definition_start = 0, hash_offset = 0;
	std::string previous_group;
	SHA256 sha;
	Phase phase = Phase::Header;
	bool failed = false, consumed = false, pending_drained = false;
	Impl(std::string_view bytes, Limits profile, std::optional<Context> context)
		: original(bytes), reader{bytes}, limits(profile), expected_context(context) {
		require(bytes.size() <= limits.request_bytes, "command byte bound");
		require(limits.mutations && limits.mutations <= 4096 && limits.overrides <= 4096 &&
					limits.request_bytes <= 16 * 1024 * 1024,
				"command work profile bound");
		require(limits.definition_bytes >= 9 && limits.definition_bytes <= limits.request_bytes,
				"definition byte profile bound");
	}
	static bool claim(CommandWorkBudget &budget, std::size_t bytes) {
		if (!budget.records || budget.bytes < bytes)
			return false;
		--budget.records;
		budget.bytes -= bytes;
		return true;
	}
	void finish_mutation() {
		auto bytes = pending.value ? definition_start - reader.bytes.size() : 0;
		require(bytes <= limits.definition_bytes, "definition byte bound");
		sizes.push_back(bytes);
		// cpp-btree has copy constructors but no move constructors. Transfer
		// its root with swap instead of making a whole-definition copy.
		command.mutations.emplace_back();
		auto &completed = command.mutations.back();
		completed.key = std::move(pending.key);
		completed.expected = pending.expected;
		completed.absence_revision = pending.absence_revision;
		if (pending.value) {
			if (auto value = std::get_if<BoolDef>(&*pending.value)) {
				completed.value = BoolDef{};
				auto &destination = std::get<BoolDef>(*completed.value);
				destination.default_value = value->default_value;
				destination.rollout_pct = value->rollout_pct;
				destination.overrides.swap(value->overrides);
			} else {
				completed.value = std::get<IntDef>(*pending.value);
			}
		}
		pending = {};
		previous_group.clear();
		phase = Phase::Mutation;
	}
	void step(CommandWorkBudget &budget) {
		while (phase != Phase::Complete) {
			switch (phase) {
			case Phase::Header:
				// Covers scalar reads, bounded strings and their validation.
				if (!claim(budget, 2048))
					return;
				command = command_prefix(reader);
				require(!expected_context || command.context == *expected_context,
						"foreign authority context");
				count = reader.number();
				require(count <= limits.mutations, "mutation count bound");
				// Reserve raw slots once; vector growth can otherwise copy all
				// previously decoded definitions when their move may throw.
				command.mutations.reserve(count);
				sizes.reserve(count);
				if (command.kind != Kind::Transaction) {
					require(!count, "nontransaction mutations");
					validate(command, limits);
				} else {
					require(command.context.cluster != Identity{} && command.context.group != Identity{} &&
								command.context.epoch != Identity{},
							"missing authority identity");
					require(!command.producer.empty() && valid_utf8(command.producer), "invalid producer");
					require(command.generation && command.sequence && command.nonce == Identity{},
							"invalid transaction identity");
					require(!command.name_space.empty() && is_valid_knob_name(command.name_space + ":x"),
							"invalid namespace");
					require(count, "mutation count bound");
				}
				phase = Phase::Mutation;
				break;
			case Phase::Mutation:
				if (command.mutations.size() == count) {
					reader.end();
					phase = Phase::Hash;
					break;
				}
				if (!claim(budget, 1024))
					return;
				pending.key = reader.text(200);
				require(is_valid_knob_name(pending.key) &&
							pending.key.substr(0, pending.key.find(':')) == command.name_space &&
							(command.mutations.empty() || command.mutations.back().key < pending.key),
						"invalid or unordered mutation key");
				pending.expected = reader.optional();
				pending.absence_revision = reader.optional();
				require(pending.absence_revision.has_value() == (pending.expected && *pending.expected == 0),
						"absent precondition requires namespace revision");
				if (!reader.flag()) {
					finish_mutation();
					break;
				}
				definition_start = reader.bytes.size();
				if (!reader.flag()) {
					pending.value = IntDef{std::bit_cast<std::int64_t>(reader.number())};
					finish_mutation();
					break;
				}
				pending.value = BoolDef{};
				{
					auto &value = std::get<BoolDef>(*pending.value);
					value.default_value = reader.flag();
					auto pct = reader.optional();
					require(!pct || *pct <= 100, "invalid rollout");
					if (pct)
						value.rollout_pct = static_cast<std::uint8_t>(*pct);
				}
				overrides = reader.number();
				require(overrides <= limits.overrides, "override count bound");
				require(definition_start - reader.bytes.size() <= limits.definition_bytes,
						"definition byte bound");
				phase = Phase::Overrides;
				if (!overrides)
					finish_mutation();
				break;
			case Phase::Overrides:
				if (!claim(budget, 512))
					return;
				{
					auto key = reader.text(128);
					require(is_valid_group_key(key) && valid_utf8(key, false) &&
								(previous_group.empty() || previous_group < key),
							"invalid or unordered group key");
					GroupOverride value;
					if (reader.flag()) {
						value = reader.flag();
					} else {
						auto pct = reader.number();
						require(pct <= 100, "invalid group rollout");
						value = GroupRollout{static_cast<std::uint8_t>(pct)};
					}
					require(definition_start - reader.bytes.size() <= limits.definition_bytes,
							"definition byte bound");
					previous_group = key;
					std::get<BoolDef>(*pending.value).overrides.emplace(std::move(key), value);
				}
				if (!--overrides)
					finish_mutation();
				break;
			case Phase::Hash: {
				auto bytes = std::min(budget.bytes, original.size() - hash_offset);
				if (!bytes)
					return;
				sha.add(original.data() + hash_offset, bytes);
				hash_offset += bytes;
				budget.bytes -= bytes;
				if (hash_offset == original.size())
					phase = Phase::Complete;
			} break;
			case Phase::Complete:
				break;
			}
		}
	}
};
CommandDecoder::CommandDecoder(std::string_view bytes, Limits limits, std::optional<Context> context)
	: impl_(std::make_unique<Impl>(bytes, limits, context)) {}
CommandDecoder::~CommandDecoder() = default;
CommandDecoder::CommandDecoder(CommandDecoder &&) noexcept = default;
void CommandDecoder::step(CommandWorkBudget &budget) {
	require(impl_ && !impl_->failed && !impl_->consumed, "inactive command decoder");
	try {
		impl_->step(budget);
	} catch (...) {
		impl_->failed = true;
		throw;
	}
}
bool CommandDecoder::done() const noexcept {
	return impl_ && !impl_->failed && !impl_->consumed && impl_->phase == Impl::Phase::Complete;
}
ValidatedCommand CommandDecoder::take() {
	require(done(), "incomplete command decoder");
	Digest result{};
	impl_->sha.getHash(result.data());
	impl_->consumed = true;
	return {std::move(impl_->command), result, impl_->limits, std::move(impl_->sizes)};
}
bool CommandDecoder::discard(CommandWorkBudget &budget) {
	if (!impl_)
		return true;
	impl_->consumed = true;
	auto drain = [&](Mutation &mutation) {
		if (mutation.value) {
			if (auto value = std::get_if<BoolDef>(&*mutation.value)) {
				while (!value->overrides.empty()) {
					if (!Impl::claim(budget, 512))
						return false;
					value->overrides.erase(value->overrides.begin());
				}
			}
		}
		if (!Impl::claim(budget, 1024))
			return false;
		mutation = {};
		return true;
	};
	if (!impl_->pending_drained) {
		if (!drain(impl_->pending))
			return false;
		impl_->pending_drained = true;
	}
	while (!impl_->command.mutations.empty()) {
		if (!drain(impl_->command.mutations.back()))
			return false;
		impl_->command.mutations.pop_back();
	}
	return true;
}

State::State(Context c, Limits l) : context_(c), limits_(l) {
	require(c.cluster != Identity{} && c.group != Identity{} && c.epoch != Identity{},
			"missing authority context");
	require(l.keys && l.namespaces && l.producers && l.mutations && l.mutations <= 4096 && l.results &&
				l.results <= 4096 && l.overrides <= 4096 && l.definition_bytes >= 9 &&
				l.definition_bytes <= l.request_bytes && l.request_bytes >= 256 &&
				l.request_bytes <= 16 * 1024 * 1024 && l.state_bytes >= l.request_bytes &&
				l.image_bytes >= l.state_bytes,
			"invalid authority limits");
	require(l.keys <= 1000000 && l.namespaces <= 65536 && l.producers <= 65536 &&
				l.image_bytes <= 1024ull * 1024 * 1024,
			"authority resource ceiling");
	auto metadata =
		std::uint64_t{256} + static_cast<std::uint64_t>(l.namespaces) * 215 +
		static_cast<std::uint64_t>(l.producers) * (177 + static_cast<std::uint64_t>(l.results) * 80);
	require(metadata <= l.image_bytes && l.state_bytes <= l.image_bytes - metadata,
			"application image cannot hold configured state and receipts");
	definitions_ = std::make_shared<detail::DeferredValues<KnobDef>>();
	producer_values_ = std::make_shared<detail::DeferredValues<Producer>>();
}
namespace {
std::shared_ptr<const KnobDef> copy_definition(detail::DeferredValues<KnobDef> &arena, const KnobDef &source,
											   std::size_t bytes) {
	auto value = arena.create(bytes);
	*value = source;
	return value;
}
std::shared_ptr<const KnobDef> move_definition(detail::DeferredValues<KnobDef> &arena, KnobDef &source,
											   std::size_t bytes) {
	auto value = arena.create(bytes);
	if (auto flag = std::get_if<BoolDef>(&source)) {
		auto &destination = std::get<BoolDef>(*value);
		destination.default_value = flag->default_value;
		destination.rollout_pct = flag->rollout_pct;
		destination.overrides.swap(flag->overrides);
	} else {
		*value = std::get<IntDef>(source);
	}
	return value;
}
std::size_t stored_footprint(std::string_view key, const Record &record) {
	return key.size() + detail::DeferredValues<KnobDef>::encoded_size(*record.value) + 16;
}
std::shared_ptr<Producer> copy_producer(detail::DeferredValues<Producer> &arena, const Producer &source) {
	auto value = arena.create(41 + source.receipts.size() * 80);
	*value = source;
	return value;
}
} // namespace
bool State::reclamation_pending() const noexcept {
	return records_.reclamation_pending() || namespaces_.reclamation_pending() ||
		   producers_.reclamation_pending() || definitions_->pending() || producer_values_->pending();
}
std::uint64_t State::retired_values() const noexcept {
	return definitions_->count() + producer_values_->count();
}
std::uint64_t State::retired_value_bytes() const noexcept {
	return definitions_->encoded_bytes() + producer_values_->encoded_bytes();
}
std::size_t State::collect(std::size_t budget) noexcept {
	CommandWorkBudget work;
	work.bytes = 64 * 1024;
	work.receipts = budget;
	return collect(work);
}
std::size_t State::collect(CommandWorkBudget &budget) noexcept {
	std::size_t freed = 0;
	unsigned idle = 0;
	while (budget.receipts && budget.bytes >= 128 && idle < 5) {
		auto previous = budget.receipts;
		std::size_t n = 0;
		auto lane = collection_cursor_;
		collection_cursor_ = (collection_cursor_ + 1) % 5;
		switch (lane) {
		case 0:
			n = records_.collect(1);
			break;
		case 1:
			n = namespaces_.collect(1);
			break;
		case 2:
			n = producers_.collect(1);
			break;
		case 3:
			n = definitions_->collect_one([&](KnobDef &value) {
				if (auto flag = std::get_if<BoolDef>(&value); flag && !flag->overrides.empty()) {
					if (budget.bytes < 512)
						return false;
					flag->overrides.erase(flag->overrides.begin());
					budget.bytes -= 512;
					--budget.receipts;
					return false;
				}
				budget.bytes -= 128;
				--budget.receipts;
				return true;
			});
			break;
		case 4:
			n = producer_values_->collect_one([&](Producer &value) {
				budget.bytes -= 128;
				--budget.receipts;
				if (!value.receipts.empty()) {
					value.receipts.pop_front();
					return false;
				}
				return true;
			});
			break;
		}
		if (n && previous == budget.receipts) {
			budget.bytes -= 128;
			--budget.receipts;
		}
		freed += n;
		idle = budget.receipts == previous ? idle + 1 : 0;
	}
	return freed;
}
Revision State::namespace_revision(std::string_view name) const {
	auto p = namespaces_.find(name);
	return p ? *p : 0;
}
const Producer *State::producer(std::string_view name) const {
	auto p = producers_.find(name);
	return p ? p->get() : nullptr;
}
void State::advance(std::uint64_t index, std::uint64_t term) {
	require(index > applied_ && index < maximum && term && term >= applied_term_,
			"invalid application boundary");
	applied_ = index;
	applied_term_ = term;
}
Result State::apply(std::uint64_t index, const Command &c, std::uint64_t term) {
	require(index > applied_ && index < maximum && term && term >= applied_term_,
			"invalid application boundary");
	require(c.context == context_, "foreign authority context");
	auto canonical_digest = digest(c, limits_);
	auto allocations_before = allocations();
	State next = *this;
	Result result{Status::Applied, revision_, namespace_revision(c.name_space), c.generation, c.sequence};
	auto current = producer(c.producer);
	auto finish = [&] {
		next.applied_ = index;
		next.applied_term_ = term;
		*this = std::move(next);
		collect(allocations() - allocations_before + 128);
		return result;
	};
	if (c.kind == Kind::Open) {
		if (current && current->active && current->nonce == c.nonce && current->generation &&
			c.generation == current->generation - 1) {
			result.generation = current->generation;
			return finish();
		}
		if ((current ? current->generation : 0) != c.generation) {
			result.status = Status::FencedSession;
			return finish();
		}
		if (!current && producers_.size() >= limits_.producers) {
			result.status = Status::Capacity;
			return finish();
		}
		if (c.generation == maximum) {
			result.status = Status::Exhausted;
			return finish();
		}
		auto p = next.producer_values_->create(41);
		p->generation = c.generation + 1;
		p->active = true;
		p->nonce = c.nonce;
		next.producers_ = producers_.set(c.producer, p);
		result.generation = p->generation;
		return finish();
	}
	if (!current) {
		result.status = Status::UnknownSession;
		return finish();
	}
	if (current->generation != c.generation || (c.kind == Kind::Transaction && !current->active)) {
		result.status = Status::FencedSession;
		return finish();
	}
	if (c.kind == Kind::Close) {
		auto p = copy_producer(*next.producer_values_, *current);
		p->active = false;
		next.producers_ = producers_.set(c.producer, p);
		return finish();
	}
	if (c.sequence <= current->high_water) {
		auto retained = std::find_if(current->receipts.begin(), current->receipts.end(),
									 [&](const Receipt &r) { return r.sequence == c.sequence; });
		if (retained == current->receipts.end()) {
			result.status = Status::ResultUnavailable;
		} else if (retained->digest != canonical_digest) {
			result.status = Status::IdentityConflict;
		} else {
			result = retained->result;
		}
		return finish();
	}
	if (current->high_water == maximum || c.sequence != current->high_water + 1) {
		result.status = Status::SequenceGap;
		return finish();
	}
	bool changed = false;
	if (c.expected_namespace && *c.expected_namespace != result.namespace_revision) {
		result.status = Status::Conflict;
	}
	for (const auto &op : c.mutations) {
		auto old = find(op.key);
		if ((op.expected && ((!*op.expected && (old || *op.absence_revision != result.namespace_revision)) ||
							 (*op.expected && (!old || old->revision != *op.expected))))) {
			result.status = Status::Conflict;
		}
		if ((op.value && (!old || *old->value != *op.value)) || (!op.value && old)) {
			changed = true;
		}
	}
	if (result.status == Status::Applied && changed) {
		if (revision_ == maximum) {
			result.status = Status::Exhausted;
		} else if (!namespaces_.find(c.name_space) && namespaces_.size() >= limits_.namespaces) {
			result.status = Status::Capacity;
		} else {
			auto candidate = records_;
			auto total = bytes_;
			auto count = records_.size();
			for (const auto &op : c.mutations) {
				auto old = find(op.key);
				if (old) {
					total -= stored_footprint(op.key, *old);
					--count;
				}
			}
			for (const auto &op : c.mutations) {
				if (op.value) {
					auto amount = footprint(op.key, *op.value, limits_);
					if (amount > limits_.state_bytes || total > limits_.state_bytes - amount) {
						result.status = Status::Capacity;
						break;
					}
					total += amount;
					++count;
				}
			}
			if (result.status == Status::Applied && count > limits_.keys) {
				result.status = Status::Capacity;
			}
			if (result.status == Status::Applied) {
				for (const auto &op : c.mutations) {
					auto old = find(op.key);
					if (op.value && (!old || *old->value != *op.value)) {
						candidate = candidate.set(
							op.key,
							Record{revision_ + 1, copy_definition(*next.definitions_, *op.value,
																  footprint(op.key, *op.value, limits_) -
																	  op.key.size() - 16)});
					} else if (!op.value && old) {
						candidate = candidate.erase(op.key);
					}
				}
				next.records_ = std::move(candidate);
				next.bytes_ = total;
				next.revision_ = revision_ + 1;
				next.namespaces_ = namespaces_.set(c.name_space, next.revision_);
				result.revision = result.namespace_revision = next.revision_;
			}
		}
	} else if (result.status == Status::Applied) {
		result.status = Status::NoChange;
	}
	auto p = copy_producer(*next.producer_values_, *current);
	p->high_water = c.sequence;
	p->receipts.push_back({c.sequence, canonical_digest, result});
	if (p->receipts.size() > limits_.results) {
		p->receipts.pop_front();
	}
	detail::DeferredValues<Producer>::set_encoded_size(*p, 41 + p->receipts.size() * 80);
	next.producers_ = producers_.set(c.producer, p);
	return finish();
}
std::vector<Result> State::apply_batch(std::uint64_t index, const std::vector<Command> &commands,
									   std::uint64_t term) {
	require(!commands.empty() && commands.size() <= 64 && index > applied_, "invalid application group");
	State next = *this;
	std::vector<Result> results;
	for (const auto &command : commands) {
		next.applied_ = applied_;
		results.push_back(next.apply(index, command, term));
	}
	*this = std::move(next);
	return results;
}
Result State::lookup(std::string_view name, std::uint64_t generation, std::uint64_t sequence) const {
	Result result{Status::UnknownSession, revision_, 0, generation, sequence};
	auto p = producer(name);
	if (!p) {
		return result;
	}
	if (p->generation != generation || !p->active) {
		result.status = Status::FencedSession;
		return result;
	}
	if (!sequence || sequence > p->high_water) {
		result.status = Status::SequenceGap;
		return result;
	}
	for (auto &receipt : p->receipts) {
		if (receipt.sequence == sequence) {
			return receipt.result;
		}
	}
	result.status = Status::ResultUnavailable;
	return result;
}
std::string encode_batch(const std::vector<Command> &commands, Limits l, std::size_t cap) {
	require(!commands.empty() && commands.size() <= 64, "transaction group bound");
	Writer w{{}, cap};
	w.raw(batch_header(commands.size(), l));
	for (const auto &command : commands) {
		auto bytes = encode(command, l);
		w.text(bytes);
	}
	return std::move(w.bytes);
}
std::size_t batch_framing_bytes(Limits limits) { return batch_header(0, limits).size(); }
std::string batch_header(std::size_t count, Limits limits) {
	require(count <= 64, "transaction group count bound");
	Writer writer{{}, 1024};
	writer.raw("DTABAT02");
	write_limits(writer, limits);
	writer.number(count);
	return std::move(writer.bytes);
}
bool dispose_command(Command &command, CommandWorkBudget &budget) noexcept {
	while (!command.mutations.empty()) {
		auto &op = command.mutations.back();
		if (op.value)
			if (auto flag = std::get_if<BoolDef>(&*op.value); flag && !flag->overrides.empty()) {
				if (!budget.receipts || budget.bytes < 512)
					return false;
				--budget.receipts;
				budget.bytes -= 512;
				flag->overrides.erase(flag->overrides.begin());
				continue;
			}
		if (!budget.receipts || budget.bytes < 1024)
			return false;
		--budget.receipts;
		budget.bytes -= 1024;
		command.mutations.pop_back();
	}
	return true;
}
BatchDecoder::BatchDecoder(std::string_view bytes, Limits limits, std::size_t cap) : limits_(limits) {
	require(bytes.size() <= cap, "transaction group bytes bound");
	Reader reader{bytes};
	require(reader.raw(8) == "DTABAT02", "unsupported transaction group");
	Writer profile{{}, 1024};
	write_limits(profile, limits);
	require(reader.raw(profile.bytes.size()) == profile.bytes, "changed replicated semantic limits");
	auto count = reader.number();
	require(count && count <= 64, "transaction group count bound");
	count_ = static_cast<std::size_t>(count);
	remaining_ = reader.bytes;
}
BatchWork BatchDecoder::next_work() {
	require(!failed_ && decoded_ < count_, "transaction group cursor unavailable");
	failed_ = true;
	Reader reader{remaining_};
	auto bytes = reader.text_view(limits_.request_bytes);
	Reader command{bytes};
	(void)command_prefix(command);
	auto mutations = command.number();
	require(mutations <= limits_.mutations, "mutation count bound");
	failed_ = false;
	return {bytes.size() + 8, static_cast<std::size_t>(mutations)};
}
Command BatchDecoder::next() {
	try {
		return decode(next_bytes(), limits_);
	} catch (...) {
		failed_ = true;
		throw;
	}
}
std::string_view BatchDecoder::next_bytes() {
	require(!failed_ && decoded_ < count_, "transaction group cursor unavailable");
	failed_ = true;
	Reader reader{remaining_};
	auto result = reader.text_view(limits_.request_bytes);
	if (decoded_ + 1 == count_)
		reader.end();
	remaining_ = reader.bytes;
	++decoded_;
	failed_ = false;
	return result;
}
std::vector<Command> decode_batch(std::string_view bytes, Limits limits, std::size_t cap) {
	BatchDecoder decoder(bytes, limits, cap);
	std::vector<Command> result;
	result.reserve(decoder.count());
	while (!decoder.done())
		result.push_back(decoder.next());
	return result;
}
struct CommandApplication::Impl {
	enum class Phase {
		Session,
		Duplicate,
		Inspect,
		Compare,
		Subtract,
		Add,
		Update,
		Namespace,
		CopyProducer,
		StoreProducer,
		Cleanup,
		Complete
	};
	State source, candidate;
	ValidatedCommand validated;
	std::uint64_t index, term;
	Result result;
	Phase phase = Phase::Session;
	std::size_t row = 0, receipt = 0, total = 0, count = 0;
	std::vector<unsigned char> changes;
	const Producer *current = nullptr;
	std::shared_ptr<Producer> producer;
	using Iterator = decltype(BoolDef::overrides)::const_iterator;
	Iterator left{}, right{};
	bool changed = false, failed = false, consumed = false, retired = false;
	Impl(const State &s, std::uint64_t i, std::uint64_t t, ValidatedCommand v)
		: source(s), candidate(s), validated(std::move(v)), index(i), term(t),
		  result{Status::Applied, s.revision(), s.namespace_revision(validated.command().name_space),
				 validated.command().generation, validated.command().sequence},
		  changes(validated.command().mutations.size(), 0), total(s.logical_bytes()), count(s.size()) {
		require(validated.valid_ && validated.limits() == s.limits() &&
					validated.command().context == s.context(),
				"foreign command capability");
		require(i > s.applied_ && i < maximum && t && t >= s.applied_term_, "invalid application boundary");
		current = source.producer(validated.command().producer);
	}
	static bool record(CommandWorkBudget &b, std::size_t bytes = 512) {
		if (!b.records || b.bytes < bytes)
			return false;
		--b.records;
		b.bytes -= bytes;
		return true;
	}
	static bool update(CommandWorkBudget &b) {
		if (!b.updates || b.bytes < 1024)
			return false;
		--b.updates;
		b.bytes -= 1024;
		return true;
	}
	static bool retirement(CommandWorkBudget &b, std::size_t bytes = 512) {
		if (!b.receipts || b.bytes < bytes)
			return false;
		--b.receipts;
		b.bytes -= bytes;
		return true;
	}
	void cleanup() {
		phase = Phase::Cleanup;
		row = 0;
	}
	void copy_producer() {
		producer = candidate.producer_values_->create(41 + current->receipts.size() * 80);
		producer->generation = current->generation;
		producer->high_water = current->high_water;
		producer->active = current->active;
		producer->nonce = current->nonce;
		receipt = 0;
		phase = Phase::CopyProducer;
	}
	void inspect_complete() {
		if (result.status == Status::Applied && changed) {
			if (source.revision_ == maximum)
				result.status = Status::Exhausted;
			else if (!source.namespaces_.find(validated.command().name_space) &&
					 source.namespaces_.size() >= source.limits_.namespaces)
				result.status = Status::Capacity;
			else {
				row = 0;
				phase = Phase::Subtract;
				return;
			}
		} else if (result.status == Status::Applied)
			result.status = Status::NoChange;
		copy_producer();
	}
	bool clear_command(CommandWorkBudget &b) {
		auto &mutations = validated.command_.mutations;
		while (!mutations.empty()) {
			auto &op = mutations.back();
			if (op.value)
				if (auto flag = std::get_if<BoolDef>(&*op.value); flag && !flag->overrides.empty()) {
					if (!retirement(b))
						return false;
					flag->overrides.erase(flag->overrides.begin());
					continue;
				}
			if (!retirement(b, 1024))
				return false;
			mutations.pop_back();
		}
		return true;
	}
};
CommandApplication::CommandApplication(const State &s, std::uint64_t index, std::uint64_t term,
									   ValidatedCommand c)
	: impl_(std::make_unique<Impl>(s, index, term, std::move(c))) {}
CommandApplication::~CommandApplication() = default;
CommandApplication::CommandApplication(CommandApplication &&) noexcept = default;
bool CommandApplication::done() const noexcept {
	return impl_ && !impl_->failed && !impl_->consumed && impl_->phase == Impl::Phase::Complete;
}
void CommandApplication::step(CommandWorkBudget &budget) {
	require(impl_ && !impl_->failed && !impl_->consumed, "command application unavailable");
	auto &d = *impl_;
	auto &c = d.validated.command_;
	using Phase = Impl::Phase;
	try {
		while (d.phase != Phase::Complete)
			switch (d.phase) {
			case Phase::Session:
				if (!Impl::record(budget, 1024))
					return;
				if (c.kind == Kind::Open) {
					if (d.current && d.current->active && d.current->nonce == c.nonce &&
						d.current->generation && c.generation == d.current->generation - 1) {
						d.result.generation = d.current->generation;
						d.cleanup();
						break;
					}
					if ((d.current ? d.current->generation : 0) != c.generation)
						d.result.status = Status::FencedSession;
					else if (!d.current && d.source.producers_.size() >= d.source.limits_.producers)
						d.result.status = Status::Capacity;
					else if (c.generation == maximum)
						d.result.status = Status::Exhausted;
					else {
						d.producer = d.candidate.producer_values_->create(41);
						d.producer->generation = c.generation + 1;
						d.producer->active = true;
						d.producer->nonce = c.nonce;
						d.result.generation = d.producer->generation;
						d.phase = Phase::StoreProducer;
						break;
					}
					d.cleanup();
					break;
				}
				if (!d.current) {
					d.result.status = Status::UnknownSession;
					d.cleanup();
					break;
				}
				if (d.current->generation != c.generation ||
					(c.kind == Kind::Transaction && !d.current->active)) {
					d.result.status = Status::FencedSession;
					d.cleanup();
					break;
				}
				if (c.kind == Kind::Close) {
					d.copy_producer();
					break;
				}
				if (c.sequence <= d.current->high_water) {
					d.phase = Phase::Duplicate;
					break;
				}
				if (d.current->high_water == maximum || c.sequence != d.current->high_water + 1) {
					d.result.status = Status::SequenceGap;
					d.cleanup();
					break;
				}
				if (c.expected_namespace && *c.expected_namespace != d.result.namespace_revision)
					d.result.status = Status::Conflict;
				d.phase = Phase::Inspect;
				break;
			case Phase::Duplicate:
				if (d.receipt == d.current->receipts.size()) {
					d.result.status = Status::ResultUnavailable;
					d.cleanup();
					break;
				}
				if (!Impl::retirement(budget, 128))
					return;
				{
					const auto &r = d.current->receipts[d.receipt++];
					if (r.sequence == c.sequence) {
						if (r.digest != d.validated.digest_)
							d.result.status = Status::IdentityConflict;
						else
							d.result = r.result;
						d.cleanup();
					}
				}
				break;
			case Phase::Inspect:
				if (d.row == c.mutations.size()) {
					d.inspect_complete();
					break;
				}
				if (!Impl::record(budget, 1024))
					return;
				{
					auto &op = c.mutations[d.row];
					auto old = d.source.find(op.key);
					if (op.expected &&
						((!*op.expected && (old || *op.absence_revision != d.result.namespace_revision)) ||
						 (*op.expected && (!old || old->revision != *op.expected))))
						d.result.status = Status::Conflict;
					bool difference = false;
					if (!op.value)
						difference = old != nullptr;
					else if (!old || old->value->index() != op.value->index())
						difference = true;
					else if (auto flag = std::get_if<BoolDef>(&*op.value)) {
						const auto &prior = std::get<BoolDef>(*old->value);
						difference = flag->default_value != prior.default_value ||
									 flag->rollout_pct != prior.rollout_pct ||
									 flag->overrides.size() != prior.overrides.size();
						if (!difference && !flag->overrides.empty()) {
							d.left = prior.overrides.begin();
							d.right = flag->overrides.begin();
							d.phase = Phase::Compare;
							break;
						}
					} else
						difference = std::get<IntDef>(*old->value) != std::get<IntDef>(*op.value);
					d.changes[d.row++] = difference;
					d.changed |= difference;
				}
				break;
			case Phase::Compare:
				if (!Impl::record(budget))
					return;
				{
					auto &flag = std::get<BoolDef>(*c.mutations[d.row].value);
					bool difference = d.left->first != d.right->first || d.left->second != d.right->second;
					++d.left;
					++d.right;
					if (difference || d.right == flag.overrides.end()) {
						d.changes[d.row++] = difference;
						d.changed |= difference;
						d.phase = Phase::Inspect;
					}
				}
				break;
			case Phase::Subtract:
				if (d.row == c.mutations.size()) {
					d.row = 0;
					d.phase = Phase::Add;
					break;
				}
				if (!Impl::record(budget))
					return;
				{
					const auto &op = c.mutations[d.row++];
					if (auto old = d.source.find(op.key)) {
						d.total -= stored_footprint(op.key, *old);
						--d.count;
					}
				}
				break;
			case Phase::Add:
				if (d.row == c.mutations.size()) {
					if (d.count > d.source.limits_.keys) {
						d.result.status = Status::Capacity;
						d.copy_producer();
					} else {
						d.row = 0;
						d.phase = Phase::Update;
					}
					break;
				}
				if (!Impl::record(budget))
					return;
				{
					auto i = d.row++;
					const auto &op = c.mutations[i];
					if (op.value) {
						auto amount = op.key.size() + d.validated.sizes_[i] + 16;
						if (amount > d.source.limits_.state_bytes ||
							d.total > d.source.limits_.state_bytes - amount) {
							d.result.status = Status::Capacity;
							d.copy_producer();
							break;
						}
						d.total += amount;
						++d.count;
					}
				}
				break;
			case Phase::Update:
				if (d.row == c.mutations.size()) {
					d.phase = Phase::Namespace;
					break;
				}
				if (!Impl::update(budget))
					return;
				{
					auto i = d.row++;
					auto &op = c.mutations[i];
					if (d.changes[i]) {
						if (op.value)
							d.candidate.records_ = d.candidate.records_.set(
								op.key, Record{d.source.revision_ + 1,
											   move_definition(*d.candidate.definitions_, *op.value,
															   d.validated.sizes_[i])});
						else
							d.candidate.records_ = d.candidate.records_.erase(op.key);
					}
				}
				break;
			case Phase::Namespace:
				if (!Impl::update(budget))
					return;
				d.candidate.bytes_ = d.total;
				d.candidate.revision_ = d.source.revision_ + 1;
				d.candidate.namespaces_ = d.candidate.namespaces_.set(c.name_space, d.candidate.revision_);
				d.result.revision = d.result.namespace_revision = d.candidate.revision_;
				d.copy_producer();
				break;
			case Phase::CopyProducer:
				if (d.receipt < d.current->receipts.size()) {
					if (!Impl::retirement(budget, 128))
						return;
					d.producer->receipts.push_back(d.current->receipts[d.receipt++]);
					break;
				}
				if (!Impl::retirement(budget, 128))
					return;
				if (c.kind == Kind::Close)
					d.producer->active = false;
				else {
					d.producer->high_water = c.sequence;
					d.producer->receipts.push_back({c.sequence, d.validated.digest_, d.result});
					if (d.producer->receipts.size() > d.source.limits_.results)
						d.producer->receipts.pop_front();
				}
				detail::DeferredValues<Producer>::set_encoded_size(*d.producer,
																   41 + d.producer->receipts.size() * 80);
				d.phase = Phase::StoreProducer;
				break;
			case Phase::StoreProducer:
				if (!Impl::update(budget))
					return;
				d.candidate.producers_ = d.candidate.producers_.set(c.producer, d.producer);
				d.producer.reset();
				d.cleanup();
				break;
			case Phase::Cleanup:
				if (!d.clear_command(budget))
					return;
				d.candidate.applied_ = d.index;
				d.candidate.applied_term_ = d.term;
				d.phase = Phase::Complete;
				break;
			case Phase::Complete:
				break;
			}
	} catch (...) {
		d.failed = true;
		throw;
	}
}
BatchResult CommandApplication::finish() {
	require(done(), "command application incomplete");
	impl_->consumed = true;
	return {std::move(impl_->candidate), {impl_->result}};
}
bool CommandApplication::discard(CommandWorkBudget &budget) {
	require(impl_ && !impl_->consumed, "command application unavailable");
	auto &d = *impl_;
	d.failed = true;
	if (!d.retired) {
		d.candidate.retire();
		d.source.retire();
		d.producer.reset();
		d.retired = true;
	}
	if (!d.clear_command(budget))
		return false;
	d.candidate.collect(budget);
	return !d.candidate.reclamation_pending();
}

BatchApplication::BatchApplication(const State &source, std::uint64_t index, std::uint64_t term,
								   std::size_t count)
	: candidate_(source), index_(index), term_(term), base_(source.applied_), count_(count) {
	require(count && count <= 64 && index > base_ && index < maximum && term && term >= source.applied_term_,
			"invalid application group");
	results_.reserve(count);
}
BatchApplication::BatchApplication(BatchApplication &&other) noexcept
	: candidate_(std::move(other.candidate_)), index_(other.index_), term_(other.term_), base_(other.base_),
	  count_(other.count_), results_(std::move(other.results_)), command_(std::move(other.command_)),
	  failed_(other.failed_), consumed_(std::exchange(other.consumed_, true)) {}
void BatchApplication::advance(const Command &command) {
	require(!failed_ && !consumed_ && !command_ && results_.size() < count_,
			"application group cursor unavailable");
	failed_ = true;
	candidate_.applied_ = base_;
	results_.push_back(candidate_.apply(index_, command, term_));
	failed_ = false;
}
void BatchApplication::start(ValidatedCommand command) {
	require(!failed_ && !consumed_ && !command_ && results_.size() < count_,
			"application group cursor unavailable");
	failed_ = true;
	candidate_.applied_ = base_;
	command_ = std::make_unique<CommandApplication>(candidate_, index_, term_, std::move(command));
	failed_ = false;
}
void BatchApplication::step(CommandWorkBudget &budget) {
	require(!failed_ && !consumed_ && command_, "application group command unavailable");
	failed_ = true;
	command_->step(budget);
	if (command_->done()) {
		auto outcome = command_->finish();
		candidate_ = std::move(outcome.state);
		results_.push_back(outcome.results.front());
		command_.reset();
	}
	failed_ = false;
}
bool BatchApplication::discard(CommandWorkBudget &budget) {
	require(!consumed_, "application group unavailable");
	failed_ = true;
	candidate_.retire();
	if (command_) {
		if (!command_->discard(budget))
			return false;
		command_.reset();
	}
	candidate_.collect(budget);
	return !candidate_.reclamation_pending();
}
BatchResult BatchApplication::finish() {
	require(done() && candidate_.applied_ == index_ && candidate_.applied_term_ == term_,
			"application group incomplete");
	consumed_ = true;
	return {std::move(candidate_), std::move(results_)};
}

Snapshot State::snapshot(std::string_view name_space) const {
	Snapshot s;
	s.version = revision_;
	s.source_id = "authority";
	records_.for_each([&](const std::string &key, const Record &r) {
		if (name_space.empty() || key.substr(0, key.find(':')) == name_space) {
			s.knobs.emplace(key, *r.value);
		}
	});
	return s;
}
ImageEncoder::ImageEncoder(State state)
	: state_(std::move(state)), records_(state_.records_.cursor()), namespaces_(state_.namespaces_.cursor()),
	  producers_(state_.producers_.cursor()) {}
void ImageEncoder::row() {
	Writer w{{}, state_.limits_.image_bytes};
	for (;;) {
		if (stage_ == 0) {
			w.raw("DTAIMG01");
			encode_context(w, state_.context_);
			write_limits(w, state_.limits_);
			w.number(state_.applied_);
			w.number(state_.applied_term_);
			w.number(state_.revision_);
			w.number(state_.records_.size());
			++stage_;
		} else if (stage_ == 1) {
			if (auto item = records_.next()) {
				w.text(*item->first);
				w.number(item->second->revision);
				definition(w, *item->second->value, state_.limits_);
			} else {
				++stage_;
				continue;
			}
		} else if (stage_ == 2) {
			w.number(state_.namespaces_.size());
			++stage_;
		} else if (stage_ == 3) {
			if (auto item = namespaces_.next()) {
				w.text(*item->first);
				w.number(*item->second);
			} else {
				++stage_;
				continue;
			}
		} else if (stage_ == 4) {
			w.number(state_.producers_.size());
			++stage_;
		} else if (stage_ == 5) {
			if (auto item = producers_.next()) {
				auto &p = *item->second;
				w.text(*item->first);
				w.number(p->generation);
				w.number(p->high_water);
				w.flag(p->active);
				w.identity(p->nonce);
				w.number(p->receipts.size());
				for (const auto &receipt : p->receipts) {
					w.number(receipt.sequence);
					w.raw({reinterpret_cast<const char *>(receipt.digest.data()), receipt.digest.size()});
					write_result(w, receipt.result);
				}
			} else {
				stage_ = 7;
				continue;
			}
		} else {
			return;
		}
		pending_ = std::move(w.bytes);
		offset_ = 0;
		return;
	}
}
std::string ImageEncoder::next() {
	std::string output;
	output.reserve(65536);
	while (output.size() < 65536) {
		if (pending_.empty()) {
			row();
			if (finished()) {
				break;
			}
		}
		auto count = std::min<std::size_t>(65536 - output.size(), pending_.size() - offset_);
		require(count <= state_.limits_.image_bytes - written_, "application image bound");
		output.append(pending_, offset_, count);
		offset_ += count;
		written_ += count;
		if (offset_ == pending_.size()) {
			pending_.clear();
			offset_ = 0;
		}
	}
	return output;
}
std::string State::checkpoint() const {
	ImageEncoder encoder(*this);
	std::string image;
	while (!encoder.finished()) {
		image += encoder.next();
	}
	return image;
}

struct ImageDecoder::Impl {
	State state;
	std::string buffer, previous;
	std::size_t received = 0;
	std::uint64_t remaining = 0, position = 0;
	unsigned stage = 0;
	bool final = false, waiting = true;
	std::optional<decltype(State::records_)::Cursor> records;
	Impl(Context context, Limits limits) : state(context, limits) {}
	void row(Reader &r) {
		auto &s = state;
		auto &l = s.limits_;
		if (stage == 0) {
			require(r.raw(8) == "DTAIMG01" && decode_context(r) == s.context_, "foreign application image");
			Writer lw{{}, 1024};
			write_limits(lw, l);
			require(r.raw(lw.bytes.size()) == lw.bytes, "changed semantic limits");
			auto applied = r.number(), term = r.number(), revision = r.number(), keys = r.number();
			require(applied < maximum && bool(applied) == bool(term), "invalid image boundary");
			require(keys <= l.keys, "image key count bound");
			s.applied_ = applied;
			s.applied_term_ = term;
			s.revision_ = revision;
			remaining = keys;
			position = 0;
			stage = 1;
		} else if (stage == 1) {
			if (!remaining) {
				stage = 2;
				previous.clear();
				return;
			}
			auto key = r.text(200);
			auto revision = r.number();
			require(is_valid_knob_name(key) && (!position || previous < key) && revision &&
						revision <= s.revision_,
					"invalid image key");
			auto definition_start = r.bytes.size();
			auto value = definition(r, l);
			auto definition_bytes = definition_start - r.bytes.size();
			auto amount = key.size() + definition_bytes + 16;
			require(amount <= l.state_bytes - s.bytes_, "logical state bound");
			s.records_ =
				s.records_.set(key, {revision, move_definition(*s.definitions_, value, definition_bytes)});
			s.bytes_ += amount;
			previous = std::move(key);
			--remaining;
			++position;
		} else if (stage == 2) {
			auto count = r.number();
			require(count <= l.namespaces, "namespace count bound");
			remaining = count;
			position = 0;
			stage = 3;
		} else if (stage == 3) {
			if (!remaining) {
				records.emplace(s.records_.cursor());
				stage = 4;
				previous.clear();
				return;
			}
			auto name = r.text(199);
			auto revision = r.number();
			require(is_valid_knob_name(name + ":x") && (!position || previous < name) && revision &&
						revision <= s.revision_,
					"invalid namespace record");
			s.namespaces_ = s.namespaces_.set(name, revision);
			previous = std::move(name);
			--remaining;
			++position;
		} else if (stage == 4) {
			if (auto item = records->next()) {
				require(s.namespace_revision(item->first->substr(0, item->first->find(':'))) >=
							item->second->revision,
						"missing namespace revision");
			} else {
				records.reset();
				stage = 5;
			}
		} else if (stage == 5) {
			auto count = r.number();
			require(count <= l.producers, "producer count bound");
			remaining = count;
			position = 0;
			stage = 6;
		} else if (stage == 6) {
			if (!remaining) {
				stage = 7;
				return;
			}
			auto name = r.text(128);
			require(!name.empty() && valid_utf8(name) && (!position || previous < name),
					"invalid producer record");
			auto p = s.producer_values_->create(41);
			p->generation = r.number();
			p->high_water = r.number();
			p->active = r.flag();
			p->nonce = r.identity();
			auto receipts = r.number();
			require(p->generation && p->nonce != Identity{} &&
						receipts == std::min<std::uint64_t>(p->high_water, l.results),
					"invalid receipt window");
			detail::DeferredValues<Producer>::set_encoded_size(*p, 41 + receipts * 80);
			for (std::uint64_t j = 0; j < receipts; ++j) {
				Receipt receipt;
				receipt.sequence = r.number();
				auto dg = r.raw(32);
				std::copy(dg.begin(), dg.end(), receipt.digest.begin());
				receipt.result = read_result(r);
				require(receipt.sequence == p->high_water - receipts + j + 1 &&
							receipt.result.sequence == receipt.sequence &&
							receipt.result.generation == p->generation &&
							receipt.result.revision <= s.revision_ &&
							receipt.result.namespace_revision <= s.revision_ &&
							(receipt.result.status == Status::Applied ||
							 receipt.result.status == Status::NoChange ||
							 receipt.result.status == Status::Conflict ||
							 receipt.result.status == Status::Capacity ||
							 receipt.result.status == Status::Exhausted),
						"invalid receipt correlation");
				p->receipts.push_back(receipt);
			}
			s.producers_ = s.producers_.set(name, p);
			previous = std::move(name);
			--remaining;
			++position;
		} else {
			require(r.bytes.empty(), "trailing authority bytes");
			if (!final)
				throw Incomplete{};
			stage = 8;
		}
	}
};
ImageDecoder::ImageDecoder(Context context, Limits limits) : impl_(std::make_unique<Impl>(context, limits)) {}
ImageDecoder::~ImageDecoder() = default;
ImageDecoder::ImageDecoder(ImageDecoder &&) noexcept = default;
ImageDecoder &ImageDecoder::operator=(ImageDecoder &&) noexcept = default;
void ImageDecoder::feed(std::string_view bytes, bool final) {
	auto &d = *impl_;
	require(d.waiting && !d.final && d.stage != 8, "image decoder input state");
	require(bytes.size() <= 65536 && bytes.size() <= d.state.limits_.image_bytes - d.received,
			"application image bound");
	// A producer row is the largest variable item; no whole-image buffering.
	auto row_bound =
		std::max<std::size_t>(d.state.limits_.definition_bytes + 256, 177 + 80 * d.state.limits_.results);
	require(bytes.size() <= row_bound + 65536 - d.buffer.size(), "image decoder row bound");
	d.buffer.append(bytes);
	d.received += bytes.size();
	d.final = final;
	d.waiting = false;
}
void ImageDecoder::step(std::size_t rows) {
	auto &d = *impl_;
	if (d.waiting || d.stage == 8)
		return;
	std::size_t consumed = 0;
	auto allocations = d.state.allocations();
	try {
		for (std::size_t i = 0; i < rows && d.stage != 8; ++i) {
			Reader r{std::string_view(d.buffer).substr(consumed)};
			auto before = r.bytes.size();
			try {
				d.row(r);
			} catch (const Incomplete &) {
				if (d.final)
					throw;
				d.waiting = true;
				break;
			}
			consumed += before - r.bytes.size();
		}
	} catch (...) {
		d.buffer.erase(0, consumed);
		throw;
	}
	d.buffer.erase(0, consumed);
	d.state.collect(d.state.allocations() - allocations + 128);
}
bool ImageDecoder::needs_input() const noexcept { return impl_->waiting; }
bool ImageDecoder::finished() const noexcept { return impl_->stage == 8; }
State ImageDecoder::take() {
	require(finished(), "incomplete application image");
	return std::move(impl_->state);
}
State ImageDecoder::discard() {
	impl_->records.reset();
	impl_->state.retire();
	return std::move(impl_->state);
}
State State::restore(std::string_view bytes, Context ctx, Limits limits) {
	ImageDecoder decoder(ctx, limits);
	std::size_t offset = 0;
	while (!decoder.finished()) {
		if (decoder.needs_input()) {
			auto count = std::min<std::size_t>(65536, bytes.size() - offset);
			decoder.feed(bytes.substr(offset, count), offset + count == bytes.size());
			offset += count;
		}
		decoder.step();
	}
	return decoder.take();
}
} // namespace detent::authority
