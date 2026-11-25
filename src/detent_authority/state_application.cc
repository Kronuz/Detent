// The stepwise application of one command and of a batch of commands to a State.
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
		require(validated.valid_ && validated.command().context == s.context(), "foreign command capability");
		admit_limits(validated.limits(), s.limits());
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
					 source.namespaces_.size() >= validated.limits_.namespaces)
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
					else if (!d.current && d.source.producers_.size() >= d.validated.limits_.producers)
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
					if (d.count > d.validated.limits_.keys) {
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
						if (amount > d.validated.limits_.state_bytes ||
							d.total > d.validated.limits_.state_bytes - amount) {
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
					if (d.producer->receipts.size() > d.validated.limits_.results)
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

} // namespace detent::authority
