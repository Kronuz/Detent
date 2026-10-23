#include "oracle_capacity.hh"
#include "detent/client.hh"
#include "detent/identity.hh"
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
		detent::local::ClientSourceOptions local;
		local.directory = argv[1];
		local.identity = identity;
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
		if (!cursor || !cursor->same_identity(identity) || cursor->revision < minimum_revision ||
			source->status().int_knobs != records || source->status().bool_knobs)
			throw std::runtime_error("local oracle generation has not converged");
		for (std::uint64_t ordinal = 0; ordinal != records; ++ordinal) {
			const auto key = "load" + std::to_string(ordinal % producers) + ":key" + std::to_string(ordinal);
			const auto value = view.get(key);
			if (!value || value.value() != std::bit_cast<std::int64_t>(number(expected)))
				throw std::runtime_error("local oracle mismatch: " + key);
		}
		std::cout << "{\"records\":" << records << ",\"revision\":" << cursor->revision
                  << ",\"open_ms\":" << std::chrono::duration<double, std::milli>(ready - open_begin).count()
                  << ",\"verify_ms\":" << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - ready).count()
                  << ",\"peak_rss_bytes_before_teardown\":" << oracle_peak_rss_bytes() << "}\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
