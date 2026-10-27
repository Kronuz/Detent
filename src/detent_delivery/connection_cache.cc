#include "connection_key.hh"
#include "detent_distribution/memory_resource.hh"
#include <sha256.h>
#include <stdexcept>

namespace detent::delivery {
namespace {
std::array<unsigned char, 32> binding(const ClientOptions &options) {
	class SHA256 hash;
	auto add = [&](std::string_view bytes) {
		std::array<unsigned char, 8> size{};
		for (unsigned i = 0; i < size.size(); ++i)
			size[i] = static_cast<unsigned char>(static_cast<std::uint64_t>(bytes.size()) >> (8 * i));
		hash.add(size.data(), size.size());
		hash.add(bytes.data(), bytes.size());
	};
	add(options.hostname);
	add(options.token);
	add({options.configured.cluster.data(), options.configured.cluster.size()});
	add({options.configured.group.data(), options.configured.group.size()});
	add({options.configured.restore_epoch.data(), options.configured.restore_epoch.size()});
	add({options.configured.scope.data(), options.configured.scope.size()});
	std::array<unsigned char, 32> result{};
	hash.getHash(result.data());
	return result;
}
} // namespace
http::client::TlsKey connection_key(const ClientOptions &options) {
	return {options.endpoint, options.hostname, options.tls_context, binding(options)};
}
std::shared_ptr<ConnectionCache> make_connection_cache(asio::any_io_executor executor, std::size_t capacity,
                                                       std::shared_ptr<distribution::StorageLedger> account) {
	if (!account || !capacity || capacity > 65536)
		throw std::invalid_argument("delivery connection cache bound");
	auto resource = allocators::make_charged_resource(std::move(account));
	try {
		return http::client::make_tls_cache(std::move(executor), capacity,
		                                    allocators::AllocationContext(resource));
	} catch (const http::client::Error &error) {
		if (error.kind() == http::client::ErrorKind::Pressure)
			throw distribution::StoragePressure(error.what());
		throw std::invalid_argument(error.what());
	}
}
ConnectionCacheStats connection_cache_stats(const std::shared_ptr<ConnectionCache> &cache) {
	return cache ? cache->stats() : ConnectionCacheStats{};
}
} // namespace detent::delivery
