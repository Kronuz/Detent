// The slot hash of PagedSegmentMarks is hashes.hh's fnv1ah64 over the 16 identity bytes. This
// pins it to the FNV-1a loop it replaced, frozen below, over fixed and generated identities.
#include "hashes.hh"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {
using Identity = std::array<char, 16>;

std::uint64_t legacy(const Identity &identity) {
	std::uint64_t value = 14695981039346656037ull;
	for (unsigned char byte : identity) {
		value ^= byte;
		value *= 1099511628211ull;
	}
	return value;
}

std::uint64_t signed_bytes(const Identity &identity) {
	std::uint64_t value = 14695981039346656037ull;
	for (char byte : identity) {
		value ^= static_cast<std::uint64_t>(static_cast<std::int64_t>(byte));
		value *= 1099511628211ull;
	}
	return value;
}

std::uint64_t shared(const Identity &identity) { return fnv1ah64::hash(identity.data(), identity.size()); }

void require(bool value, const char *what) {
	if (!value) {
		std::fprintf(stderr, "failed: %s\n", what);
		std::exit(1);
	}
}

Identity bytes(unsigned first, unsigned step) {
	Identity result;
	for (unsigned i = 0; i < result.size(); ++i)
		result[i] = static_cast<char>(first + i * step);
	return result;
}
} // namespace

int main() {
	// Values computed independently of this repository (Python, 64-bit wraparound).
	struct Golden {
		Identity identity;
		std::uint64_t hash;
	};
	const Golden golden[] = {
		{bytes(0, 0), 0x88201fb960ff6465ull},
		{bytes(255, 0), 0xd6607508f5a1e855ull},
		{bytes(0, 1), 0x7c84dc9477851775ull},
		{bytes(0x80, 1), 0xc3f091e95faf3b75ull},
	};
	for (const auto &entry : golden) {
		require(shared(entry.identity) == entry.hash, "shared hash matches the independent value");
		require(legacy(entry.identity) == entry.hash, "frozen loop matches the independent value");
	}
	std::size_t high_bytes = 0, signed_differs = 0, checked = 0;
	auto compare = [&](const Identity &identity) {
		++checked;
		require(shared(identity) == legacy(identity), "shared hash equals the frozen loop");
		for (char byte : identity)
			if (static_cast<unsigned char>(byte) >= 0x80) {
				++high_bytes;
				break;
			}
		if (signed_bytes(identity) != legacy(identity))
			++signed_differs;
	};
	for (unsigned first = 0; first < 256; ++first)
		for (unsigned step : {0u, 1u, 3u, 17u, 255u})
			compare(bytes(first, step));
	for (unsigned position = 0; position < 16; ++position)
		for (unsigned value = 0; value < 256; ++value) {
			Identity identity{};
			identity[position] = static_cast<char>(value);
			compare(identity);
		}
	std::uint64_t state = 0x9e3779b97f4a7c15ull;
	for (unsigned i = 0; i < 20000; ++i) {
		Identity identity;
		for (auto &byte : identity) {
			state ^= state << 13, state ^= state >> 7, state ^= state << 17;
			byte = static_cast<char>(state >> 32);
		}
		compare(identity);
	}
	// The vectors reach bytes at and above 0x80, where a signed conversion would differ.
	require(high_bytes > 0 && signed_differs > 0, "vectors distinguish signed from unsigned bytes");
	std::printf("shared fnv1ah64 equals the frozen FNV-1a loop on %zu identities (%zu with high bytes)\n", checked, high_bytes);
}
