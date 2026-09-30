#include "detent/hashing.hh"

#include <sha256.h>

#include <string>

namespace detent {

std::uint64_t stable_bucket(std::string_view knob_name, std::string_view hash_val) {
	std::string input;
	input.reserve(knob_name.size() + 1 + hash_val.size());
	input.append(knob_name);
	input.push_back('\0');
	input.append(hash_val);

	SHA256 sha;
	unsigned char digest[SHA256::HashBytes];
	sha.add(input.data(), input.size());
	sha.getHash(digest);

	std::uint64_t u64 = 0;
	for (int i = 0; i < 8; ++i) {
		u64 = (u64 << 8) | static_cast<std::uint64_t>(digest[i]);
	}
	return u64 % 100;
}

} // namespace detent
