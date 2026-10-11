#pragma once
#include "registry.hh"
#include <http_handler.h>

namespace detent::relay {
struct ServiceAccounts {
	std::shared_ptr<distribution::StorageLedger> metadata;
	std::array<std::shared_ptr<distribution::StorageLedger>, 3> requests;
};
struct ServiceLimits {
	std::size_t opens = 16, polls = 640, closes = 16;
	std::size_t bootstrap_transfers = 512, catchup_transfers = 128;
	distribution::SubscriptionLimits sessions{1024, 1024};
};
struct ServiceStatus {
	std::array<std::size_t, 3> requests{};
	std::size_t bootstrap = 0, catchup = 0, peak_transfers = 0;
	bool closing = false;
};
class ServiceRuntime {
  public:
	ServiceRuntime(ServiceAccounts, std::shared_ptr<const Registry>, std::span<const SessionBinding>,
				   ServiceLimits = {});
	~ServiceRuntime();
	// Close is thread-safe; step, status and drained belong to the owner executor.
	void close() noexcept;
	bool step(Sessions::Clock::time_point);
	ServiceStatus status() const noexcept;
	bool drained() const noexcept;

  private:
	friend class Service;
	struct Impl;
	distribution::StorageOwner<Impl> impl_;
};
// The host retains the runtime after facade loss until authentic originals drain.
class Service final : public http::HttpHandler {
  public:
	explicit Service(std::shared_ptr<ServiceRuntime>);
	~Service() override;
	std::unique_ptr<http::BodySink> on_request_body(http::Request &) override;
	void handle(const http::Request &, http::ResponseWriter &) override;
	void on_error(std::exception_ptr, const http::Request &, http::ResponseWriter &) override;

  private:
	std::shared_ptr<ServiceRuntime> runtime_;
};
} // namespace detent::relay
