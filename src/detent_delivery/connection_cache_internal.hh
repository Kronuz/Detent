#pragma once
#include "detent_delivery/client.hh"
#include <array>
#include <asio/ssl/stream.hpp>

namespace detent::delivery {
class ConnectionCache {
  public:
	using Stream = asio::ssl::stream<asio::ip::tcp::socket>;
	struct Entry {
		asio::ip::tcp::endpoint endpoint;
		std::shared_ptr<asio::ssl::context> tls;
		std::array<unsigned char, 32> binding{};
		std::unique_ptr<Stream> stream;
		bool leased = false, connected = false, reused = false;
	};
	ConnectionCache(asio::any_io_executor, std::size_t, std::shared_ptr<distribution::StorageLedger>);
	std::size_t acquire(const ClientOptions &, const asio::any_io_executor &);
	void release(std::size_t, bool reusable) noexcept;
	void mark_connected(std::size_t) noexcept;
	void tcp_connected() noexcept { ++stats_.connects; }
	Stream &stream(std::size_t index) { return *entries_[index].stream; }
	bool connected(std::size_t index) const { return entries_[index].connected; }
	ConnectionCacheStats stats() const noexcept { return stats_; }

  private:
	void retire(Entry &) noexcept;
	asio::any_io_executor executor_;
	std::vector<Entry, distribution::StorageAllocator<Entry>> entries_;
	ConnectionCacheStats stats_;
};
class ConnectionLease {
  public:
	ConnectionLease(const ClientOptions &, const asio::any_io_executor &);
	~ConnectionLease();
	ConnectionLease(const ConnectionLease &) = delete;
	ConnectionLease &operator=(const ConnectionLease &) = delete;
	ConnectionCache::Stream &stream();
	bool connected() const;
	void mark_connected();
	void tcp_connected();
	void reusable(bool value) noexcept { reusable_ = value; }

  private:
	std::shared_ptr<ConnectionCache> cache_;
	std::unique_ptr<ConnectionCache::Stream> one_shot_;
	std::size_t index_ = 0;
	bool reusable_ = false;
};
} // namespace detent::delivery
