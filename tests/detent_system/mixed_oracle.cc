#include "oracle_capacity.hh"
#include "detent/client.hh"
#include "detent/identity.hh"
#include "detent/validation.hh"
#include "detent_local/client_source.hh"
#include <bit>
#include <fstream>
#include <iostream>
#include <sha256.h>

namespace {
constexpr std::uint64_t maximum_generation = (1ull << 40) - 1;
std::uint64_t number(std::istream &stream) {
	std::array<unsigned char, 8> bytes{};
	stream.read(reinterpret_cast<char *>(bytes.data()), bytes.size());
	if (!stream)
		throw std::runtime_error("truncated mixed oracle");
	std::uint64_t value = 0;
	for (unsigned i = 0; i != bytes.size(); ++i)
		value |= std::uint64_t(bytes[i]) << (8 * i);
	return value;
}
void check(bool value, std::string_view key) {
	if (!value)
		throw std::runtime_error("mixed SDK oracle mismatch: " + std::string(key));
}
template <class T>
void error(const detent::Result<T, detent::KnobError> &result, detent::ErrorCategory category,
		   std::string_view key) {
	check(!result && result.error().category == category, key);
}
template <class T>
void equal(const detent::Result<T, detent::KnobError> &result, T expected, std::string_view key) {
	check(result && result.value() == expected, key);
}
std::uint64_t bucket(std::string_view key, std::string_view cohort) {
	class SHA256 hash;
	hash.add(key.data(), key.size());
	const char separator = 0;
	hash.add(&separator, 1);
	hash.add(cohort.data(), cohort.size());
	std::array<unsigned char, 32> digest{};
	hash.getHash(digest.data());
	std::uint64_t value = 0;
	for (unsigned i = 0; i != 8; ++i)
		value = (value << 8) | digest[i];
	return value % 100;
}
} // namespace
int main(int argc, char **argv) {
	try {
		if (argc != 7 && argc != 14)
			throw std::invalid_argument("mixed oracle requires directory, recipes and four identity components");
		OracleCapacity capacity(argc, argv);
		std::ifstream expected(argv[2], std::ios::binary);
		std::array<char, 8> magic{};
		expected.read(magic.data(), magic.size());
		if (!expected || std::string_view(magic.data(), magic.size()) != "DTMIXED1")
			throw std::invalid_argument("mixed oracle format");
		const auto records = number(expected), producers = number(expected), revision = number(expected),
				   namespace_bytes = number(expected);
		if (!records || records > capacity.limits.records || !producers || producers > 256 || producers > records ||
			namespace_bytes > 198 || std::filesystem::file_size(argv[2]) != 40 + namespace_bytes + 8 * records)
			throw std::invalid_argument("mixed oracle bounds");
		std::string shared_namespace(namespace_bytes, '\0');
		expected.read(shared_namespace.data(), shared_namespace.size());
		if (!expected || (!shared_namespace.empty() &&
			!detent::is_valid_knob_name(shared_namespace + ":key" + std::to_string(records - 1))))
			throw std::invalid_argument("mixed oracle namespace");
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
		const auto view = client.read_view();
		const auto cursor = view.cursor();
		if (!cursor || !cursor->same_identity(identity) || cursor->revision < revision)
			throw std::runtime_error("mixed SDK oracle generation has not converged");
		std::uint64_t booleans = 0, integers = 0, deleted = 0;
		for (std::uint64_t ordinal = 0; ordinal != records; ++ordinal) {
			const auto raw = number(expected);
			const auto generation = std::bit_cast<std::int64_t>(raw);
			const auto counter = generation < 0 ? ~raw : raw;
			if (counter > maximum_generation)
				throw std::invalid_argument("mixed oracle generation bound");
			const auto name_space = shared_namespace.empty() ? "load" + std::to_string(ordinal % producers)
															  : shared_namespace;
			const auto key = name_space + ":key" + std::to_string(ordinal);
			if (generation < 0) {
				++deleted;
				error(view.eval(key), detent::ErrorCategory::MissingKnob, key);
				error(view.get(key), detent::ErrorCategory::MissingKnob, key);
			} else if ((ordinal + counter) % 2) {
				++integers;
				const auto magnitude = static_cast<std::int64_t>((counter * 104729 + ordinal * 17) % (1ull << 60));
				const auto value = ((ordinal + counter) & 2) ? -magnitude : magnitude;
				equal(view.get(key), value, key);
				equal(view.get(key, "ramp"), value, key);
				error(view.eval(key), detent::ErrorCategory::WrongType, key);
			} else {
				++booleans;
				const bool default_value = (ordinal / 2 + counter) & 1;
				equal(view.eval(key), default_value, key);
				equal(view.eval(key, {}, "force-off"), false, key);
				equal(view.eval(key, {}, "force-on"), true, key);
				equal(view.eval(key, {}, std::string_view("opaque\0group", 12)), !default_value, key);
				equal(view.eval(key, {}, "ramp"), default_value, key);
				error(view.get(key), detent::ErrorCategory::WrongType, key);
				for (const auto cohort : {std::string_view("cohort-A"), std::string_view("cohort-B")}) {
					const auto sample = bucket(key, cohort);
					const bool global = (ordinal + counter) % 3 == 0 ? default_value
														: sample < (ordinal * 13 + counter * 7) % 101;
					equal(view.eval(key, cohort), global, key);
					equal(view.eval(key, cohort, "unknown"), global, key);
					equal(view.eval(key, cohort, "ramp"), sample < (ordinal * 17 + counter * 11) % 101, key);
					equal(view.eval(key, cohort, "force-off"), false, key);
					equal(view.eval(key, cohort, "force-on"), true, key);
					equal(view.eval(key, cohort, std::string_view("opaque\0group", 12)), !default_value, key);
				}
			}
		}
		if (source->status().bool_knobs != booleans || source->status().int_knobs != integers)
			throw std::runtime_error("mixed SDK oracle corpus cardinality mismatch");
		std::cout << "{\"records\":" << records << ",\"bool_knobs\":" << booleans
				  << ",\"int_knobs\":" << integers << ",\"deleted\":" << deleted
				  << ",\"revision\":" << cursor->revision
                  << ",\"open_ms\":" << std::chrono::duration<double, std::milli>(ready - open_begin).count()
                  << ",\"verify_ms\":" << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - ready).count()
                  << ",\"peak_rss_bytes_before_teardown\":" << oracle_peak_rss_bytes() << "}\n";
	} catch (const std::exception &exception) {
		std::cerr << exception.what() << '\n';
		return 1;
	}
}
