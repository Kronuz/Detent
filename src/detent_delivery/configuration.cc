#include "detent_delivery/configuration.hh"
#include "detent/identity.hh"
#include "detent/json_schema_internal.hh"
#include "detent/scope.hh"
#include "source_selection.hh"
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <openssl/rand.h>
#include <rapidjson/document.h>
#include <set>
#include <sys/stat.h>
#include <unistd.h>

namespace detent::delivery {
namespace {
using Value = rapidjson::Value;
void require(bool value, const char *message) {
	if (!value)
		throw std::invalid_argument(message);
}
struct File {
	int descriptor;
	~File() {
		if (descriptor >= 0)
			::close(descriptor);
	}
};
void fields(const Value &object, std::initializer_list<std::string_view> allowed) {
	require(object.IsObject(), "managed configuration requires an object");
	std::set<std::string_view> seen;
	for (auto it = object.MemberBegin(); it != object.MemberEnd(); ++it) {
		std::string_view name(it->name.GetString(), it->name.GetStringLength());
		require(seen.insert(name).second && std::find(allowed.begin(), allowed.end(), name) != allowed.end(),
				"unknown or duplicate managed configuration field");
	}
}
const Value &field(const Value &object, const char *name) {
	require(object.HasMember(name), "missing managed configuration field");
	return object[name];
}
std::string text(const Value &value, std::size_t maximum = 4096) {
	require(value.IsString() && value.GetStringLength() && value.GetStringLength() <= maximum,
			"invalid managed configuration string");
	std::string result(value.GetString(), value.GetStringLength());
	require(result.find('\0') == std::string::npos, "NUL in managed configuration");
	return result;
}
std::uint64_t number(const Value &object, const char *name, std::uint64_t fallback, std::uint64_t minimum,
					 std::uint64_t maximum) {
	if (!object.HasMember(name))
		return fallback;
	const auto &value = object[name];
	require(value.IsUint64() && value.GetUint64() >= minimum && value.GetUint64() <= maximum,
			"managed configuration integer out of range");
	return value.GetUint64();
}
template <std::size_t N> std::array<char, N> identity(const Value &value) {
	return detent::distribution::parse_identity_hex<N>(text(value, N * 2));
}

} // namespace
std::string read_private_configuration(const std::filesystem::path &configuration) {
	File file{::open(configuration.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)};
	struct stat info{};
	require(file.descriptor >= 0 && ::fstat(file.descriptor, &info) == 0 && S_ISREG(info.st_mode) &&
				(info.st_uid == 0 || info.st_uid == ::geteuid()) && info.st_nlink == 1 &&
				!(info.st_mode & 0027) && info.st_size > 0 && info.st_size <= 1024 * 1024,
			"managed credential configuration ownership, permissions or size");
	std::string bytes(static_cast<std::size_t>(info.st_size), '\0');
	std::size_t offset = 0;
	while (offset != bytes.size()) {
		auto count = ::read(file.descriptor, bytes.data() + offset, bytes.size() - offset);
		if (count < 0 && errno == EINTR)
			continue;
		require(count > 0, "short managed configuration");
		offset += count;
	}
	char extra;
	ssize_t remaining;
	do {
		remaining = ::read(file.descriptor, &extra, 1);
	} while (remaining < 0 && errno == EINTR);
	require(remaining == 0, "managed configuration changed while reading");
	return bytes;
}

ManagedOptions load_managed_options(const std::filesystem::path &configuration,
									const std::filesystem::path &directory, detent::local::StoreOpenMode mode,
									bool system) {
	auto bytes = read_private_configuration(configuration);
	require(bytes.find('\0') == std::string::npos && !detent::internal::nesting_depth_exceeds(bytes, 16),
			"managed configuration JSON bound");
	rapidjson::Document object;
	object.Parse<rapidjson::kParseIterativeFlag | rapidjson::kParseValidateEncodingFlag>(bytes.data(),
																						 bytes.size());
	require(!object.HasParseError(), "invalid managed configuration JSON");
	fields(object, {"cluster",
					"group",
					"epoch",
					"principal",
					"policy_epoch",
					"namespaces",
					"token",
					"address",
					"authority_endpoints",
					"quorum_refresh_ms",
					"port",
					"hostname",
					"ca",
					"heap_bytes",
					"mapping_bytes",
					"artifact_mode",
					"disk_bytes",
					"disk_entries",
					"disk_generations",
					"poll_interval_ms",
					"reconnect_maximum_ms",
					"delivery_mode",
					"image_format",
					"compaction_interval_ms",
					"timeout_ms",
					"compression",
					"reset_from",
					"image_limits"});
	ManagedOptions result;
	result.coordinator.directory = directory;
	if (object.HasMember("delivery_mode")) {
		result.delivery_mode_explicit = true;
		const auto mode = text(object["delivery_mode"], 16);
		require(mode == "poll" || mode == "subscription", "invalid managed delivery mode");
		result.coordinator.delivery_mode = mode == "subscription"
											   ? detent::delivery::DeliveryMode::Subscription
											   : detent::delivery::DeliveryMode::Poll;
	}
	if (object.HasMember("image_format")) {
		const auto format = text(object["image_format"], 16);
		require(format == "flat-v1" || format == "paged-v2", "invalid managed image format");
		result.coordinator.image_format =
			format == "paged-v2" ? LocalImageFormat::PagedV2 : LocalImageFormat::FlatV1;
	}
	const auto interval = number(object, "compaction_interval_ms", 60000, 0, 86400000);
	require(interval == 0 || interval >= 1000,
			"managed compaction interval must be zero or at least 1000 ms");
	result.coordinator.compaction_interval = std::chrono::milliseconds(interval);
	auto &source = result.coordinator.source;
	source.configured.cluster = identity<16>(field(object, "cluster"));
	source.configured.group = identity<16>(field(object, "group"));
	source.configured.restore_epoch = identity<16>(field(object, "epoch"));
	result.principal = text(field(object, "principal"), 128);
	result.policy_epoch = number(object, "policy_epoch", 1, 1, UINT64_MAX);
	const auto &names = field(object, "namespaces");
	require(names.IsArray() && names.Size() <= 4096, "managed namespace count");
	for (const auto &name : names.GetArray())
		source.namespaces.push_back(text(name, 198));
	std::sort(source.namespaces.begin(), source.namespaces.end());
	source.namespaces.erase(std::unique(source.namespaces.begin(), source.namespaces.end()),
							source.namespaces.end());
	std::vector<std::string_view> canonical;
	for (const auto &name : source.namespaces)
		canonical.push_back(name);
	source.configured.scope =
		detent::distribution::canonical_scope_identity(result.principal, result.policy_epoch, canonical);
	source.token = text(field(object, "token"), 128);
	require(source.token.size() >= 32 && std::all_of(source.token.begin(), source.token.end(),
													 [](unsigned char c) { return c >= 32 && c != 127; }),
			"invalid managed credential token");
	const auto address = object.HasMember("address") ? text(object["address"], 253) : "127.0.0.1";
	source.endpoint = {asio::ip::make_address(address),
					   static_cast<unsigned short>(number(object, "port", 8317, 1, 65535))};
	if (object.HasMember("authority_endpoints")) {
		require(!object.HasMember("address") && !object.HasMember("port"),
				"authority endpoint pool conflicts with address/port");
		const auto &endpoints = object["authority_endpoints"];
		require(endpoints.IsArray() && !endpoints.Empty() &&
					endpoints.Size() <= detail::SourceSelection::maximum_endpoints,
				"invalid authority endpoint count");
		for (const auto &endpoint : endpoints.GetArray()) {
			fields(endpoint, {"address", "port"});
			result.coordinator.authority_endpoints.emplace_back(
				asio::ip::make_address(text(field(endpoint, "address"), 253)),
				static_cast<unsigned short>(number(endpoint, "port", 8317, 1, 65535)));
		}
		source.endpoint = result.coordinator.authority_endpoints.front();
	} else
		require(!object.HasMember("quorum_refresh_ms"), "quorum refresh requires authority endpoints");
	result.coordinator.quorum_refresh =
		std::chrono::milliseconds(number(object, "quorum_refresh_ms", 30000, 1000, 86400000));
	const detail::SourceSelection validated(result.coordinator.authority_endpoints, source.endpoint,
											result.coordinator.quorum_refresh, 0);
	source.hostname = object.HasMember("hostname") ? text(object["hostname"], 253) : "localhost";
	require(source.hostname.find_first_of("\r\n") == std::string::npos, "invalid managed TLS hostname");
	auto ca = std::filesystem::path(text(field(object, "ca")));
	if (ca.is_relative())
		ca = configuration.parent_path() / ca;
	source.tls_context = std::make_shared<asio::ssl::context>(asio::ssl::context::tls);
	reactor::tls::configure(*source.tls_context, {"", "", ca.string()}, reactor::tls::Role::Client);
	source.timeout = std::chrono::milliseconds(number(object, "timeout_ms", 60000, 1, 86400000));
	result.coordinator.poll_interval =
		std::chrono::milliseconds(number(object, "poll_interval_ms", 200, 1, 86400000));
	result.coordinator.reconnect_maximum = std::chrono::milliseconds(
		number(object, "reconnect_maximum_ms",
			   std::max<std::int64_t>(30000, result.coordinator.poll_interval.count()),
			   result.coordinator.poll_interval.count(), 86400000));
	require(RAND_bytes(reinterpret_cast<unsigned char *>(&result.coordinator.reconnect_seed),
					   sizeof(result.coordinator.reconnect_seed)) == 1,
			"managed reconnect entropy unavailable");
	if (object.HasMember("compression")) {
		require(object["compression"].IsBool(), "managed compression requires a boolean");
		source.compression = object["compression"].GetBool();
	}
	result.heap_bytes = number(object, "heap_bytes", result.heap_bytes, 1024 * 1024, SIZE_MAX);
	result.mapping_bytes =
		number(object, "mapping_bytes", result.mapping_bytes, 4096, SIZE_MAX);
	result.disk.limits = {number(object, "disk_bytes", 1024ull * 1024 * 1024, 1024, 1ull << 40),
						  number(object, "disk_entries", 4096, 4, 1000000),
						  number(object, "disk_generations", 1024, 1, 1000000)};
	if (object.HasMember("image_limits")) {
		const auto &limits = field(object, "image_limits");
		fields(limits, {"records", "namespaces", "wire_bytes", "string_bytes", "decoded_bytes",
					   "image_bytes", "pages", "segments", "overrides", "definition_bytes"});
		auto &wire = source.limits;
		auto &view = result.coordinator.view_limits;
		wire.records = number(limits, "records", wire.records, 1, UINT32_MAX);
		wire.namespaces = number(limits, "namespaces", wire.namespaces, 1, 65536);
		wire.bytes = number(limits, "wire_bytes", wire.bytes, 1, SIZE_MAX);
		wire.string_bytes = number(limits, "string_bytes", wire.string_bytes, 1, SIZE_MAX);
		wire.storage_bytes = number(limits, "decoded_bytes", wire.storage_bytes, 1, result.heap_bytes);
		wire.overrides_per_definition = number(limits, "overrides", wire.overrides_per_definition, 0, 256);
		wire.definition_bytes = number(limits, "definition_bytes", wire.definition_bytes, 9, 16 * 1024);
		view.records = wire.records;
		view.namespaces = wire.namespaces;
		view.strings = wire.string_bytes;
		view.overrides = wire.overrides_per_definition;
		view.bytes = number(limits, "image_bytes", view.bytes, 4096, result.mapping_bytes);
		view.pages = number(limits, "pages", view.pages, 1, UINT64_MAX);
		view.segments = number(limits, "segments", view.segments, 1, SIZE_MAX);
		(void)maximum_response_wire_bytes(wire.bytes);
	}
	result.disk.mode = mode;
	result.disk.artifact_mode = number(object, "artifact_mode", system ? 0644 : 0600, 0, 0777);
	require(result.disk.artifact_mode == 0600 || result.disk.artifact_mode == 0640 ||
				result.disk.artifact_mode == 0644,
			"managed artifact_mode must be 384, 416 or 420");
	if (object.HasMember("reset_from")) {
		const auto &old = object["reset_from"];
		fields(old, {"cluster", "group", "epoch", "scope"});
		detent::distribution::Cursor cursor;
		cursor.cluster = identity<16>(field(old, "cluster"));
		cursor.group = identity<16>(field(old, "group"));
		cursor.restore_epoch = identity<16>(field(old, "epoch"));
		cursor.scope = identity<32>(field(old, "scope"));
		require(!cursor.same_identity(source.configured), "managed reset must change identity");
		result.coordinator.reset_from = cursor;
	}
	return result;
}
} // namespace detent::delivery
