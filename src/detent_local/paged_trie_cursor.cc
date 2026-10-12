#include "paged_trie_cursor.hh"
#include <algorithm>
#include <cstring>
#include <vector>

namespace detent::local::detail {
namespace p = paged;
namespace {
void require(bool value) {
	if (!value)
		throw std::invalid_argument("invalid canonical trie input");
}
} // namespace
struct TrieCursor::Impl {
	enum class Phase { Initial, Lookahead, Leaf, Close, Branch, Advance, Done };
	struct Frame {
		int depth;
		std::uint64_t keys = 0;
		std::vector<p::Child, distribution::StorageAllocator<p::Child>> children;
		Frame(int value, std::shared_ptr<distribution::StorageLedger> account)
			: depth(value), children(distribution::StorageAllocator<p::Child>(std::move(account))) {
			children.reserve(256);
		}
	};
	std::shared_ptr<distribution::StorageLedger> account;
	std::shared_ptr<const void> source;
	TrieKeyReader reader;
	TrieSourceReady prepare;
	std::uint64_t count, ordinal = 0;
	std::vector<Frame, distribution::StorageAllocator<Frame>> frames;
	std::array<char, 202> previous{}, next{};
	std::size_t previous_size = 0, next_size = 0, charge = 0;
	int common = -1;
	p::Reference root;
	Phase phase = Phase::Initial;
	bool failed = false, discarding = false;
	Impl(std::shared_ptr<distribution::StorageLedger> ledger, std::shared_ptr<const void> owner,
		 TrieKeyReader callback, std::uint64_t keys, p::Limits limits, TrieSourceReady ready)
		: account(std::move(ledger)), source(std::move(owner)), reader(callback), prepare(ready), count(keys),
		  frames(distribution::StorageAllocator<Frame>(account)) {
		require(source && reader && count <= std::uint64_t(limits.records) + limits.namespaces);
		frames.reserve(std::min<std::uint64_t>(202, count ? count - 1 : 0));
	}
	bool admit(std::size_t amount, ValidationBudget &budget) {
		if (!budget.rows)
			return false;
		if (!charge)
			charge = amount;
		const auto used = std::min(charge, budget.bytes);
		charge -= used;
		budget.bytes -= used;
		if (charge)
			return false;
		--budget.rows;
		return true;
	}
	std::size_t read(std::uint64_t index, std::span<char> output) const {
		const auto size = reader(source.get(), index, output);
		require(size && size <= output.size());
		p::decode_key({output.data(), size});
		return size;
	}
	int top() const { return frames.empty() ? -1 : frames.back().depth; }
	void push(int depth) {
		require(depth > top() && depth < 202 && frames.size() < frames.capacity());
		frames.emplace_back(depth, account);
	}
	void append(const p::Reference &reference, std::uint64_t keys) {
		auto &frame = frames.back();
		const auto label = static_cast<unsigned char>(previous[frame.depth]);
		require(frame.children.size() < 256 &&
				(frame.children.empty() || frame.children.back().label < label));
		frame.children.push_back({label, reference});
		require(keys <= count - frame.keys);
		frame.keys += keys;
	}
};
TrieCursor::TrieCursor(std::shared_ptr<distribution::StorageLedger> account,
					   std::shared_ptr<const void> source, TrieKeyReader reader, std::uint64_t count,
					   p::Limits limits, TrieSourceReady ready)
	: impl_(distribution::make_storage_owner<Impl>(account, std::malloc, std::free, account,
												   std::move(source), reader, count, limits, ready)) {}
TrieCursor::~TrieCursor() = default;
TrieCursor::TrieCursor(TrieCursor &&) noexcept = default;
void TrieCursor::step(ValidationBudget &budget) {
	if (!impl_ || impl_->failed || impl_->discarding)
		throw std::logic_error("inactive canonical trie cursor");
	auto &state = *impl_;
	try {
		for (;;) {
			switch (state.phase) {
			case Impl::Phase::Initial:
				if (!state.count) {
					state.phase = Impl::Phase::Done;
					return;
				}
				if (state.prepare && !state.prepare(state.source.get(), 0, budget))
					return;
				if (!state.admit(4 * state.previous.size(), budget))
					return;
				state.previous_size = state.read(0, state.previous);
				state.phase = Impl::Phase::Lookahead;
				break;
			case Impl::Phase::Lookahead:
				if (state.ordinal + 1 == state.count) {
					state.common = -1;
				} else {
					if (state.prepare && !state.prepare(state.source.get(), state.ordinal + 1, budget))
						return;
					if (!state.admit(4 * state.next.size(), budget))
						return;
					state.next_size = state.read(state.ordinal + 1, state.next);
					const auto length = std::min(state.previous_size, state.next_size);
					require(std::memcmp(state.previous.data(), state.next.data(), length) < 0);
					std::size_t prefix = 0;
					while (prefix < length && state.previous[prefix] == state.next[prefix])
						++prefix;
					require(prefix < length);
					state.common = prefix;
				}
				if (state.common > state.top())
					state.push(state.common);
				state.phase = Impl::Phase::Leaf;
				return;
			case Impl::Phase::Close:
				if (state.top() > state.common) {
					state.phase = Impl::Phase::Branch;
					return;
				}
				state.phase = Impl::Phase::Advance;
				break;
			case Impl::Phase::Advance:
				if (!state.admit(state.next_size, budget))
					return;
				std::copy_n(state.next.begin(), state.next_size, state.previous.begin());
				state.previous_size = state.next_size;
				++state.ordinal;
				state.phase = Impl::Phase::Lookahead;
				break;
			case Impl::Phase::Leaf:
			case Impl::Phase::Branch:
			case Impl::Phase::Done:
				return;
			}
		}
	} catch (...) {
		state.failed = true;
		throw;
	}
}
bool TrieCursor::ready() const noexcept {
	return impl_ && !impl_->failed && !impl_->discarding &&
		   (impl_->phase == Impl::Phase::Leaf || impl_->phase == Impl::Phase::Branch);
}
TrieEvent TrieCursor::event() const {
	if (!ready())
		throw std::logic_error("canonical trie event unavailable");
	const auto &state = *impl_;
	const std::string_view key(state.previous.data(), state.previous_size);
	if (state.phase == Impl::Phase::Leaf) {
		return {p::decode_key(key).kind, state.ordinal, 1, key, key.substr(state.top() + 1), {}};
	}
	const auto &frame = state.frames.back();
	const auto below = state.frames.size() == 1 ? -1 : state.frames[state.frames.size() - 2].depth;
	const auto parent = std::max(state.common, below);
	return {p::PageKind::Branch,
			0,
			frame.keys,
			key.substr(0, frame.depth),
			key.substr(parent + 1, frame.depth - parent - 1),
			frame.children};
}
bool TrieCursor::commit(const p::Reference &reference, ValidationBudget &budget) {
	if (!ready())
		throw std::logic_error("canonical trie commit unavailable");
	auto &state = *impl_;
	try {
		if (!state.admit(p::child_bytes, budget))
			return false;
		require(std::any_of(reference.logical.begin(), reference.logical.end(),
							[](char value) { return value != 0; }));
		const auto keys = state.phase == Impl::Phase::Branch ? state.frames.back().keys : 1;
		if (state.phase == Impl::Phase::Branch) {
			require(state.frames.back().children.size() >= 2);
			state.frames.pop_back();
			if (state.common > state.top())
				state.push(state.common);
		}
		if (state.frames.empty()) {
			require(keys == state.count);
			state.root = reference;
			state.phase = Impl::Phase::Done;
		} else {
			state.append(reference, keys);
			state.phase = Impl::Phase::Close;
		}
		return true;
	} catch (...) {
		state.failed = true;
		throw;
	}
}
bool TrieCursor::done() const noexcept {
	return impl_ && !impl_->failed && !impl_->discarding && impl_->phase == Impl::Phase::Done;
}
p::Reference TrieCursor::root() const {
	if (!done())
		throw std::logic_error("canonical trie incomplete");
	return impl_->root;
}
bool TrieCursor::discard(ValidationBudget &budget) {
	if (!impl_)
		return true;
	if (!budget.rows)
		return false;
	impl_->discarding = true;
	while (!impl_->frames.empty() && budget.rows) {
		--budget.rows;
		impl_->frames.pop_back();
	}
	if (!budget.rows)
		return false;
	--budget.rows;
	impl_.reset();
	return true;
}
} // namespace detent::local::detail
