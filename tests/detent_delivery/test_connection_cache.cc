#include "../../src/detent_delivery/connection_cache_internal.hh"
#include <asio/io_context.hpp>
#include <iostream>
#include <source_location>

namespace t = detent::delivery;
namespace d = detent::distribution;
namespace {
void check(bool value, std::source_location where = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("connection cache invariant at " + std::to_string(where.line()));
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
			const auto slot = cache->acquire(options, io.get_executor());
			bool pressure = false;
			try {
				cache->acquire(options, io.get_executor());
			} catch (const t::SourceUnavailableError &) {
				pressure = true;
			}
			check(pressure && cache->stats().leased == 1 && cache->stats().pressure == 1);
			cache->mark_connected(slot);
			cache->release(slot, true);
			options.base = options.configured;
			options.base->revision = 42;
			check(cache->acquire(options, io.get_executor()) == slot);
			cache->release(slot, true);
			check(cache->stats().reuses == 1 && cache->stats().reuse_attempts == 1);
			auto changed = [&](const t::ClientOptions &value) {
				const auto before = cache->stats();
				const auto acquired = cache->acquire(value, io.get_executor());
				check(cache->stats().reuses == before.reuses);
				check(cache->stats().retirements == before.retirements + 1);
				cache->mark_connected(acquired);
				cache->release(acquired, true);
			};
			options.token += "different";
			changed(options);
			options.configured.scope[0] = 's';
			changed(options);
			options.hostname = "elsewhere.localhost";
			changed(options);
			options.endpoint.port(4632);
			changed(options);
			auto original = options.tls_context;
			options.tls_context = std::shared_ptr<asio::ssl::context>(original.get(), [](auto *) {});
			changed(options);
			options.tls_context = original;
			changed(options);
			bool wrong_owner = false;
			try {
				cache->acquire(options, other.get_executor());
			} catch (const std::invalid_argument &) {
				wrong_owner = true;
			}
			check(wrong_owner && cache->stats().leased == 0);
			const auto acquired = cache->acquire(options, io.get_executor());
			cache->release(acquired, false);
			check(cache->stats().live == 0 && cache->stats().leased == 0 && cache->stats().peak_live == 1);
		}
		check(account->used() == 0);
		std::cout << "bounded source-bound connection cache passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
