// The wire codec of commands and batches: validation, encoding and decoding, and their stepwise encoder and decoder.
#include "state_internal.hh"
#include "detent/utf8.hh"
#include "state.hh"
#include "deferred_values.hh"
#include "history_format.hh"
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
void admit_limits(const Limits &recorded, const Limits &configured) {
	for (const auto &f : limit_fields) {
		const auto was = recorded.*f.field, is = configured.*f.field;
		if (was == is || (f.raisable && was < is))
			continue;
		throw std::invalid_argument(
			"authority application_limits." + std::string(f.name) + " " + std::to_string(is) +
			(f.raisable ? " is below the recorded " + std::to_string(was) + "; limits only rise"
						: " differs from the recorded " + std::to_string(was) +
							  "; only keys, state_bytes and image_bytes rise at a restart"));
	}
}
Limits recorded_limits(std::string_view entry, const Limits &configured) {
	Reader r{entry};
	const auto magic = r.raw(8);
	require(magic == "DTABAT02" || magic == "DTAADM01", "unsupported authority entry");
	return read_limits(r, configured);
}

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
BatchDecoder::BatchDecoder(std::string_view bytes, Limits limits, std::size_t cap) {
	require(bytes.size() <= cap, "transaction group bytes bound");
	Reader reader{bytes};
	require(reader.raw(8) == "DTABAT02", "unsupported transaction group");
	limits_ = read_limits(reader, limits);
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
} // namespace detent::authority
