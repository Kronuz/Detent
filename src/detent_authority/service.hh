#pragma once
#include "node.hh"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <future>
#include <http_handler.h>
#include <mutex>
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
	std::size_t requests = 1024, queued_bytes = 16 * 1024 * 1024;
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
	struct Task {
		enum class Kind { Command, Read, Result } kind;
		std::shared_ptr<Admission> admission;
		Command command;
		std::string name_space;
		std::vector<std::string> keys;
		std::uint64_t generation = 0, sequence = 0;
		std::promise<Reply> promise;
		std::chrono::steady_clock::time_point deadline;
	};
	ServiceOptions options_;
	std::mutex mutex_;
	std::condition_variable condition_;
	std::deque<std::shared_ptr<Task>> queue_;
	std::set<std::string> busy_producers_;
	std::map<std::string, std::size_t> producer_admission_;
	std::size_t admitted_ = 0, admitted_bytes_ = 0;
	bool stopping_ = false;
	std::string health_ = "{}";
	std::thread thread_;
	std::shared_ptr<Admission> admit(const http::Request &request);
	void run(std::promise<void> ready);
	Reply execute(const http::Request &request, std::shared_ptr<Admission> admission);
	void release(const Admission &admission);
};
} // namespace detent::authority
