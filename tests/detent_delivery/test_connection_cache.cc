#include "../../src/detent_delivery/connection_key.hh"
#include <asio/io_context.hpp>
#include <iostream>
#include <source_location>

namespace t = detent::delivery;
namespace d = detent::distribution;
namespace client = http::client;
namespace {
void check(bool value, std::source_location where = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("connection binding invariant at " + std::to_string(where.line()));
}
} // namespace
int main() {
	try {
		auto account = std::make_shared<d::StorageLedger>(1024 * 1024);
		{
			asio::io_context io, other;
			auto cache = t::make_connection_cache(io.get_executor(), 1, account);
			t::ClientOptions options;
			options.endpoint = {asio::ip::address_v4::loopback(), 4631};
			options.hostname = "localhost";
			options.token = "0123456789abcdef0123456789abcdef";
			options.tls_context = std::make_shared<asio::ssl::context>(asio::ssl::context::tls_client);
			options.tls_context->set_verify_mode(asio::ssl::verify_peer);
			const auto original = t::connection_key(options).binding;
			options.base = options.configured;
			options.base->revision = 42;
			check(t::connection_key(options).binding == original);
			const auto changed = [&](auto mutate) {
				auto copy = options;
				mutate(copy);
				check(t::connection_key(copy).binding != original);
			};
			changed([](auto &copy) { copy.token += "different"; });
			changed([](auto &copy) { copy.hostname = "elsewhere.localhost"; });
			changed([](auto &copy) { copy.configured.cluster[0] = 'c'; });
			changed([](auto &copy) { copy.configured.group[0] = 'g'; });
			changed([](auto &copy) { copy.configured.restore_epoch[0] = 'e'; });
			changed([](auto &copy) { copy.configured.scope[0] = 's'; });
			{
				client::TlsLease lease(t::connection_key(options), io.get_executor(), cache);
				bool pressure = false, mismatch = false;
				try {
					client::TlsLease excess(t::connection_key(options), io.get_executor(), cache);
				} catch (const client::Error &error) {
					pressure = error.kind() == client::ErrorKind::Pressure;
				}
				try {
					client::TlsLease wrong(t::connection_key(options), other.get_executor(), cache);
				} catch (const client::Error &error) {
					mismatch = error.kind() == client::ErrorKind::Options;
				}
				check(pressure && mismatch && cache->stats().leased == 1);
			}
			const auto stats = t::connection_cache_stats(cache);
			check(stats.leased == 0 && stats.live == 0 && stats.peak_live == 1 && stats.retirements == 1);
		}
		check(account->used() == 0);
		std::cout << "delivery credential, authority and scope bindings passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
