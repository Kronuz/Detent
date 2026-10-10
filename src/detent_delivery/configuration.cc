#include "detent_delivery/configuration.hh"
#include "detent/identity.hh"
#include "detent/json_schema_internal.hh"
#include "detent/scope.hh"
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
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
					"delivery_mode",
					"timeout_ms",
					"compression",
					"reset_from"});
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
	source.hostname = object.HasMember("hostname") ? text(object["hostname"], 253) : "localhost";
	require(source.hostname.find_first_of("\r\n") == std::string::npos, "invalid managed TLS hostname");
	auto ca = std::filesystem::path(text(field(object, "ca")));
	if (ca.is_relative())
		ca = configuration.parent_path() / ca;
	source.tls_context = std::make_shared<asio::ssl::context>(asio::ssl::context::tls);
	detent::transport::configure_tls(*source.tls_context, {"", "", ca.string()},
									 detent::transport::TlsRole::PublisherClient);
	source.timeout = std::chrono::milliseconds(number(object, "timeout_ms", 60000, 1, 86400000));
	result.coordinator.poll_interval =
		std::chrono::milliseconds(number(object, "poll_interval_ms", 200, 1, 86400000));
	if (object.HasMember("compression")) {
		require(object["compression"].IsBool(), "managed compression requires a boolean");
		source.compression = object["compression"].GetBool();
	}
	result.heap_bytes = number(object, "heap_bytes", result.heap_bytes, 1024 * 1024, 1024ull * 1024 * 1024);
	result.mapping_bytes =
		number(object, "mapping_bytes", result.mapping_bytes, 4096, 4ull * 1024 * 1024 * 1024);
	result.disk.limits = {number(object, "disk_bytes", 1024ull * 1024 * 1024, 1024, 1ull << 40),
						  number(object, "disk_entries", 4096, 4, 1000000),
						  number(object, "disk_generations", 1024, 1, 1000000)};
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
