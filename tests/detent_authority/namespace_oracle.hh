#pragma once
// Compares every replicated namespace field, the directory and every record of two states.
#include "state.hh"
#include <cassert>
#include <string>
#include <tuple>
#include <vector>

namespace detent::authority::testing {
inline void same_namespaces(const State &a, const State &b) {
	assert(a.revision() == b.revision() && a.logical_bytes() == b.logical_bytes() && a.size() == b.size());
	assert(a.positions_enabled() == b.positions_enabled() && a.directory_position() == b.directory_position());
	std::vector<std::tuple<std::uint64_t, std::string, Namespace>> left, right;
	a.for_each_directory_entry(0, [&](auto d, auto &name, auto &entry) { left.emplace_back(d, name, entry); });
	b.for_each_directory_entry(0, [&](auto d, auto &name, auto &entry) { right.emplace_back(d, name, entry); });
	assert(left == right);
	std::vector<std::pair<std::string, Revision>> ra, rb;
	a.for_each([&](const std::string &key, const Record &r) {
		ra.emplace_back(key, r.revision);
		assert(b.find(key) && *b.find(key)->value == *r.value);
	});
	b.for_each([&](const std::string &key, const Record &r) { rb.emplace_back(key, r.revision); });
	assert(ra == rb);
}
} // namespace detent::authority::testing
