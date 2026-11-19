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
	std::size_t bootstrap = 0, catchup = 0, peak_transfers = 0;
	std::size_t sessions = 0;
	bool closing = false;
};
class ServiceRuntime {
  public:
	// `mirror`, when set, answers W5 sessions (version 2) and the history routes a downstream relay
	// reads; version 1 stays with the bundles.
	// `wake` runs on any thread once a request, a reply or a mirror answer gives step() work: the owner's wake.
	ServiceRuntime(ServiceAccounts, std::shared_ptr<const Registry>, std::span<const SessionBinding>,
				   ServiceLimits = {}, std::shared_ptr<MirrorService> mirror = {}, std::function<void()> wake = {});
	~ServiceRuntime();
	// Close is thread-safe; step, status and drained belong to the owner executor.
	void close() noexcept;
	// One turn's share of the requests and sessions; true when it changed anything. `rescan` starts a
	// sweep over every occupied slot and session; swept() is true once the sweep has visited them all.
	bool step(Sessions::Clock::time_point, bool rescan = true);
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
