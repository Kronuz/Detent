#pragma once
#include "../detent_cli/config.hh"
#include "detent/owner_loop.hh"
#include "detent_distribution/protocol.hh"
#include "detent_distribution_http/session_request.hh"
#include "node.hh"
#include "subscription.hh"
#include "subscription_admission.hh"
#include "peers.hh"
#include <chrono>
#include <connection_admission.h>
#include <deque>
#include <filesystem>
#include <functional>
#include <future>
#include <variant>
#include <http_handler.h>
#include <mutex>
#include <optional>
#include <set>

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
	std::optional<kronuz::consensus::transport::PeerOptions> peers;
	std::optional<reactor::tls::Options> publisher_tls;
	asio::ip::address publisher_bind = asio::ip::make_address(detent_cli::kLoopbackHost);
	std::string publisher_name = "localhost";
	// Linux queue setup policy; namespace/open composites may still use bounded fallback.
	std::string storage_backend = "auto";
	std::size_t requests = 1024, queued_bytes = 16 * 1024 * 1024;
	std::size_t reply_bytes = 16 * 1024 * 1024;
	std::size_t delivery_heap_bytes = 128 * 1024 * 1024;
	distribution::Limits delivery;
	SubscriptionLimits subscriptions;
	SubscriptionRequestLimits subscription_requests;
	std::chrono::milliseconds subscription_wait{30000};
	std::chrono::milliseconds timeout{5000};
	std::optional<std::chrono::milliseconds> delivery_timeout;
};
ServiceOptions load_service_options(const std::filesystem::path &path);
class Service final : public http::HttpHandler {
  public:
	explicit Service(ServiceOptions options);
	~Service() override;
	void observe_connections(std::shared_ptr<const reactor::ConnectionAdmission> observer);
	task::task<void> handle(http::Request &request, http::ResponseWriter &response) override;
	std::unique_ptr<http::RequestExtension> create_extension(const http::Request &request) override;
	std::unique_ptr<http::BodySink> on_request_body(http::Request &request) override;
	// Ends every owner wait with its truthful reply (D10) and stops admitting. Call before the HTTP
	// service stops.
	void begin_shutdown() noexcept;
	// Waits until the runtime has settled every task and left its loop; the owner keeps running.
	// Call after the HTTP service stops and before the owner's phases.
	void drain();
	// The runtime's loop; its D15 phases belong to the process.
	[[nodiscard]] detent::OwnerLoop &owner() noexcept { return owner_; }

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
		enum class Kind { Command, Read, Result, Distribution, SessionOpen, SessionNext, SessionClose } kind;
		std::shared_ptr<Delivery> delivery;
		std::shared_ptr<const AuthorizedScope> retiring_scope;
		std::shared_ptr<Admission> admission;
		Command command;
		bool retiring = false, settled = false;
		bool quorum_pending = false, quorum_ready = false, exporting = false, session_initialized = false;
		bool force_snapshot = false, export_ready = false;
		bool authorization_queued = false, authorization_ready = false;
		distribution::Identity session{};
		SubscriptionPrincipal principal;
		std::optional<SubscriptionRequestToken> subscription_request;
		std::optional<SubscriptionOfferToken> subscription_offer;
		std::optional<distribution::Cursor> published;
		std::size_t authorization_cursor = 0, retirement_remaining = 0, retirement_slot = 0;
		std::size_t grant_cursor = 0;
		std::uint64_t authorization_incarnation = 0;
		std::string name_space;
		std::vector<std::string> keys;
		std::uint64_t generation = 0, sequence = 0;
		std::function<void(Reply)> complete;
		// A command, read or result waits on `done` on the owner; `outcome` is set once, by complete.
		task::event done;
		std::optional<Reply> outcome;
		std::chrono::steady_clock::time_point deadline;
		std::chrono::steady_clock::time_point poll_deadline;
	};
	ServiceOptions options_;
	std::shared_ptr<Accounting> accounting_;
	std::mutex mutex_;
	std::deque<std::shared_ptr<Task>> queue_;
	std::deque<std::shared_ptr<Task>> subscription_queue_;
	std::deque<std::shared_ptr<Task>> subscription_close_queue_;
	std::set<std::string> busy_producers_;
	bool stopping_ = false;
	std::string health_ = "{}";
	std::shared_ptr<const reactor::ConnectionAdmission> connections_;
	std::chrono::steady_clock::time_point health_stamp_{};
	task::cancel_source stop_;
	std::shared_future<void> runtime_done_;
	std::shared_ptr<Admission> admit(const http::Request &request);
	task::task<void> run(std::promise<void> ready);
	void distribute(const http::Request &, http::ResponseWriter &, std::shared_ptr<Admission>);
	void subscription(const http::Request &, http::ResponseWriter &, std::shared_ptr<Admission>);
	struct Extension;
	struct Outcome;
	struct Prepared;
	std::shared_ptr<Delivery> distribution_request(const distribution_http::SessionRequest &,
												   const Admission &);
	void stream_task(http::ResponseWriter &, const std::shared_ptr<Task> &);
	// Parses and validates a command, read or result on the loop; nothing is queued.
	Prepared prepare(const http::Request &request, std::shared_ptr<Admission> admission,
					 std::chrono::steady_clock::time_point deadline);
	// On the owner: queues a prepared task, or answers it at once (stopping, producer busy).
	void enqueue(const std::shared_ptr<Task> &task);
	Outcome reply_for(Task &task, task::cancel_reason why);
	Reply enveloped(Reply reply) const;
	// Answers a submitted request through an owned stream that keeps the connection (D8).
	void write_owned(http::Request &, http::ResponseWriter &, Reply, std::shared_ptr<Admission>,
					 std::chrono::steady_clock::time_point outcome_deadline);
	// Declared last: destroyed first, so the loop is joined before anything the runtime uses goes.
	detent::OwnerLoop owner_;
};
} // namespace detent::authority
