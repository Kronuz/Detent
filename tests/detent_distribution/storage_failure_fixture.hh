#pragma once
#include "detent_distribution/protocol.hh"
#include <array>
#include <cstdlib>
#include <limits>
#include <optional>

namespace storage_failure {
struct Allocation {
	void *pointer = nullptr;
	std::size_t bytes = 0;
};
inline std::array<Allocation, 128> live{};
inline std::size_t attempts = 0, live_bytes = 0, fail_at = std::numeric_limits<std::size_t>::max();
inline void *allocate(std::size_t bytes) {
	if (attempts++ == fail_at)
		return nullptr;
	auto pointer = std::malloc(bytes);
	if (!pointer)
		return nullptr;
	for (auto &allocation : live) {
		if (!allocation.pointer) {
			allocation = {pointer, bytes};
			live_bytes += bytes;
			return pointer;
		}
	}
	std::abort();
}
inline void release(void *pointer) {
	for (auto &allocation : live) {
		if (allocation.pointer == pointer) {
			live_bytes -= allocation.bytes;
			allocation = {};
			std::free(pointer);
			return;
		}
	}
	std::abort();
}
template <class MakeDecoder, class Check> void verify(std::string_view wire, MakeDecoder make, Check check) {
	namespace d = detent::distribution;
	using Decoder = decltype(make(d::Limits{}));
	using Value = decltype(std::declval<Decoder &>().finish());
	d::Limits limits;
	limits.storage_page_bytes = 256;
	limits.storage_allocate = allocate;
	limits.storage_release = release;
	auto feed = [&](auto &decoder) {
		auto input = std::span(wire.data(), wire.size());
		while (!input.empty()) {
			d::WorkBudget budget{1, 1};
			decoder.feed(input, budget);
		}
	};
	auto discard = [](auto &owner) {
		for (unsigned turns = 0; turns < 100000; ++turns) {
			d::WorkBudget budget{64, 1};
			if (owner.discard(budget))
				return;
		}
		throw std::runtime_error("storage cleanup did not progress");
	};
	std::size_t count = 0, arena_peak = 0, aggregate_peak = 0, retained_capacity = 0;
	{
		attempts = 0;
		std::optional<Value> value;
		{
			auto decoder = make(limits);
			feed(decoder);
			value.emplace(decoder.finish());
			count = attempts;
			arena_peak = value->peak_storage_capacity();
			aggregate_peak = value->storage_account()->peak();
			check(count > 4 && value->storage_account()->used() == live_bytes &&
				  value->storage_capacity() == arena_peak && aggregate_peak > arena_peak);
		}
		retained_capacity = live_bytes;
		check(value->storage_account()->used() == retained_capacity && retained_capacity > arena_peak);
		auto moved = std::move(*value);
		check(moved.storage_capacity() == arena_peak && value->storage_capacity() == 0);
		if constexpr (requires { value->cursor(); })
			check(value->cursor() == d::Cursor{});
		else
			check(value->base() == d::Cursor{} && value->target() == d::Cursor{});
		discard(moved);
		check(moved.storage_capacity() == 0 && moved.peak_storage_capacity() == arena_peak &&
			  moved.storage_account()->used() == live_bytes && live_bytes > 0);
	}
	check(live_bytes == 0);
	for (std::size_t failure = 0; failure < count; ++failure) {
		attempts = 0;
		fail_at = failure;
		std::optional<Decoder> decoder;
		bool rejected = false;
		try {
			decoder.emplace(make(limits));
			feed(*decoder);
		} catch (const std::bad_alloc &) {
			rejected = true;
		}
		check(rejected && live_bytes <= limits.storage_bytes);
		fail_at = std::numeric_limits<std::size_t>::max();
		if (decoder) {
			bool fenced = false;
			try {
				(void)decoder->finish();
			} catch (const std::invalid_argument &) {
				fenced = true;
			}
			check(fenced);
			discard(*decoder);
		}
		check(live_bytes == 0);
	}
	for (auto quota : {std::size_t(0), aggregate_peak - 1}) {
		limits.storage_bytes = quota;
		std::optional<Decoder> decoder;
		bool rejected = false;
		try {
			decoder.emplace(make(limits));
			feed(*decoder);
		} catch (const d::StoragePressure &) {
			rejected = true;
		}
		check(rejected && live_bytes <= quota);
		if (decoder)
			discard(*decoder);
		check(live_bytes == 0);
	}
	// An admitted base and a whole second decoding candidate must compete
	// for controls, scope rows and payload pages in the same physical account.
	limits.storage_bytes = aggregate_peak;
	limits.storage_account = std::make_shared<d::StorageLedger>(retained_capacity + aggregate_peak - 1);
	{
		std::optional<Value> first;
		{
			auto decoder = make(limits);
			feed(decoder);
			first.emplace(decoder.finish());
		}
		check(limits.storage_account->used() == retained_capacity && live_bytes == retained_capacity);
		std::optional<Decoder> second;
		bool pressured = false;
		try {
			second.emplace(make(limits));
			feed(*second);
		} catch (const d::StoragePressure &) {
			pressured = true;
		}
		check(pressured && limits.storage_account->used() == live_bytes);
		if (second)
			discard(*second);
		check(limits.storage_account->used() == retained_capacity && first->storage_capacity() == arena_peak);
		discard(*first);
		check(first->storage_capacity() == 0 && limits.storage_account->used() == live_bytes &&
			  live_bytes > 0);
		first.reset();
		check(limits.storage_account->used() == 0);
		auto retry = make(limits);
		feed(retry);
		auto accepted = retry.finish();
		check(accepted.storage_capacity() == arena_peak && limits.storage_account->used() == live_bytes);
	}
	check(live_bytes == 0 && limits.storage_account->used() == 0 &&
		  limits.storage_account->peak() <= limits.storage_account->limit());
}
} // namespace storage_failure
