#include "oracle_capacity.hh"
#include "detent/identity.hh"
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
int main(int argc, char **argv) {
	try {
		if (argc != 7 && argc != 16)
			throw std::invalid_argument(
				"local oracle requires directory, values and four identity components");
		OracleCapacity capacity(argc, argv);
		std::ifstream expected(argv[2], std::ios::binary);
		const auto records = number(expected), producers = number(expected),
				   minimum_revision = number(expected);
		if (!records || records > capacity.limits.records || !producers || producers > 256 || producers > records ||
			std::filesystem::file_size(argv[2]) != 24 + 8 * records)
			throw std::invalid_argument("local oracle bounds");
		detent::distribution::Cursor identity;
		identity.cluster = detent::distribution::parse_identity_hex<16>(argv[3]);
		identity.group = detent::distribution::parse_identity_hex<16>(argv[4]);
		identity.restore_epoch = detent::distribution::parse_identity_hex<16>(argv[5]);
		identity.scope = detent::distribution::parse_identity_hex<32>(argv[6]);
		const auto open_begin = std::chrono::steady_clock::now();
		detent::local::GenerationReader reader(argv[1], capacity.limits, identity);
		reader.refresh();
		const auto ready = std::chrono::steady_clock::now();
		auto view = reader.read_view();
		if (!view || !view.cursor().same_identity(identity) || view.cursor().revision < minimum_revision ||
			view.int_count() != records || view.bool_count())
			throw std::runtime_error("local oracle generation has not converged");
		for (std::uint64_t ordinal = 0; ordinal != records; ++ordinal) {
			const auto name_space = "load" + std::to_string(ordinal % producers);
			const auto key = "key" + std::to_string(ordinal);
			const auto value = view.get(name_space, key);
			if (!value || value.value() != std::bit_cast<std::int64_t>(number(expected)))
				throw std::runtime_error("local oracle mismatch: " + name_space + ":" + key);
		}
		std::cout << "{\"records\":" << records << ",\"revision\":" << view.cursor().revision
                  << ",\"open_ms\":" << std::chrono::duration<double, std::milli>(ready - open_begin).count()
                  << ",\"verify_ms\":" << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - ready).count()
                  << ",\"peak_rss_bytes_before_teardown\":" << oracle_peak_rss_bytes() << "}\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
