#pragma once
#include "mirror_service.hh"
#include "registry.hh"
#include "service_limits.hh"
#include <functional>
#include <http_handler.h>

namespace detent::relay {
struct ServiceAccounts {
	std::shared_ptr<distribution::StorageLedger> metadata;
	std::array<std::shared_ptr<distribution::StorageLedger>, 3> requests;
};
struct ServiceStatus {
	std::array<std::size_t, 3> requests{};
	bool closing = false;
};
class ServiceRuntime {
  public:
	// `mirror` answers W5 sessions (version 2) and the history routes a downstream relay reads.
	// `wake` runs on any thread once a request, a reply or a mirror answer gives step() work: the owner's wake.
	ServiceRuntime(ServiceAccounts, std::shared_ptr<const Registry>, std::shared_ptr<MirrorService>,
				   ServiceLimits = {}, std::function<void()> wake = {});
	~ServiceRuntime();
	// Close is thread-safe; step, status and drained belong to the owner executor.
	void close() noexcept;
	// One turn's share of the requests (32 polls, 8 opens, 8 closes, round robin) and 64 sessions; true
	// when it changed anything. rescan() starts a round over every occupied slot and session; swept() is
	// true once the turns since have visited them all.
	bool step(std::chrono::steady_clock::time_point);
	void rescan() noexcept;
	bool swept() const noexcept;
	ServiceStatus status() const noexcept;
	void health(std::shared_ptr<const distribution::OwnedBuffer>);
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
	std::unique_ptr<http::RequestExtension> create_extension(const http::Request &) override;
	std::unique_ptr<http::BodySink> on_request_body(http::Request &) override;
	task::task<void> handle(http::Request &, http::ResponseWriter &) override;
	void on_error(std::exception_ptr, const http::Request &, http::ResponseWriter &) override;

  private:
	struct Extension;
	std::shared_ptr<ServiceRuntime> runtime_;
};
} // namespace detent::relay
