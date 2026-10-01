#pragma once

#include <array>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace detent::detail {

// Bound allocation while reading, including files that grow after opening.
inline std::string read_file(const std::filesystem::path& path, std::size_t max_bytes) {
	std::ifstream in(path, std::ios::binary);
	if (!in) throw std::runtime_error("cannot open file: " + path.string());
	std::string text;
	std::array<char, 16384> buffer;
	for (;;) {
		in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
		auto count = static_cast<std::size_t>(in.gcount());
		if (count > max_bytes - text.size()) {
			throw std::runtime_error("file exceeds size limit: " + path.string());
		}
		text.append(buffer.data(), count);
		if (in.bad() || (in.fail() && !in.eof())) {
			throw std::runtime_error("cannot read file: " + path.string());
		}
		if (in.eof()) return text;
	}
}

} // namespace detent::detail
