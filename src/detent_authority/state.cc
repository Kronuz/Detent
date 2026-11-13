#include "detent/utf8.hh"
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
using detent::valid_utf8;

// Presence bits of a definition header: rollout_pct, rollout_value, nonempty overrides.
constexpr unsigned has_rollout = 1, has_value = 2, has_overrides = 4;
unsigned presence(const KnobDef &def) {
	return (def.rollout_pct ? has_rollout : 0) | (def.rollout_value ? has_value : 0) |
		   (def.overrides.empty() ? 0 : has_overrides);
}
void octet(Writer &w, unsigned value) {
	char c = static_cast<char>(value);
	w.raw({&c, 1});
}
unsigned octet(Reader &r) { return static_cast<unsigned char>(r.raw(1)[0]); }
// Writes the header: default, presence byte, then the fields it names.
void definition_header(Writer &w, const KnobDef &def, const Limits &limits) {
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
void override_row(Writer &w, std::string_view key, const GroupOverride &value) {
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
std::uint64_t definition_header(Reader &r, KnobDef &def, const Limits &limits) {
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
GroupOverride override_value(Reader &r) {
	const auto kind = octet(r);
	require(kind <= 1, "invalid override kind");
	if (!kind)
		return std::bit_cast<std::int64_t>(r.number());
	auto pct = octet(r);
	require(pct <= 100, "invalid group rollout");
	return GroupRollout{static_cast<std::uint8_t>(pct)};
}
void definition(Writer &w, const KnobDef &def, const Limits &limits) {
	auto start = w.bytes.size();
	definition_header(w, def, limits);
	for (const auto &[key, value] : def.overrides)
		override_row(w, key, value);
	require(w.bytes.size() - start <= limits.definition_bytes, "definition byte bound");
}
KnobDef definition(Reader &r, const Limits &limits) {
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
void write_policy(Writer &w, const LayoutPolicy &p) {
	for (auto n : {p.seal_transactions, p.seal_bytes, p.period, p.carry_bytes, p.carry_count, p.ratio,
				   p.alpha_num, p.alpha_den, p.cap, p.log_entries, p.log_bytes})
		w.number(n);
}
LayoutPolicy read_policy(Reader &r) {
	LayoutPolicy p;
	for (auto *n : {&p.seal_transactions, &p.seal_bytes, &p.period, &p.carry_bytes, &p.carry_count, &p.ratio,
					&p.alpha_num, &p.alpha_den, &p.cap, &p.log_entries, &p.log_bytes})
		*n = r.number();
	validate(p);
	return p;
}
void write_limits(Writer &w, Limits l) {
	for (auto n : {l.keys, l.namespaces, l.producers, l.mutations, l.request_bytes, l.state_bytes,
				   l.image_bytes, l.definition_bytes, l.overrides, l.results}) {
		w.number(n);
	}
}
} // namespace

std::string encode_policy(const LayoutPolicy &policy) {
	Writer w{{}, 88};
	write_policy(w, policy);
	return std::move(w.bytes);
}
LayoutPolicy decode_policy(std::string_view bytes) {
	Reader r{bytes};
	auto policy = read_policy(r);
	r.end();
	return policy;
}
std::string encode_definition(const KnobDef &value, const Limits &limits) {
	Writer w{{}, limits.definition_bytes};
	definition(w, value, limits);
	return std::move(w.bytes);
}
KnobDef decode_definition(std::string_view bytes, const Limits &limits) {
	Reader r{bytes};
	auto value = definition(r, limits);
	r.end();
	return value;
}
void validate(const LayoutPolicy &p) {
	// Bounds keep every product of the layout's arithmetic inside 128 bits.
	require(p.seal_transactions && p.seal_bytes && p.period && p.period <= (1u << 20) &&
				p.carry_bytes <= (std::uint64_t{1} << 32) && p.carry_count <= 256 && p.ratio >= 2 &&
				p.ratio <= 256 && p.alpha_num && p.alpha_num <= 65536 && p.alpha_den && p.alpha_den <= 65536 &&
				p.cap && p.cap <= 256 && p.log_entries >= 8 && p.log_bytes >= 8,
			"invalid layout policy");
}
std::string semantic_profile(Limits limits) {
	Writer writer{{}, 256};
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
	decltype(KnobDef::overrides)::const_iterator group{};
	const KnobDef *flag = nullptr;
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
					d.flag = &*op.value;
					definition_header(w, *d.flag, d.limits);
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
					override_row(w, key, value);
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
		// Transfer the override table by swap instead of copying the definition.
		command.mutations.emplace_back();
		auto &completed = command.mutations.back();
		completed.key = std::move(pending.key);
		completed.expected = pending.expected;
		completed.absence_revision = pending.absence_revision;
		if (pending.value) {
			require(!definition_error(*pending.value), "invalid definition shape");
			completed.value = KnobDef{};
			auto &destination = *completed.value;
			destination.default_value = pending.value->default_value;
			destination.rollout_pct = pending.value->rollout_pct;
			destination.rollout_value = pending.value->rollout_value;
			destination.overrides.swap(pending.value->overrides);
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
				pending.value = KnobDef{};
				overrides = definition_header(reader, *pending.value, limits);
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
					auto value = override_value(reader);
					require(definition_start - reader.bytes.size() <= limits.definition_bytes,
							"definition byte bound");
					previous_group = key;
					pending.value->overrides.emplace(std::move(key), value);
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
			auto &overrides = mutation.value->overrides;
			while (!overrides.empty()) {
				if (!Impl::claim(budget, 512))
					return false;
				overrides.erase(std::prev(overrides.end())); // last element: no shift
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
	require(l.keys <= UINT32_MAX && l.namespaces <= 65536 && l.producers <= 65536 &&
				l.image_bytes <= std::uint64_t(INT64_MAX),
			"authority resource ceiling");
	auto metadata =
		std::uint64_t{1024} + static_cast<std::uint64_t>(l.namespaces) * 248 +
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
	value->default_value = source.default_value;
	value->rollout_pct = source.rollout_pct;
	value->rollout_value = source.rollout_value;
	value->overrides.swap(source.overrides);
	return value;
}
std::size_t canonical_bytes(std::string_view key, const Record *record) {
	return key.size() + (record ? detail::DeferredValues<KnobDef>::encoded_size(*record->value) : 0) + 16;
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
std::size_t detail::collect_definition(DeferredValues<KnobDef> &arena, CommandWorkBudget &budget) noexcept {
	if (!budget.receipts || budget.bytes < 128)
		return 0;
	return arena.collect_one([&](KnobDef &value) {
		if (!value.overrides.empty()) {
			if (budget.bytes < 512)
				return false;
			value.overrides.erase(std::prev(value.overrides.end())); // last element: no shift
			budget.bytes -= 512;
			--budget.receipts;
			return false;
		}
		budget.bytes -= 128;
		--budget.receipts;
		return true;
	});
}
bool State::reclamation_pending() const noexcept {
	return records_.reclamation_pending() || namespaces_.reclamation_pending() ||
		   directory_.reclamation_pending() || producers_.reclamation_pending() || definitions_->pending() || producer_values_->pending();
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
	while (budget.receipts && budget.bytes >= 128 && idle < 6) {
		auto previous = budget.receipts;
		std::size_t n = 0;
		auto lane = collection_cursor_;
		collection_cursor_ = (collection_cursor_ + 1) % 6;
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
			n = detail::collect_definition(*definitions_, budget);
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
		case 5:
			n = directory_.collect(1);
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
	return p ? p->position : 0;
}
Identity derive_incarnation(const Context &context, const Identity &seed, std::uint64_t directory,
							std::string_view name) {
	Writer w{{}, 512};
	w.raw("detent namespace incarnation");
	encode_context(w, context);
	w.identity(seed);
	w.number(directory);
	w.text(name);
	SHA256 sha;
	sha.add(w.bytes.data(), w.bytes.size());
	Digest hash{};
	sha.getHash(hash.data());
	Identity result{};
	std::copy_n(reinterpret_cast<const char *>(hash.data()), result.size(), result.begin());
	return result;
}
NamespaceCursor State::namespace_cursor(std::string_view name) const {
	auto entry = namespaces_.find(name);
	return {context_.cluster, entry ? entry->incarnation : Identity{}, entry ? entry->position : 0};
}
CursorAnswer State::answer(std::string_view name, const NamespaceCursor &cursor) const {
	auto entry = namespaces_.find(name);
	if (!entry || !entry->live)
		return CursorAnswer::Absent;
	if (!positions_enabled() || cursor.cluster != context_.cluster || cursor.incarnation != entry->incarnation)
		return CursorAnswer::FullCopy;
	if (cursor.position == entry->position)
		return CursorAnswer::Current;
	return cursor.position < entry->position ? CursorAnswer::Behind : CursorAnswer::Ahead;
}
Namespace State::changed_namespace(std::string_view name) const {
	auto found = namespaces_.find(name);
	auto next = found ? *found : Namespace{};
	if (!positions_enabled()) {
		next.position = revision_ + 1;
		return next;
	}
	++next.position;
	if (!found || !found->live) {
		// A first write, or one after a drop, creates a new incarnation; positions continue.
		next.directory = directory_head_ + 1;
		next.incarnation = derive_incarnation(context_, seed_, next.directory, name);
		next.origin = next.position - 1;
		next.live = true;
	}
	return next;
}
void State::count_change(std::uint64_t bytes) noexcept {
	if (!positions_enabled())
		return;
	++seal_.transactions;
	seal_.bytes += bytes;
}
bool State::close_entry(std::uint64_t payload_bytes) {
	sealed_ = {};
	if (!positions_enabled())
		return false;
	auto &c = seal_;
	const auto &p = policy_;
	if (!c.forced) {
		++c.entries;
		c.log_bytes += payload_bytes;
		const bool due = c.transactions >= p.seal_transactions || c.bytes >= p.seal_bytes ||
						 c.entries >= std::max<std::uint64_t>(1, p.log_entries / 8) ||
						 c.log_bytes >= std::max<std::uint64_t>(1, p.log_bytes / 8);
		if (!due)
			return false;
		++c.ordinal;
	}
	// A new policy takes effect here; a new period only where the old one divides the ordinal.
	if (pending_) {
		auto next = *pending_;
		if (next.period != policy_.period && c.ordinal % policy_.period) {
			next.period = policy_.period;
			policy_ = next;
		} else {
			policy_ = next;
			pending_.reset();
		}
	}
	sealed_ = std::move(touched_);
	touched_ = {};
	c = {c.ordinal, applied_};
	return true;
}
void State::store_namespace(const std::string &name, std::uint64_t previous_directory, const Namespace &next) {
	namespaces_ = namespaces_.set(name, next);
	if (next.directory != previous_directory) {
		if (previous_directory)
			directory_ = directory_.erase(previous_directory);
		directory_ = directory_.set(next.directory, name);
		directory_head_ = std::max(directory_head_, next.directory);
	}
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
				auto previous = namespaces_.find(c.name_space);
				auto entry = changed_namespace(c.name_space);
				entry.bytes = entry.bytes + total - bytes_;
				std::uint64_t changes = 0;
				for (const auto &op : c.mutations) {
					auto old = find(op.key);
					const bool set = op.value && (!old || *old->value != *op.value);
					if (set) {
						auto amount = footprint(op.key, *op.value, limits_);
						candidate = candidate.set(
							op.key, Record{entry.position, copy_definition(*next.definitions_, *op.value,
																		   amount - op.key.size() - 16)});
						changes += amount;
					} else if (!op.value && old) {
						candidate = candidate.erase(op.key);
						changes += canonical_bytes(op.key, nullptr);
					}
					if (positions_enabled() && (set || (!op.value && old)))
						next.touched_ = next.touched_.set(op.key, entry.position);
				}
				next.count_change(changes);
				next.records_ = std::move(candidate);
				next.bytes_ = total;
				next.revision_ = revision_ + 1;
				next.store_namespace(c.name_space, previous ? previous->directory : 0, entry);
				result.revision = next.revision_;
				result.namespace_revision = entry.position;
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
		if (op.value && !op.value->overrides.empty()) {
			if (!budget.receipts || budget.bytes < 512)
				return false;
			--budget.receipts;
			budget.bytes -= 512;
			op.value->overrides.erase(std::prev(op.value->overrides.end())); // last element: no shift
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
		CaptureChange,
		CopyProducer,
		StoreProducer,
		Cleanup,
		Complete
	};
	State source, candidate;
	std::shared_ptr<AppliedChangeRecorder> recorder;
	std::shared_ptr<AppliedChangeDraft> draft;
	bool history_started = false, history_failed = false;
	ValidatedCommand validated;
	std::uint64_t index, term;
	Result result;
	Phase phase = Phase::Session;
	std::size_t row = 0, receipt = 0, total = 0, count = 0;
	std::uint64_t changed_bytes = 0; // Canonical bytes of the changes, for the seal trigger.
	Namespace entry;
	std::uint64_t previous_directory = 0;
	std::vector<unsigned char> changes;
	const Producer *current = nullptr;
	std::shared_ptr<Producer> producer;
	using Iterator = decltype(KnobDef::overrides)::const_iterator;
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
				const auto &name = validated.command().name_space;
				auto previous = source.namespaces_.find(name);
				previous_directory = previous ? previous->directory : 0;
				entry = source.changed_namespace(name);
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
			if (op.value && !op.value->overrides.empty()) {
				if (!retirement(b))
					return false;
				op.value->overrides.erase(std::prev(op.value->overrides.end())); // last element: no shift
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
                                     ValidatedCommand command, std::shared_ptr<AppliedChangeRecorder> recorder)
 : impl_(std::make_unique<Impl>(s,index,term,std::move(command))) {
 impl_->recorder=std::move(recorder);
}
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
					else if (!old)
						difference = true;
					else {
						const auto &flag = *op.value;
						const auto &prior = *old->value;
						difference = flag.default_value != prior.default_value ||
									 flag.rollout_pct != prior.rollout_pct ||
									 flag.rollout_value != prior.rollout_value ||
									 flag.overrides.size() != prior.overrides.size();
						if (!difference && !flag.overrides.empty()) {
							d.left = prior.overrides.begin();
							d.right = flag.overrides.begin();
							d.phase = Phase::Compare;
							break;
						}
					}
					d.changes[d.row++] = difference;
					d.changed |= difference;
				}
				break;
			case Phase::Compare:
				if (!Impl::record(budget))
					return;
				{
					auto &flag = *c.mutations[d.row].value;
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
						d.changed_bytes += op.key.size() + (op.value ? d.validated.sizes_[i] : 0) + 16;
						if (d.source.positions_enabled())
							d.candidate.touched_ = d.candidate.touched_.set(op.key, d.entry.position);
						if (op.value)
							d.candidate.records_ = d.candidate.records_.set(
								op.key, Record{d.entry.position,
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
				d.entry.bytes = d.entry.bytes + d.total - d.source.bytes_;
				d.candidate.store_namespace(c.name_space, d.previous_directory, d.entry);
				d.candidate.count_change(d.changed_bytes);
				d.result.revision = d.candidate.revision_;
				d.result.namespace_revision = d.entry.position;
				if (d.recorder)
					d.phase = Phase::CaptureChange;
				else
					d.copy_producer();
				break;
			case Phase::CaptureChange:
				if (!d.history_started) {
					d.history_started = true;
					try {
						d.draft = d.recorder->capture(d.candidate, c, d.changes);
						if (!d.draft)
							d.recorder->gap(d.candidate.revision_);
					} catch (...) {
						d.history_failed = true;
						d.recorder->gap(d.candidate.revision_);
					}
				}
				if (d.draft) {
					if (d.history_failed) {
						if (!d.draft->discard(budget))
							return;
					} else {
						try {
							if (!d.draft->step(budget))
								return;
							d.recorder->stage(d.draft);
						} catch (...) {
							d.history_failed = true;
							d.recorder->gap(d.candidate.revision_);
							break;
						}
					}
					d.draft.reset();
				}
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
	if (d.draft) {
		if (!d.draft->discard(budget)) return false;
		d.draft.reset();
	}
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
								   std::size_t count, std::shared_ptr<AppliedChangeRecorder> recorder)
	: candidate_(source), index_(index), term_(term), base_(source.applied_), count_(count), recorder_(std::move(recorder)) {
	require(count && count <= 64 && index > base_ && index < maximum && term && term >= source.applied_term_,
			"invalid application group");
	results_.reserve(count);
}
BatchApplication::BatchApplication(BatchApplication &&other) noexcept
	: candidate_(std::move(other.candidate_)), index_(other.index_), term_(other.term_), base_(other.base_),
	  count_(other.count_), results_(std::move(other.results_)), command_(std::move(other.command_)),
	  recorder_(std::move(other.recorder_)), failed_(other.failed_), consumed_(std::exchange(other.consumed_, true)) {}
void BatchApplication::advance(const Command &command) {
	require(!failed_ && !consumed_ && !command_ && results_.size() < count_,
			"application group cursor unavailable");
	failed_ = true;
	candidate_.applied_ = base_;
	auto before = candidate_.revision();
	results_.push_back(candidate_.apply(index_, command, term_));
	if (recorder_ && candidate_.revision() != before) recorder_->gap(candidate_.revision());
	failed_ = false;
}
void BatchApplication::start(ValidatedCommand command) {
	require(!failed_ && !consumed_ && !command_ && results_.size() < count_,
			"application group cursor unavailable");
	failed_ = true;
	candidate_.applied_ = base_;
	command_ = std::make_unique<CommandApplication>(candidate_, index_, term_, std::move(command), recorder_);
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

namespace {
void validate_administrative(const AdministrativeCommand &c) {
	require(c.context.cluster != Identity{} && c.context.group != Identity{} && c.context.epoch != Identity{},
			"missing authority identity");
	if (c.kind == AdministrativeKind::EnablePositions)
		require(c.nonce != Identity{} && c.name_space.empty() && c.incarnation == Identity{},
				"invalid enabling entry");
	else if (c.kind == AdministrativeKind::SetLayout)
		require(c.nonce == Identity{} && c.name_space.empty() && c.incarnation == Identity{},
				"invalid layout entry");
	else
		require(c.kind == AdministrativeKind::DropNamespace && c.nonce == Identity{} &&
					c.incarnation != Identity{} && !c.name_space.empty() &&
					c.name_space.size() < 200 && is_valid_knob_name(c.name_space + ":x") &&
					c.policy == LayoutPolicy{},
				"invalid namespace drop");
	validate(c.policy);
}
bool carries_policy(AdministrativeKind kind) { return kind != AdministrativeKind::DropNamespace; }
} // namespace
bool is_administrative(std::string_view payload) noexcept { return payload.starts_with("DTAADM01"); }
std::string encode(const AdministrativeCommand &c, Limits l) {
	validate_administrative(c);
	Writer w{{}, 1024};
	w.raw("DTAADM01");
	write_limits(w, l);
	encode_context(w, c.context);
	w.number(static_cast<std::uint8_t>(c.kind));
	w.identity(c.nonce);
	w.text(c.name_space);
	w.identity(c.incarnation);
	if (carries_policy(c.kind))
		write_policy(w, c.policy);
	return std::move(w.bytes);
}
AdministrativeCommand decode_administrative(std::string_view bytes, Limits l) {
	Reader r{bytes};
	require(r.raw(8) == "DTAADM01", "unsupported administrative entry");
	Writer profile{{}, 1024};
	write_limits(profile, l);
	require(r.raw(profile.bytes.size()) == profile.bytes, "changed replicated semantic limits");
	AdministrativeCommand c;
	c.context = decode_context(r);
	auto kind = r.number();
	require(kind >= 1 && kind <= 3, "unknown administrative kind");
	c.kind = static_cast<AdministrativeKind>(kind);
	c.nonce = r.identity();
	c.name_space = r.text(199);
	c.incarnation = r.identity();
	if (carries_policy(c.kind))
		c.policy = read_policy(r);
	r.end();
	validate_administrative(c);
	return c;
}
AdministrativeApplication::AdministrativeApplication(const State &source, std::uint64_t index,
													 std::uint64_t term, AdministrativeCommand command)
	: source_(source), candidate_(source), index_(index), term_(term), command_(std::move(command)),
	  result_{Status::Applied, source.revision_, 0, 0, 0} {
	validate_administrative(command_);
	require(command_.context == source.context_, "foreign authority context");
	require(index > source.applied_ && index < maximum && term && term >= source.applied_term_,
			"invalid application boundary");
}
void AdministrativeApplication::step(CommandWorkBudget &budget) {
	require(!consumed_ && !retired_, "administrative application unavailable");
	auto claim = [&] {
		if (!budget.updates || budget.bytes < 1024)
			return false;
		--budget.updates;
		budget.bytes -= 1024;
		return true;
	};
	auto complete = [&] {
		candidate_.applied_ = index_;
		candidate_.applied_term_ = term_;
		phase_ = Phase::Complete;
	};
	if (phase_ == Phase::Inspect) {
		if (!claim())
			return;
		const auto &name = command_.name_space;
		if (command_.kind == AdministrativeKind::EnablePositions) {
			if (source_.positions_enabled()) {
				result_.status = Status::NoChange;
				return complete();
			}
			// Each namespace keeps its stored revision as its starting position;
			// the entry is seal 0, where every namespace's first base is taken.
			candidate_.seed_ = command_.nonce;
			candidate_.policy_ = command_.policy;
			candidate_.seal_ = {0, 0, 0, 0, 0, 0, true};
			cursor_.emplace(source_.namespaces_.cursor());
			phase_ = Phase::Assign;
		} else if (command_.kind == AdministrativeKind::SetLayout) {
			if (!source_.positions_enabled())
				result_.status = Status::Conflict;
			else if (command_.policy == (source_.pending_ ? *source_.pending_ : source_.policy_))
				result_.status = Status::NoChange;
			else
				candidate_.pending_ = command_.policy;
			return complete();
		} else {
			auto entry = source_.namespaces_.find(name);
			if (!source_.positions_enabled())
				result_.status = Status::Conflict;
			else if (!entry || (!entry->live && entry->incarnation == command_.incarnation))
				result_.status = Status::NoChange; // Absent, or this incarnation is already dropped.
			else if (!entry->live || entry->incarnation != command_.incarnation)
				result_.status = Status::Conflict;
			else if (source_.revision_ == maximum)
				result_.status = Status::Exhausted;
			else {
				// The drop is one change at the next position: every key goes, the
				// tombstone keeps the position so a re-creation never reuses one.
				candidate_.records_ = source_.records_.erase_range(name + ":", name + ";");
				candidate_.touched_ = source_.touched_.erase_range(name + ":", name + ";");
				candidate_.count_change(0);
				candidate_.bytes_ = source_.bytes_ - entry->bytes;
				candidate_.revision_ = source_.revision_ + 1;
				auto next = *entry;
				++next.position;
				next.bytes = 0;
				next.live = false;
				next.directory = source_.directory_head_ + 1;
				candidate_.store_namespace(name, entry->directory, next);
				result_.revision = candidate_.revision_;
				result_.namespace_revision = next.position;
			}
			return complete();
		}
	}
	while (phase_ == Phase::Assign && claim()) {
		auto row = cursor_->next();
		if (!row) {
			cursor_.reset();
			return complete();
		}
		auto next = *row->second;
		next.directory = candidate_.directory_head_ + 1;
		next.origin = next.position;
		next.incarnation = derive_incarnation(candidate_.context_, candidate_.seed_, next.directory, *row->first);
		candidate_.store_namespace(*row->first, 0, next);
	}
}
BatchResult AdministrativeApplication::finish() {
	require(done(), "administrative application incomplete");
	consumed_ = true;
	return {std::move(candidate_), {result_}};
}
bool AdministrativeApplication::discard(CommandWorkBudget &budget) {
	require(!consumed_, "administrative application unavailable");
	if (!retired_) {
		cursor_.reset();
		candidate_.retire();
		source_.retire();
		retired_ = true;
	}
	candidate_.collect(budget);
	return !candidate_.reclamation_pending();
}
Result State::administer(std::uint64_t index, const AdministrativeCommand &command, std::uint64_t term) {
	auto allocations_before = allocations();
	AdministrativeApplication application(*this, index, term, command);
	while (!application.done()) {
		CommandWorkBudget budget;
		application.step(budget);
	}
	auto outcome = application.finish();
	*this = std::move(outcome.state);
	collect(allocations() - allocations_before + 128);
	return outcome.results.front();
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
	  producers_(state_.producers_.cursor()), touched_(state_.touched_.cursor()) {}
void ImageEncoder::row() {
	Writer w{{}, state_.limits_.image_bytes};
	for (;;) {
		if (stage_ == 0) {
			w.raw("DTAIMG03");
			encode_context(w, state_.context_);
			write_limits(w, state_.limits_);
			w.number(state_.applied_);
			w.number(state_.applied_term_);
			w.number(state_.revision_);
			w.number(state_.records_.size());
			w.identity(state_.seed_);
			w.number(state_.directory_head_);
			write_policy(w, state_.policy_);
			w.flag(state_.pending_.has_value());
			if (state_.pending_)
				write_policy(w, *state_.pending_);
			const auto &c = state_.seal_;
			for (auto n : {c.ordinal, c.index, c.transactions, c.bytes, c.entries, c.log_bytes})
				w.number(n);
			w.flag(c.forced);
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
				// Live sizes are recomputed from the records.
				w.text(*item->first);
				w.number(item->second->position);
				w.number(item->second->directory);
				w.number(item->second->origin);
				w.flag(item->second->live);
				w.identity(item->second->incarnation);
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
				++stage_;
				continue;
			}
		} else if (stage_ == 6) {
			w.number(state_.touched_.size());
			++stage_;
		} else if (stage_ == 7) {
			if (auto item = touched_.next()) {
				w.text(*item->first);
				w.number(*item->second);
			} else {
				stage_ = 9;
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
	std::uint64_t remaining = 0, position = 0, namespace_bytes = 0, counted_bytes = 0;
	std::string counting; // The namespace whose live size stage 4 is summing.
	unsigned stage = 0;
	bool final = false, waiting = true, complete = false, released = false;
	std::optional<decltype(State::records_)::Cursor> records;
	Impl(Context context, Limits limits) : state(context, limits) {}
	// A namespace's records are contiguous in key order, so its live size is one run.
	void count_namespace() {
		if (!counting.empty()) {
			auto entry = *state.namespaces_.find(counting);
			entry.bytes = namespace_bytes;
			state.namespaces_ = state.namespaces_.set(counting, entry);
		}
		counting.clear();
		namespace_bytes = 0;
	}
	void row(Reader &r) {
		auto &s = state;
		auto &l = s.limits_;
		if (stage == 0) {
			require(r.raw(8) == "DTAIMG03" && decode_context(r) == s.context_, "foreign application image");
			Writer lw{{}, 1024};
			write_limits(lw, l);
			require(r.raw(lw.bytes.size()) == lw.bytes, "changed semantic limits");
			auto applied = r.number(), term = r.number(), revision = r.number(), keys = r.number();
			require(applied < maximum && bool(applied) == bool(term), "invalid image boundary");
			require(keys <= l.keys, "image key count bound");
			s.seed_ = r.identity();
			s.directory_head_ = r.number();
			require(s.positions_enabled() || !s.directory_head_, "invalid namespace directory");
			s.policy_ = read_policy(r);
			if (r.flag())
				s.pending_ = read_policy(r);
			auto &c = s.seal_;
			for (auto *n : {&c.ordinal, &c.index, &c.transactions, &c.bytes, &c.entries, &c.log_bytes})
				*n = r.number();
			c.forced = r.flag();
			require(c.index <= applied && (s.positions_enabled() || c == SealCounters{}) &&
						(!c.forced || !c.ordinal),
					"invalid seal counters");
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
			Namespace entry;
			entry.position = r.number();
			entry.directory = r.number();
			entry.origin = r.number();
			entry.live = r.flag();
			entry.incarnation = r.identity();
			require(is_valid_knob_name(name + ":x") && (!position || previous < name) && entry.position &&
						entry.position <= s.revision_ && entry.origin <= entry.position,
					"invalid namespace record");
			if (s.positions_enabled()) {
				require(entry.directory && entry.directory <= s.directory_head_ &&
							!s.directory_.find(entry.directory),
						"invalid namespace directory");
				// A live namespace's incarnation is its creation's; a tombstone keeps the dropped one.
				require(entry.live ? entry.incarnation ==
										 derive_incarnation(s.context_, s.seed_, entry.directory, name)
								   : entry.incarnation != Identity{},
						"invalid namespace incarnation");
				s.directory_ = s.directory_.set(entry.directory, name);
			} else
				require(!entry.directory && entry.live && entry.incarnation == Identity{},
						"invalid namespace directory");
			s.namespaces_ = s.namespaces_.set(name, entry);
			previous = std::move(name);
			--remaining;
			++position;
		} else if (stage == 4) {
			if (auto item = records->next()) {
				auto name = std::string_view(*item->first).substr(0, item->first->find(':'));
				auto entry = s.namespaces_.find(name);
				require(entry && entry->live && entry->position >= item->second->revision,
						"missing namespace revision");
				if (name != counting) {
					count_namespace();
					counting = name;
				}
				auto amount = stored_footprint(*item->first, *item->second);
				namespace_bytes += amount;
				counted_bytes += amount;
			} else {
				count_namespace();
				require(counted_bytes == s.bytes_, "namespace size mismatch");
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
		} else if (stage == 7) {
			auto count = r.number();
			require(count <= l.keys + s.seal_.transactions * l.mutations, "accumulator count bound");
			require(s.positions_enabled() || !count, "accumulator before positions");
			remaining = count;
			position = 0;
			previous.clear();
			stage = 8;
		} else if (stage == 8) {
			if (!remaining) {
				stage = 9;
				return;
			}
			auto key = r.text(200);
			auto at = r.number();
			auto name = std::string_view(key).substr(0, key.find(':'));
			auto entry = s.namespaces_.find(name);
			require(is_valid_knob_name(key) && (!position || previous < key) && entry && entry->live &&
						at > entry->origin && at <= entry->position,
					"invalid accumulator key");
			s.touched_ = s.touched_.set(key, at);
			previous = std::move(key);
			--remaining;
			++position;
		} else {
			require(r.bytes.empty(), "trailing authority bytes");
			if (!final)
				throw Incomplete{};
			stage = 10;
		}
	}
};
ImageDecoder::ImageDecoder(Context context, Limits limits) : impl_(std::make_unique<Impl>(context, limits)) {}
ImageDecoder::~ImageDecoder() = default;
ImageDecoder::ImageDecoder(ImageDecoder &&) noexcept = default;
ImageDecoder &ImageDecoder::operator=(ImageDecoder &&) noexcept = default;
void ImageDecoder::feed(std::string_view bytes, bool final) {
	auto &d = *impl_;
	require(!d.released && d.waiting && !d.final && d.stage != 10, "image decoder input state");
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
	if (d.complete)
		return;
	require(!d.released, "released image decoder");
	if (d.state.reclamation_pending()) {
		CommandWorkBudget cleanup;
		d.state.collect(cleanup);
		if (d.state.reclamation_pending())
			return;
	}
	if (d.stage == 10) {
		d.complete = true;
		return;
	}
	if (d.waiting)
		return;
	std::size_t consumed = 0;
	auto allocations = d.state.allocations();
	try {
		for (std::size_t i = 0; i < rows && d.stage != 10; ++i) {
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
	d.complete = d.stage == 10 && !d.state.reclamation_pending();
}
bool ImageDecoder::needs_input() const noexcept {
	return !impl_->released && !impl_->complete && impl_->waiting && !impl_->state.reclamation_pending();
}
bool ImageDecoder::finished() const noexcept { return impl_->complete; }
State ImageDecoder::take() {
	require(!impl_->released && finished(), "incomplete application image");
	impl_->released = true;
	return std::move(impl_->state);
}
State ImageDecoder::discard() {
	require(!impl_->released, "released image decoder");
	impl_->released = true;
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
