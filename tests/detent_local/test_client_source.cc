#include "detent/client.hh"
#include "detent_local/client_source.hh"
#include "snapshot_fixture.hh"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <new>
#include <thread>
#include <unistd.h>

thread_local bool count_allocations = false;
thread_local std::size_t allocations = 0;
namespace detent::test {
struct AllocationProbe {
	static void allocate(std::size_t) {
		if (count_allocations)
			++allocations;
	}
};
} // namespace detent::test
#include "support/allocation_probe.hh"

namespace l = detent::local;
namespace {
void write(const std::filesystem::path &path, std::span<const char> bytes) {
	std::ofstream stream(path, std::ios::binary | std::ios::trunc);
	stream.write(bytes.data(), bytes.size());
	stream.close();
	require(static_cast<bool>(stream));
	std::filesystem::permissions(path,
								 std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
}
void publish(const std::filesystem::path &directory, std::shared_ptr<const d::ValidatedSnapshot> snapshot,
			 char id) {
	const auto generation = l::next_generation(std::nullopt, snapshot->cursor(), snapshot->profile(), true).id;
	l::ImageBuilder builder(std::move(snapshot));
	while (!builder.done()) {
		l::ValidationBudget budget;
		builder.step(budget);
	}
	auto image = builder.bytes();
	auto name = l::generation_name(generation);
	write(directory / (name + ".image"), {image->data(), image->size()});
	write(directory / (name + ".lease"), {});
	write(directory / "selected.next", l::encode_descriptor(builder.descriptor()));
	std::filesystem::rename(directory / "selected.next", directory / "selected");
}
std::shared_ptr<const d::ValidatedSnapshot> empty() {
	std::string out;
	auto n = [&](std::uint64_t value) { m::unsigned_integer(out, value); };
	auto a = [&](std::uint32_t value) { m::array(out, value); };
	a(7);
	n(1);
	n(1);
	a(2);
	n(1);
	n(1);
	a(5);
	auto target = cursor();
	target.revision = 43;
	for (const auto *identity : {&target.cluster, &target.group, &target.restore_epoch})
		m::string(out, {identity->data(), identity->size()}, true);
	m::string(out, {target.scope.data(), target.scope.size()}, true);
	n(43);
	a(4);
	n(1);
	n(0);
	n(0);
	n(3);
	a(1);
	a(2);
	m::string(out, "app");
	n(43);
	a(0);
	return std::make_shared<const d::ValidatedSnapshot>(decode(out, target));
}
} // namespace
int main() {
	try {
		auto directory =
			std::filesystem::current_path() / ".scratch" / ("local-client-" + std::to_string(::getpid()));
		std::filesystem::create_directories(directory);
		std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
		auto defaults = directory.parent_path() / (directory.filename().string() + ".json");
		const std::string json = R"({"schema_version":1,"knobs":{
"app:count":{"default":0},
"app:flag":{"default":0},
"app:long_fallback_integer_name_for_allocation_probe":{"default":25}}})";
		write(defaults, json);
		l::ClientSourceOptions source_options;
		source_options.directory = directory;
		source_options.identity = cursor();
		source_options.auto_refresh = false;
		std::shared_ptr<l::LocalReadSource> source;
		detent::InitOptions options;
		options.defaults_path = defaults;
		options.read_source = [&](std::shared_ptr<const detent::Snapshot> baked,
								  std::shared_ptr<const detent::OverrideTable> overrides) {
			source =
				std::make_shared<l::LocalReadSource>(source_options, std::move(baked), std::move(overrides));
			return source;
		};
		auto client = std::make_unique<detent::Client>(options);
		require(client->get("app:count").value() == 0);
		publish(directory, std::make_shared<const d::ValidatedSnapshot>(decode(fixture())), 'a');
		require(source->refresh());
		require(!source->refresh());
		require(client->get("app:count").value() == std::numeric_limits<std::int64_t>::min());
		require(client->get("app:missing").error().name == "app:missing");
		require(client->get("app:flag").value() == 0);
		require(client->get("app:flag", "host", "beta").value() ==
				(detent::stable_bucket("app:flag", "host") < 17));
		require(client->health().accepted_version == 42 && client->health().source_connected);
		std::shared_ptr<l::LocalReadSource> cli_source;
		auto cli_options = options;
		cli_options.cli_overrides = {"app:count=17", "app:flag=false"};
		cli_options.read_source = [&](std::shared_ptr<const detent::Snapshot> baked,
									  std::shared_ptr<const detent::OverrideTable> overrides) {
			cli_source =
				std::make_shared<l::LocalReadSource>(source_options, std::move(baked), std::move(overrides));
			return cli_source;
		};
		auto cli_client = std::make_unique<detent::Client>(cli_options);
		require(cli_source->refresh());
		require(cli_client->get("app:count").value() == 17 &&
				cli_client->get("app:flag", "host", "beta").value() == 0);
		auto pinned = client->read_view();
		require(pinned.get_as<std::uint64_t>("app:count").error().category ==
				detent::ErrorCategory::ConversionFailed);
		allocations = 0;
		count_allocations = true;
		void *volatile probe = ::operator new(32);
		::operator delete(probe);
		count_allocations = false;
		require(allocations > 0);
		allocations = 0;
		count_allocations = true;
		for (unsigned i = 0; i != 1000; ++i) {
			auto view = client->read_view();
			require(view.get("app:count").value() == std::numeric_limits<std::int64_t>::min());
			require(view.get("app:long_fallback_integer_name_for_allocation_probe").value() == 25);
		}
		count_allocations = false;
		require(allocations == 0);
		publish(directory, empty(), 'b');
		require(source->refresh());
		require(client->get("app:count").value() == 0);
		require(client->read_view().cursor()->revision == 43 && pinned.cursor()->revision == 42);
		require(pinned.get("app:count").value() == std::numeric_limits<std::int64_t>::min());
		auto automatic = source_options;
		automatic.auto_refresh = true;
		automatic.poll_interval = std::chrono::milliseconds(10);
		detent::InitOptions automatic_options;
		automatic_options.defaults_path = defaults;
		automatic_options.read_source = l::make_local_read_source(automatic);
		{
			detent::Client automatic_client(automatic_options);
			auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
			while (!automatic_client.health().source_connected &&
				   std::chrono::steady_clock::now() < deadline) {
				require(automatic_client.get("app:count").value() == 0);
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			require(automatic_client.health().source_connected &&
					automatic_client.read_view().cursor()->revision == 43);
		}
		write(directory / "selected", {});
		bool rejected = false;
		try {
			source->refresh();
		} catch (const std::exception &) {
			rejected = true;
		}
		require(rejected && !client->health().source_connected &&
				client->read_view().cursor()->revision == 43);
		client.reset();
		source.reset();
		cli_client.reset();
		cli_source.reset();
		require(pinned.get("app:count").value() == std::numeric_limits<std::int64_t>::min());
		std::filesystem::remove(defaults);
		std::filesystem::remove_all(directory);
		std::cout
			<< "local Client, absence-only defaults, pinned scope and zero-allocation acquisition passed\n";
	} catch (const std::exception &error) {
		count_allocations = false;
		std::cerr << error.what() << '\n';
		return 1;
	}
}
