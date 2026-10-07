#include "detent/hashing.hh"

#include <sha256.h>

namespace detent {

std::uint64_t stable_bucket(std::string_view knob_name, std::string_view hash_val) {
	SHA256 sha;
	unsigned char digest[SHA256::HashBytes];
	constexpr char separator = '\0';
	sha.add(knob_name.data(), knob_name.size());
	sha.add(&separator, 1);
	sha.add(hash_val.data(), hash_val.size());
	sha.getHash(digest);

	std::uint64_t u64 = 0;
	for (int i = 0; i < 8; ++i) {
		u64 = (u64 << 8) | static_cast<std::uint64_t>(digest[i]);
	}
	return u64 % 100;
}

} // namespace detent
