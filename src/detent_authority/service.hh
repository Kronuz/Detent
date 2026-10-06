#pragma once
#include "node.hh"
#include "transport/peers.hh"
#include <chrono>
#include <deque>
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
};
struct ServiceOptions {
	NodeOptions node;
	std::filesystem::path directory;
	std::vector<Credential> credentials;
	std::optional<transport::PeerOptions> peers;
	std::optional<transport::TlsOptions> publisher_tls;
	std::size_t requests = 1024, queued_bytes = 16 * 1024 * 1024;
	std::size_t reply_bytes = 16 * 1024 * 1024;
	std::chrono::milliseconds timeout{5000};
};
ServiceOptions load_service_options(const std::filesystem::path &path);
class Service final : public http::HttpHandler {
  public:
	explicit Service(ServiceOptions options);
	~Service() override;
	void handle(const http::Request &request, http::ResponseWriter &response) override;
	bool should_offload(const http::Request &request) const override { return request.path != "/v1/health"; }
	std::unique_ptr<http::BodySink> on_request_body(http::Request &request) override;

  private:
	struct Reply {
		int status;
		std::string body;
	};
	struct Admission;
	struct Runtime;
	struct Accounting;
	struct ReplyAdmission;
	struct Task {
		enum class Kind { Command, Read, Result } kind;
		std::shared_ptr<Admission> admission;
		Command command;
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
	std::thread thread_;
	std::shared_ptr<Admission> admit(const http::Request &request);
	void run(std::promise<void> ready);
	std::optional<Reply> execute(const http::Request &request, std::shared_ptr<Admission> admission,
								 std::function<void(Reply)> complete);
};
} // namespace detent::authority
