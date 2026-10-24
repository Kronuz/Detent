#pragma once

// A minimal sorted-vector map. Entries live in one contiguous
// std::vector<std::pair<Key, T>>, sorted by Compare with unique keys.
//
// Differences from std::map that callers must know:
//   * Any insert or erase invalidates every iterator, pointer and reference
//     into the map (std::map invalidates only the erased element). Reserve
//     before taking references that must survive an insert, or re-find.
//   * value_type is std::pair<Key, T>, not pair<const Key, T>. Callers must
//     never modify a key through an iterator; doing so breaks the ordering.
//   * Single insertion and erase in the middle cost O(n) moves. Appending a
//     key greater than every present key is amortized O(1). Build large maps
//     from arbitrary-order input with flat_map_builder (sorts once), update
//     many entries with flat_map_merge (one linear pass), and remove many with
//     erase_if (one linear pass); erasing in a loop is O(n^2).
//   * Moves of value_type never throw (static_assert below), so shifting and
//     erase never allocate or throw, and vector insertion keeps the strong
//     guarantee: a refused allocation leaves the map unchanged. Copy
//     assignment copies first and then swaps, so a failed copy also leaves
//     the target unchanged; an element-wise vector assignment would leave a
//     source prefix in front of the old tail, out of order.
//   * erase_if's predicate must not throw; erase_if is noexcept, so a
//     throwing predicate terminates instead of leaving shifted keys unsorted.
//
// This is std::flat_map's shape reduced to what Detent uses; <flat_map> is
// not available in every supported standard library.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace detent {

template <class Key, class T, class Compare>
class flat_map_builder;

template <class Key, class T, class Compare = std::less<>>
class flat_map {
public:
	using key_type = Key;
	using mapped_type = T;
	using value_type = std::pair<Key, T>;
	using key_compare = Compare;
	using container_type = std::vector<value_type>;
	using size_type = typename container_type::size_type;
	using difference_type = typename container_type::difference_type;
	using reference = value_type&;
	using const_reference = const value_type&;
	using iterator = typename container_type::iterator;
	using const_iterator = typename container_type::const_iterator;
	using reverse_iterator = typename container_type::reverse_iterator;
	using const_reverse_iterator = typename container_type::const_reverse_iterator;

	// Heterogeneous lookup is enabled only for transparent comparators.
	template <class K>
	static constexpr bool is_transparent = requires { typename Compare::is_transparent; };

	static_assert(std::is_nothrow_move_constructible_v<value_type> && std::is_nothrow_move_assignable_v<value_type>,
	              "flat_map needs nothrow moves so shifting and erase cannot fail");

	flat_map() = default;
	explicit flat_map(const Compare& comp) : comp_(comp) {}
	flat_map(const flat_map&) = default;
	flat_map(flat_map&&) noexcept = default;
	flat_map& operator=(flat_map&&) noexcept = default;
	flat_map& operator=(const flat_map& other) {
		if (this != &other) {
			flat_map copy(other);
			swap(copy);
		}
		return *this;
	}

	[[nodiscard]] iterator begin() noexcept { return v_.begin(); }
	[[nodiscard]] const_iterator begin() const noexcept { return v_.begin(); }
	[[nodiscard]] const_iterator cbegin() const noexcept { return v_.cbegin(); }
	[[nodiscard]] iterator end() noexcept { return v_.end(); }
	[[nodiscard]] const_iterator end() const noexcept { return v_.end(); }
	[[nodiscard]] const_iterator cend() const noexcept { return v_.cend(); }
	[[nodiscard]] reverse_iterator rbegin() noexcept { return v_.rbegin(); }
	[[nodiscard]] const_reverse_iterator rbegin() const noexcept { return v_.rbegin(); }
	[[nodiscard]] reverse_iterator rend() noexcept { return v_.rend(); }
	[[nodiscard]] const_reverse_iterator rend() const noexcept { return v_.rend(); }

	[[nodiscard]] bool empty() const noexcept { return v_.empty(); }
	[[nodiscard]] size_type size() const noexcept { return v_.size(); }
	[[nodiscard]] size_type capacity() const noexcept { return v_.capacity(); }
	[[nodiscard]] size_type max_size() const noexcept { return v_.max_size(); }
	void clear() noexcept { v_.clear(); }
	void reserve(size_type n) { v_.reserve(n); }
	void shrink_to_fit() { v_.shrink_to_fit(); }
	[[nodiscard]] key_compare key_comp() const { return comp_; }

	// --- lookup -----------------------------------------------------------

	[[nodiscard]] iterator lower_bound(const key_type& k) { return lower_bound_impl(k); }
	[[nodiscard]] const_iterator lower_bound(const key_type& k) const { return lower_bound_impl(k); }
	template <class K>
		requires is_transparent<K>
	[[nodiscard]] iterator lower_bound(const K& k) {
		return lower_bound_impl(k);
	}
	template <class K>
		requires is_transparent<K>
	[[nodiscard]] const_iterator lower_bound(const K& k) const {
		return lower_bound_impl(k);
	}

	[[nodiscard]] iterator find(const key_type& k) { return find_impl(*this, k); }
	[[nodiscard]] const_iterator find(const key_type& k) const { return find_impl(*this, k); }
	template <class K>
		requires is_transparent<K>
	[[nodiscard]] iterator find(const K& k) {
		return find_impl(*this, k);
	}
	template <class K>
		requires is_transparent<K>
	[[nodiscard]] const_iterator find(const K& k) const {
		return find_impl(*this, k);
	}

	[[nodiscard]] size_type count(const key_type& k) const { return find(k) != end() ? 1 : 0; }
	template <class K>
		requires is_transparent<K>
	[[nodiscard]] size_type count(const K& k) const {
		return find(k) != end() ? 1 : 0;
	}
	[[nodiscard]] bool contains(const key_type& k) const { return find(k) != end(); }
	template <class K>
		requires is_transparent<K>
	[[nodiscard]] bool contains(const K& k) const {
		return find(k) != end();
	}

	[[nodiscard]] mapped_type& at(const key_type& k) { return at_impl(*this, k); }
	[[nodiscard]] const mapped_type& at(const key_type& k) const { return at_impl(*this, k); }
	template <class K>
		requires is_transparent<K>
	[[nodiscard]] mapped_type& at(const K& k) {
		return at_impl(*this, k);
	}
	template <class K>
		requires is_transparent<K>
	[[nodiscard]] const mapped_type& at(const K& k) const {
		return at_impl(*this, k);
	}

	// --- insertion --------------------------------------------------------

	mapped_type& operator[](const key_type& k) { return try_emplace(k).first->second; }
	mapped_type& operator[](key_type&& k) { return try_emplace(std::move(k)).first->second; }

	template <class... Args>
	std::pair<iterator, bool> try_emplace(const key_type& k, Args&&... args) {
		return try_emplace_impl(k, std::forward<Args>(args)...);
	}
	template <class... Args>
	std::pair<iterator, bool> try_emplace(key_type&& k, Args&&... args) {
		return try_emplace_impl(std::move(k), std::forward<Args>(args)...);
	}

	std::pair<iterator, bool> insert(value_type&& v) { return emplace_value(std::move(v)); }
	std::pair<iterator, bool> insert(const value_type& v) { return emplace_value(value_type(v)); }

	template <class... Args>
	std::pair<iterator, bool> emplace(Args&&... args) {
		return emplace_value(value_type(std::forward<Args>(args)...));
	}

	template <class M>
	std::pair<iterator, bool> insert_or_assign(const key_type& k, M&& obj) {
		return insert_or_assign_impl(k, std::forward<M>(obj));
	}
	template <class M>
	std::pair<iterator, bool> insert_or_assign(key_type&& k, M&& obj) {
		return insert_or_assign_impl(std::move(k), std::forward<M>(obj));
	}

	// --- erase ------------------------------------------------------------

	iterator erase(const_iterator pos) noexcept { return v_.erase(pos); }
	iterator erase(iterator pos) noexcept { return v_.erase(const_iterator(pos)); }
	iterator erase(const_iterator first, const_iterator last) noexcept { return v_.erase(first, last); }
	size_type erase(const key_type& k) { return erase_key(k); }
	template <class K>
		requires(is_transparent<K> && !std::is_convertible_v<K, iterator> && !std::is_convertible_v<K, const_iterator>)
	size_type erase(const K& k) {
		return erase_key(k);
	}

	void swap(flat_map& other) noexcept(std::is_nothrow_swappable_v<Compare>) {
		using std::swap;
		swap(comp_, other.comp_);
		v_.swap(other.v_);
	}
	friend void swap(flat_map& a, flat_map& b) noexcept(noexcept(a.swap(b))) { a.swap(b); }

	[[nodiscard]] friend bool operator==(const flat_map& a, const flat_map& b) { return a.v_ == b.v_; }

	template <class Pred>
	friend size_type erase_if(flat_map& map, Pred pred) noexcept {
		const auto first = std::remove_if(map.v_.begin(), map.v_.end(), pred);
		const auto removed = static_cast<size_type>(map.v_.end() - first);
		map.v_.erase(first, map.v_.end());
		return removed;
	}

private:
	friend class flat_map_builder<Key, T, Compare>;

	template <class K>
	[[nodiscard]] auto lower_bound_impl(const K& k) {
		return std::lower_bound(v_.begin(), v_.end(), k,
		                        [this](const value_type& e, const K& key) { return comp_(e.first, key); });
	}
	template <class K>
	[[nodiscard]] auto lower_bound_impl(const K& k) const {
		return std::lower_bound(v_.begin(), v_.end(), k,
		                        [this](const value_type& e, const K& key) { return comp_(e.first, key); });
	}
	template <class Self, class K>
	static auto find_impl(Self& self, const K& k) {
		auto it = self.lower_bound_impl(k);
		if (it != self.v_.end() && !self.comp_(k, it->first))
			return it;
		return self.v_.end();
	}
	template <class Self, class K>
	static auto& at_impl(Self& self, const K& k) {
		auto it = self.find_impl(self, k);
		if (it == self.v_.end())
			throw std::out_of_range("flat_map::at");
		return it->second;
	}
	[[nodiscard]] bool greater_than_all(const key_type& k) const { return v_.empty() || comp_(v_.back().first, k); }

	template <class K, class... Args>
	std::pair<iterator, bool> try_emplace_impl(K&& k, Args&&... args) {
		if (greater_than_all(k)) {
			v_.emplace_back(std::piecewise_construct, std::forward_as_tuple(std::forward<K>(k)),
			                std::forward_as_tuple(std::forward<Args>(args)...));
			return {std::prev(v_.end()), true};
		}
		auto it = lower_bound_impl(k);
		if (it != v_.end() && !comp_(k, it->first))
			return {it, false};
		it = v_.emplace(it, std::piecewise_construct, std::forward_as_tuple(std::forward<K>(k)),
		                std::forward_as_tuple(std::forward<Args>(args)...));
		return {it, true};
	}

	std::pair<iterator, bool> emplace_value(value_type&& v) {
		if (greater_than_all(v.first)) {
			v_.push_back(std::move(v));
			return {std::prev(v_.end()), true};
		}
		auto it = lower_bound_impl(v.first);
		if (it != v_.end() && !comp_(v.first, it->first))
			return {it, false};
		it = v_.insert(it, std::move(v));
		return {it, true};
	}

	template <class K, class M>
	std::pair<iterator, bool> insert_or_assign_impl(K&& k, M&& obj) {
		if (greater_than_all(k)) {
			v_.emplace_back(std::forward<K>(k), std::forward<M>(obj));
			return {std::prev(v_.end()), true};
		}
		auto it = lower_bound_impl(k);
		if (it != v_.end() && !comp_(k, it->first)) {
			it->second = std::forward<M>(obj);
			return {it, false};
		}
		it = v_.emplace(it, std::forward<K>(k), std::forward<M>(obj));
		return {it, true};
	}

	template <class K>
	size_type erase_key(const K& k) {
		auto it = find_impl(*this, k);
		if (it == v_.end())
			return 0;
		v_.erase(it);
		return 1;
	}

	container_type v_;
	[[no_unique_address]] Compare comp_{};
};

// Bulk construction from arbitrary-order input. Entries are appended while
// tracking whether the keys stay strictly increasing (the canonical-writer
// case: no duplicate is possible and nothing needs sorting). finish()
// reports the first duplicate in INPUT order, or sorts once. Inserting n
// arbitrary-order entries one by one is O(n^2); this is O(n log n).
//
// The builder fills a map the caller owns and keeps a reference to it. After
// push() the map is in input order and must not be read until finish()
// returns nullopt. When finish() returns an index, the map still holds the
// entries in input order (entry(i) names the duplicate) and must be
// discarded.
template <class Key, class T, class Compare = std::less<>>
class flat_map_builder {
public:
	using map_type = flat_map<Key, T, Compare>;

	explicit flat_map_builder(map_type& map) : map_(map) {}

	// Grows geometrically, so repeated calls with slowly rising totals stay linear.
	void reserve(std::size_t n) {
		auto& v = map_.v_;
		if (n > v.capacity())
			v.reserve(std::max(n, v.capacity() * 2));
	}

	template <class K, class V>
	void push(K&& key, V&& value) {
		auto& v = map_.v_;
		if (sorted_ && !v.empty() && !map_.comp_(v.back().first, key))
			sorted_ = false;
		v.emplace_back(std::forward<K>(key), std::forward<V>(value));
	}

	[[nodiscard]] std::size_t size() const noexcept { return map_.v_.size(); }
	[[nodiscard]] const typename map_type::value_type& entry(std::size_t input_index) const {
		return map_.v_[input_index];
	}

	// nullopt: the map is sorted and unique. Otherwise the input index of the
	// first entry (in input order) whose key repeats an earlier entry.
	[[nodiscard]] std::optional<std::size_t> finish() {
		if (sorted_)
			return std::nullopt;
		auto& v = map_.v_;
		if (v.size() > std::numeric_limits<std::uint32_t>::max())
			throw std::length_error("flat_map_builder: too many entries");
		std::vector<std::uint32_t> order(v.size());
		for (std::size_t i = 0; i < order.size(); ++i)
			order[i] = static_cast<std::uint32_t>(i);
		const auto& comp = map_.comp_;
		// Key order, input order within equal keys: the second element of
		// each equal group is that group's first repeat in input order.
		std::sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b) {
			if (comp(v[a].first, v[b].first))
				return true;
			if (comp(v[b].first, v[a].first))
				return false;
			return a < b;
		});
		std::optional<std::size_t> first_repeat;
		for (std::size_t i = 1; i < order.size(); ++i) {
			if (!comp(v[order[i - 1]].first, v[order[i]].first)) {
				const std::size_t repeat = order[i];
				if (!first_repeat || repeat < *first_repeat)
					first_repeat = repeat;
			}
		}
		if (first_repeat)
			return first_repeat;
		// Apply the permutation in place by following cycles (nothrow moves).
		for (std::size_t i = 0; i < order.size(); ++i) {
			if (order[i] == i)
				continue;
			auto tmp = std::move(v[i]);
			std::size_t j = i;
			for (;;) {
				const std::size_t src = order[j];
				order[j] = static_cast<std::uint32_t>(j);
				if (src == i) {
					v[j] = std::move(tmp);
					break;
				}
				v[j] = std::move(v[src]);
				j = src;
			}
		}
		sorted_ = true;
		return std::nullopt;
	}

private:
	map_type& map_;
	bool sorted_ = true;
};

// One linear pass over two sorted maps, in key order. a_only(a_entry),
// b_only(b_entry) and both(a_entry, b_entry) are called once per key, so
// updating m entries of an n-entry map costs O(n + m) instead of O(m log n)
// shifts of O(n) each.
template <class A, class B, class OnA, class OnB, class OnBoth>
void flat_map_merge(const A& a, const B& b, OnA&& a_only, OnB&& b_only, OnBoth&& both) {
	const auto comp = a.key_comp();
	auto ai = a.begin();
	auto bi = b.begin();
	while (ai != a.end() && bi != b.end()) {
		if (comp(ai->first, bi->first)) {
			a_only(*ai++);
		} else if (comp(bi->first, ai->first)) {
			b_only(*bi++);
		} else {
			both(*ai++, *bi++);
		}
	}
	for (; ai != a.end(); ++ai)
		a_only(*ai);
	for (; bi != b.end(); ++bi)
		b_only(*bi);
}

} // namespace detent
