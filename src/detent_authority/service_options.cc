// load_service_options: the authority configuration file read, bounded and validated.
#include "service_internal.hh"
#include <asio.hpp>
#include <cctype>
#include <fcntl.h>
#include <limits>
#include <sys/stat.h>
#include <unistd.h>

namespace detent::authority {
namespace {
// A voter's host as written: an IP literal (not unspecified or multicast) or an RFC 1123 name of at
// most 253 bytes. It is never resolved here; consensus's transport resolves names (D60).
void voter_host(const std::string &host) {
	check(!host.empty() && host.size() <= 253, "voter host must be 1 to 253 bytes");
	asio::error_code error;
	if (auto address = asio::ip::make_address(host, error); !error) {
		check(!address.is_unspecified() && !address.is_multicast(), "voter host IP literal");
		return;
	}
	std::size_t start = 0;
	for (std::size_t i = 0; i <= host.size(); ++i) {
		if (i != host.size() && host[i] != '.') {
			const auto c = static_cast<unsigned char>(host[i]);
			check(std::isalnum(c) || c == '-', "voter host name characters");
			continue;
		}
		check(i > start && i - start <= 63 && host[start] != '-' && host[i - 1] != '-', "voter host name label");
		if (i != host.size())
			start = i + 1;
	}
	// A numeric final label would read as an IPv4 form (inet_aton) rather than a name.
	check(!std::all_of(host.begin() + static_cast<std::ptrdiff_t>(start), host.end(),
					   [](unsigned char c) { return std::isdigit(c); }),
		  "voter host name must not end in a numeric label");
}
// The address a named local voter listens on without resolving its name: the IPv6 wildcard when
// the host has IPv6 (consensus opens it dual-stack on every OS), else the IPv4 wildcard.
asio::ip::address local_wildcard() {
	asio::io_context io;
	asio::ip::tcp::socket probe(io);
	asio::error_code error;
	if (!probe.open(asio::ip::tcp::v6(), error))
		return asio::ip::address_v6::any();
	return asio::ip::address_v4::any();
}
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
			   "delivery_limits", "subscription_limits", "application_limits", "journal_limits",
			   "retained_state"});
	ServiceOptions o;
	if (d.HasMember("application_limits")) {
		const auto &limits = field(d, "application_limits");
		fields(limits, {"keys", "namespaces", "producers", "mutations", "request_bytes",
					   "state_bytes", "image_bytes", "definition_bytes", "overrides", "results", "layout"});
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
		// The policy the enabling entry proposes (S5.2); the log limits come from consensus_limits.
		if (limits.HasMember("layout")) {
			const auto &layout = field(limits, "layout");
			fields(layout, {"seal_transactions", "seal_bytes", "period", "carry_bytes", "carry_count", "ratio",
							"alpha_num", "alpha_den", "cap"});
			auto &policy = application.layout;
			for (auto [key, target] : {std::pair{"seal_transactions", &policy.seal_transactions},
									   {"seal_bytes", &policy.seal_bytes}, {"period", &policy.period},
									   {"carry_bytes", &policy.carry_bytes}, {"carry_count", &policy.carry_count},
									   {"ratio", &policy.ratio}, {"alpha_num", &policy.alpha_num},
									   {"alpha_den", &policy.alpha_den}, {"cap", &policy.cap}})
				if (layout.HasMember(key))
					*target = number(field(layout, key));
			try {
				validate(policy);
			} catch (const std::exception &) {
				check(false, "authority layout policy");
			}
		}
	}
	if (d.HasMember("retained_state")) {
		const auto &limits = field(d, "retained_state");
		fields(limits, {"command_bytes", "log_bytes", "log_entries", "batch_bytes", "batch_entries"});
		auto set = [&](const char *key, std::size_t &target) {
			if (!limits.HasMember(key))
				return;
			const auto value = number(field(limits, key));
			check(value <= std::numeric_limits<std::size_t>::max(), "retained-state limit width");
			target = static_cast<std::size_t>(value);
		};
		auto &retained = o.node.scheduling.retained;
		set("command_bytes", retained.command_bytes);
		set("log_bytes", retained.log_bytes);
		set("log_entries", retained.log_entries);
		set("batch_bytes", retained.batch_bytes);
		set("batch_entries", retained.batch_entries);
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
			  "fixed voter count: an odd number of voters, at least 3, within the voter limit");
		std::set<std::pair<std::string, std::uint64_t>> addresses;
		for (const auto &voter : voters.GetArray()) {
			fields(voter, {"id", "host", "port", "certificate_sha256"});
			auto id = number(field(voter, "id")), port = number(field(voter, "port"));
			check(id && port && port <= 65535, "voter ID or port");
			auto host = text(field(voter, "host"), 1024);
			voter_host(host);
			check(addresses.emplace(host, port).second, "duplicate voter host and port");
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
			if (id == o.node.configuration.local) {
				// A name is not resolved to bind: the local voter listens on the wildcard.
				asio::error_code error;
				auto address = asio::ip::make_address(host, error);
				peers.listen = {error ? local_wildcard() : address, static_cast<unsigned short>(port)};
			} else
				peers.addresses.emplace(id, kronuz::consensus::transport::PeerAddress{
												std::move(host), static_cast<std::uint16_t>(port)});
		}
		check(peers.listen.port(), "local voter missing");
		// The fetch lane carries every history file these limits let a voter write.
		peers.fetch_bytes = history::fetch_bytes(o.node.application);
		o.peers = std::move(peers);
		// Production elections cover durable vote/term barriers and network latency.
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
		for (const auto &name : spaces.GetArray())
			check(c.namespaces.insert(text(name)).second, "invalid namespace authorization");
		c.grants = grants_of(c.namespaces);
		o.credentials.push_back(std::move(c));
	}
	(void)State(o.node.context, o.node.application);
	validate_node_options(o.node);
	return o;
}
} // namespace detent::authority
