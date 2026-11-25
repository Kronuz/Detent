#include "oracle_capacity.hh"
#include "detent/identity.hh"
#include "detent_distribution/frame_input.hh"
#include "detent_local/reader.hh"
#include <bit>
#include <fstream>
#include <iostream>

namespace {
std::uint64_t number(std::istream &stream) {
	std::array<unsigned char, 8> bytes{};
	stream.read(reinterpret_cast<char *>(bytes.data()), bytes.size());
	if (!stream)
		throw std::runtime_error("truncated local oracle");
	std::uint64_t result = 0;
	for (unsigned i = 0; i != bytes.size(); ++i)
		result |= std::uint64_t(bytes[i]) << (8 * i);
	return result;
}
} // namespace
// Reads producer p's namespace `load<p>` from its own table entry and checks every value.
int main(int argc, char **argv) {
	try {
		if (argc != 4 && argc != 13)
			throw std::invalid_argument("local oracle requires directory, values and cluster");
		OracleCapacity capacity(argc, argv, 4);
		std::ifstream expected(argv[2], std::ios::binary);
		const auto records = number(expected), producers = number(expected);
		if (!records || records > capacity.limits.records || !producers || producers > 256 || producers > records ||
			std::filesystem::file_size(argv[2]) != 16 + 8 * producers + 8 * records)
			throw std::invalid_argument("local oracle bounds");
		std::vector<std::uint64_t> minimum(producers);
		for (auto &revision : minimum)
			revision = number(expected);
		std::vector<std::int64_t> values(records);
		for (auto &value : values)
			value = std::bit_cast<std::int64_t>(number(expected));
		const auto cluster = detent::distribution::parse_identity_hex<16>(argv[3]);
		std::chrono::duration<double, std::milli> open{}, verify{};
		std::uint64_t revision = 0;
		for (std::uint64_t producer = 0; producer != producers; ++producer) {
			const auto name = "load" + std::to_string(producer);
			const auto open_begin = std::chrono::steady_clock::now();
			detent::local::GenerationReader reader(argv[1], capacity.limits, std::nullopt, std::nullopt, name);
			reader.refresh();
			const auto ready = std::chrono::steady_clock::now();
			const auto view = reader.read_view();
			const auto count = (records - producer + producers - 1) / producers;
			if (!view || view.cursor().cluster != cluster || view.cursor().position < minimum[producer] ||
			    view.size() != count)
				throw std::runtime_error("local oracle namespace has not converged: " + name);
			for (auto ordinal = producer; ordinal < records; ordinal += producers) {
				const auto key = "key" + std::to_string(ordinal);
				const auto value = view.get(name, key);
				if (!value || value.value() != values[ordinal])
					throw std::runtime_error("local oracle mismatch: " + name + ":" + key);
			}
			revision = std::max(revision, view.cursor().position);
			open += ready - open_begin;
			verify += std::chrono::steady_clock::now() - ready;
		}
		std::cout << "{\"records\":" << records << ",\"revision\":" << revision << ",\"open_ms\":" << open.count()
				  << ",\"verify_ms\":" << verify.count()
				  << ",\"peak_rss_bytes_before_teardown\":" << oracle_peak_rss_bytes() << "}\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
