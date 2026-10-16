#include "detent_delivery/configuration.hh"
#include "fleet_load.hh"
#include <charconv>
#include <cstdio>
#include <limits>
#include <string_view>

namespace {
std::uint64_t number(std::string_view value, std::uint64_t minimum, std::uint64_t maximum) {
	std::uint64_t result = 0;
	const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
	if (error != std::errc{} || end != value.data() + value.size() || result < minimum || result > maximum)
		throw std::invalid_argument("invalid fleet numeric argument");
	return result;
}
} // namespace
int main(int argc, char **argv) {
	try {
		detent::fleet::Options options;
		std::vector<std::filesystem::path> configurations;
		for (int index = 1; index < argc; ++index) {
			const std::string_view name(argv[index]);
			if (name == "--help") {
				std::puts("detent_fleet_load --source PRIVATE.json [--source PRIVATE.json] --result NEW.json "
						  "[--subscribers N] [--active N] [--workers N] [--seconds N] [--heap-bytes N] "
						  "[--seed N] [--warm-revision N] [--fail-launch-after N]");
				return 0;
			}
			if (index + 1 == argc)
				throw std::invalid_argument("fleet option requires a value");
			const std::string_view value(argv[++index]);
			if (name == "--source") {
				if (configurations.size() == 8)
					throw std::invalid_argument("fleet source count exceeds eight");
				configurations.emplace_back(value);
			} else if (name == "--result")
				options.result = value;
			else if (name == "--subscribers")
				options.subscribers = number(value, 1, 50000);
			else if (name == "--active")
				options.active = number(value, 1, 50000);
			else if (name == "--workers")
				options.workers = number(value, 1, 16);
			else if (name == "--seconds")
				options.lifetime = std::chrono::seconds(number(value, 1, 86400));
			else if (name == "--heap-bytes")
				options.heap_bytes = number(value, 1024 * 1024, 24ull * 1024 * 1024 * 1024);
			else if (name == "--seed")
				options.seed = number(value, 0, std::numeric_limits<std::uint64_t>::max());
			else if (name == "--fail-launch-after")
				options.fail_launch_after = number(value, 0, 50000);
			else if (name == "--warm-revision")
				options.warm_revision = number(value, 1, std::numeric_limits<std::uint64_t>::max());
			else
				throw std::invalid_argument("unknown fleet option");
		}
		if (configurations.empty() || options.result.empty() || options.active > options.subscribers ||
			options.workers > options.active)
			throw std::invalid_argument("fleet requires sources, result and valid concurrency bounds");
		for (const auto &path : configurations) {
			auto managed = detent::delivery::load_managed_options(
				path, path.parent_path(), detent::local::StoreOpenMode::FreshEmpty, false);
			if (!managed.coordinator.authority_endpoints.empty())
				throw std::invalid_argument("fleet sources require explicit single endpoints");
			options.sources.push_back(std::move(managed.coordinator.source));
		}
		return detent::fleet::run(std::move(options));
	} catch (const std::exception &error) {
		std::fprintf(stderr, "fleet: %s\n", error.what());
		return 1;
	}
}
