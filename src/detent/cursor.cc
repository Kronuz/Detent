#include "detent/cursor.hh"
#include <algorithm>
#include <sha256.h>
#include <string>
#include <string_view>

namespace detent::distribution {
NamespaceCursor namespace_cursor(const Cursor &cursor) {
	constexpr std::string_view domain = "detent.agent-store.incarnation";
	std::string input(domain);
	for (const auto *identity : {&cursor.cluster, &cursor.group, &cursor.restore_epoch})
		input.append(identity->data(), identity->size());
	input.append(cursor.scope.data(), cursor.scope.size());
	SHA256 hash;
	hash.add(input.data(), input.size());
	std::array<char, 32> sum{};
	hash.getHash(reinterpret_cast<unsigned char *>(sum.data()));
	NamespaceCursor out{cursor.cluster, {}, cursor.revision};
	std::copy_n(sum.begin(), out.incarnation.size(), out.incarnation.begin());
	return out;
}
} // namespace detent::distribution
