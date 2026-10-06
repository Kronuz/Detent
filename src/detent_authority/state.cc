#include "state.hh"
#include "detent/validation.hh"
#include <algorithm>
#include <bit>
#include <limits>
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
		auto p = std::make_shared<Producer>();
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
		auto p = std::make_shared<Producer>(*current);
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
					total -= footprint(op.key, *old->value, limits_);
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
							op.key, Record{revision_ + 1, std::make_shared<KnobDef>(*op.value)});
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
	auto p = std::make_shared<Producer>(*current);
	p->high_water = c.sequence;
	p->receipts.push_back({c.sequence, canonical_digest, result});
	if (p->receipts.size() > limits_.results) {
		p->receipts.pop_front();
	}
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
	w.raw("DTABAT02");
	write_limits(w, l);
	w.number(commands.size());
	for (const auto &command : commands) {
		auto bytes = encode(command, l);
		w.text(bytes);
	}
	return std::move(w.bytes);
}
std::size_t batch_framing_bytes(Limits limits) { return 8 + semantic_profile(limits).size() + 8; }
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
	require(!failed_ && decoded_ < count_, "transaction group cursor unavailable");
	failed_ = true;
	Reader reader{remaining_};
	auto result = decode(reader.text_view(limits_.request_bytes), limits_);
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
BatchApplication::BatchApplication(const State &source, std::uint64_t index, std::uint64_t term,
								   std::size_t count)
	: candidate_(source), index_(index), term_(term), base_(source.applied_), count_(count) {
	require(count && count <= 64 && index > base_ && index < maximum && term && term >= source.applied_term_,
			"invalid application group");
	results_.reserve(count);
}
BatchApplication::BatchApplication(BatchApplication &&other) noexcept
	: candidate_(std::move(other.candidate_)), index_(other.index_), term_(other.term_), base_(other.base_),
	  count_(other.count_), results_(std::move(other.results_)), failed_(other.failed_),
	  consumed_(std::exchange(other.consumed_, true)) {}
void BatchApplication::advance(const Command &command) {
	require(!failed_ && !consumed_ && results_.size() < count_, "application group cursor unavailable");
	failed_ = true;
	candidate_.applied_ = base_;
	results_.push_back(candidate_.apply(index_, command, term_));
	failed_ = false;
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
			auto value = definition(r, l);
			auto amount = footprint(key, value, l);
			require(amount <= l.state_bytes - s.bytes_, "logical state bound");
			s.records_ = s.records_.set(key, {revision, std::make_shared<KnobDef>(std::move(value))});
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
			auto p = std::make_shared<Producer>();
			p->generation = r.number();
			p->high_water = r.number();
			p->active = r.flag();
			p->nonce = r.identity();
			auto receipts = r.number();
			require(p->generation && p->nonce != Identity{} &&
						receipts == std::min<std::uint64_t>(p->high_water, l.results),
					"invalid receipt window");
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
