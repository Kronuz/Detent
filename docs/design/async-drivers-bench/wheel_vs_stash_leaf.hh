// Candidate (c) of docs/design/wheel-evidence.md: stash improved for a loop owner. stash's levels
// (StashSlots) are unchanged; this leaf replaces StashValues. A slot holds the entry pointer itself
// (no value box), and the entry keeps a back-pointer to its slot, so the owner cancels in O(1) by
// emptying the slot, and a walk never meets a cancelled entry. The claim, bound and fill protocol
// is StashValues's (stash.h:852-1089, stash:d249352) without the box.
//
// Contract: any thread may add an unarmed entry; only the walking thread cancels, and only an
// entry whose add happened before the cancel. The entries belong to their owners, never to stash.
#pragma once

#include "stash.h"

#include <atomic>
#include <cstddef>

// Entry needs a member `std::atomic<Entry*>* slot`, null while unfiled.
template <typename Entry, std::size_t _Size>
class StashEntries : public Stash<Entry, _Size, true> {
	using Stash_T = Stash<Entry, _Size, true>;
	using Cursor = typename Stash_T::Cursor;
	using Data = typename Stash_T::Data;

	std::size_t walk_cur = 0;      // the first slot not yet known drained (parks on a reserved hole)
	std::atomic_size_t atom_end{0};  // the producers' reserve frontier
	Cursor walk_cursor;
	std::atomic<Data*> atom_claim_hint{nullptr};

public:
	StashEntries() = default;

	// Stash's chunks delete what their slots hold: detach every entry still filed first.
	~StashEntries() {
		const auto end = atom_end.load(std::memory_order_acquire);
		Cursor cursor;
		for (std::size_t i = 0; i < end; ++i) {
			std::atomic<Entry*>* slot = nullptr;
			if (Stash_T::get(&slot, i, false, cursor) != StashState::Ok) { continue; }
			Entry* e = slot->exchange(nullptr, std::memory_order_acquire);
			if (e && e != stash_reserved<Entry>()) { e->slot = nullptr; }
		}
	}

	// Empties the entry's slot: the walk skips it and clean frees it with its leaf.
	static void cancel(Entry& e) noexcept {
		if (e.slot) {
			e.slot->store(nullptr, std::memory_order_relaxed);
			e.slot = nullptr;
		}
	}

	template <typename T>
	bool next(StashContext& ctx, T* value_ptr, unsigned long long) noexcept {
		if (ctx.op != StashContext::Operation::walk) { return false; }  // clean frees whole leaves
		const auto sentinel = stash_reserved<Entry>();
		const auto end = atom_end.fetch_add(0, std::memory_order_acquire);  // RMW: never coherence-stale
		std::size_t cur = walk_cur;
		bool parked = false;
		while (cur < end) {
			std::atomic<Entry*>* slot = nullptr;
			if (Stash_T::get(&slot, cur, false, walk_cursor) != StashState::Ok) {
				parked = true;  // reserved, its chunk not yet published
				break;
			}
			Entry* e = slot->load(std::memory_order_acquire);
			if (e == sentinel) {  // reserved, not yet filled: park on the first one
				parked = true;
				++cur;
				continue;
			}
			if (!parked) { walk_cur = cur + 1; }
			if (!e) {  // drained or cancelled
				++cur;
				continue;
			}
			e = slot->exchange(nullptr, std::memory_order_acquire);
			if (!e) {
				++cur;
				continue;
			}
			e->slot = nullptr;
			*value_ptr = e;
			return true;
		}
		if (parked || walk_cur < atom_end.fetch_add(0, std::memory_order_acquire)) {
			if (ctx.begin_key < ctx.pending_floor) { ctx.pending_floor = ctx.begin_key; }
		}
		return false;
	}

	void put(StashContext& ctx, unsigned long long key, Entry* e) {
		const auto slot_index = atom_end.fetch_add(1, std::memory_order_relaxed);
		std::atomic<Entry*>* slot = nullptr;
		const bool hinted = slot_index >= _Size;
		Cursor claim;
		if (hinted) {
			if (Data* hint = atom_claim_hint.load(std::memory_order_acquire)) {
				claim.node = hint;
				claim.base = hint->index() * _Size;
			}
		}
		try {
			if (hinted) {
				Stash_T::get(&slot, slot_index, true, claim);
			} else {
				Stash_T::get(&slot, slot_index, true);
			}
		} catch (...) {
			stash_out_of_memory_after_claim();
		}
		e->slot = slot;
		auto last_valid_key = ctx.atom_last_valid_key.load(std::memory_order_relaxed);
		while (key > last_valid_key &&
		       !ctx.atom_last_valid_key.compare_exchange_weak(last_valid_key, key,
		           std::memory_order_release, std::memory_order_relaxed)) {}
		Entry* expected = stash_reserved<Entry>();
		slot->compare_exchange_strong(expected, e, std::memory_order_release, std::memory_order_relaxed);
		if (hinted && slot_index % _Size == 0) { atom_claim_hint.store(claim.node, std::memory_order_release); }
	}
};
