#pragma once
#include <cstdint>
#include <string_view>

namespace detent {
inline bool valid_utf8(std::string_view s, bool reject_zero = true) {
	for (std::size_t i = 0; i < s.size();) {
		auto c = static_cast<unsigned char>(s[i++]);
		if (c < 0x80) {
			if (!c && reject_zero) {
				return false;
			}
			continue;
		}
		unsigned count;
		std::uint32_t value, minimum;
		if (c >= 0xc2 && c <= 0xdf) {
			count = 1;
			value = c & 31;
			minimum = 0x80;
		} else if (c >= 0xe0 && c <= 0xef) {
			count = 2;
			value = c & 15;
			minimum = 0x800;
		} else if (c >= 0xf0 && c <= 0xf4) {
			count = 3;
			value = c & 7;
			minimum = 0x10000;
		} else {
			return false;
		}
		if (count > s.size() - i) {
			return false;
		}
		for (unsigned j = 0; j < count; ++j) {
			auto b = static_cast<unsigned char>(s[i++]);
			if ((b & 0xc0) != 0x80) {
				return false;
			}
			value = (value << 6) | (b & 63);
		}
		if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) {
			return false;
		}
	}
	return true;
}
} // namespace detent
