#include "connection_cache_internal.hh"
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
ConnectionCache::ConnectionCache(asio::any_io_executor executor, std::size_t capacity,
								 std::shared_ptr<distribution::StorageLedger> account)
	: executor_(std::move(executor)), entries_(distribution::StorageAllocator<Entry>(account)) {
	if (!account || !capacity || capacity > 65536)
		throw std::invalid_argument("delivery connection cache bound");
	entries_.resize(capacity);
}
std::shared_ptr<ConnectionCache> make_connection_cache(asio::any_io_executor executor, std::size_t capacity,
													   std::shared_ptr<distribution::StorageLedger> account) {
	return std::allocate_shared<ConnectionCache>(distribution::StorageAllocator<ConnectionCache>(account),
												 std::move(executor), capacity, account);
}
ConnectionCacheStats connection_cache_stats(const std::shared_ptr<ConnectionCache> &cache) {
	return cache ? cache->stats() : ConnectionCacheStats{};
}
void ConnectionCache::retire(Entry &entry) noexcept {
	if (!entry.stream)
		return;
	asio::error_code ignored;
	entry.stream->next_layer().close(ignored);
	entry.stream.reset();
	entry.tls.reset();
	entry.connected = false;
	--stats_.live;
	++stats_.retirements;
}
std::size_t ConnectionCache::acquire(const ClientOptions &options, const asio::any_io_executor &executor) {
	if (executor != executor_)
		throw std::invalid_argument("delivery connection cache executor mismatch");
	const auto key = binding(options);
	std::optional<std::size_t> available;
	for (std::size_t i = 0; i < entries_.size(); ++i) {
		auto &entry = entries_[i];
		if (entry.leased)
			continue;
		if (entry.stream && entry.connected && entry.endpoint == options.endpoint &&
			entry.tls.get() == options.tls_context.get() && !entry.tls.owner_before(options.tls_context) &&
			!options.tls_context.owner_before(entry.tls) && entry.binding == key) {
			entry.leased = true;
			entry.reused = true;
			++stats_.leased;
			++stats_.reuse_attempts;
			return i;
		}
		if (!available || !entry.stream)
			available = i;
	}
	if (!available) {
		++stats_.pressure;
		throw SourceUnavailableError("delivery connection cache is fully leased");
	}
	auto &entry = entries_[*available];
	retire(entry);
	auto stream = std::make_unique<Stream>(executor_, *options.tls_context);
	reactor::tls::bind_hostname(stream->native_handle(), options.hostname);
	entry.stream = std::move(stream);
	entry.endpoint = options.endpoint;
	entry.tls = options.tls_context;
	entry.binding = key;
	entry.leased = true;
	entry.reused = false;
	++stats_.leased;
	++stats_.live;
	stats_.peak_live = std::max(stats_.peak_live, stats_.live);
	return *available;
}
void ConnectionCache::mark_connected(std::size_t index) noexcept {
	entries_[index].connected = true;
	++stats_.handshakes;
}
void ConnectionCache::release(std::size_t index, bool reusable) noexcept {
	auto &entry = entries_[index];
	if (reusable && entry.reused)
		++stats_.reuses;
	if (!reusable)
		retire(entry);
	entry.leased = false;
	--stats_.leased;
}
ConnectionLease::ConnectionLease(const ClientOptions &options, const asio::any_io_executor &executor)
	: cache_(options.connections) {
	if (cache_)
		index_ = cache_->acquire(options, executor);
	else {
		one_shot_ = std::make_unique<ConnectionCache::Stream>(executor, *options.tls_context);
		reactor::tls::bind_hostname(one_shot_->native_handle(), options.hostname);
	}
}
ConnectionLease::~ConnectionLease() {
	if (cache_)
		cache_->release(index_, reusable_);
	else if (one_shot_) {
		asio::error_code ignored;
		one_shot_->next_layer().close(ignored);
	}
}
ConnectionCache::Stream &ConnectionLease::stream() { return cache_ ? cache_->stream(index_) : *one_shot_; }
bool ConnectionLease::connected() const { return cache_ && cache_->connected(index_); }
void ConnectionLease::mark_connected() {
	if (cache_)
		cache_->mark_connected(index_);
}
void ConnectionLease::tcp_connected() {
	if (cache_)
		cache_->tcp_connected();
}
} // namespace detent::delivery
