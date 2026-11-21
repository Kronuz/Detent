#include "detent/scope.hh"
#include "detent_relay/configuration.hh"
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
		// One file of process and role settings, and the one private file named by `secrets`.
		rapidjson::Document host, secrets;
		host.Parse(R"({"mirror":{"directory":"mirror","cluster":"63636363636363636363636363636363",
"namespaces":["app","app.one"],"upstreams":[{"address":"127.0.0.1","port":8317}]},
"secrets":"secrets.json","log_level":"info","max_in_flight":64,"idle_timeout_ms":1000,
"shutdown_timeout_ms":2000})");
		secrets.Parse(R"({"upstream_token":"upstream-token-0123456789abcdef0123456789",
"credentials":[{"token":"downstream-token-0123456789abcdef0123456789","principal":"reader",
"namespaces":["app.*"]}]})");
		add_path(host, "certificate", tls / "one.pem");
		add_path(host, "private_key", tls / "one.key");
		add_path(host, "ca", tls / "ca.pem");
		const auto configuration = directory / "detent-relay.json";
		auto load = [&] {
			write(directory / "secrets.json", secrets);
			write(configuration, host);
			return detent::relay::load_host_options(configuration);
		};
		auto options = load();
		check(options.endpoint.address().is_loopback() && options.endpoint.port() == 8318);
		check(options.hostname == "localhost" && options.certificate_authority == (tls / "ca.pem").string());
		const auto &mirror = options.mirror;
		check(mirror.service.directory == directory / "mirror");
		check(mirror.service.namespaces == std::vector<std::string>{"app", "app.one"});
		check(mirror.upstream.endpoints.size() == 1 && mirror.upstream.endpoints[0].port() == 8317);
		check(mirror.upstream.hostname == "localhost" && mirror.upstream.tls);
		check(options.credentials.size() == 1);
		const auto &credential = options.credentials[0];
		check(credential.principal ==
			  detent::distribution::SubscriptionPrincipal{
				  detent::distribution::canonical_scope_identity("reader", 1, {}), 1});
		check(credential.namespaces->allows("app.one") && !credential.namespaces->allows("app") &&
			  !credential.namespaces->allows("other"));
		check(options.heap_bytes == options.control_bytes + options.baseline_bytes + mirror.answer_bytes);
		check(options.connections == 1024 && options.service_limits.opens == 16 &&
			  options.service_limits.polls == 640 && options.service_limits.closes == 16);
		auto rejected = [&] {
			bool failed = false;
			try {
				load();
			} catch (const std::invalid_argument &) {
				failed = true;
			}
			check(failed);
		};
		// The mirror is required; bundles are no configuration field, at the top or in a credential.
		rapidjson::Value saved(host["mirror"], host.GetAllocator());
		host.RemoveMember("mirror");
		rejected();
		host.AddMember("mirror", saved, host.GetAllocator());
		host.AddMember("bundles", rapidjson::Value(rapidjson::kArrayType), host.GetAllocator());
		rejected();
		host.RemoveMember("bundles");
		secrets["credentials"][0].AddMember("bundles", rapidjson::Value(rapidjson::kArrayType), host.GetAllocator());
		rejected();
		secrets["credentials"][0].RemoveMember("bundles");
		// A credential grants W13 namespace patterns, at least one, each well formed.
		rapidjson::Value patterns(secrets["credentials"][0]["namespaces"], host.GetAllocator());
		secrets["credentials"][0].RemoveMember("namespaces");
		rejected();
		secrets["credentials"][0].AddMember("namespaces", rapidjson::Value(rapidjson::kArrayType),
										 host.GetAllocator());
		rejected();
		secrets["credentials"][0]["namespaces"].PushBack("app*", host.GetAllocator());
		rejected();
		secrets["credentials"][0]["namespaces"] = patterns;
		secrets["credentials"][0].AddMember("policy_epoch", 2, host.GetAllocator());
		check(load().credentials[0].principal.policy_epoch == 2);
		secrets["credentials"][0]["policy_epoch"].SetUint(0);
		rejected();
		secrets["credentials"][0].RemoveMember("policy_epoch");
		rapidjson::Value duplicate(secrets["credentials"][0], host.GetAllocator());
		secrets["credentials"].PushBack(duplicate, host.GetAllocator());
		rejected();
		secrets["credentials"].PopBack();
		// Admission lanes, connections and the heap.
		host.AddMember("max_clients", 16384, host.GetAllocator());
		rapidjson::Value limits(rapidjson::kObjectType);
		limits.AddMember("opens", 4, host.GetAllocator());
		limits.AddMember("polls", 10000, host.GetAllocator());
		limits.AddMember("closes", 4, host.GetAllocator());
		host.AddMember("service_limits", limits, host.GetAllocator());
		auto scaled = load();
		check(scaled.connections == 16384 && scaled.service_limits.opens == 4 &&
			  scaled.service_limits.polls == 10000 && scaled.service_limits.closes == 4);
		host["max_clients"].SetUint(0);
		rejected();
		host["max_clients"].SetUint(65537);
		rejected();
		host.RemoveMember("max_clients");
		auto &configured_limits = host["service_limits"];
		configured_limits["polls"].SetUint(0);
		rejected();
		configured_limits["polls"].SetUint(16);
		configured_limits.AddMember("sessions", 16, host.GetAllocator());
		rejected();
		host.RemoveMember("service_limits");
		host.AddMember("hostname", "bad\rhost", host.GetAllocator());
		rejected();
		host.RemoveMember("hostname");
		host.AddMember("heap_bytes", 1024, host.GetAllocator());
		rejected();
		host.RemoveMember("heap_bytes");
		// The mirror's own fields.
		host["mirror"].AddMember("answer_bytes", std::uint64_t{1} << 33, host.GetAllocator());
		rejected();
		host["mirror"]["answer_bytes"].SetUint64(std::uint64_t{1} << 30);
		check(load().heap_bytes == options.control_bytes + options.baseline_bytes + (std::uint64_t{1} << 30));
		host["mirror"].RemoveMember("answer_bytes");
		host["mirror"]["namespaces"].PushBack("aa", host.GetAllocator());
		rejected();
		host["mirror"]["namespaces"].PopBack();
		host["mirror"]["upstreams"].SetArray();
		rejected();
		host["mirror"]["upstreams"].PushBack(rapidjson::Value(rapidjson::kObjectType), host.GetAllocator());
		rejected();
		host["mirror"]["upstreams"][0].AddMember("address", "127.0.0.1", host.GetAllocator());
		host["mirror"]["upstreams"][0].AddMember("port", 8317, host.GetAllocator());
		// The upstream token is a secret: required in the secrets file, refused in the configuration.
		check(load().mirror.upstream.token == "upstream-token-0123456789abcdef0123456789");
		secrets.RemoveMember("upstream_token");
		rejected();
		secrets.AddMember("upstream_token", "upstream-token-0123456789abcdef0123456789", secrets.GetAllocator());
		host["mirror"].AddMember("token", "upstream-token-0123456789abcdef0123456789", host.GetAllocator());
		rejected();
		host["mirror"].RemoveMember("token");
		host.AddMember("port", 65536, host.GetAllocator());
		rejected();
		host["port"].SetInt(0); // binds a free port
		check(load().endpoint.port() == 0);
		host.RemoveMember("port");
		rapidjson::Value saved_credentials(secrets["credentials"], secrets.GetAllocator());
		secrets["credentials"].SetArray();
		rejected();
		// The secrets file is private: group or other access, a link or a missing file is refused; the
		// configuration may be group-readable but not writable by others.
		secrets["credentials"] = saved_credentials;
		check(bool(load().credentials.size()));
		for (const auto mode : {std::filesystem::perms::group_read, std::filesystem::perms::others_read,
								std::filesystem::perms::group_write}) {
			std::filesystem::permissions(directory / "secrets.json", std::filesystem::perms::owner_read |
																		 std::filesystem::perms::owner_write | mode);
			bool failed = false;
			try {
				detent::relay::load_host_options(configuration);
			} catch (const std::invalid_argument &) {
				failed = true;
			}
			check(failed);
		}
		std::filesystem::permissions(directory / "secrets.json",
									 std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
		check(bool(load().credentials.size()));
		std::filesystem::permissions(configuration, std::filesystem::perms::owner_read |
														std::filesystem::perms::owner_write |
														std::filesystem::perms::group_read |
														std::filesystem::perms::others_read);
		detent::relay::load_host_options(configuration);
		std::filesystem::permissions(configuration, std::filesystem::perms::owner_read |
														std::filesystem::perms::owner_write |
														std::filesystem::perms::group_write);
		bool writable = false;
		try {
			detent::relay::load_host_options(configuration);
		} catch (const std::invalid_argument &) {
			writable = true;
		}
		check(writable);
		write(configuration, host);
		std::filesystem::remove(directory / "secrets.json");
		bool missing = false;
		try {
			detent::relay::load_host_options(configuration);
		} catch (const std::invalid_argument &) {
			missing = true;
		}
		check(missing);
		std::filesystem::remove_all(directory);
		std::cout << "mirror-only relay, namespace grants, TLS and budget configuration passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
