#pragma once
#include "detent_delivery/configuration.hh"
#include "sessions.hh"

namespace detent::relay {
struct ConfiguredBundle {
	delivery::ManagedOptions source;
	std::size_t cache_bytes = 64 * 1024 * 1024;
	std::size_t retained_edges = 64;
};
struct ConfiguredCredential {
	std::array<unsigned char, 32> token_hash{};
	SessionAuthorization authorization;
};
struct HostOptions {
	asio::ip::tcp::endpoint endpoint{asio::ip::address_v4::loopback(), 8318};
	std::shared_ptr<asio::ssl::context> tls_context;
	std::vector<ConfiguredBundle> bundles;
	std::vector<ConfiguredCredential> credentials;
	std::size_t control_bytes = 64 * 1024 * 1024;
	std::size_t baseline_bytes = 16 * 1024 * 1024;
	std::size_t heap_bytes = 0;
};
// Startup worker only; each source configuration has private credentials.
HostOptions load_host_options(const std::filesystem::path &, bool system);
} // namespace detent::relay
