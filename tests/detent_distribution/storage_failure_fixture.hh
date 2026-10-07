#pragma once
#include "detent_distribution/protocol.hh"
#include <array>
#include <cstdlib>
#include <limits>

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
	std::size_t count = 0, peak = 0;
	{
		attempts = 0;
		auto decoder = make(limits);
		feed(decoder);
		auto value = decoder.finish();
		count = attempts;
		peak = value.peak_storage_capacity();
		check(count > 4 && live_bytes == value.storage_capacity() && peak == live_bytes);
		auto moved = std::move(value);
		check(moved.storage_capacity() == live_bytes && value.storage_capacity() == 0);
		if constexpr (requires { value.cursor(); })
			check(value.cursor() == d::Cursor{});
		else
			check(value.base() == d::Cursor{} && value.target() == d::Cursor{});
		for (;;) {
			auto before = live_bytes;
			d::WorkBudget budget{64, 1};
			bool done = moved.discard(budget);
			check(live_bytes <= before && moved.storage_capacity() == live_bytes);
			if (done)
				break;
		}
		check(live_bytes == 0 && moved.peak_storage_capacity() == peak);
	}
	for (std::size_t failure = 0; failure < count; ++failure) {
		attempts = 0;
		fail_at = failure;
		auto decoder = make(limits);
		bool rejected = false;
		try {
			feed(decoder);
		} catch (const std::bad_alloc &) {
			rejected = true;
		}
		check(rejected && live_bytes <= limits.storage_bytes);
		fail_at = std::numeric_limits<std::size_t>::max();
		bool fenced = false;
		try {
			(void)decoder.finish();
		} catch (const std::invalid_argument &) {
			fenced = true;
		}
		check(fenced);
		for (;;) {
			d::WorkBudget budget{64, 1};
			if (decoder.discard(budget))
				break;
		}
		check(live_bytes == 0);
	}
	for (auto quota : {std::size_t(0), peak - 1}) {
		limits.storage_bytes = quota;
		auto decoder = make(limits);
		bool rejected = false;
		try {
			feed(decoder);
		} catch (const std::length_error &) {
			rejected = true;
		}
		check(rejected && live_bytes <= quota);
		for (;;) {
			d::WorkBudget budget{64, 1};
			if (decoder.discard(budget))
				break;
		}
		check(live_bytes == 0);
	}
}
} // namespace storage_failure
