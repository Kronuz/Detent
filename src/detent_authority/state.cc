// State: the replicated application state, its reclamation, lookups and single-command apply.
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
std::size_t canonical_bytes(std::string_view key, const Record *record) {
	return key.size() + (record ? detail::DeferredValues<KnobDef>::encoded_size(*record->value) : 0) + 16;
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
	} else if (c.index) {
		++c.ordinal; // A seal entry; the enabling entry's seal 0 has no previous seal.
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
} // namespace detent::authority
