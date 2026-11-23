#pragma once
#include "dense_fixture.hh"
#include <fstream>
#include <memory>

namespace dense_test {
struct Row {
	std::string key;
	std::vector<char> body;
};
using Rows = std::vector<Row>;
inline std::vector<char> load(const std::string &path) {
	std::ifstream file(path, std::ios::binary);
	if (!file)
		throw std::runtime_error("cannot open " + path);
	return {std::istreambuf_iterator<char>(file), {}};
}
inline std::shared_ptr<Rows> corpus(const std::string &path) {
	auto data = load(path);
	require(data.size() >= 32 && std::string_view(data.data(), 8) == "DTCORP01");
	const auto count = l::read_integer(data, 8, 8), keys = l::read_integer(data, 16, 8),
			   bodies = l::read_integer(data, 24, 8);
	const auto key_at = 32 + 4 * (count + 1), body_offsets = key_at + keys,
			   body_at = body_offsets + 4 * (count + 1);
	require(body_at + bodies + count == data.size());
	auto rows = std::make_shared<Rows>();
	for (std::size_t i = 0; i < count; ++i) {
		const auto k0 = l::read_integer(data, 32 + 4 * i, 4),
				   k1 = l::read_integer(data, 36 + 4 * i, 4),
				   b0 = l::read_integer(data, body_offsets + 4 * i, 4),
				   b1 = l::read_integer(data, body_offsets + 4 * i + 4, 4);
		require(k0 <= k1 && k1 <= keys && b0 <= b1 && b1 <= bodies);
		rows->push_back(
			{std::string(data.data() + key_at + k0, k1 - k0),
			 std::vector<char>(data.begin() + body_at + b0, data.begin() + body_at + b1)});
	}
	return rows;
}
} // namespace dense_test
