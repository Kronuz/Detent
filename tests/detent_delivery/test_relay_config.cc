#include "detent/scope.hh"
#include "detent_relay/configuration.hh"
#include "detent_relay/registry.hh"
#include <fstream>
#include <iostream>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <unistd.h>

namespace {
void check(bool value) {
	if (!value)
		throw std::runtime_error("relay configuration invariant");
}
void write(const std::filesystem::path &path, const rapidjson::Document &object) {
	rapidjson::StringBuffer bytes;
	rapidjson::Writer<rapidjson::StringBuffer> writer(bytes);
	object.Accept(writer);
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	output.write(bytes.GetString(), bytes.GetSize());
	output.close();
	check(bool(output));
	std::filesystem::permissions(path,
								 std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
}
void add_path(rapidjson::Document &object, const char *name, const std::filesystem::path &path) {
	const auto value = path.string();
	object.AddMember(rapidjson::Value(name, object.GetAllocator()),
					 rapidjson::Value(value.data(), value.size(), object.GetAllocator()),
					 object.GetAllocator());
}
} // namespace
int main(int argc, char **argv) {
	try {
		check(argc == 2);
		const auto tls = std::filesystem::absolute(argv[1]);
		const auto directory =
			std::filesystem::current_path() / ".scratch" / ("relay-config-" + std::to_string(::getpid()));
		std::filesystem::create_directories(directory);
		std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
		rapidjson::Document source;
		source.Parse(R"({"cluster":"63636363636363636363636363636363",
"group":"67676767676767676767676767676767","epoch":"65656565656565656565656565656565",
"principal":"reader","namespaces":["app"],"token":"0123456789abcdef0123456789abcdef"})");
		add_path(source, "ca", tls / "ca.pem");
		write(directory / "source.json", source);
		rapidjson::Document host;
		host.Parse(R"({"bundles":[{"source":"source.json","directory":"state","mode":"fresh"}],
"credentials":[{"token":"downstream-token-0123456789abcdef0123456789","principal":"reader","bundles":[0]}]})");
		add_path(host, "certificate", tls / "one.pem");
		add_path(host, "private_key", tls / "one.key");
		add_path(host, "ca", tls / "ca.pem");
		const auto configuration = directory / "relay.json";
		auto load = [&] {
			write(configuration, host);
			return detent::relay::load_host_options(configuration, false);
		};
		auto options = load();
		check(options.endpoint.address().is_loopback() && options.endpoint.port() == 8318);
		check(options.bundles.size() == 1 && options.credentials.size() == 1);
		check(options.hostname == "localhost" && options.certificate_authority == (tls / "ca.pem").string());
		check(options.bundles[0].source.disk.artifact_mode == 0600);
		check(detent::relay::load_host_options(configuration, true).bundles[0].source.disk.artifact_mode ==
			  0600);
		check(options.bundles[0].source.coordinator.delivery_mode ==
			  detent::delivery::DeliveryMode::Subscription);
		check(!options.bundles[0].source.delivery_mode_explicit);
		check(options.credentials[0].authorization.principal.identity ==
			  detent::distribution::canonical_scope_identity("reader", 1, {}));
		check(options.credentials[0].authorization.allows(0) &&
			  !options.credentials[0].authorization.allows(1));
		check(options.heap_bytes == options.control_bytes + options.baseline_bytes +
										options.bundles[0].source.heap_bytes +
										options.bundles[0].cache_bytes);
		auto rejected = [&] {
			bool failed = false;
			try {
				load();
			} catch (const std::invalid_argument &) {
				failed = true;
			}
			check(failed);
		};

        host["bundles"][0].AddMember("cache_bytes", std::uint64_t{2147483648}, host.GetAllocator());
        write(configuration, host);
        check(load().bundles[0].cache_bytes == 2147483648);
        source.AddMember("heap_bytes", std::uint64_t{UINT64_MAX}, source.GetAllocator());
        write(directory / "source.json", source);
        rejected();
        source.RemoveMember("heap_bytes");
        write(directory / "source.json", source);
        host["bundles"][0].RemoveMember("cache_bytes");
        write(configuration, host);
		check(options.connections == 1024 && options.service_limits.polls == 640 &&
			  options.service_limits.sessions.sessions == 1024);
		host.AddMember("connections", 16384, host.GetAllocator());
		rapidjson::Value limits(rapidjson::kObjectType);
		limits.AddMember("sessions", 10000, host.GetAllocator());
		limits.AddMember("per_principal", 10000, host.GetAllocator());
		limits.AddMember("polls", 10000, host.GetAllocator());
		host.AddMember("service_limits", limits, host.GetAllocator());
		auto scaled = load();
		check(scaled.connections == 16384 && scaled.service_limits.polls == 10000 &&
			  scaled.service_limits.sessions.sessions == 10000 &&
			  scaled.service_limits.sessions.per_principal == 10000);
		host["connections"].SetUint(0);
		rejected();
		host["connections"].SetUint(65537);
		rejected();
		host.RemoveMember("connections");
		auto &configured_limits = host["service_limits"];
		configured_limits["polls"].SetUint(0);
		rejected();
		configured_limits["polls"].SetUint(16);
		configured_limits["sessions"].SetUint(16);
		configured_limits["per_principal"].SetUint(17);
		rejected();
		configured_limits.RemoveMember("per_principal");
		auto smaller = load();
		check(smaller.service_limits.bootstrap_transfers == 16 &&
			  smaller.service_limits.catchup_transfers == 16 &&
			  smaller.service_limits.sessions.per_principal == 16);
		configured_limits.AddMember("bootstrap_transfers", 17, host.GetAllocator());
		rejected();
		configured_limits.RemoveMember("bootstrap_transfers");
		configured_limits.AddMember("idle_ms", 1000, host.GetAllocator());
		configured_limits.AddMember("maximum_ms", 999, host.GetAllocator());
		rejected();
		configured_limits["maximum_ms"].SetUint(86400001);
		rejected();
		configured_limits["maximum_ms"].SetUint(1000);
		configured_limits.AddMember("unknown", 1, host.GetAllocator());
		rejected();
		host.RemoveMember("service_limits");
		host.AddMember("hostname", "bad\rhost", host.GetAllocator());
		rejected();
		host.RemoveMember("hostname");
		host.AddMember("heap_bytes", 1024, host.GetAllocator());
		rejected();
		host.RemoveMember("heap_bytes");
		host["credentials"][0]["principal"].SetString("other");
		rejected();
		host["credentials"][0]["principal"].SetString("reader");
		host["credentials"][0].AddMember("policy_epoch", 2, host.GetAllocator());
		rejected();
		source.AddMember("policy_epoch", 2, source.GetAllocator());
		write(directory / "source.json", source);
		{
			auto rotated = load();
			const auto &configured = rotated.bundles[0].source;
			const std::array<std::string_view, 1> names{"app"};
			const detent::relay::RegistryBundle binding{
				configured.coordinator.source.configured,
				{detent::distribution::canonical_scope_identity("reader", 1, {}), 2},
				names};
			auto account = std::make_shared<detent::distribution::StorageLedger>(1024 * 1024);
			detent::relay::Registry registry(account, {&binding, 1}, rotated.credentials);
			const auto *authorization =
				registry.authenticate("Bearer downstream-token-0123456789abcdef0123456789");
			check(authorization && authorization->principal.policy_epoch == 2);
			check(registry.resolve(*authorization, binding.identity, names) == 0);
		}
		source.RemoveMember("policy_epoch");
		write(directory / "source.json", source);
		host["credentials"][0].RemoveMember("policy_epoch");
		host["credentials"][0]["bundles"][0].SetUint(1);
		rejected();
		host["credentials"][0]["bundles"][0].SetUint(0);
		rapidjson::Value duplicate(host["bundles"][0], host.GetAllocator());
		host["bundles"].PushBack(duplicate, host.GetAllocator());
		rejected();
		host["bundles"][1]["directory"].SetString("other-state");
		rejected();
		host["bundles"].PopBack();
		source.AddMember("delivery_mode", "poll", source.GetAllocator());
		write(directory / "source.json", source);
		rejected();
		source["delivery_mode"].SetString("subscription");
		write(directory / "source.json", source);
		check(load().bundles[0].source.delivery_mode_explicit);
		host.AddMember("port", 0, host.GetAllocator());
		rejected();
		host.RemoveMember("port");
		host.AddMember("credentials", rapidjson::Value(rapidjson::kArrayType), host.GetAllocator());
		rejected();
		std::filesystem::remove_all(directory);
		std::cout << "finite relay bindings, exact grants, TLS and budget configuration passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
