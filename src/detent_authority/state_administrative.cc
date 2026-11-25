// Administrative commands: their codec, stepwise application and State::administer.
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
namespace {
bool carries_policy(AdministrativeKind kind) {
	return kind == AdministrativeKind::EnablePositions || kind == AdministrativeKind::SetLayout;
}
void validate_administrative(const AdministrativeCommand &c, const Limits &l) {
	require(c.context.cluster != Identity{} && c.context.group != Identity{} && c.context.epoch != Identity{},
			"missing authority identity");
	if (c.kind == AdministrativeKind::EnablePositions)
		require(c.nonce != Identity{} && c.name_space.empty() && c.incarnation == Identity{},
				"invalid enabling entry");
	else if (c.kind == AdministrativeKind::SetLayout)
		require(c.nonce == Identity{} && c.name_space.empty() && c.incarnation == Identity{},
				"invalid layout entry");
	else if (c.kind == AdministrativeKind::Seal)
		require(c.nonce == Identity{} && c.name_space.empty() && c.incarnation == Identity{} &&
					c.policy == LayoutPolicy{},
				"invalid seal entry");
	else
		require(c.kind == AdministrativeKind::DropNamespace && c.nonce == Identity{} &&
					c.incarnation != Identity{} && !c.name_space.empty() &&
					c.name_space.size() < 200 && is_valid_knob_name(c.name_space + ":x") &&
					c.policy == LayoutPolicy{},
				"invalid namespace drop");
	validate(c.policy);
	require(!carries_policy(c.kind) || history::file_bytes(l, c.policy) <= history::fetch_bytes(l),
			"layout policy writes history files above a peer fetch");
}
} // namespace
bool is_administrative(std::string_view payload) noexcept { return payload.starts_with("DTAADM01"); }
std::string encode(const AdministrativeCommand &c, Limits l) {
	validate_administrative(c, l);
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
	const auto recorded = read_limits(r, l);
	AdministrativeCommand c;
	c.context = decode_context(r);
	auto kind = r.number();
	require(kind >= 1 && kind <= 4, "unknown administrative kind");
	c.kind = static_cast<AdministrativeKind>(kind);
	c.nonce = r.identity();
	c.name_space = r.text(199);
	c.incarnation = r.identity();
	if (carries_policy(c.kind))
		c.policy = read_policy(r);
	r.end();
	validate_administrative(c, recorded);
	return c;
}
AdministrativeApplication::AdministrativeApplication(const State &source, std::uint64_t index,
													 std::uint64_t term, AdministrativeCommand command, Limits profile)
	: source_(source), candidate_(source), index_(index), term_(term), command_(std::move(command)),
	  profile_(profile), result_{Status::Applied, source.revision_, 0, 0, 0} {
	admit_limits(profile_, source.limits_);
	validate_administrative(command_, profile_);
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
			else if (history::file_bytes(profile_, command_.policy, source_.policy_) >
					 history::fetch_bytes(profile_))
				result_.status = Status::Conflict; // Files written across the change would outgrow a fetch.
			else
				candidate_.pending_ = command_.policy;
			return complete();
		} else if (command_.kind == AdministrativeKind::Seal) {
			if (!source_.positions_enabled())
				result_.status = Status::Conflict;
			else if (!source_.seal_.transactions)
				result_.status = Status::NoChange; // A seal applied first, or nothing changed.
			else
				candidate_.seal_.forced = true;
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

} // namespace detent::authority
