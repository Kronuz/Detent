#include "detent_relay/configuration.hh"
#include "detent/config_trust_internal.hh"
#include "detent/json_schema_internal.hh"
#include "detent/scope.hh"
#include <rapidjson/document.h>
#include <reactor_tls.h>
#include <set>
#include <sha256.h>

namespace detent::relay {
namespace {
using Value = rapidjson::Value;
constexpr std::size_t maximum_heap_bytes = std::min<std::uint64_t>(SIZE_MAX, 1ull << 40);
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
distribution::Identity identity(const Value &value) {
	const auto hex = text(value, 32);
	distribution::Identity out{};
	auto digit = [](char c) {
		require((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'), "invalid relay identity digit");
		return c <= '9' ? c - '0' : c - 'a' + 10;
	};
	require(hex.size() == 32, "a relay identity is 32 hex digits");
	for (std::size_t i = 0; i < out.size(); ++i)
		out[i] = static_cast<char>(digit(hex[2 * i]) << 4 | digit(hex[2 * i + 1]));
	require(out != distribution::Identity{}, "zero relay identity");
	return out;
}
// The relay as a W8 reader: its mirror directory, the names it mirrors and the upstream it reads
// (the authority's voters or another relay), with a credential and no consensus.
ConfiguredMirror mirror(const Value &object, const std::filesystem::path &base, const std::string &ca) {
	fields(object, {"directory", "cluster", "namespaces", "upstreams", "hostname", "ca", "token", "interval_ms",
					"timeout_ms", "compression", "sessions", "answer_bytes"});
	ConfiguredMirror result;
	auto &service = result.service;
	service.directory = path(field(object, "directory"), base);
	service.cluster = identity(field(object, "cluster"));
	const auto &names = field(object, "namespaces");
	require(names.IsArray() && !names.Empty() && names.Size() <= 4096, "relay mirror namespace count");
	for (const auto &name : names.GetArray()) {
		auto value = text(name, 198);
		require(distribution::valid_namespace_name(value) &&
					(service.namespaces.empty() || service.namespaces.back() < value),
				"relay mirror namespaces must be valid names in increasing order");
		service.namespaces.push_back(std::move(value));
	}
	service.interval = std::chrono::milliseconds(number(object, "interval_ms", 250, 1, 60000));
	service.sessions = number(object, "sessions", service.sessions, 1, 65536);
	result.answer_bytes = number(object, "answer_bytes", result.answer_bytes, 1024 * 1024, 1ull << 32);
	auto &upstream = result.upstream;
	const auto &upstreams = field(object, "upstreams");
	require(upstreams.IsArray() && !upstreams.Empty() && upstreams.Size() <= 64, "relay mirror upstream count");
	for (const auto &value : upstreams.GetArray()) {
		fields(value, {"address", "port"});
		upstream.endpoints.emplace_back(asio::ip::make_address(text(field(value, "address"), 253)),
										number(value, "port", 0, 1, 65535));
	}
	upstream.hostname = object.HasMember("hostname") ? text(object["hostname"], 253) : "localhost";
	require(upstream.hostname.find_first_of("\r\n") == std::string::npos, "invalid relay upstream hostname");
	upstream.token = text(field(object, "token"), 128);
	require(upstream.token.find_first_of("\r\n") == std::string::npos, "invalid relay upstream token");
	upstream.timeout = std::chrono::milliseconds(number(object, "timeout_ms", 10000, 1, 600000));
	if (object.HasMember("compression")) {
		require(object["compression"].IsBool(), "relay mirror compression is a boolean");
		upstream.compression = object["compression"].GetBool();
	}
	upstream.tls = std::make_shared<asio::ssl::context>(asio::ssl::context::tls);
	reactor::tls::configure(*upstream.tls,
							{"", "", object.HasMember("ca") ? path(object["ca"], base).string() : ca},
							reactor::tls::Role::Client);
	return result;
}
} // namespace
HostOptions load_host_options(const std::filesystem::path &configuration) {
	auto bytes = detent::internal::read_private_configuration(configuration);
	require(bytes.find('\0') == std::string::npos && !detent::internal::nesting_depth_exceeds(bytes, 16),
			"relay configuration JSON bound");
	rapidjson::Document object;
	object.Parse<rapidjson::kParseIterativeFlag | rapidjson::kParseValidateEncodingFlag>(bytes.data(),
																						 bytes.size());
	require(!object.HasParseError(), "invalid relay configuration JSON");
	fields(object, {"address", "port", "hostname", "certificate", "private_key", "ca", "mirror", "credentials",
					"control_bytes", "baseline_bytes", "heap_bytes", "connections", "service_limits"});
	HostOptions result;
	result.connections = number(object, "connections", result.connections, 1, 65536);
	if (object.HasMember("service_limits")) {
		const auto &value = object["service_limits"];
		fields(value, {"opens", "polls", "closes"});
		auto &limits = result.service_limits;
		limits.opens = number(value, "opens", limits.opens, 1, 65536);
		limits.polls = number(value, "polls", limits.polls, 1, 65536);
		limits.closes = number(value, "closes", limits.closes, 1, 65536);
	}
	const auto base = std::filesystem::absolute(configuration).parent_path();
	if (object.HasMember("address"))
		result.endpoint.address(asio::ip::make_address(text(object["address"], 253)));
	result.endpoint.port(number(object, "port", 8318, 0, 65535));
	if (object.HasMember("hostname"))
		result.hostname = text(object["hostname"], 253);
	require(result.hostname.find_first_of("\r\n") == std::string::npos, "invalid relay TLS hostname");
	result.certificate_authority = path(field(object, "ca"), base).string();
	result.control_bytes =
		number(object, "control_bytes", result.control_bytes, 16 * 1024 * 1024, 1ull << 30);
	result.baseline_bytes = number(object, "baseline_bytes", result.baseline_bytes, 1024 * 1024, 1ull << 30);
	result.heap_bytes = result.control_bytes + result.baseline_bytes;
	result.mirror = mirror(field(object, "mirror"), base, result.certificate_authority);
	require(result.mirror.answer_bytes <= maximum_heap_bytes - result.heap_bytes,
			"relay aggregate heap funding exceeds host limit");
	result.heap_bytes += result.mirror.answer_bytes;
	const auto minimum_heap = result.heap_bytes;
	result.heap_bytes = number(object, "heap_bytes", minimum_heap, minimum_heap, maximum_heap_bytes);
	const auto &credentials = field(object, "credentials");
	require(credentials.IsArray() && !credentials.Empty() && credentials.Size() <= 10000,
			"relay credential count");
	std::set<std::array<unsigned char, 32>> tokens;
	for (const auto &value : credentials.GetArray()) {
		fields(value, {"token", "principal", "policy_epoch", "namespaces"});
		const auto token = text(field(value, "token"), 128);
		require(token.size() >= 32 && std::all_of(token.begin(), token.end(),
												  [](unsigned char c) { return c >= 32 && c != 127; }),
				"invalid relay token");
		ConfiguredCredential credential;
		class SHA256 hash;
		hash.add(token.data(), token.size());
		hash.getHash(credential.token_hash.data());
		require(tokens.insert(credential.token_hash).second, "duplicate relay credential token");
		credential.principal = {distribution::canonical_scope_identity(text(field(value, "principal"), 128), 1, {}),
								number(value, "policy_epoch", 1, 1, UINT64_MAX)};
		// W13 patterns for sessions and the history routes.
		const auto &patterns = field(value, "namespaces");
		require(patterns.IsArray() && !patterns.Empty() &&
					patterns.Size() <= distribution::NamespaceGrants::maximum_patterns,
				"relay namespace grants");
		std::vector<std::string> owned;
		for (const auto &pattern : patterns.GetArray())
			owned.push_back(text(pattern, 200));
		const std::vector<std::string_view> views(owned.begin(), owned.end());
		credential.namespaces = std::make_shared<const distribution::NamespaceGrants>(views);
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
