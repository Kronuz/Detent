#pragma once
#include "detent_distribution/protocol.hh"
#include "node.hh"
#include "transport/peers.hh"
#include <chrono>
#include <connection_admission.h>
#include <deque>
#include <detent_http/address.hh>
#include <filesystem>
#include <functional>
#include <future>
#include <http_handler.h>
#include <mutex>
#include <optional>
#include <set>
#include <thread>

namespace detent::authority {
struct Credential {
	std::string token, producer;
	std::set<std::string> namespaces;
	std::string principal;
	std::uint64_t policy_epoch = 1;
	bool publish = true;
};
struct ServiceOptions {
	NodeOptions node;
	std::filesystem::path directory;
	std::vector<Credential> credentials;
	std::optional<transport::PeerOptions> peers;
	std::optional<transport::TlsOptions> publisher_tls;
	asio::ip::address publisher_bind = asio::ip::make_address(detent_http::kLoopbackHost);
	std::string publisher_name = "localhost";
	// Linux queue setup policy; namespace/open composites may still use bounded fallback.
	std::string storage_backend = "auto";
	std::size_t requests = 1024, queued_bytes = 16 * 1024 * 1024;
	std::size_t reply_bytes = 16 * 1024 * 1024;
	std::size_t delivery_heap_bytes = 128 * 1024 * 1024;
	distribution::Limits delivery;
	std::chrono::milliseconds timeout{5000};
};
ServiceOptions load_service_options(const std::filesystem::path &path);
class Service final : public http::HttpHandler {
  public:
	explicit Service(ServiceOptions options);
	~Service() override;
	void observe_connections(std::shared_ptr<const reactor::ConnectionAdmission> observer);
	void handle(const http::Request &request, http::ResponseWriter &response) override;
	bool should_offload(const http::Request &request) const override { return request.path != "/v1/health"; }
	std::unique_ptr<http::BodySink> on_request_body(http::Request &request) override;

  private:
	struct ReplyAdmission;
	struct Reply {
		int status;
		std::string body;
		bool enveloped = false;
		std::shared_ptr<ReplyAdmission> reservation;
	};
	struct Admission;
	struct Runtime;
	struct Accounting;
	struct Delivery;
	struct Task {
		enum class Kind { Command, Read, Result, Distribution } kind;
		std::shared_ptr<Delivery> delivery;
		std::shared_ptr<Admission> admission;
		Command command;
		bool retiring = false, settled = false;
		std::string name_space;
		std::vector<std::string> keys;
		std::uint64_t generation = 0, sequence = 0;
		std::function<void(Reply)> complete;
		std::chrono::steady_clock::time_point deadline;
	};
	ServiceOptions options_;
	std::shared_ptr<Accounting> accounting_;
	std::mutex mutex_;
	std::deque<std::shared_ptr<Task>> queue_;
	std::set<std::string> busy_producers_;
	bool stopping_ = false;
	std::string health_ = "{}";
	std::shared_ptr<const reactor::ConnectionAdmission> connections_;
	std::chrono::steady_clock::time_point health_stamp_{};
	std::thread thread_;
	std::shared_ptr<Admission> admit(const http::Request &request);
	void run(std::promise<void> ready);
	void distribute(const http::Request &, http::ResponseWriter &, std::shared_ptr<Admission>);
	std::optional<Reply> execute(const http::Request &request, std::shared_ptr<Admission> admission,
								 std::function<void(Reply)> complete,
								 std::chrono::steady_clock::time_point deadline);
};
} // namespace detent::authority
