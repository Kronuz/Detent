#pragma once
#include "http_upstream.hh"
#include "mirror_service.hh"
#include "registry.hh"
#include "service_limits.hh"

namespace detent::relay {
// W8: the namespaces this relay mirrors, its upstream, and the bytes its answers may hold.
struct ConfiguredMirror {
	MirrorServiceOptions service;
	HttpUpstreamOptions upstream;
	std::size_t answer_bytes = 64 * 1024 * 1024;
};
struct HostOptions {
	asio::ip::tcp::endpoint endpoint{asio::ip::address_v4::loopback(), 8318};
	std::shared_ptr<asio::ssl::context> tls_context;
	std::string certificate_authority, hostname = "localhost";
	ConfiguredMirror mirror;
	std::vector<ConfiguredCredential> credentials;
	std::size_t control_bytes = 64 * 1024 * 1024;
	std::size_t baseline_bytes = 16 * 1024 * 1024;
	std::size_t heap_bytes = 0;
	std::size_t connections = 1024;
	ServiceLimits service_limits;
};
// Startup worker only; the configuration file holds private credentials.
HostOptions load_host_options(const std::filesystem::path &);
} // namespace detent::relay
