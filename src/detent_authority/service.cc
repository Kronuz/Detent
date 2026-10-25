#include "service.hh"
#include "delivery_export.hh"
#include "detent/json_schema.hh"
#include "detent/json_schema_internal.hh"
#include "detent/scope.hh"
#include "detent/validation.hh"
#include "detent_distribution/memory_resource.hh"
#include "detent_distribution_http/reply_pump.hh"
#include "prepared.hh"
#include "read_reply.hh"
#include <asio.hpp>
#include <fcntl.h>
#include <http_owned_stream.h>
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__linux__)
#include <completion/asio_completion.h>
#include <completion/native_completion.h>
#include <journal/mutation.h>
#endif
#include <completion/posix.h>
#include <fstream>
#include <limits>
#include <metrics.hh>
#include <random>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <sha256.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace detent::authority {
namespace {
using Clock = std::chrono::steady_clock;
std::uint64_t elapsed_ns(Clock::time_point start, Clock::time_point end = Clock::now()) {
	auto value = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
	return value > 0 ? static_cast<std::uint64_t>(value) : 0;
}
std::optional<std::uint64_t> owner_cpu_ns() noexcept {
#ifdef CLOCK_THREAD_CPUTIME_ID
	timespec value{};
	if (::clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value) || value.tv_sec < 0 || value.tv_nsec < 0 ||
		value.tv_nsec >= 1000000000)
		return std::nullopt;
	auto seconds = static_cast<std::uint64_t>(value.tv_sec);
	auto nanoseconds = static_cast<std::uint64_t>(value.tv_nsec);
	if (seconds > (std::numeric_limits<std::uint64_t>::max() - nanoseconds) / 1000000000)
		return std::nullopt;
	return seconds * 1000000000 + nanoseconds;
#else
	return std::nullopt;
#endif
}
std::string duration_json(const kronuz::metrics::DurationSnapshot &sample) {
	std::string result = "{\"count\":" + std::to_string(sample.count) +
						 ",\"sum_ns\":" + std::to_string(sample.sum_ns) +
						 ",\"maximum_ns\":" + std::to_string(sample.maximum_ns) + ",\"buckets\":[";
	for (std::size_t i = 0; i < sample.buckets.size(); ++i) {
		if (i) {
			result += ',';
		}
		result += std::to_string(sample.buckets[i]);
	}
	return result + "]}";
}

void check(bool b, const char *message) {
	if (!b) {
		throw std::invalid_argument(message);
	}
}
using Object = rapidjson::Value;
void fields(const Object &v, std::initializer_list<std::string_view> allowed) {
	check(v.IsObject(), "expected object");
	std::set<std::string_view> seen;
	for (auto it = v.MemberBegin(); it != v.MemberEnd(); ++it) {
		std::string_view key(it->name.GetString(), it->name.GetStringLength());
		check(seen.insert(key).second && std::find(allowed.begin(), allowed.end(), key) != allowed.end(),
			  "unknown or duplicate field");
	}
}
const Object &field(const Object &v, const char *key) {
	check(v.HasMember(key), "missing field");
	return v[key];
}
std::string text(const Object &v, std::size_t cap = 200) {
	check(v.IsString() && v.GetStringLength() <= cap, "invalid string");
	auto value = std::string(v.GetString(), v.GetStringLength());
	check(value.find('\0') == std::string::npos, "NUL in string");
	return value;
}
std::uint64_t number(const Object &v) {
	check(v.IsUint64(), "invalid unsigned integer");
	return v.GetUint64();
}
std::optional<std::uint64_t> optional(const Object &v, const char *key) {
	return v.HasMember(key) ? std::optional<std::uint64_t>{number(v[key])} : std::nullopt;
}
Identity identity(std::string_view hex) {
	check(hex.size() == 32, "identity requires 32 hex characters");
	Identity id{};
	auto nibble = [](char c) {
		if (c >= '0' && c <= '9') {
			return c - '0';
		}
		if (c >= 'a' && c <= 'f') {
			return c - 'a' + 10;
		}
		throw std::invalid_argument("invalid identity hex");
	};
	for (unsigned i = 0; i < 16; ++i) {
		id[i] = static_cast<char>((nibble(hex[2 * i]) << 4) | nibble(hex[2 * i + 1]));
	}
	check(id != Identity{}, "zero identity");
	return id;
}
std::string hex(Identity id) {
	constexpr char digits[] = "0123456789abcdef";
	std::string out;
	for (unsigned char byte : id) {
		out += digits[byte >> 4];
		out += digits[byte & 15];
	}
	return out;
}
void unique_members(const rapidjson::Value &value) {
	if (value.IsObject()) {
		std::set<std::string_view> names;
		for (auto it = value.MemberBegin(); it != value.MemberEnd(); ++it) {
			check(names.insert({it->name.GetString(), it->name.GetStringLength()}).second,
				  "duplicate JSON field");
			unique_members(it->value);
		}
	} else if (value.IsArray())
		for (const auto &item : value.GetArray())
			unique_members(item);
}
rapidjson::Document document(std::string_view bytes, std::size_t cap) {
	check(bytes.size() <= cap && bytes.find('\0') == std::string_view::npos &&
			  !detent::internal::nesting_depth_exceeds(bytes, 64),
		  "JSON bound");
	rapidjson::Document d;
	d.Parse<rapidjson::kParseIterativeFlag | rapidjson::kParseValidateEncodingFlag>(bytes.data(),
																					bytes.size());
	check(!d.HasParseError() && d.IsObject(), "invalid JSON object");
	unique_members(d);
	return d;
}
std::string result_json(Result r) {
	constexpr const char *names[] = {
		"invalid",			 "applied",		   "no_change",	   "conflict",
		"unknown_session",	 "session_fenced", "out_of_order", "result_unavailable",
		"identity_conflict", "capacity",	   "exhausted"};
	return "{\"status\":\"" + std::string(names[static_cast<unsigned>(r.status)]) +
		   "\",\"revision\":" + std::to_string(r.revision) +
		   ",\"namespace_revision\":" + std::to_string(r.namespace_revision) +
		   ",\"generation\":" + std::to_string(r.generation) + ",\"sequence\":" + std::to_string(r.sequence) +
		   "}";
}
const char *read_failure(const Completion &completion, bool fenced) noexcept {
	if (completion.read && !fenced) {
		if (completion.rejection == kronuz::consensus::RejectReason::NotLeader)
			return "{\"error\":\"source_not_leader\"}";
		if (completion.rejection == kronuz::consensus::RejectReason::Busy)
			return "{\"error\":\"admission_full\"}";
	}
	return "{\"error\":\"outcome_unknown\"}";
}
int result_status(Result r) {
	switch (r.status) {
	case Status::Applied:
	case Status::NoChange:
		return 200;
	case Status::Capacity:
		return 429;
	default:
		return 409;
	}
}
class Sink final : public http::BodySink {
	std::string &body_;
	std::size_t cap_;

  public:
	Sink(std::string &body, std::size_t cap) : body_(body), cap_(cap) {}
	void write(std::string_view bytes) override {
		check(body_.size() <= cap_ && bytes.size() <= cap_ - body_.size(), "authority body bound");
		body_.append(bytes);
	}
	void end() override {}
};
} // namespace
ServiceOptions load_service_options(const std::filesystem::path &path) {
	int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0) {
		throw std::runtime_error("cannot open authority configuration");
	}
	struct Close {
		int fd;
		~Close() { ::close(fd); }
	} close{fd};
	struct stat status{};
	check(::fstat(fd, &status) == 0 && S_ISREG(status.st_mode) &&
			  (status.st_uid == geteuid() || status.st_uid == 0) && !(status.st_mode & 0007) &&
			  !(status.st_mode & 0022) && status.st_size >= 0 && status.st_size <= 1024 * 1024,
		  "authority configuration ownership or size");
	std::string bytes(static_cast<std::size_t>(status.st_size), '\0');
	std::size_t offset = 0;
	while (offset < bytes.size()) {
		auto n = ::read(fd, bytes.data() + offset, bytes.size() - offset);
		if (n <= 0) {
			throw std::runtime_error("short authority configuration");
		}
		offset += static_cast<std::size_t>(n);
	}
	auto d = document(bytes, 1024 * 1024);
	fields(d, {"cluster", "group", "epoch", "directory", "credentials", "node", "voters", "peer_tls",
			   "publisher_tls", "publisher_bind", "publisher_name", "storage_backend", "consensus_limits",
			   "delivery_limits", "subscription_limits", "application_limits", "journal_limits"});
	ServiceOptions o;
	if (d.HasMember("application_limits")) {
		const auto &limits = field(d, "application_limits");
		fields(limits, {"keys", "namespaces", "producers", "mutations", "request_bytes",
					   "state_bytes", "image_bytes", "definition_bytes", "overrides", "results"});
		auto set = [&](const char *key, std::size_t &target) {
			if (!limits.HasMember(key))
				return;
			const auto value = number(field(limits, key));
			check(value <= std::numeric_limits<std::size_t>::max(), "application limit width");
			target = static_cast<std::size_t>(value);
		};
		auto &application = o.node.application;
		set("keys", application.keys);
		set("namespaces", application.namespaces);
		set("producers", application.producers);
		set("mutations", application.mutations);
		set("request_bytes", application.request_bytes);
		set("state_bytes", application.state_bytes);
		set("image_bytes", application.image_bytes);
		set("definition_bytes", application.definition_bytes);
		set("overrides", application.overrides);
		set("results", application.results);
	}
	if (d.HasMember("journal_limits")) {
		const auto &limits = field(d, "journal_limits");
		fields(limits, {"hard_bytes", "hard_entries", "control_bytes", "control_entries",
					   "replacement_bytes", "replacement_entries"});
		auto set = [&](const char *name, std::uint64_t &target) {
			if (limits.HasMember(name))
				target = number(field(limits, name));
		};
		set("hard_bytes", o.node.storage.hard.logical_bytes);
		set("hard_entries", o.node.storage.hard.entries);
		set("control_bytes", o.node.storage.control_pool.logical_bytes);
		set("control_entries", o.node.storage.control_pool.entries);
		set("replacement_bytes", o.node.storage.replacement_pool.logical_bytes);
		set("replacement_entries", o.node.storage.replacement_pool.entries);
	}
	if (d.HasMember("subscription_limits")) {
		const auto &limits = field(d, "subscription_limits");
		fields(limits,
			   {"sessions", "per_principal", "idle_ms", "maximum_ms", "opens", "polls", "closes",
				"per_principal_controls", "per_principal_polls", "open_bytes", "control_bytes", "wait_ms"});
		auto count = [&](const char *key, std::size_t &target, std::size_t maximum) {
			if (!limits.HasMember(key))
				return;
			const auto value = number(field(limits, key));
			check(value && value <= maximum, "subscription limit bound");
			target = static_cast<std::size_t>(value);
		};
		count("sessions", o.subscriptions.sessions, 65536);
		count("per_principal", o.subscriptions.per_principal, 65536);
		count("opens", o.subscription_requests.opens, 128);
		count("polls", o.subscription_requests.polls, 512);
		count("closes", o.subscription_requests.closes, 128);
		count("per_principal_controls", o.subscription_requests.per_principal_controls, 65536);
		count("per_principal_polls", o.subscription_requests.per_principal_polls, 65536);
		count("open_bytes", o.subscription_requests.open_bytes, 1024 * 1024);
		count("control_bytes", o.subscription_requests.control_bytes, 16384);
		auto duration = [&](const char *key, std::chrono::milliseconds &target, std::uint64_t maximum) {
			if (!limits.HasMember(key))
				return;
			const auto value = number(field(limits, key));
			check(value <= maximum, "subscription duration bound");
			target = std::chrono::milliseconds(value);
		};
		duration("idle_ms", o.subscriptions.idle_lifetime, 86400000);
		duration("maximum_ms", o.subscriptions.maximum_lifetime, 86400000);
		duration("wait_ms", o.subscription_wait, 60000);
	}
	if (d.HasMember("delivery_limits")) {
		const auto &limits = field(d, "delivery_limits");
		fields(limits, {"heap_bytes", "wire_bytes", "string_bytes", "records", "namespaces",
					   "overrides", "definition_bytes", "timeout_ms"});
		if (limits.HasMember("heap_bytes"))
			o.delivery_heap_bytes = number(field(limits, "heap_bytes"));
		if (limits.HasMember("timeout_ms")) {
			const auto value = number(field(limits, "timeout_ms"));
			check(value && value <= 86400000, "authority delivery timeout bound");
			o.delivery_timeout = std::chrono::milliseconds(value);
		}
		if (limits.HasMember("wire_bytes"))
			o.delivery.bytes = number(field(limits, "wire_bytes"));
		auto set = [&](const char *name, std::uint32_t &target, std::uint64_t maximum) {
			if (!limits.HasMember(name))
				return;
			const auto value = number(field(limits, name));
			check(value && value <= maximum, "authority delivery limit bound");
			target = static_cast<std::uint32_t>(value);
		};
		set("records", o.delivery.records, UINT32_MAX);
		set("namespaces", o.delivery.namespaces, 65536);
		set("overrides", o.delivery.overrides_per_definition, 256);
		set("definition_bytes", o.delivery.definition_bytes, 16 * 1024);
		if (limits.HasMember("string_bytes"))
			o.delivery.string_bytes = number(field(limits, "string_bytes"));
	}
	if (d.HasMember("consensus_limits")) {
		const auto &limits = field(d, "consensus_limits");
		fields(limits, {"log_entries", "log_bytes"});
		if (limits.HasMember("log_entries")) {
			const auto count = number(field(limits, "log_entries"));
			check(count >= 64 && count <= o.node.consensus.log_entries, "authority log entry limit");
			o.node.consensus.log_entries = static_cast<std::size_t>(count);
		}
		if (limits.HasMember("log_bytes")) {
			const auto bytes = number(field(limits, "log_bytes"));
			check(bytes >= o.node.consensus.rpc_bytes && bytes <= o.node.consensus.log_bytes,
				  "authority log byte limit");
			o.node.consensus.log_bytes = static_cast<std::size_t>(bytes);
		}
	}
	if (d.HasMember("storage_backend"))
		o.storage_backend = text(field(d, "storage_backend"), 16);
	check(o.storage_backend == "auto" || o.storage_backend == "native" || o.storage_backend == "fallback",
		  "authority storage backend policy");
#ifndef __linux__
	check(o.storage_backend == "auto", "explicit storage backend policy currently requires Linux");
#endif
	o.node.context = {identity(text(field(d, "cluster"))), identity(text(field(d, "group"))),
					  identity(text(field(d, "epoch")))};
	o.node.configuration.cluster = o.node.context.cluster;
	o.node.configuration.configuration = o.node.context.group;
	o.node.configuration.local = 1;
	o.node.configuration.voters = {1};
	o.directory = text(field(d, "directory"), 4096);
	if (o.directory.is_relative()) {
		o.directory = path.parent_path() / o.directory;
	}
	auto tls = [&](const Object &value) {
		fields(value, {"certificate", "key", "ca"});
		auto resolve = [&](const char *key) {
			std::filesystem::path p(text(field(value, key), 4096));
			return (p.is_relative() ? path.parent_path() / p : p).string();
		};
		return reactor::tls::Options{resolve("certificate"), resolve("key"), resolve("ca")};
	};
	check(d.HasMember("node") == d.HasMember("voters"), "node and voters must be configured together");
	if (d.HasMember("voters")) {
		o.node.configuration.local = number(field(d, "node"));
		o.node.configuration.voters.clear();
		kronuz::consensus::transport::PeerOptions peers;
		peers.tls = tls(field(d, "peer_tls"));
		auto &voters = field(d, "voters");
		check(voters.IsArray() && voters.Size() >= 3 && voters.Size() <= o.node.consensus.voters &&
				  voters.Size() % 2,
			  "fixed voter count");
		std::set<asio::ip::tcp::endpoint> addresses;
		for (const auto &voter : voters.GetArray()) {
			fields(voter, {"id", "host", "port", "certificate_sha256"});
			auto id = number(field(voter, "id")), port = number(field(voter, "port"));
			check(id && port && port <= 65535, "voter ID or port");
			auto address = asio::ip::make_address(text(field(voter, "host")));
			asio::ip::tcp::endpoint endpoint(address, static_cast<unsigned short>(port));
			check(!address.is_unspecified() && !address.is_multicast() && addresses.insert(endpoint).second,
				  "voter address");
			auto hex = text(field(voter, "certificate_sha256"));
			check(hex.size() == 64, "certificate fingerprint length");
			kronuz::consensus::transport::Fingerprint fingerprint;
			auto nibble = [](char c) -> unsigned {
				if (c >= '0' && c <= '9')
					return c - '0';
				if (c >= 'a' && c <= 'f')
					return c - 'a' + 10;
				if (c >= 'A' && c <= 'F')
					return c - 'A' + 10;
				throw std::invalid_argument("certificate fingerprint hex");
			};
			for (unsigned i = 0; i != fingerprint.size(); ++i)
				fingerprint[i] =
					static_cast<unsigned char>((nibble(hex[2 * i]) << 4) | nibble(hex[2 * i + 1]));
			check(peers.pins.emplace(id, fingerprint).second, "duplicate voter");
			o.node.configuration.voters.push_back(id);
			if (id == o.node.configuration.local)
				peers.listen = endpoint;
			else
				peers.addresses.emplace(id, endpoint);
		}
		check(peers.listen.port(), "local voter missing");
		o.peers = std::move(peers);
		// Production elections must cover durable vote/term barriers as well as
		// network latency. The Core defaults are intended for small fixtures.
		o.node.timing.heartbeat = 100;
		o.node.timing.rpc_timeout = 500;
		o.node.timing.election_min = 1000;
		o.node.timing.election_max = 2000;
		o.publisher_tls = tls(field(d, "publisher_tls"));
	} else {
		check(!d.HasMember("peer_tls"), "peer TLS requires fixed voters");
		if (d.HasMember("publisher_tls"))
			o.publisher_tls = tls(field(d, "publisher_tls"));
	}
	if (d.HasMember("publisher_bind"))
		o.publisher_bind = asio::ip::make_address(text(field(d, "publisher_bind"), 256));
	check(!o.publisher_bind.is_multicast() && (o.publisher_bind.is_loopback() || o.publisher_tls),
		  "non-loopback publishers require TLS");
	if (d.HasMember("publisher_name"))
		o.publisher_name = text(field(d, "publisher_name"), 253);
	check(o.publisher_name.find_first_of(" \t\r\n") == std::string::npos, "publisher TLS name");
	auto &credentials = field(d, "credentials");
	check(credentials.IsArray() && !credentials.Empty() && credentials.Size() <= o.node.application.producers,
		  "credential count bound");
	std::set<std::string> tokens, producers;
	for (const auto &value : credentials.GetArray()) {
		fields(value, {"token", "producer", "namespaces", "principal", "policy_epoch", "publish"});
		Credential c{text(field(value, "token"), 128), text(field(value, "producer"), 128), {}};
		if (value.HasMember("principal"))
			c.principal = text(field(value, "principal"), 128);
		if (value.HasMember("policy_epoch"))
			c.policy_epoch = number(field(value, "policy_epoch"));
		if (value.HasMember("publish")) {
			check(field(value, "publish").IsBool(), "credential publish must be boolean");
			c.publish = field(value, "publish").GetBool();
		}
		check(c.policy_epoch != 0, "authorization policy epoch must be nonzero");
		check(c.token.size() >= 32 && !c.producer.empty() && tokens.insert(c.token).second &&
				  producers.insert(c.producer).second,
			  "invalid or duplicate credentials");
		auto &spaces = field(value, "namespaces");
		check(spaces.IsArray() && !spaces.Empty() && spaces.Size() <= o.node.application.namespaces,
			  "namespace authorization bound");
		for (const auto &name : spaces.GetArray()) {
			auto s = text(name);
			check(detent::is_valid_knob_name(s + ":x") && c.namespaces.insert(s).second,
				  "invalid namespace authorization");
		}
		o.credentials.push_back(std::move(c));
	}
	(void)State(o.node.context, o.node.application);
	validate_node_options(o.node);
	return o;
}
struct Service::Accounting {
	struct GrantView {
		distribution::ScopeIdentity digest;
		std::vector<std::string_view, distribution::StorageAllocator<std::string_view>> names;
		explicit GrantView(std::shared_ptr<distribution::StorageLedger> account)
			: names(distribution::StorageAllocator<std::string_view>(std::move(account))) {}
	};
	using GrantViews = std::vector<GrantView, distribution::StorageAllocator<GrantView>>;
	std::shared_ptr<distribution::StorageLedger> delivery_account;
	std::shared_ptr<std::pmr::memory_resource> delivery_resource;
	std::unique_ptr<SubscriptionRequests> subscription_requests;
	std::mutex mutex;
	std::vector<Credential> credentials;
	std::vector<Digest> credential_hashes;
	std::optional<GrantViews> credential_grants;
	std::map<std::string, std::size_t> producers;
	std::size_t requests = 0, body_bytes = 0, reply_bytes = 0;
	kronuz::metrics::AtomicDurationHistogram publisher_ready, reply_retired;
};
struct Service::Delivery : distribution_http::OwnedReplyPump {
	std::shared_ptr<const AuthorizedScope> scope;
	std::optional<distribution::Cursor> base;
	ExportSelection selection = ExportSelection::Automatic;
	std::optional<ChangeHistory::Handoff> handoff;
	bool compress = false, retiring = false;
};
struct Service::Admission {
	std::shared_ptr<Accounting> accounting;
	const Credential *credential;
	std::size_t bytes;
	std::size_t credential_index = 0;
	std::shared_ptr<SubscriptionRequests::Lease> subscription;
	bool active = false;
	Clock::time_point admitted = Clock::now();
	~Admission() {
		if (!active) {
			return;
		}
		std::lock_guard lock(accounting->mutex);
		--accounting->requests;
		accounting->body_bytes -= bytes;
		auto found = accounting->producers.find(credential->producer);
		if (found != accounting->producers.end() && --found->second == 0) {
			accounting->producers.erase(found);
		}
	}
};
struct Service::ReplyAdmission {
	std::shared_ptr<Accounting> accounting;
	std::size_t bytes = 0;
	bool active = false;
	Clock::time_point ready = Clock::now();
	~ReplyAdmission() {
		if (active) {
			std::lock_guard lock(accounting->mutex);
			accounting->reply_bytes -= bytes;
			accounting->reply_retired.observe(elapsed_ns(ready));
		}
	}
};
Service::Service(ServiceOptions options) : options_(std::move(options)) {
	check(options_.storage_backend == "auto" || options_.storage_backend == "native" ||
			  options_.storage_backend == "fallback",
		  "authority storage backend policy");
#ifndef __linux__
	check(options_.storage_backend == "auto", "explicit storage backend policy currently requires Linux");
#endif
	check(options_.requests && options_.requests <= 65536 &&
			  options_.queued_bytes >= options_.node.application.request_bytes &&
			  options_.timeout.count() > 0 && options_.timeout <= std::chrono::hours(24) &&
			  options_.reply_bytes >= 1024 && !options_.credentials.empty(),
		  "invalid authority service limits");
	check(!options_.delivery_timeout || (options_.delivery_timeout->count() > 0 &&
			  *options_.delivery_timeout <= std::chrono::hours(24)),
		  "invalid authority delivery timeout");
	check(options_.delivery_heap_bytes >= 1024 * 1024 &&
			  options_.delivery_heap_bytes <= SIZE_MAX && options_.delivery.bytes &&
			  options_.delivery.bytes <= SIZE_MAX && options_.delivery.namespaces &&
			  options_.delivery.namespaces <= 4096,
		  "invalid authority delivery limits");
	accounting_ = std::make_shared<Accounting>();
	accounting_->delivery_account =
		std::make_shared<distribution::StorageLedger>(options_.delivery_heap_bytes);
	accounting_->delivery_resource = distribution::make_storage_resource(accounting_->delivery_account);
	check(options_.subscription_wait.count() >= 0 && options_.subscription_wait <= std::chrono::minutes(1),
		  "invalid subscription poll duration");
	check(options_.subscription_requests.polls <= 512, "subscription polls must preserve listener headroom");
	check(options_.subscription_requests.opens <= 128 && options_.subscription_requests.closes <= 128,
		  "subscription controls must preserve listener headroom");
	accounting_->subscription_requests =
		std::make_unique<SubscriptionRequests>(accounting_->delivery_account, options_.subscription_requests);
	accounting_->credentials = std::move(options_.credentials);
	accounting_->credential_grants.emplace(
		distribution::StorageAllocator<Accounting::GrantView>(accounting_->delivery_account));
	accounting_->credential_grants->reserve(accounting_->credentials.size());
	accounting_->credential_hashes.reserve(accounting_->credentials.size());
	for (const auto &credential : accounting_->credentials) {
		check(credential.policy_epoch && credential.principal.size() <= 128,
			  "invalid authorization identity");
		class SHA256 hash;
		hash.add(credential.token.data(), credential.token.size());
		Digest digest{};
		hash.getHash(digest.data());
		accounting_->credential_hashes.push_back(digest);
		accounting_->credential_grants->emplace_back(accounting_->delivery_account);
		auto &grants = accounting_->credential_grants->back();
		check(credential.namespaces.size() <= 4096, "credential grant count bound");
		grants.names.reserve(credential.namespaces.size());
		for (const auto &name : credential.namespaces)
			grants.names.push_back(name);
		grants.digest =
			distribution::canonical_scope_identity("detent-subscription-grants-v1", 1, grants.names);
	}
	std::promise<void> ready;
	auto future = ready.get_future();
	thread_ = std::thread([this, ready = std::move(ready)]() mutable { run(std::move(ready)); });
	try {
		future.get();
	} catch (...) {
		thread_.join();
		throw;
	}
}
Service::~Service() {
	{
		std::lock_guard lock(mutex_);
		stopping_ = true;
	}
	if (thread_.joinable()) {
		thread_.join();
	}
}
void Service::observe_connections(std::shared_ptr<const reactor::ConnectionAdmission> observer) {
	std::lock_guard lock(mutex_);
	connections_ = std::move(observer);
}
std::shared_ptr<Service::Admission> Service::admit(const http::Request &request) {
	auto header = request.header("Authorization");
	if (!header.starts_with("Bearer ") || header.size() > 135) {
		throw http::RequestRejected(401, "{\"error\":\"unauthorized\"}", http::RejectionContent::Json);
	}
	auto token = header.substr(7);
	class SHA256 supplied;
	supplied.add(token.data(), token.size());
	Digest want{};
	supplied.getHash(want.data());
	const Credential *found = nullptr;
	std::size_t found_index = 0;
	for (std::size_t index = 0; index < accounting_->credentials.size(); ++index) {
		const auto &c = accounting_->credentials[index];
		const auto &have = accounting_->credential_hashes[index];
		unsigned difference = 0;
		for (unsigned i = 0; i < 32; ++i) {
			difference |= want[i] ^ have[i];
		}
		if (!difference) {
			found = &c;
			found_index = index;
		}
	}
	if (!found) {
		throw http::RequestRejected(401, "{\"error\":\"unauthorized\"}", http::RejectionContent::Json);
	}
	if (!found->publish &&
		(request.path == "/v2/open" || request.path == "/v2/close" || request.path == "/v2/transactions")) {
		throw http::RequestRejected(403, "{\"error\":\"publish_forbidden\"}", http::RejectionContent::Json);
	}
	std::optional<SubscriptionRequestKind> subscription_kind;
	if (auto kind = distribution_http::classify_session_route(request.path)) {
		switch (*kind) {
		case distribution_http::RequestKind::Open:
			subscription_kind = SubscriptionRequestKind::Open;
			break;
		case distribution_http::RequestKind::Next:
			subscription_kind = SubscriptionRequestKind::Next;
			break;
		case distribution_http::RequestKind::Close:
			subscription_kind = SubscriptionRequestKind::Close;
			break;
		default:
			throw std::logic_error("invalid subscription route classification");
		}
	}
	if (subscription_kind) {
		std::lock_guard lock(mutex_);
		if (stopping_)
			throw http::RequestRejected(503, "{\"error\":\"admission_full\"}", http::RejectionContent::Json);
		const auto principal = distribution::canonical_scope_identity(
			found->principal.empty() ? found->producer : found->principal, 1, {});
		auto lease = accounting_->subscription_requests->admit(principal, *subscription_kind);
		if (!lease)
			throw http::RequestRejected(503, "{\"error\":\"admission_full\"}", http::RejectionContent::Json);
		auto admission = std::allocate_shared<Admission>(
			distribution::StorageAllocator<Admission>(accounting_->delivery_account));
		admission->accounting = accounting_;
		admission->credential = found;
		admission->credential_index = found_index;
		admission->bytes = accounting_->subscription_requests->body_limit(*subscription_kind);
		admission->subscription = std::move(lease);
		return admission;
	}
	auto bytes = options_.node.application.request_bytes;
	auto admission = std::make_shared<Admission>();
	admission->accounting = accounting_;
	admission->credential = found;
	admission->credential_index = found_index;
	admission->bytes = bytes;
	std::lock_guard lock(mutex_);
	std::lock_guard account_lock(accounting_->mutex);
	auto per_producer = accounting_->producers.find(found->producer);
	if (stopping_ || accounting_->requests >= options_.requests ||
		(per_producer != accounting_->producers.end() && per_producer->second >= 16) ||
		accounting_->body_bytes > options_.queued_bytes ||
		bytes > options_.queued_bytes - accounting_->body_bytes) {
		throw http::RequestRejected(503, "{\"error\":\"admission_full\"}", http::RejectionContent::Json);
	}
	++accounting_->producers[found->producer];
	++accounting_->requests;
	accounting_->body_bytes += bytes;
	admission->active = true;
	return admission;
}
std::unique_ptr<http::BodySink> Service::on_request_body(http::Request &request) {
	if (request.path == "/v1/health" && request.method == "GET") {
		return std::make_unique<Sink>(request.body, 0);
	}
	request.user_data = admit(request);
	auto admission = std::static_pointer_cast<Admission>(request.user_data);
	if (admission->subscription) {
		class SubscriptionSink final : public http::BodySink {
			std::shared_ptr<SubscriptionRequests::Lease> lease_;
			std::size_t limit_;

		  public:
			explicit SubscriptionSink(std::shared_ptr<SubscriptionRequests::Lease> lease, std::size_t limit)
				: lease_(std::move(lease)), limit_(limit) {}
			void write(std::string_view bytes) override {
				if (bytes.size() > limit_ - lease_->body().size())
					throw http::RequestRejected(413, "{\"error\":\"body_bound\"}",
												http::RejectionContent::Json);
				lease_->body().append(bytes);
			}
			void end() override { lease_->body().seal_prefix(); }
		};
		return std::make_unique<SubscriptionSink>(admission->subscription, admission->bytes);
	}
	return std::make_unique<Sink>(request.body, options_.node.application.request_bytes);
}
std::optional<Service::Reply> Service::execute(const http::Request &request,
											   std::shared_ptr<Admission> admission,
											   std::function<void(Reply)> complete,
											   Clock::time_point deadline) {
	check(request.method == "POST", "authority endpoints require POST");
	auto d = document(request.body, options_.node.application.request_bytes);
	auto request_fields = [&](const Object &value, std::initializer_list<std::string_view> allowed) {
		check(identity(text(field(value, "cluster"))) == options_.node.context.cluster &&
				  identity(text(field(value, "group"))) == options_.node.context.group &&
				  identity(text(field(value, "epoch"))) == options_.node.context.epoch,
			  "foreign request authority");
		std::set<std::string_view> seen;
		for (auto it = value.MemberBegin(); it != value.MemberEnd(); ++it) {
			std::string_view key(it->name.GetString(), it->name.GetStringLength());
			check(seen.insert(key).second &&
					  (key == "cluster" || key == "group" || key == "epoch" ||
					   std::find(allowed.begin(), allowed.end(), key) != allowed.end()),
				  "unknown or duplicate request field");
		}
	};
	auto task = std::make_shared<Task>();
	task->admission = std::move(admission);
	task->complete = std::move(complete);
	task->command.context = options_.node.context;
	task->command.producer = task->admission->credential->producer;
	task->deadline = deadline;
	if (request.path == "/v2/open") {
		request_fields(d, {"expected_generation", "nonce"});
		task->kind = Task::Kind::Command;
		task->command.kind = Kind::Open;
		task->command.generation = number(field(d, "expected_generation"));
		task->command.nonce = identity(text(field(d, "nonce")));
	} else if (request.path == "/v2/close") {
		request_fields(d, {"generation"});
		task->kind = Task::Kind::Command;
		task->command.kind = Kind::Close;
		task->command.generation = number(field(d, "generation"));
	} else if (request.path == "/v2/transactions") {
		request_fields(d, {"generation", "sequence", "namespace", "expected_namespace", "operations"});
		task->kind = Task::Kind::Command;
		auto &c = task->command;
		c.generation = number(field(d, "generation"));
		c.sequence = number(field(d, "sequence"));
		c.name_space = text(field(d, "namespace"));
		c.expected_namespace = optional(d, "expected_namespace");
		check(task->admission->credential->namespaces.contains(c.name_space), "unauthorized namespace");
		auto &operations = field(d, "operations");
		check(operations.IsArray() && operations.Size() <= options_.node.application.mutations,
			  "operation count bound");
		for (const auto &item : operations.GetArray()) {
			fields(item, {"key", "value", "expected_revision", "absent_namespace_revision"});
			Mutation op;
			op.key = text(field(item, "key"));
			op.expected = optional(item, "expected_revision");
			op.absence_revision = optional(item, "absent_namespace_revision");
			auto &value = field(item, "value");
			if (!value.IsNull()) {
				auto parsed = detent::internal::parse_knob_def(value, op.key);
				check(parsed.has_value(), "invalid knob definition");
				op.value = std::move(parsed).value();
			}
			c.mutations.push_back(std::move(op));
		}
	} else if (request.path == "/v2/read") {
		request_fields(d, {"namespace", "keys"});
		task->kind = Task::Kind::Read;
		task->name_space = text(field(d, "namespace"));
		check(task->admission->credential->namespaces.contains(task->name_space), "unauthorized namespace");
		auto &keys = field(d, "keys");
		check(keys.IsArray() && keys.Size() <= options_.node.application.mutations, "read key count bound");
		std::set<std::string> unique;
		for (const auto &value : keys.GetArray()) {
			auto key = text(value);
			check(detent::is_valid_knob_name(key) && key.substr(0, key.find(':')) == task->name_space &&
					  unique.insert(key).second,
				  "invalid read key");
			task->keys.push_back(std::move(key));
		}
	} else if (request.path == "/v2/result") {
		request_fields(d, {"generation", "sequence"});
		task->kind = Task::Kind::Result;
		task->generation = number(field(d, "generation"));
		task->sequence = number(field(d, "sequence"));
	} else {
		return Reply{404, "{\"error\":\"unknown_endpoint\"}"};
	}
	if (task->kind == Task::Kind::Command) {
		(void)encode(task->command, options_.node.application);
	}
	{
		std::lock_guard lock(mutex_);
		if (stopping_) {
			return Reply{503, "{\"error\":\"stopping\"}"};
		}
		if (task->kind == Task::Kind::Command && !busy_producers_.insert(task->command.producer).second) {
			return Reply{429, "{\"error\":\"producer_busy\"}"};
		}
		try {
			queue_.push_back(task);
		} catch (...) {
			if (task->kind == Task::Kind::Command) {
				busy_producers_.erase(task->command.producer);
			}
			throw;
		}
	}
	return std::nullopt;
}

std::shared_ptr<Service::Delivery>
Service::distribution_request(const distribution_http::SessionRequest &request, const Admission &admission) {
	const auto &context = options_.node.context;
	check(request.identity.cluster == context.cluster && request.identity.group == context.group &&
			  request.identity.restore_epoch == context.epoch,
		  "foreign distribution authority");
	const auto &credential = *admission.credential;
	auto account = accounting_->delivery_account;
	auto delivery = std::allocate_shared<Delivery>(distribution::StorageAllocator<Delivery>(account));
	delivery->scope = AuthorizedScope::authorize_canonical(
		credential.principal.empty() ? credential.producer : credential.principal, credential.policy_epoch,
		request.namespaces, (*accounting_->credential_grants)[admission.credential_index].names, account,
		options_.delivery.namespaces);
	check(request.identity.scope == delivery->scope->identity(), "distribution policy or scope mismatch");
	delivery->compress = request.compressed;
	delivery->base = request.base;
	return delivery;
}

void Service::stream_task(http::ResponseWriter &response, const std::shared_ptr<Task> &task) {
	auto delivery = task->delivery;
	auto account = accounting_->delivery_account;
	http::StreamOptions stream;
	stream.resource = accounting_->delivery_resource;
	stream.reply.deadline = task->deadline + std::chrono::seconds(1);
	stream.reply.request_retention = task->admission;
	stream.chunk_bytes = distribution::OwnedFragmentEncoder::maximum_wire_bytes();
	stream.maximum_bytes = (options_.delivery.bytes / distribution::fragment_bytes + 1) * stream.chunk_bytes;
	distribution_http::OwnedReplyPump::callbacks(stream, delivery);
	task->complete = [delivery, account](Reply value) {
		delivery->send_control(value.status, value.body, account);
	};
	delivery->reply = response.stream(std::move(stream));
	check(static_cast<bool>(delivery->reply), "owned distribution streaming unavailable");
}

void Service::distribute(const http::Request &request, http::ResponseWriter &response,
						 std::shared_ptr<Admission> admission) {
	check(request.method == "POST", "distribution requires POST");
	auto parsed = distribution_http::parse_session_request_on_worker(
		accounting_->delivery_account, distribution_http::RequestKind::Distribution, request.body,
		{std::min<std::size_t>(admission->bytes, 1024 * 1024), options_.delivery.namespaces,
		 static_cast<std::uint64_t>(options_.subscription_wait.count())});
	auto delivery = distribution_request(parsed->request(), *admission);
	auto task =
		std::allocate_shared<Task>(distribution::StorageAllocator<Task>(accounting_->delivery_account));
	task->kind = Task::Kind::Distribution;
	task->admission = std::move(admission);
	task->delivery = delivery;
	task->deadline = Clock::now() + options_.delivery_timeout.value_or(options_.timeout);
	std::lock_guard lock(mutex_);
	check(!stopping_, "authority is stopping");
	stream_task(response, task);
	try {
		queue_.push_back(std::move(task));
	} catch (...) {
		delivery->reply->abandon();
	}
}

void Service::subscription(const http::Request &request, http::ResponseWriter &response,
						   std::shared_ptr<Admission> admission) {
	check(request.method == "POST", "subscription requires POST");
	auto &input = admission->subscription->body();
	if (!input.sealed()) {
		input.append(request.body);
		input.seal_prefix();
	}
	const auto kind = distribution_http::classify_session_route(request.path);
	check(kind.has_value(), "unknown subscription route");
	auto parsed = distribution_http::parse_session_request_on_worker(
		accounting_->delivery_account, *kind, input.view(),
		{admission->bytes, options_.delivery.namespaces,
		 static_cast<std::uint64_t>(options_.subscription_wait.count())});
	const auto &command = parsed->request();
	auto account = accounting_->delivery_account;
	auto task = std::allocate_shared<Task>(distribution::StorageAllocator<Task>(account));
	task->admission = std::move(admission);
	const auto &credential = *task->admission->credential;
	task->principal = {distribution::canonical_scope_identity(
						   credential.principal.empty() ? credential.producer : credential.principal, 1, {}),
					   credential.policy_epoch};
	task->deadline = Clock::now() + options_.timeout;
	if (command.kind == distribution_http::RequestKind::Open) {
		task->kind = Task::Kind::SessionOpen;
		task->delivery = distribution_request(command, *task->admission);
		task->force_snapshot = command.force_snapshot;
		check(RAND_bytes(reinterpret_cast<unsigned char *>(task->session.data()), task->session.size()) == 1,
			  "subscription identity entropy");
	} else {
		task->delivery = std::allocate_shared<Delivery>(distribution::StorageAllocator<Delivery>(account));
		task->session = command.session;
		if (command.kind == distribution_http::RequestKind::Close)
			task->kind = Task::Kind::SessionClose;
		else {
			task->kind = Task::Kind::SessionNext;
			task->sequence = command.after;
			task->deadline = Clock::now() + options_.delivery_timeout.value_or(options_.timeout) +
				std::chrono::milliseconds(command.wait_ms);
			task->poll_deadline = Clock::now() + std::chrono::milliseconds(command.wait_ms);
			task->published = command.published;
		}
	}
	auto delivery = task->delivery;
	std::lock_guard lock(mutex_);
	check(!stopping_, "authority is stopping");
	stream_task(response, task);
	try {
		if (task->kind == Task::Kind::SessionClose)
			subscription_close_queue_.push_back(std::move(task));
		else
			subscription_queue_.push_back(std::move(task));
	} catch (...) {
		delivery->reply->abandon();
	}
}

void Service::handle(const http::Request &request, http::ResponseWriter &response) {
	const auto context = options_.node.context;
	auto envelope = [context](Reply reply) {
		if (!reply.enveloped && !reply.body.empty() && reply.body[0] == '{') {
			reply.body = "{\"authority\":{\"cluster\":\"" + hex(context.cluster) + "\",\"group\":\"" +
						 hex(context.group) + "\",\"epoch\":\"" + hex(context.epoch) + "\"}," +
						 reply.body.substr(1);
		}
		return reply;
	};
	std::shared_ptr<http::DeferredReply> deferred;
	std::function<void(Reply)> complete;
	Reply reply;
	try {
		if (request.path == "/v1/health" && request.method == "GET") {
			std::lock_guard lock(mutex_);
			reply = {200, health_};
			if (!reply.body.empty() && reply.body.back() == '}') {
				auto age = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - health_stamp_)
							   .count();
				reply.body.pop_back();
				if (reply.body.size() > 1) {
					reply.body += ',';
				}
				reply.body += "\"snapshot_age_ms\":" + std::to_string(std::max<std::int64_t>(age, 0)) + "}";
			}
		} else {
			auto admission =
				request.user_data ? std::static_pointer_cast<Admission>(request.user_data) : admit(request);
			if (admission->subscription) {
				subscription(request, response, std::move(admission));
				return;
			}
			if (request.path == "/v2/distribution") {
				distribute(request, response, std::move(admission));
				return;
			}
			const auto outcome_deadline = Clock::now() + options_.timeout;
			http::DeferredOptions deferred_options;
			// Outcome completion needs a bounded window to write its timeout reply.
			deferred_options.deadline = outcome_deadline + std::chrono::seconds(1);
			deferred_options.limits.body_bytes = options_.reply_bytes - 256;
			deferred_options.request_retention = admission;
			deferred = response.defer(std::move(deferred_options));
			if (deferred) {
				complete = [deferred, accounting = accounting_, limit = options_.reply_bytes,
							envelope](Reply value) {
					try {
						value = envelope(std::move(value));
						auto lease = std::move(value.reservation);
						if (!lease) {
							lease = std::make_shared<ReplyAdmission>();
							lease->accounting = accounting;
						}
						// This endpoint emits only its fixed JSON content type and transport framing.
						if (value.body.size() > limit - 256) {
							deferred->abandon();
							return;
						}
						if (lease->active) {
							check(lease->bytes == value.body.size() + 256, "read reply reservation mismatch");
							lease->ready = Clock::now();
						} else {
							lease->bytes = value.body.size() + 256;
							std::lock_guard lock(accounting->mutex);
							if (lease->bytes > limit - accounting->reply_bytes) {
								deferred->abandon();
								return;
							}
							accounting->reply_bytes += lease->bytes;
							lease->active = true;
						}
						http::Response owned;
						owned.status = value.status;
						owned.body = std::move(value.body);
						owned.content_type = "application/json";
						if (deferred->complete(std::move(owned), lease) != http::ReplySubmit::Accepted) {
							deferred->abandon();
						}
					} catch (...) {
						deferred->abandon();
					}
				};
				auto immediate = execute(request, std::move(admission), complete, outcome_deadline);
				if (immediate) {
					complete(std::move(*immediate));
				}
				return;
			}
			// Compatibility for synchronous recording/embedded writers. Production Asio
			// transfers response authority and never blocks on a per-request future.
			auto promise = std::make_shared<std::promise<Reply>>();
			auto future = promise->get_future();
			auto deadline = outcome_deadline;
			auto immediate = execute(
				request, std::move(admission),
				[promise](Reply value) { promise->set_value(std::move(value)); }, deadline);
			if (immediate) {
				reply = std::move(*immediate);
			} else if (future.wait_until(deadline) == std::future_status::ready) {
				reply = future.get();
			} else {
				reply = {503, "{\"error\":\"outcome_unknown\"}"};
			}
		}
	} catch (const http::RequestRejected &error) {
		reply = {error.status(), error.what()};
	} catch (const std::invalid_argument &error) {
		rapidjson::StringBuffer buffer;
		rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
		writer.String(error.what());
		reply = {400, "{\"error\":\"invalid_request\",\"detail\":" +
						  std::string(buffer.GetString(), buffer.GetSize()) + "}"};
	} catch (const std::exception &) {
		reply = {503, "{\"error\":\"outcome_unknown\"}"};
	}
	if (deferred) {
		if (complete) {
			complete(std::move(reply));
		} else {
			deferred->abandon();
		}
		return;
	}
	reply = envelope(std::move(reply));
	response.status(reply.status);
	response.set_header("Content-Type", "application/json");
	response.write(reply.body);
	response.end();
}

struct Service::Runtime {
	asio::io_context io{1};
	bool active = false;
	std::exception_ptr failure;
	kronuz::metrics::DurationHistogram<> owner_turn, owner_cpu, wake_lateness, job_settlement,
		fenced_job_observed;
	kronuz::metrics::Counter cpu_clock_errors;
	struct Flight {
		std::shared_ptr<kronuz::journal::IOOperation> operation;
		Clock::time_point admitted;
	};
	std::optional<Flight> storage_flight;
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__linux__)
	std::shared_ptr<kronuz::journal::NativeCompletionQueue> queue;
	std::shared_ptr<kronuz::journal::AsioCompletionDriver<kronuz::journal::NativeCompletionQueue>> driver;
	explicit Runtime(const ServiceOptions &options) {
#ifdef __linux__
		using Policy = kronuz::journal::LinuxCompletionPolicy;
		queue = std::make_shared<kronuz::journal::NativeCompletionQueue>(
			options.storage_backend == "native"		? Policy::RequireNative
			: options.storage_backend == "fallback" ? Policy::Fallback
													: Policy::Automatic);
#else
		(void)options;
		queue = std::make_shared<kronuz::journal::NativeCompletionQueue>();
#endif
		driver =
			std::make_shared<kronuz::journal::AsioCompletionDriver<kronuz::journal::NativeCompletionQueue>>(
				io.get_executor(), queue, true);
	}

#else
	explicit Runtime(const ServiceOptions &) {}
#endif
	void dispatch(const std::shared_ptr<Node> &owner) {
		auto &node = *owner;
		if (storage_flight && !active && storage_flight->operation->done() &&
			(!node.worker().owns_io_operation(storage_flight->operation) || node.worker().fenced())) {
			auto &histogram = node.worker().fenced() ? fenced_job_observed : job_settlement;
			histogram.observe(elapsed_ns(storage_flight->admitted));
			storage_flight.reset();
		}
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__linux__)
		if (auto operation = node.worker().take_io_operation()) {
			if (active) {
				throw std::logic_error("authority dispatched overlapping storage jobs");
			}
			if (storage_flight) {
				throw std::logic_error("authority overlapped unsettled storage observations");
			}
			storage_flight.emplace(Flight{operation, Clock::now()});
			active = true;
			try {
				asio::co_spawn(io, driver->run(operation),
							   [this, owner, operation](std::exception_ptr error) {
								   if (error) {
									   // A dead driver cannot gracefully reap an original
									   // still owned by its backend. Follow its fail-stop
									   // policy rather than refunding or waiting forever.
									   if (operation->in_flight())
										   std::terminate();
									   failure = error;
									   owner->worker().storage_operation_failed(
										   operation, "authority completion driver failed");
								   }
								   active = false;
							   });
			} catch (...) {
				active = false;
				if (operation->in_flight())
					std::terminate();
				node.worker().storage_operation_failed(operation, "authority completion dispatch failed");
				throw;
			}
		}
#else
		(void)node;
#endif
	}
	asio::awaitable<void> run(Service &service, std::promise<void> ready);
};
void Service::run(std::promise<void> ready) {
	try {
		Runtime runtime(options_);
		asio::co_spawn(runtime.io, runtime.run(*this, std::move(ready)), [this](std::exception_ptr error) {
			if (error) {
				std::lock_guard lock(mutex_);
				stopping_ = true;
				health_stamp_ = Clock::now();
				health_ = std::string("{\"mode\":\"") +
						  (options_.peers ? "durable_fixed_voters" : "durable_single_voter") +
						  "\",\"fenced\":true}";
			}
		});
		runtime.io.run();
	} catch (...) {
		// A backend/runtime failure before admission must settle the constructor.
		try {
			ready.set_exception(std::current_exception());
		} catch (const std::future_error &) {
		}
	}
}
asio::awaitable<void> Service::Runtime::run(Service &service, std::promise<void> ready) {
	auto &options_ = service.options_;
	auto &mutex_ = service.mutex_;
	auto &stopping_ = service.stopping_;
	auto &queue_ = service.queue_;
	auto &busy_producers_ = service.busy_producers_;
	auto &health_ = service.health_;
	auto accounting_ = service.accounting_;
	asio::steady_timer timer(io);
	std::shared_ptr<Node> owned_node;
	std::shared_ptr<AuthorityPeers> peers;

	std::map<std::uint64_t, std::vector<std::shared_ptr<Task>>> pending;
	std::uint64_t next = 0;
	bool started = false;
	std::uint64_t last_tick = 0;
	auto election_delay = [&] {
		std::uint32_t entropy = 0;
		check(RAND_bytes(reinterpret_cast<unsigned char *>(&entropy), sizeof(entropy)) == 1,
			  "election entropy");
		const auto &timing = options_.node.timing;
		return timing.election_min + entropy % (timing.election_max - timing.election_min + 1);
	};
	using Tasks = std::vector<std::shared_ptr<Task>>;
	using PendingTasks = std::map<std::uint64_t, Tasks>;
	struct Preparation {
		Tasks tasks;
		std::shared_ptr<Task> current;
		std::optional<CommandEncoder> encoder;
		std::optional<EncodedCommand> encoded;
		std::optional<ProposalBuilder> builder;
		std::optional<PreparedProposal> proposal;
		PendingTasks::node_type waiter;
		std::uint64_t request = 0;
		bool sealed = false, cancelling = false;
		std::size_t cancelled = 0;
		Preparation() { tasks.reserve(64); }
	};
	struct ReadyRead {
		std::shared_ptr<Task> task;
		std::uint64_t boundary = 0;
	};
	std::vector<ReadyRead> ready_reads(options_.requests);
	std::size_t read_head = 0, read_tail = 0, read_count = 0, legacy_ready = 0;
	bool subscription_export_turn = true;
	std::size_t subscription_ready = 0;
	std::vector<std::shared_ptr<Task>, distribution::StorageAllocator<std::shared_ptr<Task>>>
		legacy_deliveries{
			distribution::StorageAllocator<std::shared_ptr<Task>>(accounting_->delivery_account)};
	std::size_t control_head = 0, control_tail = 0, control_count = 0;
	struct ReadSlot {
		std::shared_ptr<Task> task;
		State capture;
		std::optional<ReadReplyEncoder> encoder;
		std::shared_ptr<ReplyAdmission> reservation;
		bool retiring = false, quota_blocked = false;
		ReadSlot(std::shared_ptr<Task> t, const State &state) : task(std::move(t)), capture(state) {}
	};
	std::optional<ReadSlot> reading;
	struct DeliverySlot {
		std::shared_ptr<Task> task;
		std::optional<DeliveryExport> exporter;
		std::shared_ptr<const distribution::OwnedBuffer> pending;
		std::size_t pending_disposal = 0;
		bool retiring = false, source_drained = false;
		bool discard_pending(distribution::WorkBudget &budget) noexcept {
			if (!pending)
				return true;
			if (!budget.tokens)
				return false;
			if (!pending_disposal)
				pending_disposal = pending->storage_capacity();
			const auto n = std::min(pending_disposal, budget.bytes);
			pending_disposal -= n;
			budget.bytes -= n;
			if (pending_disposal)
				return false;
			--budget.tokens;
			pending.reset();
			return true;
		}
	};
	std::optional<DeliverySlot> delivering;
	std::shared_ptr<ChangeHistory> history;
	std::optional<Subscriptions> subscriptions;
	using SessionTasks =
		std::vector<std::shared_ptr<Task>, distribution::StorageAllocator<std::shared_ptr<Task>>>;
	SessionTasks session_tasks(
		distribution::StorageAllocator<std::shared_ptr<Task>>(accounting_->delivery_account));
	using SessionPendingEntry = std::pair<const std::uint64_t, std::shared_ptr<Task>>;
	std::map<std::uint64_t, std::shared_ptr<Task>, std::less<>,
			 distribution::StorageAllocator<SessionPendingEntry>>
		session_pending(distribution::StorageAllocator<SessionPendingEntry>(accounting_->delivery_account));
	std::size_t session_cursor = 0;
	std::size_t close_cursor = 0;
	struct TaskQueue {
		SessionTasks ring;
		std::size_t head = 0, tail = 0, count = 0;
		TaskQueue(std::shared_ptr<distribution::StorageLedger> account, std::size_t capacity)
			: ring(distribution::StorageAllocator<std::shared_ptr<Task>>(std::move(account))) {
			ring.resize(capacity);
		}
		void push(std::shared_ptr<Task> task) {
			check(count < ring.size(), "authorization queue capacity exhausted");
			ring[tail] = std::move(task);
			tail = (tail + 1) % ring.size();
			++count;
		}
		std::shared_ptr<Task> pop() {
			auto task = std::move(ring[head]);
			head = (head + 1) % ring.size();
			--count;
			return task;
		}
	};
	std::optional<TaskQueue> close_authorization, session_authorization, session_retirement;
	kronuz::metrics::Counter subscription_reads, subscription_exports, subscription_idle;
	kronuz::metrics::Counter export_expired, export_disconnected, export_limits, export_errors;
	using ActiveSessionEntry = std::pair<const distribution::Identity, std::weak_ptr<Task>>;
	std::map<distribution::Identity, std::weak_ptr<Task>, std::less<>,
			 distribution::StorageAllocator<ActiveSessionEntry>>
		active_session_requests(
			distribution::StorageAllocator<ActiveSessionEntry>(accounting_->delivery_account));
	std::vector<Completion> held_completions;
	std::size_t completion_cursor = 0, completion_task = 0;
	std::uint64_t expiry_request = 0;
	std::size_t expiry_task = 0;
	bool read_first = false;
	std::optional<Preparation> preparing;
	std::shared_ptr<Task> carry_task;
	std::optional<EncodedCommand> carry;
	std::vector<std::shared_ptr<Task>> retired_tasks(options_.requests);
	std::size_t retired_head = 0, retired_tail = 0, retired_count = 0, retired_task_bytes = 0;
	auto collect_tasks = [&](CommandWorkBudget &parent) {
		CommandWorkBudget budget;
		budget.bytes = parent.bytes / 2;
		budget.receipts = parent.receipts / 2;
		const auto reserved_bytes = budget.bytes, reserved_receipts = budget.receipts;
		while (retired_count) {
			auto &task = retired_tasks[retired_head];
			if (!dispose_command(task->command, budget))
				break;
			while (!task->keys.empty() && budget.receipts) {
				--budget.receipts;
				task->keys.pop_back();
			}
			if (!task->keys.empty())
				break;
			if (!budget.receipts || budget.bytes < 256)
				break;
			--budget.receipts;
			budget.bytes -= 256;
			retired_task_bytes -= task->admission->bytes;
			task.reset();
			retired_head = (retired_head + 1) % retired_tasks.size();
			--retired_count;
		}
		parent.bytes -= reserved_bytes - budget.bytes;
		parent.receipts -= reserved_receipts - budget.receipts;
	};
	auto begin_delivery_retirement = [](Task &task) {
		if (task.retiring)
			return;
		task.retiring = true;
		task.retirement_remaining =
			task.admission->subscription ? task.admission->subscription->body().storage_capacity() : 0;
		auto scope = std::move(task.delivery->scope);
		if (scope && scope.use_count() == 1) {
			task.retirement_remaining += scope->storage_capacity();
			task.retiring_scope = std::move(scope);
		}
	};
	auto complete = [&](const std::shared_ptr<Task> &task, Reply reply) {
		if (task->settled)
			return;
		if (task->export_ready) {
			task->export_ready = false;
			--subscription_ready;
		}
		if (task->kind == Task::Kind::Distribution && task->delivery) {
			check(control_count < legacy_deliveries.size(), "legacy control retirement capacity exhausted");
			legacy_deliveries[control_tail] = task;
			control_tail = (control_tail + 1) % legacy_deliveries.size();
			++control_count;
		}
		if (!task->retiring && (!task->command.mutations.empty() || !task->keys.empty())) {
			check(retired_count < retired_tasks.size(), "publisher retirement capacity exhausted");
			task->retiring = true;
			retired_tasks[retired_tail] = task;
			retired_tail = (retired_tail + 1) % retired_tasks.size();
			++retired_count;
			retired_task_bytes += task->admission->bytes;
		}
		task->settled = true;
		auto callback = std::move(task->complete);
		if (callback) {
			accounting_->publisher_ready.observe(elapsed_ns(task->admission->admitted));
		}
		if (task->kind == Task::Kind::Command) {
			std::lock_guard lock(mutex_);
			busy_producers_.erase(task->command.producer);
		}
		if (callback) {
			callback(std::move(reply));
		}
	};
	try {
		// Provision the parent directory explicitly; newly created final directory
		// receives a durable parent namespace barrier before Journal creation.
		if (std::filesystem::create_directory(options_.directory)) {
			check(::chmod(options_.directory.c_str(), 0700) == 0, "authority directory permissions");
			auto parent = options_.directory.parent_path();
			if (parent.empty()) {
				parent = ".";
			}
			int fd = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
			if (fd < 0) {
				throw std::runtime_error("authority parent directory");
			}
			auto synced = ::fsync(fd);
			::close(fd);
			if (synced) {
				throw std::runtime_error("authority directory durability");
			}
		}
		auto node_options = options_.node;
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__linux__)
		node_options.scheduling.async_storage = true;
#else
		check(!node_options.scheduling.async_storage && !node_options.scheduling.async_appends,
			  "native completion storage is unavailable on this platform");
#endif
		owned_node = std::make_shared<Node>(std::make_unique<kronuz::journal::PosixIO>(options_.directory),
											node_options);
		auto &node = *owned_node;
		history = std::allocate_shared<ChangeHistory>(
			distribution::StorageAllocator<ChangeHistory>(accounting_->delivery_account),
			accounting_->delivery_account);
		subscriptions.emplace(accounting_->delivery_account, options_.subscriptions);
		legacy_deliveries.resize(options_.requests);
		session_tasks.resize(options_.subscription_requests.opens + options_.subscription_requests.polls +
							 options_.subscription_requests.closes);
		session_retirement.emplace(accounting_->delivery_account, session_tasks.size());
		close_authorization.emplace(accounting_->delivery_account, options_.subscription_requests.closes);
		session_authorization.emplace(accounting_->delivery_account,
									  options_.subscription_requests.opens +
										  options_.subscription_requests.polls);
		auto last_health = Clock::time_point{};
		auto publish_health = [&] {
			auto captured_at = Clock::now();
			std::shared_ptr<const reactor::ConnectionAdmission> observer;
			{
				std::lock_guard lock(mutex_);
				observer = service.connections_;
			}
			auto connections = observer ? observer->stats() : reactor::ConnectionStats{};
			auto peer_occupancy = peers ? peers->occupancy() : kronuz::consensus::transport::PeerOccupancy{};
			std::size_t requests, body_bytes, reply_bytes;
			{
				std::lock_guard lock(accounting_->mutex);
				requests = accounting_->requests;
				body_bytes = accounting_->body_bytes;
				reply_bytes = accounting_->reply_bytes;
			}
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__linux__)
			auto io_stats = queue->stats();
			auto driver_stats = driver->stats();
			auto primitive_durations = [](const auto &values) {
				std::string encoded = "[";
				for (const auto &value : values) {
					if (encoded.size() > 1) {
						encoded += ',';
					}
					encoded += duration_json(value);
				}
				return encoded + "]";
			};
			std::string storage_health =
				",\"storage_async\":true,\"io_native_submitted\":" +
				std::to_string(io_stats.native_submitted) +
				",\"io_fallback_submitted\":" + std::to_string(io_stats.fallback_submitted);
			storage_health += ",\"io_native_completed\":" + std::to_string(io_stats.native_completed) +
							  ",\"io_fallback_completed\":" + std::to_string(io_stats.fallback_completed) +
							  ",\"io_read_bytes\":" + std::to_string(io_stats.file_read_bytes) +
							  ",\"io_write_bytes\":" + std::to_string(io_stats.file_write_bytes) +
							  ",\"io_notification_errors\":" + std::to_string(io_stats.notification_errors) +
							  ",\"io_maximum_outstanding\":" + std::to_string(io_stats.maximum_outstanding);
			auto primitive_counts = [](const auto &counts) {
				std::string value = "[";
				for (auto count : counts) {
					if (value.size() > 1)
						value += ',';
					value += std::to_string(count);
				}
				return value + "]";
			};
			storage_health +=
				",\"io_native_by_primitive\":" + primitive_counts(io_stats.native_by_primitive) +
				",\"io_fallback_by_primitive\":" + primitive_counts(io_stats.fallback_by_primitive);
			const auto &policy = node.checkpoint_policy();
			const auto policy_age =
				policy.due && last_tick >= policy.due_since ? last_tick - policy.due_since : 0;
			storage_health +=
				",\"checkpoint_due\":" + std::string(policy.due ? "true" : "false") +
				",\"checkpoint_due_age_ms\":" + std::to_string(policy_age) +
				",\"checkpoint_attempts\":" + std::to_string(policy.attempts.get()) +
				",\"checkpoint_reservation_unavailable\":" +
				std::to_string(policy.reservation_unavailable.get()) +
				",\"checkpoint_captured_boundary\":" + std::to_string(policy.captured_boundary) +
				",\"checkpoint_published_boundary\":" + std::to_string(policy.published_boundary);
			const auto append = peers ? peers->stats().append : kronuz::consensus::transport::AppendTimings{};
			storage_health +=
				",\"append_emit_attempts\":" + std::to_string(append.attempts.get()) +
				",\"append_observations\":" + std::to_string(append.observations.get()) +
				",\"append_duplicate_attempts\":" + std::to_string(append.duplicate_attempts.get()) +
				",\"append_unmatched_responses\":" + std::to_string(append.unmatched.get()) +
				",\"append_successful_responses\":" + std::to_string(append.successful.get()) +
				",\"append_rejected_responses\":" + std::to_string(append.rejected.get()) +
				",\"append_emit_to_response_ns\":" + duration_json(append.response.snapshot()) +
				",\"append_emit_to_higher_term_response_ns\":" +
				duration_json(append.higher_term_response.snapshot()) +
				",\"append_observation_timeout_ns\":" + duration_json(append.timeout.snapshot()) +
				",\"append_observation_superseded_ns\":" + duration_json(append.superseded.snapshot()) +
				",\"append_write_failure_ns\":" + duration_json(append.write_failure.snapshot()) +
				",\"append_disconnect_ns\":" + duration_json(append.disconnected.snapshot());
			storage_health +=
				",\"io_submitted_to_reaped_ns\":" + primitive_durations(driver_stats.submitted_to_reaped) +
				",\"io_failed_submitted_to_reaped_ns\":" +
				primitive_durations(driver_stats.failed_submitted_to_reaped) +
				",\"io_original_drive_ns\":" + duration_json(driver_stats.original_drive);

#ifdef __linux__
			storage_health +=
				",\"io_native_available\":" + std::string(queue->native_available() ? "true" : "false") +
				",\"io_setup_error\":" + std::to_string(queue->setup_error()) +
				",\"io_ring_errors\":" + std::to_string(io_stats.ring_errors);
#endif
#else
			const std::string storage_health = ",\"storage_async\":false";
#endif
			std::string encoded =
				std::string("{\"mode\":\"") +
				(options_.peers ? "durable_fixed_voters" : "durable_single_voter") +
				"\",\"connections_active\":" + std::to_string(connections.active) +
				",\"connections_high_water\":" + std::to_string(connections.high_water) +
				",\"connections_accepted\":" + std::to_string(connections.accepted) +
				",\"connections_rejected\":" + std::to_string(connections.rejected) +
				",\"duration_bucket_upper_bounds_ns\":" +
				[] {
					std::string bounds = "[";
					for (auto bound : kronuz::metrics::duration_bounds_ns) {
						if (bounds.size() > 1)
							bounds += ',';
						bounds += std::to_string(bound);
					}
					return bounds + "]";
				}() +
				",\"ready\":" + std::string(node.worker().ready() ? "true" : "false") +
				",\"role\":" + std::to_string(static_cast<unsigned>(node.worker().role())) +
				",\"committed\":" + std::to_string(node.worker().committed()) +
				",\"term\":" + std::to_string(node.worker().term()) +
				",\"applied\":" + std::to_string(node.worker().applied_index()) +
				",\"revision\":" + std::to_string(node.state().revision()) +
				",\"knobs\":" + std::to_string(node.state().size()) + ",\"delivery_captures\":" +
				std::to_string(delivering && delivering->exporter && !delivering->source_drained ? 1 : 0) +
				",\"requests\":" + std::to_string(requests) +
				",\"reserved_body_bytes\":" + std::to_string(body_bytes) +
				",\"reserved_reply_bytes\":" + std::to_string(reply_bytes) +
				",\"retired_log_bytes\":" + std::to_string(node.worker().retired_log_bytes()) +
				",\"retired_log_entries\":" + std::to_string(node.worker().retired_log_entries()) +
				",\"retired_state_values\":" + std::to_string(node.state().retired_values()) +
				",\"retired_state_value_bytes\":" + std::to_string(node.state().retired_value_bytes()) +
				",\"read_quota_blocked\":" +
				std::string(reading && reading->quota_blocked ? "true" : "false") +
				",\"read_ready_tasks\":" + std::to_string(read_count) +
				",\"read_captures\":" + std::to_string(reading ? 1 : 0) + ",\"read_capture_logical_bytes\":" +
				std::to_string(reading ? reading->capture.logical_bytes() : 0) +
				",\"read_buffer_reserved_bytes\":" +
				std::to_string(reading && reading->reservation && reading->reservation->active
								   ? reading->reservation->bytes
								   : 0) +
				",\"retired_publisher_tasks\":" + std::to_string(retired_count) +
				",\"retired_publisher_bytes\":" + std::to_string(retired_task_bytes) +
				",\"subscription_sessions\":" + std::to_string(subscriptions->size()) +
				",\"subscription_quorum_reads\":" + std::to_string(subscription_reads.get()) +
				",\"subscription_exports\":" + std::to_string(subscription_exports.get()) +
				",\"distribution_export_expired\":" + std::to_string(export_expired.get()) +
				",\"distribution_export_disconnected\":" + std::to_string(export_disconnected.get()) +
				",\"distribution_export_limits\":" + std::to_string(export_limits.get()) +
				",\"distribution_export_errors\":" + std::to_string(export_errors.get()) +
				",\"subscription_idle_responses\":" + std::to_string(subscription_idle.get()) +
				",\"owner_turn_ns\":" + duration_json(owner_turn.snapshot()) +
				",\"owner_turn_cpu_ns\":" + duration_json(owner_cpu.snapshot()) +
				",\"owner_cpu_clock_errors\":" + std::to_string(cpu_clock_errors.get()) +
				",\"publisher_admitted_to_ready_ns\":" +
				duration_json(accounting_->publisher_ready.snapshot()) +
				",\"reply_ready_to_retired_ns\":" + duration_json(accounting_->reply_retired.snapshot()) +
				",\"wake_lateness_ns\":" + duration_json(wake_lateness.snapshot()) +
				",\"io_job_settlement_ns\":" + duration_json(job_settlement.snapshot()) +
				",\"io_job_fence_observed_ns\":" + duration_json(fenced_job_observed.snapshot()) +
				storage_health + ",\"peer_incoming_bytes\":" + std::to_string(peer_occupancy.incoming_bytes) +
				",\"peer_outgoing_bytes\":" + std::to_string(peer_occupancy.outgoing_bytes) +
				",\"peer_incoming_high_water\":" + std::to_string(peer_occupancy.incoming_high_water) +
				",\"peer_outgoing_high_water\":" + std::to_string(peer_occupancy.outgoing_high_water) +
				",\"peer_connections\":" + std::to_string(peer_occupancy.connections) +
				",\"peer_handshakes\":" + std::to_string(peer_occupancy.handshakes) +
				",\"peer_queued_control\":" + std::to_string(peer_occupancy.queued_control) +
				",\"peer_retained_inputs\":" + std::to_string(peer_occupancy.retained_inputs) +
				",\"peer_decoded_to_admitted_ns\":" +
				duration_json(peers ? peers->stats().decoded_to_admitted.snapshot()
									: kronuz::metrics::DurationSnapshot{}) +
				",\"peer_admitted\":" + std::to_string(peers ? peers->stats().admitted : 0) +
				",\"peer_dropped\":" + std::to_string(peers ? peers->stats().dropped : 0) +
				",\"checkpoint_busy\":" + (node.checkpoint_busy() ? "true" : "false") +
				",\"fenced\":" + (node.worker().fenced() ? "true" : "false") + "}";
			{
				std::lock_guard lock(mutex_);
				health_ = std::move(encoded);
				service.health_stamp_ = captured_at;
			}
			last_health = captured_at;
		};
		if (std::filesystem::exists(options_.directory / "manifest")) {
			node.recover();
		} else {
			Identity id{};
			std::random_device random;
			for (char &byte : id) {
				byte = static_cast<char>(random());
			}
			if (id == Identity{}) {
				id[0] = 1;
			}
			node.create(id);
		}
		auto origin = Clock::now();
		while (!node.worker().ready()) {
			node.service({0, election_delay()});
			dispatch(owned_node);
			node.take_network_actions();
			if (node.worker().fenced()) {
				throw std::runtime_error("authority storage initialization fenced");
			}
			timer.expires_after(std::chrono::milliseconds(1));
			co_await timer.async_wait(asio::use_awaitable);
		}
		// Seed the first deadline before Start. Later Tick samples affect future
		// resets and cannot randomize a deadline that is already in force.
		node.service({0, election_delay()});
		node.start();
		// Storage readiness precedes incremental committed application replay.
		// Finish that replay before admitting live requests or optional history.
		while (node.state().applied_index() < node.worker().committed() || node.application_work_pending()) {
			node.service({0, election_delay()});
			dispatch(owned_node);
			check(!node.worker().fenced(), "authority recovery application fenced");
			if (node.state().reclamation_pending()) {
				co_await asio::post(io, asio::use_awaitable);
			} else {
				timer.expires_after(std::chrono::milliseconds(1));
				co_await timer.async_wait(asio::use_awaitable);
			}
		}
		node.attach_change_recorder(history);
		if (options_.peers) {
			peers = std::make_shared<AuthorityPeers>(io.get_executor(), owned_node,
													 peer_configuration(node_options), *options_.peers);
			peers->start();
		}
		check(!node.worker().fenced(), "authority committed application/profile mismatch");
		publish_health();
		ready.set_value();
		started = true;
		auto scheduled_wakeup = Clock::now();
		for (;;) {
			auto owner_started = Clock::now();
			auto cpu_started = owner_cpu_ns();
			wake_lateness.observe(elapsed_ns(scheduled_wakeup, owner_started));
			{
				std::lock_guard lock(mutex_);
				if (stopping_) {
					break;
				}
			}
			auto now = Clock::now();
			auto tick = static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(now - origin).count());
			// Fresh independent entropy avoids repeated split votes after partitions.
			last_tick = tick;
			if (peers)
				peers->admit();
			CommandWorkBudget node_budget, publisher_budget;
			node_budget.bytes = publisher_budget.bytes = 4096;
			node_budget.records = publisher_budget.records = 8;
			node_budget.updates = publisher_budget.updates = 4;
			node_budget.receipts = publisher_budget.receipts = 16;
			node.service({tick, election_delay()}, node_budget);
			// Node always runs first. Reuse only its unspent allocation so idle
			// application work does not turn frontend progress into extra wakes.
			publisher_budget.bytes += node_budget.bytes;
			publisher_budget.records += node_budget.records;
			publisher_budget.updates += node_budget.updates;
			publisher_budget.receipts += node_budget.receipts;
			dispatch(owned_node);
			if (failure) {
				std::rethrow_exception(failure);
			}
			for (auto &action : node.take_network_actions()) {
				if (std::holds_alternative<kronuz::consensus::Send>(action)) {
					if (!peers)
						throw std::logic_error("single-voter authority cannot send");
					peers->send(std::move(std::get<kronuz::consensus::Send>(action)));
				}
			}
			if (peers)
				peers->pump();
			if (completion_cursor == held_completions.size()) {
				held_completions = node.take_completions();
				completion_cursor = completion_task = 0;
			}
			for (unsigned callbacks = 0; completion_cursor < held_completions.size() && callbacks < 8;) {
				auto &completion = held_completions[completion_cursor];
				auto session = session_pending.find(completion.request);
				if (session != session_pending.end()) {
					auto task = session->second;
					task->quorum_pending = false;
					session_pending.erase(session);
					if (completion.rejection || node.worker().fenced())
						complete(task, {503, read_failure(completion, node.worker().fenced())});
					else {
						task->quorum_ready = true;
						task->generation = completion.index;
						if (task->kind == Task::Kind::SessionNext && !task->settled) {
							task->export_ready = true;
							++subscription_ready;
						}
					}
					++completion_cursor;
					completion_task = 0;
					++callbacks;
					continue;
				}
				auto found = pending.find(completion.request);
				if (found == pending.end()) {
					++completion_cursor;
					completion_task = 0;
					++callbacks;
					continue;
				}
				if (completion_task == found->second.size()) {
					pending.erase(found);
					++completion_cursor;
					completion_task = 0;
					continue;
				}
				const auto i = completion_task++;
				auto &task = found->second[i];
				++callbacks;
				if (completion.rejection || node.worker().fenced())
					complete(task, {503, read_failure(completion, node.worker().fenced())});
				else if (task->kind == Task::Kind::Command) {
					auto r = completion.results.at(i);
					complete(task, {result_status(r), result_json(r)});
				} else if (task->kind == Task::Kind::Result) {
					auto r = node.state().lookup(task->admission->credential->producer, task->generation,
												 task->sequence);
					complete(task, {result_status(r), result_json(r)});
				} else {
					check(read_count < ready_reads.size(), "read ready capacity exhausted");
					ready_reads[read_tail] = {task, completion.index};
					read_tail = (read_tail + 1) % ready_reads.size();
					++read_count;
					if (task->kind == Task::Kind::Distribution)
						++legacy_ready;
				}
			}

			// Keep a stable request/task cursor; completion delivery may erase map
			// nodes between turns. Expiration never cancels accepted Core or IO work.
			for (unsigned checked = 0; checked < 8 && !pending.empty(); ++checked) {
				auto it = pending.lower_bound(expiry_request);
				if (it == pending.end()) {
					expiry_request = 0;
					expiry_task = 0;
					break;
				}
				if (it->first != expiry_request) {
					expiry_request = it->first;
					expiry_task = 0;
				}
				auto &tasks = it->second;
				if (expiry_task < tasks.size()) {
					auto &task = tasks[expiry_task++];
					if (!task->settled && (node.worker().fenced() || now >= task->deadline))
						complete(task, {503, "{\"error\":\"outcome_unknown\"}"});
				}
				if (expiry_task == tasks.size()) {
					const auto id = it->first;
					if (std::all_of(tasks.begin(), tasks.end(), [](auto &task) { return task->settled; })) {
						node.forget(id);
						pending.erase(it);
					}
					expiry_request = id + 1;
					expiry_task = 0;
				}
			}

			if (node.worker().fenced()) {
				throw std::runtime_error("authority worker fenced");
			}
			std::deque<std::shared_ptr<Task>> expired;
			{
				std::lock_guard lock(mutex_);
				while (!queue_.empty() && now >= queue_.front()->deadline && expired.size() < 8) {
					expired.push_back(queue_.front());
					queue_.pop_front();
				}
			}
			for (auto &task : expired) {
				complete(task, {503, "{\"error\":\"outcome_unknown\"}"});
			}
			auto session_error = [&](const std::shared_ptr<Task> &task, SubscriptionResult result) {
				int status = 409;
				const char *error = "session_out_of_sync";
				switch (result) {
				case SubscriptionResult::Expired:
					status = 410;
					error = "session_expired";
					break;
				case SubscriptionResult::Forbidden:
					status = 403;
					error = "scope_forbidden";
					break;
				case SubscriptionResult::IdentityChanged:
					error = "identity_changed";
					break;
				case SubscriptionResult::SnapshotRequired:
					error = "snapshot_required";
					break;
				case SubscriptionResult::Busy:
					error = "session_busy";
					break;
				case SubscriptionResult::Full:
					status = 503;
					error = "admission_full";
					break;
				default:
					break;
				}
				complete(task, {status, "{\"error\":\"" + std::string(error) + "\"}"});
			};
			auto authorize = [&](TaskQueue &queue, distribution::WorkBudget work) {
				while (queue.count && work.tokens && work.bytes) {
					auto held = queue.pop();
					auto &task = *held;
					task.authorization_queued = false;
					--work.tokens;
					if (task.settled || now >= task.deadline || task.delivery->settled.load()) {
						complete(held, {503, "{\"error\":\"authorization_timeout\"}"});
						continue;
					}
					const auto *metadata = subscriptions->metadata(task.session, now);
					if (!metadata || metadata->incarnation != task.authorization_incarnation) {
						session_error(held, SubscriptionResult::Expired);
						continue;
					}
					if (metadata->principal != task.principal || metadata->scope != task.delivery->scope) {
						session_error(held, SubscriptionResult::IdentityChanged);
						continue;
					}
					const auto &grants = (*accounting_->credential_grants)[task.admission->credential_index];
					for (unsigned compared = 0; compared < 8 && work.tokens && !task.settled; ++compared) {
						if (task.authorization_cursor == task.delivery->scope->size())
							break;
						if (task.grant_cursor == grants.names.size()) {
							session_error(held, SubscriptionResult::Forbidden);
							break;
						}
						const auto name = task.delivery->scope->name(task.authorization_cursor);
						const auto grant = grants.names[task.grant_cursor];
						const auto bytes = name.size() + grant.size() + 1;
						if (bytes > work.bytes)
							break;
						work.bytes -= bytes;
						--work.tokens;
						const auto order = name.compare(grant);
						if (order < 0) {
							session_error(held, SubscriptionResult::Forbidden);
							break;
						}
						if (!order)
							++task.authorization_cursor;
						++task.grant_cursor;
					}
					if (task.settled)
						continue;
					if (task.authorization_cursor == task.delivery->scope->size()) {
						const auto result = subscriptions->authorize(
							task.session, task.authorization_incarnation, task.principal,
							task.delivery->scope->identity(), grants.digest, now);
						if (result == SubscriptionResult::Accepted)
							task.authorization_ready = true;
						else
							session_error(held, result);
					} else {
						task.authorization_queued = true;
						queue.push(std::move(held));
						if (work.bytes < 397)
							break;
					}
				}
			};
			authorize(*close_authorization, {65536, 512});
			authorize(*session_authorization, {8192, 64});
			CommandWorkBudget session_cleanup{publisher_budget.bytes / 4, 0, 0,
											  publisher_budget.receipts / 4};
			const auto cleanup_bytes = session_cleanup.bytes, cleanup_receipts = session_cleanup.receipts;
			for (unsigned checked = 0; checked < 16; ++checked) {
				const bool close_lane = checked < 8;
				const auto close_slots = options_.subscription_requests.closes;
				const auto ordinal = close_lane ? close_cursor : close_slots + session_cursor;
				if (close_lane)
					close_cursor = (close_cursor + 1) % close_slots;
				else
					session_cursor = (session_cursor + 1) % (session_tasks.size() - close_slots);
				auto &held = session_tasks[ordinal];
				if (!held) {
					std::lock_guard lock(mutex_);
					auto &queue =
						close_lane ? service.subscription_close_queue_ : service.subscription_queue_;
					if (!queue.empty()) {
						held = std::move(queue.front());
						held->retirement_slot = ordinal;
						queue.pop_front();
					}
				}
				if (!held)
					continue;
				auto &task = *held;
				auto &delivery = *task.delivery;
				if (!task.settled && !task.exporting && (now >= task.deadline || delivery.settled.load()))
					complete(held, {503, "{\"error\":\"outcome_unknown\"}"});
				if (task.settled) {
					delivery.pump_control();
					if (task.quorum_pending || task.exporting || task.authorization_queued ||
						!delivery.settled.load())
						continue;
					if (!task.retiring) {
						begin_delivery_retirement(task);
						if (task.subscription_request) {
							subscriptions->release(*task.subscription_request);
							auto active = active_session_requests.find(task.session);
							if (active != active_session_requests.end() &&
								active->second.lock().get() == &task)
								active_session_requests.erase(active);
						}
						if (task.kind == Task::Kind::SessionOpen && task.session_initialized &&
							!delivery.written.load())
							subscriptions->close(task.session, task.principal);
						session_retirement->push(held);
					}
					continue;
				}
				if (task.quorum_pending || task.exporting)
					continue;
				if (task.authorization_queued)
					continue;
				try {
					if (task.kind != Task::Kind::SessionOpen && !task.session_initialized) {
						const auto *metadata = subscriptions->metadata(task.session, now);
						if (!metadata) {
							if (task.kind == Task::Kind::SessionClose) {
								const auto result = subscriptions->close(task.session, task.principal);
								if (result == SubscriptionResult::Accepted)
									complete(held, {204, {}});
								else
									session_error(held, result);
							} else
								session_error(held, SubscriptionResult::Expired);
							continue;
						}
						if (metadata->principal != task.principal) {
							session_error(held, metadata->principal.identity == task.principal.identity
													? SubscriptionResult::IdentityChanged
													: SubscriptionResult::Forbidden);
							continue;
						}
						delivery.scope = metadata->scope;
						delivery.compress = metadata->compression;
						if (!task.authorization_incarnation)
							task.authorization_incarnation = metadata->incarnation;
						if (task.authorization_incarnation != metadata->incarnation) {
							session_error(held, SubscriptionResult::Expired);
							continue;
						}
						const auto grants =
							(*accounting_->credential_grants)[task.admission->credential_index].digest;
						if (metadata->creator_grants == grants || metadata->last_authorized_grants == grants)
							task.authorization_ready = true;
						if (!task.authorization_ready) {
							task.authorization_queued = true;
							(task.kind == Task::Kind::SessionClose ? *close_authorization
																   : *session_authorization)
								.push(held);
							continue;
						}
						if (task.kind == Task::Kind::SessionClose) {
							const auto result = subscriptions->close(task.session, task.principal,
																	 task.authorization_incarnation);
							if (result == SubscriptionResult::Accepted)
								complete(held, {204, {}});
							else
								session_error(held, result);
							continue;
						}
						auto active = active_session_requests.find(task.session);
						if (active != active_session_requests.end()) {
							if (auto previous = active->second.lock();
								previous && previous->settled && previous->delivery->settled.load() &&
								!previous->quorum_pending && !previous->exporting &&
								previous->subscription_request)
								subscriptions->release(*previous->subscription_request);
						}
						const bool inserted = active == active_session_requests.end();
						if (inserted)
							active =
								active_session_requests.emplace(task.session, std::weak_ptr<Task>{}).first;
						auto admitted =
							subscriptions->next(task.session, task.principal, task.sequence, task.published,
												now, task.authorization_incarnation);
						if (!admitted.token) {
							if (inserted)
								active_session_requests.erase(active);
							session_error(held, admitted.result);
							continue;
						}
						active->second = held;
						task.subscription_request = admitted.token;
						task.session_initialized = true;
					}
					const auto &context = options_.node.context;
					distribution::Cursor current{context.cluster, context.group, context.epoch,
												 delivery.scope->identity(), node.state().revision()};
					if (task.kind == Task::Kind::SessionOpen && task.quorum_ready) {
						check(node.state().applied_index() >= task.generation,
							  "subscription boundary not published");
						const auto result = subscriptions->open(
							task.session, task.principal, delivery.scope, current, delivery.base,
							task.force_snapshot, delivery.compress, *history, now,
							(*accounting_->credential_grants)[task.admission->credential_index].digest);
						if (result != SubscriptionResult::Accepted) {
							session_error(held, result);
							continue;
						}
						task.session_initialized = true;
						auto probe = subscriptions->next(task.session, task.principal, 0, {}, now);
						check(probe.token.has_value(), "subscription negotiation state");
						const auto mode = subscriptions->poll(*probe.token, current, *history, now);
						subscriptions->release(*probe.token);
						complete(
							held,
							{200, "{\"version\":1,\"session\":\"" + hex(task.session) +
									  "\",\"window\":0,\"mode\":\"" +
									  (mode == SubscriptionResult::SnapshotReady ? "snapshot" : "resume") +
									  "\",\"wait_ms\":" + std::to_string(options_.subscription_wait.count()) +
									  "}"});
						continue;
					}
					if (task.kind == Task::Kind::SessionNext) {
						const auto result =
							subscriptions->poll(*task.subscription_request, current, *history, now);
						if (result == SubscriptionResult::AwaitSettlement)
							continue;
						if (result == SubscriptionResult::Parked) {
							if (task.export_ready) {
								task.export_ready = false;
								--subscription_ready;
							}
							if (now >= task.poll_deadline) {
								subscription_idle.add();
								complete(held, {204, {}});
							}
							continue;
						}
						if (result != SubscriptionResult::SnapshotReady &&
							result != SubscriptionResult::ResumeReady) {
							session_error(held, result);
							continue;
						}
						if (task.quorum_ready && !delivering && (subscription_export_turn || !legacy_ready)) {
							check(node.state().applied_index() >= task.generation,
								  "subscription boundary not published");
							delivery.selection = result == SubscriptionResult::SnapshotReady
													 ? ExportSelection::SnapshotOnly
													 : ExportSelection::ResumeOnly;
							delivery.base = subscriptions->base(*task.subscription_request);
							delivery.handoff = subscriptions->handoff(*task.subscription_request);
							delivering.emplace();
							delivering->task = held;
							task.exporting = true;
							subscription_export_turn = false;
							task.export_ready = false;
							--subscription_ready;
						}
					}
					if (!task.quorum_ready && !task.exporting && session_pending.size() < 4 &&
						pending.size() + session_pending.size() <
							std::max<std::size_t>(1, options_.node.waiters / 2)) {
						check(next < (std::uint64_t{1} << 63) - 1, "authority local request exhaustion");
						const auto id = ++next;
						session_pending.emplace(id, held);
						task.quorum_pending = true;
						if (node.read(id) == kronuz::consensus::SubmitResult::Accepted) {
							task.quorum_pending = true;
							subscription_reads.add();
						} else {
							task.quorum_pending = false;
							session_pending.erase(id);
						}
					}
				} catch (const std::exception &) {
					if (task.quorum_pending)
						throw;
					complete(held, {503, "{\"error\":\"distribution_unavailable\"}"});
				}
			}
			{
				while (session_retirement->count && session_cleanup.receipts) {
					auto &task = session_retirement->ring[session_retirement->head];
					const auto bytes = std::min(task->retirement_remaining, session_cleanup.bytes);
					task->retirement_remaining -= bytes;
					session_cleanup.bytes -= bytes;
					if (task->retirement_remaining)
						break;
					--session_cleanup.receipts;
					auto retired = session_retirement->pop();
					session_tasks[retired->retirement_slot].reset();
					retired.reset();
				}
				publisher_budget.bytes -= cleanup_bytes - session_cleanup.bytes;
				publisher_budget.receipts -= cleanup_receipts - session_cleanup.receipts;
				CommandWorkBudget cleanup{publisher_budget.bytes / 4, 0, 0, publisher_budget.receipts / 4};
				const auto reserved_bytes = cleanup.bytes, reserved_receipts = cleanup.receipts;
				const auto visits = std::min<std::size_t>(control_count, 8);
				for (std::size_t i = 0; i < visits; ++i) {
					auto task = std::move(legacy_deliveries[control_head]);
					control_head = (control_head + 1) % legacy_deliveries.size();
					--control_count;
					auto &delivery = *task->delivery;
					delivery.pump_control();
					if (delivery.settled.load() && !task->exporting) {
						begin_delivery_retirement(*task);
						const auto bytes = std::min(task->retirement_remaining, cleanup.bytes);
						task->retirement_remaining -= bytes;
						cleanup.bytes -= bytes;
						if (!task->retirement_remaining && cleanup.receipts) {
							--cleanup.receipts;
							task.reset();
						}
					}
					if (task) {
						legacy_deliveries[control_tail] = std::move(task);
						control_tail = (control_tail + 1) % legacy_deliveries.size();
						++control_count;
					}
				}
				publisher_budget.bytes -= reserved_bytes - cleanup.bytes;
				publisher_budget.receipts -= reserved_receipts - cleanup.receipts;
				distribution::WorkBudget collection{publisher_budget.bytes / 4, 4};
				const auto bytes = collection.bytes;
				subscriptions->collect(collection, now);
				publisher_budget.bytes -= bytes - collection.bytes;
			}
			CommandWorkBudget read_budget{0, 0, 0, 0};
			if (reading || read_count || delivering) {
				bool publisher_runnable = preparing.has_value() || carry_task || retired_count;
				{
					std::lock_guard lock(mutex_);
					publisher_runnable = publisher_runnable || !queue_.empty();
				}
				read_first = !read_first;
				if (!publisher_runnable || read_first) {
					read_budget = publisher_budget;
					publisher_budget = {0, 0, 0, 0};
				}
			}
			if (!reading && read_count && read_budget.records) {
				auto &ready_read = ready_reads[read_head];
				bool consume = true;
				if (now >= ready_read.task->deadline) {
					complete(ready_read.task, {503, "{\"error\":\"outcome_unknown\"}"});
				} else if (ready_read.task->kind == Task::Kind::Distribution) {
					if (delivering || (subscription_export_turn && subscription_ready)) {
						consume = false;
						// Rotate bounded metadata, never capture another authority root.
						if (read_tail != read_head) {
							auto waiting = std::move(ready_read);
							ready_read = {};
							ready_reads[read_tail] = std::move(waiting);
						}
						read_tail = (read_tail + 1) % ready_reads.size();
						read_head = (read_head + 1) % ready_reads.size();
					} else {
						check(node.state().applied_index() >= ready_read.boundary,
							  "distribution boundary not published");
						delivering.emplace();
						delivering->task = ready_read.task;
						delivering->task->exporting = true;
						subscription_export_turn = true;
					}
				} else {
					check(node.state().applied_index() >= ready_read.boundary, "read boundary not published");
					reading.emplace(ready_read.task, node.state());
				}
				if (consume) {
					if (ready_read.task->kind == Task::Kind::Distribution)
						--legacy_ready;
					ready_read = {};
					read_head = (read_head + 1) % ready_reads.size();
					--read_count;
				}
			}
			if (delivering) {
				auto &slot = *delivering;
				auto &delivery = *slot.task->delivery;
				distribution::WorkBudget wire{read_budget.bytes / 2, read_budget.receipts / 2};
				read_budget.bytes -= wire.bytes;
				read_budget.receipts -= wire.tokens;
				auto terminate = [&] {
					if (delivery.started)
						delivery.reply->abandon();
					complete(slot.task, {503, "{\"error\":\"distribution_unavailable\"}"});
					slot.retiring = true;
				};
				if (!slot.retiring) {
					try {
						if (delivery.settled.load() || now >= slot.task->deadline) {
							if (delivery.settled.load())
								export_disconnected.add();
							else
								export_expired.add();
							terminate();
						} else if (wire.bytes && wire.tokens) {
							if (!slot.exporter) {
								distribution::Identity update{};
								check(RAND_bytes(reinterpret_cast<unsigned char *>(update.data()),
												 update.size()) == 1,
									  "distribution update entropy");
								slot.exporter.emplace(node.state(), delivery.scope, history, delivery.base,
													  update, delivery.compress, options_.delivery,
													  delivery.selection, delivery.handoff);
								if (slot.task->subscription_request)
									subscription_exports.add();
							}
							if (!slot.pending && delivery.writable.load())
								slot.exporter->step(wire);
							if (slot.exporter->counted() && !delivery.started) {
								std::optional<distribution_http::SessionHeaders> session;
								if (slot.task->subscription_request) {
									slot.task->subscription_offer = subscriptions->offer(
										*slot.task->subscription_request, slot.exporter->target(),
										slot.exporter->history_handoff(), now);
									session = {slot.task->session, slot.task->subscription_offer->window};
								}
								auto metadata = distribution_http::update_headers(slot.exporter->kind(),
																				  slot.exporter->target(),
																				  delivery.compress, session);
								if (delivery.reply->start_chunked(metadata, true) !=
									http::StreamSubmit::Accepted)
									throw std::runtime_error("distribution stream closed");
								delivery.started = true;
							}
							if (!slot.pending && slot.exporter->frame_ready())
								slot.pending = slot.exporter->take_frame();
							if (slot.pending) {
								const auto result = delivery.publish(slot.pending);
								if (result != http::StreamSubmit::Accepted &&
									result != http::StreamSubmit::Busy)
									throw std::runtime_error("distribution stream refused frame");
							}
							if (slot.exporter->done() && !slot.pending) {
								if (delivery.reply->finish() != http::StreamSubmit::Accepted)
									throw std::runtime_error("distribution stream refused finish");
								complete(slot.task, {200, {}});
								slot.retiring = true;
							}
						}
					} catch (const SnapshotRequired &reset) {
						if (slot.task->subscription_request && !delivery.started) {
							subscriptions->close(slot.task->session, slot.task->principal);
							complete(slot.task,
									 {409, std::string("{\"error\":\"snapshot_required\",\"reason\":\"") +
											   (reset.reason() == SnapshotRequired::Reason::HistoryLost
													? "history_lost"
													: "representation_limit") +
											   "\"}"});
							slot.retiring = true;
						} else
							terminate();
					} catch (const std::length_error &) {
						export_limits.add();
						terminate();
					} catch (const std::exception &) {
						export_errors.add();
						terminate();
					}
				}
				if (slot.retiring) {
					slot.discard_pending(wire);
					const bool drained = !slot.exporter || slot.exporter->discard(wire, read_budget);
					slot.source_drained = drained && !slot.pending;
					if (drained && !slot.pending && delivery.settled.load()) {
						if (slot.task->subscription_offer)
							subscriptions->settle(*slot.task->subscription_offer, delivery.written.load());
						slot.task->exporting = false;
						delivering.reset();
					}
				}
				read_budget.bytes += wire.bytes;
				read_budget.receipts += wire.tokens;
			}
			if (reading && read_budget.records) {
				auto &r = *reading;
				auto retire_read = [&] {
					r.encoder.reset();
					r.reservation.reset();
					r.capture.retire();
					r.retiring = true;
				};
				if (!r.retiring) {
					try {
						if (Clock::now() >= r.task->deadline) {
							complete(r.task, {503, "{\"error\":\"outcome_unknown\"}"});
							retire_read();
						} else {
							if (!r.encoder)
								r.encoder.emplace(r.capture, r.task->name_space, r.task->keys,
												  options_.node.context, options_.reply_bytes - 256);
							r.quota_blocked = false;
							if (!r.encoder->counted())
								r.encoder->step(read_budget);
							if (r.encoder->counted() && (!r.reservation || !r.reservation->active) &&
								read_budget.records && read_budget.bytes >= 256) {
								--read_budget.records;
								read_budget.bytes -= 256;
								if (!r.reservation) {
									r.reservation = std::make_shared<ReplyAdmission>();
									r.reservation->accounting = accounting_;
									r.reservation->bytes = r.encoder->encoded_size() + 256;
								}
								auto &lease = r.reservation;
								{
									std::lock_guard lock(accounting_->mutex);
									if (lease->bytes <= options_.reply_bytes - accounting_->reply_bytes) {
										accounting_->reply_bytes += lease->bytes;
										lease->active = true;
									}
								}
								if (lease->active)
									r.encoder->begin_encoding();
								else
									r.quota_blocked = true;
							}
							if (r.reservation && r.reservation->active)
								r.encoder->step(read_budget);
							if (r.encoder->done()) {
								if (Clock::now() >= r.task->deadline)
									complete(r.task, {503, "{\"error\":\"outcome_unknown\"}"});
								else
									complete(r.task,
											 {200, r.encoder->take(), true, std::move(r.reservation)});
								retire_read();
							}
						}
					} catch (const std::length_error &) {
						complete(r.task, {413, "{\"error\":\"reply_bound\"}"});
						retire_read();
					} catch (const std::bad_alloc &) {
						complete(r.task, {503, "{\"error\":\"outcome_unknown\"}"});
						retire_read();
					}
				}
				if (r.retiring) {
					if (!r.capture.shares_reclamation_with(node.state()))
						r.capture.collect(read_budget);
					if (r.capture.shares_reclamation_with(node.state()) || !r.capture.reclamation_pending())
						reading.reset();
				}
			}
			// Publisher preparation owns task charges across encoding and admission.
			// One encoded command that does not fit is carried to the next batch.
			collect_tasks(publisher_budget);
			if (carry_task && now >= carry_task->deadline) {
				complete(carry_task, {503, "{\"error\":\"outcome_unknown\"}"});
				carry_task.reset();
				carry.reset();
			}
			if (!preparing && publisher_budget.records) {
				std::shared_ptr<Task> first;
				if (carry_task)
					first = std::move(carry_task);
				else {
					std::lock_guard lock(mutex_);
					if (!queue_.empty() &&
						(queue_.front()->kind != Task::Kind::Command || queue_.size() >= 64 ||
						 now >= queue_.front()->deadline - options_.timeout + std::chrono::milliseconds(5))) {
						first = queue_.front();
						queue_.pop_front();
					}
				}
				if (first) {
					check(next < (std::uint64_t{1} << 63) - 1, "authority local request exhaustion");
					if (first->kind != Task::Kind::Command) {
						auto id = ++next;
						// Reserve bookkeeping before accepting the read obligation.
						pending.emplace(id, Tasks{first});
						auto result = node.read(id);
						if (result != kronuz::consensus::SubmitResult::Accepted) {
							pending.erase(id);
							std::lock_guard lock(mutex_);
							queue_.push_front(std::move(first));
						}
					} else {
						preparing.emplace();
						auto &p = *preparing;
						p.request = ++next;
						p.builder.emplace(options_.node.context, options_.node.application,
										  options_.node.consensus.command_bytes);
						p.current = std::move(first);
						if (carry) {
							p.encoded.emplace(std::move(*carry));
							carry.reset();
						}
					}
				}
			}
			if (preparing) {
				auto &p = *preparing;
				if (!p.proposal && ((p.current && now >= p.current->deadline) ||
									std::any_of(p.tasks.begin(), p.tasks.end(),
												[&](const auto &task) { return now >= task->deadline; })))
					p.cancelling = true;
				if (p.proposal && !p.tasks.empty() && now >= p.tasks.front()->deadline)
					p.cancelling = true;
				if (p.cancelling) {
					bool encoder_drained = !p.encoder || p.encoder->discard(publisher_budget);
					if (encoder_drained) {
						p.encoder.reset();
						p.encoded.reset();
						p.builder.reset();
						p.proposal.reset();
						p.waiter = {};
						if (p.current && publisher_budget.records && publisher_budget.bytes >= 256) {
							--publisher_budget.records;
							publisher_budget.bytes -= 256;
							complete(p.current, {503, "{\"error\":\"outcome_unknown\"}"});
							p.current.reset();
						}
						while (!p.current && p.cancelled < p.tasks.size() && publisher_budget.records &&
							   publisher_budget.bytes >= 256) {
							--publisher_budget.records;
							publisher_budget.bytes -= 256;
							complete(p.tasks[p.cancelled++], {503, "{\"error\":\"outcome_unknown\"}"});
						}
						if (!p.current && p.cancelled == p.tasks.size())
							preparing.reset();
					}
				} else {
					while (!p.sealed && publisher_budget.bytes) {
						if (p.builder->command_pending()) {
							p.builder->step(publisher_budget);
							if (p.builder->command_pending())
								break;
						}
						if (p.current) {
							if (!p.encoded) {
								if (!p.encoder)
									p.encoder.emplace(p.current->command, options_.node.application);
								p.encoder->step(publisher_budget);
								if (!p.encoder->done())
									break;
								p.encoded.emplace(p.encoder->take_encoded());
								p.encoder.reset();
							}
							if (!p.builder->append(*p.encoded)) {
								check(p.builder->count() != 0, "publisher command cannot fit an empty batch");
								carry_task = std::move(p.current);
								carry.emplace(std::move(*p.encoded));
								p.encoded.reset();
								p.builder->seal();
								p.sealed = true;
								break;
							}
							p.encoded.reset();
							p.tasks.push_back(std::move(p.current));
							continue;
						}
						if (p.builder->count() == 64) {
							p.builder->seal();
							p.sealed = true;
							break;
						}
						{
							std::lock_guard lock(mutex_);
							if (!queue_.empty() && queue_.front()->kind == Task::Kind::Command &&
								now < queue_.front()->deadline) {
								p.current = queue_.front();
								queue_.pop_front();
							}
						}
						if (!p.current) {
							p.builder->seal();
							p.sealed = true;
							break;
						}
					}
					if (p.sealed && !p.proposal) {
						p.builder->step(publisher_budget);
						if (p.builder->done()) {
							p.proposal.emplace(p.builder->take());
							p.builder.reset();
							PendingTasks reserved;
							reserved.emplace(p.request, p.tasks);
							p.waiter = reserved.extract(p.request);
						}
					}
					if (p.proposal && Clock::now() >= p.tasks.front()->deadline)
						p.cancelling = true;
					if (p.proposal && !p.cancelling) {
						auto id = p.request;
						pending.insert(std::move(p.waiter));
						auto result = node.propose_prepared(id, *p.proposal);
						if (result == kronuz::consensus::SubmitResult::Accepted)
							preparing.reset();
						else {
							p.waiter = pending.extract(id);
							if (result == kronuz::consensus::SubmitResult::Fenced)
								p.cancelling = true;
						}
					}
				}
			}
			if (Clock::now() - last_health >= std::chrono::milliseconds(250)) {
				publish_health();
			}
			auto cpu_finished = owner_cpu_ns();
			owner_turn.observe(elapsed_ns(owner_started));
			if (cpu_started && cpu_finished && *cpu_finished >= *cpu_started) {
				owner_cpu.observe(*cpu_finished - *cpu_started);
			} else {
				cpu_clock_errors.add();
			}
			if (node.application_work_pending() || retired_count || (read_count && !reading && !delivering) ||
				(delivering && ((!delivering->retiring && delivering->task->delivery->writable.load()) ||
								(delivering->retiring && !delivering->source_drained))) ||
				(reading && (reading->retiring || !reading->quota_blocked)) ||
				completion_cursor < held_completions.size() ||
				(preparing && (!preparing->proposal || preparing->cancelling))) {
				scheduled_wakeup = Clock::now();
				co_await asio::post(io, asio::use_awaitable);
			} else {
				scheduled_wakeup = Clock::now() + std::chrono::milliseconds(1);
				timer.expires_at(scheduled_wakeup);
				co_await timer.async_wait(asio::use_awaitable);
			}
		}
	} catch (...) {
		if (!started) {
			ready.set_exception(std::current_exception());
		} else {
			std::lock_guard lock(mutex_);
			stopping_ = true;
			service.health_stamp_ = Clock::now();
			health_ = std::string("{\"mode\":\"") +
					  (options_.peers ? "durable_fixed_voters" : "durable_single_voter") +
					  "\",\"fenced\":true}";
		}
	}
	accounting_->subscription_requests->close();
	if (subscriptions)
		subscriptions->close_all();
	close_authorization.reset();
	session_authorization.reset();
	for (auto &task : session_tasks) {
		if (!task)
			continue;
		complete(task, {503, "{\"error\":\"stopping\"}"});
		task->authorization_queued = false;
		task->delivery->reply->abandon();
	}
	if (owned_node)
		owned_node->close();
	if (peers)
		peers->stop();
	if (delivering) {
		delivering->task->delivery->reply->abandon();
		complete(delivering->task, {503, "{\"error\":\"stopping\"}"});
		for (;;) {
			distribution::WorkBudget wire;
			CommandWorkBudget roots;
			delivering->discard_pending(wire);
			const bool drained = !delivering->exporter || delivering->exporter->discard(wire, roots);
			if (drained && !delivering->pending && delivering->task->delivery->settled.load())
				break;
			co_await asio::post(io, asio::use_awaitable);
		}
		if (delivering->task->subscription_offer)
			subscriptions->settle(*delivering->task->subscription_offer, false);
		delivering->task->exporting = false;
		delivering.reset();
	}
	if (reading) {
		complete(reading->task, {503, "{\"error\":\"outcome_unknown\"}"});
		reading->encoder.reset();
		reading->reservation.reset();
		reading->capture.retire();
		while (!reading->capture.shares_reclamation_with(owned_node->state()) &&
			   reading->capture.reclamation_pending()) {
			CommandWorkBudget budget;
			reading->capture.collect(budget);
			co_await asio::post(io, asio::use_awaitable);
		}
		reading.reset();
	}
	while (read_count) {
		complete(ready_reads[read_head].task, {503, "{\"error\":\"outcome_unknown\"}"});
		ready_reads[read_head] = {};
		read_head = (read_head + 1) % ready_reads.size();
		--read_count;
		co_await asio::post(io, asio::use_awaitable);
	}
	if (preparing && preparing->encoder) {
		for (;;) {
			CommandWorkBudget budget;
			if (preparing->encoder->discard(budget))
				break;
			co_await asio::post(io, asio::use_awaitable);
		}
	}
	unsigned terminal_callbacks = 0;
	if (preparing) {
		if (preparing->current)
			complete(preparing->current, {503, "{\"error\":\"outcome_unknown\"}"});
		for (auto &task : preparing->tasks) {
			complete(task, {503, "{\"error\":\"outcome_unknown\"}"});
			if (++terminal_callbacks == 8) {
				terminal_callbacks = 0;
				co_await asio::post(io, asio::use_awaitable);
			}
		}
		preparing.reset();
	}
	if (carry_task) {
		complete(carry_task, {503, "{\"error\":\"outcome_unknown\"}"});
		carry_task.reset();
		carry.reset();
	}
	if (peers)
		peers->stop();
	while (active || (storage_flight && storage_flight->operation->done()) ||
		   (owned_node && !owned_node->drained()) ||
		   (peers && (!peers->drained() || !owned_node->channel().drained()))) {
		if (owned_node) {
			CommandWorkBudget budget;
			owned_node->retirement_step(budget);
			dispatch(owned_node);
		}
		if (owned_node && !owned_node->drained() && !active) {
			co_await asio::post(io, asio::use_awaitable);
		} else {
			timer.expires_after(std::chrono::milliseconds(1));
			co_await timer.async_wait(asio::use_awaitable);
		}
	}

	while (!pending.empty()) {
		auto current = pending.begin();
		auto &tasks = current->second;
		while (!tasks.empty()) {
			complete(tasks.back(), {503, "{\"error\":\"outcome_unknown\"}"});
			tasks.pop_back();
			if (++terminal_callbacks == 8) {
				terminal_callbacks = 0;
				co_await asio::post(io, asio::use_awaitable);
			}
		}
		pending.erase(current);
	}
	// Core and storage originals drain before session read reservations release.
	session_pending.clear();
	active_session_requests.clear();
	if (session_retirement)
		while (session_retirement->count)
			session_retirement->pop();
	for (auto &task : session_tasks) {
		if (!task)
			continue;
		task->quorum_pending = false;
		if (task->subscription_request)
			subscriptions->release(*task->subscription_request);
		while (!task->delivery->settled.load())
			co_await asio::post(io, asio::use_awaitable);
		begin_delivery_retirement(*task);
		while (task->retirement_remaining) {
			task->retirement_remaining -= std::min<std::size_t>(task->retirement_remaining, 8192);
			co_await asio::post(io, asio::use_awaitable);
		}
		task.reset();
	}
	if (subscriptions) {
		while (!subscriptions->drained()) {
			distribution::WorkBudget work;
			subscriptions->collect(work, Clock::now());
			co_await asio::post(io, asio::use_awaitable);
		}
	}
	std::deque<std::shared_ptr<Task>> remaining_sessions;
	std::deque<std::shared_ptr<Task>> remaining_closes;
	{
		std::lock_guard lock(mutex_);
		remaining_sessions.swap(service.subscription_queue_);
		remaining_closes.swap(service.subscription_close_queue_);
	}
	while (!remaining_sessions.empty() || !remaining_closes.empty()) {
		auto &queue = remaining_closes.empty() ? remaining_sessions : remaining_closes;
		auto &task = queue.front();
		complete(task, {503, "{\"error\":\"stopping\"}"});
		task->delivery->reply->abandon();
		while (!task->delivery->settled.load())
			co_await asio::post(io, asio::use_awaitable);
		begin_delivery_retirement(*task);
		while (task->retirement_remaining) {
			task->retirement_remaining -= std::min<std::size_t>(task->retirement_remaining, 8192);
			co_await asio::post(io, asio::use_awaitable);
		}
		queue.pop_front();
		co_await asio::post(io, asio::use_awaitable);
	}
	std::deque<std::shared_ptr<Task>> remaining;
	{
		std::lock_guard lock(mutex_);
		remaining.swap(queue_);
	}
	while (!remaining.empty()) {
		complete(remaining.front(), {503, "{\"error\":\"outcome_unknown\"}"});
		remaining.pop_front();
		if (++terminal_callbacks == 8) {
			terminal_callbacks = 0;
			co_await asio::post(io, asio::use_awaitable);
		}
	}
	while (control_count) {
		auto task = std::move(legacy_deliveries[control_head]);
		control_head = (control_head + 1) % legacy_deliveries.size();
		--control_count;
		task->delivery->reply->abandon();
		while (!task->delivery->settled.load())
			co_await asio::post(io, asio::use_awaitable);
		begin_delivery_retirement(*task);
		while (task->retirement_remaining) {
			task->retirement_remaining -= std::min<std::size_t>(task->retirement_remaining, 8192);
			co_await asio::post(io, asio::use_awaitable);
		}
		task.reset();
		co_await asio::post(io, asio::use_awaitable);
	}
	while (retired_count) {
		CommandWorkBudget budget;
		collect_tasks(budget);
		co_await asio::post(io, asio::use_awaitable);
	}
}
} // namespace detent::authority
