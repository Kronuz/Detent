#include "oracle_capacity.hh"
#include "detent/client.hh"
#include "detent/identity.hh"
#include "detent_distribution/frame_input.hh"
#include "detent_local/client_source.hh"
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
			detent::local::ClientSourceOptions local;
			local.directory = argv[1];
			local.name_space = name;
			local.auto_refresh = false;
			local.limits = capacity.limits;
			detent::InitOptions options;
			std::shared_ptr<detent::local::LocalReadSource> source;
			options.read_source = [&](std::shared_ptr<const detent::Snapshot> defaults,
									  std::shared_ptr<const detent::OverrideTable> overrides) {
				source = std::make_shared<detent::local::LocalReadSource>(local, std::move(defaults),
																		  std::move(overrides));
				return source;
			};
			const auto open_begin = std::chrono::steady_clock::now();
			detent::Client client(std::move(options));
			source->refresh();
			const auto ready = std::chrono::steady_clock::now();
			auto view = client.read_view();
			const auto cursor = view.cursor();
			const auto count = (records - producer + producers - 1) / producers;
			if (!cursor || cursor->cluster != cluster || cursor->position < minimum[producer] ||
				source->status().knobs != count)
				throw std::runtime_error("local oracle namespace has not converged: " + name);
			for (auto ordinal = producer; ordinal < records; ordinal += producers) {
				const auto key = name + ":key" + std::to_string(ordinal);
				const auto value = view.get(key);
				if (!value || value.value() != values[ordinal])
					throw std::runtime_error("local oracle mismatch: " + key);
			}
			revision = std::max(revision, cursor->position);
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
