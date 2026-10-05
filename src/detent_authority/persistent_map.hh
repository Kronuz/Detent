#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace detent::authority::detail {

// Immutable AVL paths. Captures share a root; updates replace O(log N) nodes.
// All ownership changes run on the application's owning executor.
template <class Key, class Value, class Compare = std::less<>> class PersistentMap {
	struct Node;
	using Ptr = std::shared_ptr<const Node>;
	struct Arena {
		Node *retired = nullptr;
		std::size_t live = 0, allocations = 0;
		void retire(Node *node) noexcept {
			node->retired_next = retired;
			retired = node;
		}
		std::size_t collect(std::size_t budget) noexcept {
			std::size_t count = 0;
			while (retired && count < budget) {
				auto node = retired;
				retired = node->retired_next;
				--live;
				++count;
				delete node;
			}
			return count;
		}
		~Arena() {
			while (retired) {
				collect(1024);
			}
		}
	};
	struct Node {
		Key key;
		Value value;
		Ptr left, right;
		int height;
		std::size_t count;
		Node *retired_next = nullptr;
		Node(Key k, Value v, Ptr l, Ptr r)
			: key(std::move(k)), value(std::move(v)), left(std::move(l)), right(std::move(r)),
			  height(1 + std::max(h(left), h(right))), count(1 + n(left) + n(right)) {}
	};
	std::shared_ptr<Arena> arena_ = std::make_shared<Arena>();
	Ptr root_;
	static int h(const Ptr &p) { return p ? p->height : 0; }
	static std::size_t n(const Ptr &p) { return p ? p->count : 0; }
	Ptr make(const Key &k, const Value &v, Ptr l, Ptr r) {
		auto node = new Node(k, v, std::move(l), std::move(r));
		std::weak_ptr<Arena> owner = arena_;
		++arena_->live;
		++arena_->allocations;
		Ptr result(node, [owner](const Node *pointer) noexcept {
			if (auto arena = owner.lock()) {
				arena->retire(const_cast<Node *>(pointer));
			} else {
				delete pointer;
			}
		});
		return result;
	}
	Ptr balance(const Key &k, const Value &v, Ptr l, Ptr r) {
		if (h(l) > h(r) + 1) {
			if (h(l->left) >= h(l->right)) {
				return make(l->key, l->value, l->left, make(k, v, l->right, r));
			}
			auto pivot = l->right;
			return make(pivot->key, pivot->value, make(l->key, l->value, l->left, pivot->left),
						make(k, v, pivot->right, r));
		}
		if (h(r) > h(l) + 1) {
			if (h(r->right) >= h(r->left)) {
				return make(r->key, r->value, make(k, v, l, r->left), r->right);
			}
			auto pivot = r->left;
			return make(pivot->key, pivot->value, make(k, v, l, pivot->left),
						make(r->key, r->value, pivot->right, r->right));
		}
		return make(k, v, std::move(l), std::move(r));
	}
	Ptr put(const Ptr &p, const Key &k, const Value &v) {
		if (!p) {
			return make(k, v, {}, {});
		}
		if (Compare{}(k, p->key)) {
			return balance(p->key, p->value, put(p->left, k, v), p->right);
		}
		if (Compare{}(p->key, k)) {
			return balance(p->key, p->value, p->left, put(p->right, k, v));
		}
		return make(k, v, p->left, p->right);
	}
	Ptr remove(const Ptr &p, const Key &k) {
		if (!p) {
			return {};
		}
		if (Compare{}(k, p->key)) {
			return balance(p->key, p->value, remove(p->left, k), p->right);
		}
		if (Compare{}(p->key, k)) {
			return balance(p->key, p->value, p->left, remove(p->right, k));
		}
		if (!p->left) {
			return p->right;
		}
		if (!p->right) {
			return p->left;
		}
		auto next = p->right;
		while (next->left) {
			next = next->left;
		}
		return balance(next->key, next->value, p->left, remove(p->right, next->key));
	}
	template <class F> static void visit(const Ptr &p, F &f) {
		if (!p) {
			return;
		}
		visit(p->left, f);
		f(p->key, p->value);
		visit(p->right, f);
	}

  public:
	class Cursor {
		Ptr root_;
		std::vector<Ptr> stack_;
		void left(Ptr p) {
			while (p) {
				stack_.push_back(p);
				p = p->left;
			}
		}

	  public:
		explicit Cursor(Ptr root) : root_(std::move(root)) { left(root_); }
		std::optional<std::pair<const Key *, const Value *>> next() {
			if (stack_.empty()) {
				return std::nullopt;
			}
			auto p = stack_.back();
			stack_.pop_back();
			left(p->right);
			return std::pair{&p->key, &p->value};
		}
	};
	Cursor cursor() const { return Cursor(root_); }
	void retire_root() noexcept { root_.reset(); }
	std::size_t collect(std::size_t budget) noexcept { return arena_->collect(budget); }
	bool reclamation_pending() const noexcept { return arena_->retired != nullptr; }
	std::size_t retained_nodes() const noexcept { return arena_->live; }
	std::size_t allocations() const noexcept { return arena_->allocations; }
	std::size_t size() const noexcept { return n(root_); }
	int height() const noexcept { return h(root_); }
	template <class K> const Value *find(const K &key) const {
		auto p = root_.get();
		while (p) {
			if (Compare{}(key, p->key)) {
				p = p->left.get();
			} else if (Compare{}(p->key, key)) {
				p = p->right.get();
			} else {
				return &p->value;
			}
		}
		return nullptr;
	}
	PersistentMap set(const Key &k, const Value &v) const {
		PersistentMap result = *this;
		result.root_ = result.put(root_, k, v);
		return result;
	}
	PersistentMap erase(const Key &k) const {
		PersistentMap result = *this;
		result.root_ = result.remove(root_, k);
		return result;
	}
	template <class F> void for_each(F &&f) const { visit(root_, f); }
};

} // namespace detent::authority::detail
