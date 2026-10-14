#include "detent_relay/configuration.hh"
#include "detent/json_schema_internal.hh"
#include "detent/scope.hh"
#include <rapidjson/document.h>
#include <set>
#include <sha256.h>

namespace detent::relay {
namespace {
using Value = rapidjson::Value;
void require(bool value, const char *message) {
	if (!value)
		throw std::invalid_argument(message);
}
void fields(const Value &object, std::initializer_list<std::string_view> allowed) {
	require(object.IsObject(), "relay configuration requires an object");
	std::set<std::string_view> seen;
	for (auto it = object.MemberBegin(); it != object.MemberEnd(); ++it) {
		std::string_view name(it->name.GetString(), it->name.GetStringLength());
		require(seen.insert(name).second && std::find(allowed.begin(), allowed.end(), name) != allowed.end(),
				"unknown or duplicate relay configuration field");
	}
}
const Value &field(const Value &object, const char *name) {
	require(object.HasMember(name), "missing relay configuration field");
	return object[name];
}
std::string text(const Value &value, std::size_t maximum = 4096) {
	require(value.IsString() && value.GetStringLength() && value.GetStringLength() <= maximum,
			"invalid relay configuration string");
	std::string result(value.GetString(), value.GetStringLength());
	require(result.find('\0') == std::string::npos, "NUL in relay configuration");
	return result;
}
std::uint64_t number(const Value &object, const char *name, std::uint64_t fallback, std::uint64_t minimum,
					 std::uint64_t maximum) {
	if (!object.HasMember(name))
		return fallback;
	const auto &value = object[name];
	require(value.IsUint64() && value.GetUint64() >= minimum && value.GetUint64() <= maximum,
			"relay configuration integer out of range");
	return value.GetUint64();
}
std::filesystem::path path(const Value &value, const std::filesystem::path &base) {
	auto result = std::filesystem::path(text(value));
	return (result.is_absolute() ? result : base / result).lexically_normal();
}
} // namespace
HostOptions load_host_options(const std::filesystem::path &configuration, bool system) {
	auto bytes = delivery::read_private_configuration(configuration);
	require(bytes.find('\0') == std::string::npos && !detent::internal::nesting_depth_exceeds(bytes, 16),
			"relay configuration JSON bound");
	rapidjson::Document object;
	object.Parse<rapidjson::kParseIterativeFlag | rapidjson::kParseValidateEncodingFlag>(bytes.data(),
																						 bytes.size());
	require(!object.HasParseError(), "invalid relay configuration JSON");
	fields(object, {"address", "port", "hostname", "certificate", "private_key", "ca", "bundles",
					"credentials", "control_bytes", "baseline_bytes", "heap_bytes"});
	HostOptions result;
	const auto base = std::filesystem::absolute(configuration).parent_path();
	if (object.HasMember("address"))
		result.endpoint.address(asio::ip::make_address(text(object["address"], 253)));
	result.endpoint.port(number(object, "port", 8318, 1, 65535));
	if (object.HasMember("hostname"))
		result.hostname = text(object["hostname"], 253);
	require(result.hostname.find_first_of("\r\n") == std::string::npos, "invalid relay TLS hostname");
	result.certificate_authority = path(field(object, "ca"), base).string();
	result.control_bytes =
		number(object, "control_bytes", result.control_bytes, 16 * 1024 * 1024, 1ull << 30);
	result.baseline_bytes = number(object, "baseline_bytes", result.baseline_bytes, 1024 * 1024, 1ull << 30);
	result.heap_bytes = result.control_bytes + result.baseline_bytes;
	const auto &bundles = field(object, "bundles");
	require(bundles.IsArray() && !bundles.Empty() && bundles.Size() <= 256, "relay bundle count");
	std::set<std::filesystem::path> directories;
	for (const auto &value : bundles.GetArray()) {
		fields(value, {"source", "directory", "mode", "cache_bytes", "retained_edges"});
		auto directory = path(field(value, "directory"), base);
		require(directories.insert(std::filesystem::weakly_canonical(directory)).second,
				"duplicate relay durable directory");
		auto mode = local::StoreOpenMode::ExistingManaged;
		if (value.HasMember("mode")) {
			const auto configured = text(value["mode"], 16);
			require(configured == "existing" || configured == "fresh", "invalid relay store mode");
			if (configured == "fresh")
				mode = local::StoreOpenMode::FreshEmpty;
		}
		ConfiguredBundle bundle;
		bundle.source =
			delivery::load_managed_options(path(field(value, "source"), base), directory, mode, system);
		require(!bundle.source.delivery_mode_explicit ||
					bundle.source.coordinator.delivery_mode == delivery::DeliveryMode::Subscription,
				"relay sources require subscription delivery");
		bundle.source.coordinator.delivery_mode = delivery::DeliveryMode::Subscription;
		bundle.source.disk.artifact_mode = 0600;
		bundle.cache_bytes = number(value, "cache_bytes", bundle.cache_bytes, 1024 * 1024, 1ull << 30);
		bundle.retained_edges = number(value, "retained_edges", bundle.retained_edges, 1, 256);
		for (const auto &prior : result.bundles)
			require(!prior.source.coordinator.source.configured.same_identity(
						bundle.source.coordinator.source.configured),
					"duplicate relay bundle identity");
		result.heap_bytes += bundle.source.heap_bytes + bundle.cache_bytes;
		result.bundles.push_back(std::move(bundle));
	}
	const auto minimum_heap = result.heap_bytes;
	result.heap_bytes = number(object, "heap_bytes", minimum_heap, minimum_heap, 1ull << 40);
	const auto &credentials = field(object, "credentials");
	require(credentials.IsArray() && !credentials.Empty() && credentials.Size() <= 10000,
			"relay credential count");
	std::set<std::array<unsigned char, 32>> tokens;
	for (const auto &value : credentials.GetArray()) {
		fields(value, {"token", "principal", "policy_epoch", "bundles"});
		const auto token = text(field(value, "token"), 128);
		require(token.size() >= 32 && std::all_of(token.begin(), token.end(),
												  [](unsigned char c) { return c >= 32 && c != 127; }),
				"invalid relay token");
		ConfiguredCredential credential;
		class SHA256 hash;
		hash.add(token.data(), token.size());
		hash.getHash(credential.token_hash.data());
		require(tokens.insert(credential.token_hash).second, "duplicate relay credential token");
		const auto principal = text(field(value, "principal"), 128);
		const auto policy = number(value, "policy_epoch", 1, 1, UINT64_MAX);
		credential.authorization.principal = {distribution::canonical_scope_identity(principal, 1, {}),
											  policy};
		const auto &grants = field(value, "bundles");
		require(grants.IsArray() && !grants.Empty() && grants.Size() <= result.bundles.size(),
				"relay bundle grants");
		for (const auto &grant : grants.GetArray()) {
			require(grant.IsUint() && grant.GetUint() < result.bundles.size(), "invalid relay bundle grant");
			const auto index = grant.GetUint();
			const auto &source = result.bundles[index].source;
			require(source.principal == principal && source.policy_epoch == policy &&
						!credential.authorization.allows(index),
					"relay grant principal, policy or duplicate mismatch");
			credential.authorization.bundles[index / 64] |= std::uint64_t{1} << (index % 64);
		}
		class SHA256 fingerprint;
		constexpr std::string_view domain = "detent-relay-bundle-grants-v1";
		fingerprint.add(domain.data(), domain.size());
		for (std::size_t index = 0; index < result.bundles.size(); ++index)
			if (credential.authorization.allows(index)) {
				const auto &cursor = result.bundles[index].source.coordinator.source.configured;
				fingerprint.add(cursor.cluster.data(), cursor.cluster.size());
				fingerprint.add(cursor.group.data(), cursor.group.size());
				fingerprint.add(cursor.restore_epoch.data(), cursor.restore_epoch.size());
				fingerprint.add(cursor.scope.data(), cursor.scope.size());
			}
		unsigned char digest[32];
		fingerprint.getHash(digest);
		std::copy_n(reinterpret_cast<const char *>(digest), 32, credential.authorization.grants.begin());
		result.credentials.push_back(std::move(credential));
	}
	std::sort(result.credentials.begin(), result.credentials.end(),
			  [](const auto &a, const auto &b) { return a.token_hash < b.token_hash; });
	result.tls_context = std::make_shared<asio::ssl::context>(asio::ssl::context::tls);
	reactor::tls::configure(*result.tls_context,
							 {path(field(object, "certificate"), base).string(),
							  path(field(object, "private_key"), base).string(),
							  path(field(object, "ca"), base).string()},
							 reactor::tls::Role::Server);
	return result;
}
} // namespace detent::relay
