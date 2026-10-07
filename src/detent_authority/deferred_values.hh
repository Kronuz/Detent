#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace detent::authority::detail {
// Immutable shared values become inaccessible to weak_ptr::lock before they
// enter this queue. Final releases may occur on another thread; allocation,
// metadata changes and collection belong to one owner. No allocation occurs
// in the deleter. Synchronous destruction is the fallback outside that owner
// runtime, whose shutdown/discard paths must explicitly drain the queue.
// Value is a default-constructible, non-final class and must not retain other
// values from this arena. Metadata access accepts only values created here.
template <class Value> class DeferredValues : public std::enable_shared_from_this<DeferredValues<Value>> {
	struct Node : Value {
		explicit Node(std::size_t bytes) : encoded_bytes(bytes) {}
		Node *next = nullptr;
		std::size_t encoded_bytes;
	};
	std::atomic<Node *> incoming_{nullptr};
	Node *owned_ = nullptr;
	std::atomic<std::uint64_t> count_{0}, bytes_{0};
	void retire(Node *node) noexcept {
		count_.fetch_add(1, std::memory_order_relaxed);
		bytes_.fetch_add(node->encoded_bytes, std::memory_order_relaxed);
		auto head = incoming_.load(std::memory_order_relaxed);
		do {
			node->next = head;
		} while (!incoming_.compare_exchange_weak(head, node, std::memory_order_release,
												  std::memory_order_relaxed));
	}
	void receive() noexcept {
		if (!owned_)
			owned_ = incoming_.exchange(nullptr, std::memory_order_acquire);
	}

  public:
	DeferredValues() = default;
	DeferredValues(const DeferredValues &) = delete;
	DeferredValues &operator=(const DeferredValues &) = delete;
	~DeferredValues() {
		for (;;) {
			receive();
			if (!owned_)
				break;
			auto node = owned_;
			owned_ = node->next;
			delete node;
		}
	}
	std::shared_ptr<Value> create(std::size_t encoded_bytes = 0) {
		auto owner = this->shared_from_this();
		auto node = new Node(encoded_bytes);
		// If control-block allocation throws, shared_ptr invokes this same
		// deleter. The empty value still retains original disposal ownership.
		return {node, [owner = std::move(owner)](Value *value) noexcept {
					owner->retire(static_cast<Node *>(value));
				}};
	}
	static std::size_t encoded_size(const Value &value) noexcept {
		return static_cast<const Node &>(value).encoded_bytes;
	}
	static void set_encoded_size(Value &value, std::size_t bytes) noexcept {
		static_cast<Node &>(value).encoded_bytes = bytes;
	}
	// dispose performs a bounded step and returns true only when ordinary
	// destruction of the remaining value is bounded. Partial progress keeps
	// the same original allocation charged until final deletion.
	template <class Dispose> std::size_t collect_one(Dispose &&dispose) {
		receive();
		if (!owned_ || !std::forward<Dispose>(dispose)(static_cast<Value &>(*owned_)))
			return 0;
		auto node = owned_;
		owned_ = node->next;
		const auto bytes = node->encoded_bytes;
		delete node;
		bytes_.fetch_sub(bytes, std::memory_order_relaxed);
		count_.fetch_sub(1, std::memory_order_relaxed);
		return 1;
	}
	bool pending() const noexcept { return owned_ || incoming_.load(std::memory_order_acquire); }
	std::uint64_t count() const noexcept { return count_.load(std::memory_order_relaxed); }
	std::uint64_t encoded_bytes() const noexcept { return bytes_.load(std::memory_order_relaxed); }
};
} // namespace detent::authority::detail
