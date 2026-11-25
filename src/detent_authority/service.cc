// The authority's HTTP service: construction, admission and request handling.
#include "service.hh"
#include "service_internal.hh"
#include "delivery_export.hh"
#include "frames_export.hh"
#include "history_format.hh"
#include "layout_export.hh"
#include "peer_history.hh"
#include "detent/assert_off_loop.hh"
#include "detent/json_schema.hh"
#include "detent/json_schema_internal.hh"
#include "detent/scope.hh"
#include "detent/validation.hh"
#include "detent_distribution/memory_resource.hh"
#include "detent_transport/await_io.hh"
#include "detent_distribution_http/reply_pump.hh"
#include "prepared.hh"
#include "read_reply.hh"
#include <algorithm>
#include <asio.hpp>
#include <cctype>
#include <fcntl.h>
#include <http_owned_stream.h>
#include <completion/posix.h>
#include <fstream>
#include <limits>
#include <metrics.hh>
#include <random>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <sha256.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace detent::authority {
Service::Service(ServiceOptions options) : options_(std::move(options)) {
	check(options_.storage_backend == "auto" || options_.storage_backend == "native" ||
			  options_.storage_backend == "fallback",
		  "authority storage backend policy");
#ifndef __linux__
	check(options_.storage_backend == "auto", "explicit storage backend policy currently requires Linux");
#endif
	check(options_.requests && options_.requests <= 65536 &&
			  options_.queued_bytes >= options_.node.application.request_bytes &&
			  options_.timeout.count() > 0 && options_.timeout <= std::chrono::hours(24) &&
			  options_.reply_bytes >= 1024 && !options_.credentials.empty(),
		  "invalid authority service limits");
	check(!options_.delivery_timeout || (options_.delivery_timeout->count() > 0 &&
			  *options_.delivery_timeout <= std::chrono::hours(24)),
		  "invalid authority delivery timeout");
	check(!options_.peers || options_.peers->fetch_bytes >= history::fetch_bytes(options_.node.application),
		  "authority peers' fetch_bytes is below the largest history file the application limits allow");
	check(options_.delivery_heap_bytes >= 1024 * 1024 &&
			  options_.delivery_heap_bytes <= SIZE_MAX && options_.delivery.bytes &&
			  options_.delivery.bytes <= SIZE_MAX && options_.delivery.namespaces &&
			  options_.delivery.namespaces <= 4096,
		  "invalid authority delivery limits");
	accounting_ = std::make_shared<Accounting>();
	accounting_->delivery_account =
		std::make_shared<distribution::StorageLedger>(options_.delivery_heap_bytes);
	accounting_->delivery_resource = distribution::make_storage_resource(accounting_->delivery_account);
	check(options_.subscription_wait.count() >= 0 && options_.subscription_wait <= std::chrono::minutes(1),
		  "invalid subscription poll duration");
	check(options_.seal_after.count() > 0 && options_.seal_after <= std::chrono::minutes(1), "invalid seal interval");
	check(options_.subscription_requests.polls <= 512, "subscription polls must preserve listener headroom");
	check(options_.subscription_requests.opens <= 128 && options_.subscription_requests.closes <= 128,
		  "subscription controls must preserve listener headroom");
	accounting_->subscription_requests =
		std::make_unique<SubscriptionRequests>(accounting_->delivery_account, options_.subscription_requests);
	accounting_->credentials = std::move(options_.credentials);
	accounting_->credential_grants.emplace(
		distribution::StorageAllocator<Accounting::GrantView>(accounting_->delivery_account));
	accounting_->credential_grants->reserve(accounting_->credentials.size());
	accounting_->credential_hashes.reserve(accounting_->credentials.size());
	for (auto &credential : accounting_->credentials) {
		check(credential.policy_epoch && credential.principal.size() <= 128,
			  "invalid authorization identity");
		credential.grants = grants_of(credential.namespaces);
		class SHA256 hash;
		hash.add(credential.token.data(), credential.token.size());
		Digest digest{};
		hash.getHash(digest.data());
		accounting_->credential_hashes.push_back(digest);
		accounting_->credential_grants->emplace_back(accounting_->delivery_account);
		auto &grants = accounting_->credential_grants->back();
		check(credential.namespaces.size() <= 4096, "credential grant count bound");
		grants.names.reserve(credential.namespaces.size());
		for (const auto &name : credential.namespaces)
			if (distribution::valid_namespace_name(name)) // Version 1 follows exact names only.
				grants.names.push_back(name);
		grants.digest =
			distribution::canonical_scope_identity("detent-subscription-grants-v1", 1, grants.names);
	}
	detent::assert_off_loop();
	std::promise<void> ready;
	auto future = ready.get_future();
	auto finished = std::make_shared<std::promise<void>>();
	runtime_done_ = finished->get_future().share();
	task::spawn(task::scheduler_ref::of(owner_.scheduler()), run(std::move(ready)),
				[finished](std::exception_ptr) { finished->set_value(); });
	try {
		future.get();
	} catch (...) {
		drain();
		throw;
	}
}
Service::~Service() {
	// N4: stopping_ is set before the runtime may leave its loop; the owner stops only after the
	// runtime has settled every task, or its join would never see the queue drained.
	drain();
	owner_.stop();
	owner_.close();
	owner_.destroy_roots();
}
std::optional<std::string> Service::failure() const {
	std::lock_guard lock(mutex_);
	return failure_;
}

void Service::begin_shutdown() noexcept {
	{
		std::lock_guard lock(mutex_);
		stopping_ = true;
	}
	owner_.wake().raise();
	stop_.request(task::cancel_reason::shutdown);
}
void Service::drain() {
	detent::assert_off_loop();
	{
		std::lock_guard lock(mutex_);
		stopping_ = true;
	}
	owner_.wake().raise();
	if (runtime_done_.valid())
		runtime_done_.wait();
}
void Service::observe_connections(std::shared_ptr<const reactor::ConnectionAdmission> observer) {
	std::lock_guard lock(mutex_);
	connections_ = std::move(observer);
}
std::shared_ptr<Service::Admission> Service::admit(const http::Request &request) {
	auto header = request.header("Authorization");
	if (!header.starts_with("Bearer ") || header.size() > 135) {
		throw http::RequestRejected(401, "{\"error\":\"unauthorized\"}", http::RejectionContent::Json);
	}
	auto token = header.substr(7);
	class SHA256 supplied;
	supplied.add(token.data(), token.size());
	Digest want{};
	supplied.getHash(want.data());
	const Credential *found = nullptr;
	std::size_t found_index = 0;
	for (std::size_t index = 0; index < accounting_->credentials.size(); ++index) {
		const auto &c = accounting_->credentials[index];
		const auto &have = accounting_->credential_hashes[index];
		unsigned difference = 0;
		for (unsigned i = 0; i < 32; ++i) {
			difference |= want[i] ^ have[i];
		}
		if (!difference) {
			found = &c;
			found_index = index;
		}
	}
	if (!found) {
		throw http::RequestRejected(401, "{\"error\":\"unauthorized\"}", http::RejectionContent::Json);
	}
	if (!found->publish &&
		(request.path == "/v2/open" || request.path == "/v2/close" || request.path == "/v2/transactions")) {
		throw http::RequestRejected(403, "{\"error\":\"publish_forbidden\"}", http::RejectionContent::Json);
	}
	std::optional<SubscriptionRequestKind> subscription_kind;
	if (auto kind = distribution_http::classify_session_route(request.path)) {
		switch (*kind) {
		case distribution_http::RequestKind::Open:
			subscription_kind = SubscriptionRequestKind::Open;
			break;
		case distribution_http::RequestKind::Next:
			subscription_kind = SubscriptionRequestKind::Next;
			break;
		case distribution_http::RequestKind::Close:
			subscription_kind = SubscriptionRequestKind::Close;
			break;
		default:
			throw std::logic_error("invalid subscription route classification");
		}
	}
	if (subscription_kind) {
		std::lock_guard lock(mutex_);
		if (stopping_)
			throw http::RequestRejected(503, "{\"error\":\"admission_full\"}", http::RejectionContent::Json);
		const auto principal = distribution::canonical_scope_identity(
			found->principal.empty() ? found->producer : found->principal, 1, {});
		auto lease = accounting_->subscription_requests->admit(principal, *subscription_kind);
		if (!lease)
			throw http::RequestRejected(503, "{\"error\":\"admission_full\"}", http::RejectionContent::Json);
		auto admission = std::allocate_shared<Admission>(
			distribution::StorageAllocator<Admission>(accounting_->delivery_account));
		admission->accounting = accounting_;
		admission->credential = found;
		admission->credential_index = found_index;
		admission->bytes = accounting_->subscription_requests->body_limit(*subscription_kind);
		admission->subscription = std::move(lease);
		return admission;
	}
	auto bytes = options_.node.application.request_bytes;
	auto admission = std::make_shared<Admission>();
	admission->accounting = accounting_;
	admission->credential = found;
	admission->credential_index = found_index;
	admission->bytes = bytes;
	std::lock_guard lock(mutex_);
	std::lock_guard account_lock(accounting_->mutex);
	auto per_producer = accounting_->producers.find(found->producer);
	if (stopping_ || accounting_->requests >= options_.requests ||
		(per_producer != accounting_->producers.end() && per_producer->second >= 16) ||
		accounting_->body_bytes > options_.queued_bytes ||
		bytes > options_.queued_bytes - accounting_->body_bytes) {
		throw http::RequestRejected(503, "{\"error\":\"admission_full\"}", http::RejectionContent::Json);
	}
	++accounting_->producers[found->producer];
	++accounting_->requests;
	accounting_->body_bytes += bytes;
	admission->active = true;
	return admission;
}
std::unique_ptr<http::RequestExtension> Service::create_extension(const http::Request &request) {
	if (request.path == "/v1/health" && request.method == "GET") {
		if (!request.header("Transfer-Encoding").empty() ||
			(!request.header("Content-Length").empty() && request.header("Content-Length") != "0"))
			throw http::RequestRejected(400, "{\"error\":\"health_body_forbidden\"}",
										http::RejectionContent::Json);
		return nullptr;
	}
	auto extension = std::make_unique<Extension>();
	extension->admission = admit(request);
	return extension;
}
std::unique_ptr<http::BodySink> Service::on_request_body(http::Request &request) {
	if (request.path == "/v1/health" && request.method == "GET")
		return nullptr;
	// Subscription bodies land in their lease; every other body is buffered in Request::body, capped by
	// the service's set_max_body (request_bytes).
	auto &admission = request.ext<Extension>().admission;
	if (!admission->subscription)
		return nullptr;
	class SubscriptionSink final : public http::BodySink {
		std::shared_ptr<SubscriptionRequests::Lease> lease_;
		std::size_t limit_;

	  public:
		explicit SubscriptionSink(std::shared_ptr<SubscriptionRequests::Lease> lease, std::size_t limit)
			: lease_(std::move(lease)), limit_(limit) {}
		void write(std::string_view bytes) override {
			if (bytes.size() > limit_ - lease_->body().size())
				throw http::RequestRejected(413, "{\"error\":\"body_bound\"}",
											http::RejectionContent::Json);
			lease_->body().append(bytes);
		}
		void end() override { lease_->body().seal_prefix(); }
	};
	return std::make_unique<SubscriptionSink>(admission->subscription, admission->bytes);
}
Service::Prepared Service::prepare(const http::Request &request, std::shared_ptr<Admission> admission,
								   Clock::time_point deadline) {
	check(request.method == "POST", "authority endpoints require POST");
	auto d = document(request.body, options_.node.application.request_bytes);
	auto request_fields = [&](const Object &value, std::initializer_list<std::string_view> allowed) {
		check(identity(text(field(value, "cluster"))) == options_.node.context.cluster &&
				  identity(text(field(value, "group"))) == options_.node.context.group &&
				  identity(text(field(value, "epoch"))) == options_.node.context.epoch,
			  "foreign request authority");
		std::set<std::string_view> seen;
		for (auto it = value.MemberBegin(); it != value.MemberEnd(); ++it) {
			std::string_view key(it->name.GetString(), it->name.GetStringLength());
			check(seen.insert(key).second &&
					  (key == "cluster" || key == "group" || key == "epoch" ||
					   std::find(allowed.begin(), allowed.end(), key) != allowed.end()),
				  "unknown or duplicate request field");
		}
	};
	auto task = std::make_shared<Task>();
	task->admission = std::move(admission);
	task->command.context = options_.node.context;
	task->command.producer = task->admission->credential->producer;
	task->deadline = deadline;
	if (request.path == "/v2/open") {
		request_fields(d, {"expected_generation", "nonce"});
		task->kind = Task::Kind::Command;
		task->command.kind = Kind::Open;
		task->command.generation = number(field(d, "expected_generation"));
		task->command.nonce = identity(text(field(d, "nonce")));
	} else if (request.path == "/v2/close") {
		request_fields(d, {"generation"});
		task->kind = Task::Kind::Command;
		task->command.kind = Kind::Close;
		task->command.generation = number(field(d, "generation"));
	} else if (request.path == "/v2/transactions") {
		request_fields(d, {"generation", "sequence", "namespace", "expected_namespace", "operations"});
		task->kind = Task::Kind::Command;
		auto &c = task->command;
		c.generation = number(field(d, "generation"));
		c.sequence = number(field(d, "sequence"));
		c.name_space = text(field(d, "namespace"));
		c.expected_namespace = optional(d, "expected_namespace");
		check(task->admission->credential->grants.allows(c.name_space), "unauthorized namespace");
		auto &operations = field(d, "operations");
		check(operations.IsArray() && operations.Size() <= options_.node.application.mutations,
			  "operation count bound");
		for (const auto &item : operations.GetArray()) {
			fields(item, {"key", "value", "expected_revision", "absent_namespace_revision"});
			Mutation op;
			op.key = text(field(item, "key"));
			op.expected = optional(item, "expected_revision");
			op.absence_revision = optional(item, "absent_namespace_revision");
			auto &value = field(item, "value");
			if (!value.IsNull()) {
				auto parsed = detent::internal::parse_knob_def(value, op.key);
				check(parsed.has_value(), "invalid knob definition");
				op.value = std::move(parsed).value();
			}
			c.mutations.push_back(std::move(op));
		}
	} else if (request.path == "/v2/read") {
		request_fields(d, {"namespace", "keys"});
		task->kind = Task::Kind::Read;
		task->name_space = text(field(d, "namespace"));
		check(task->admission->credential->grants.allows(task->name_space), "unauthorized namespace");
		auto &keys = field(d, "keys");
		check(keys.IsArray() && keys.Size() <= options_.node.application.mutations, "read key count bound");
		std::set<std::string> unique;
		for (const auto &value : keys.GetArray()) {
			auto key = text(value);
			check(detent::is_valid_knob_name(key) && key.substr(0, key.find(':')) == task->name_space &&
					  unique.insert(key).second,
				  "invalid read key");
			task->keys.push_back(std::move(key));
		}
	} else if (request.path == "/v2/result") {
		request_fields(d, {"generation", "sequence"});
		task->kind = Task::Kind::Result;
		task->generation = number(field(d, "generation"));
		task->sequence = number(field(d, "sequence"));
	} else if (request.path == "/v2/history/layouts") {
		// W8: a relay's read of its namespaces' layouts; every name is granted.
		fields(d, {"namespaces"});
		task->kind = Task::Kind::HistoryLayouts;
		auto &names = field(d, "namespaces");
		check(names.IsArray() && names.Size() && names.Size() <= options_.delivery.namespaces, "namespace count bound");
		for (const auto &value : names.GetArray()) {
			auto name = text(value);
			check(distribution::valid_namespace_name(name) && task->admission->credential->grants.allows(name),
				  "unauthorized namespace");
			task->keys.push_back(std::move(name));
		}
	} else if (request.path == "/v2/history/file") {
		// W8: one file of a granted namespace's layout, by the name the layout gives it, whole or a
		// range of it (D138): `offset` and `length` together.
		fields(d, {"namespace", "name", "offset", "length"});
		task->kind = Task::Kind::HistoryFile;
		task->name_space = text(field(d, "namespace"));
		check(distribution::valid_namespace_name(task->name_space) &&
				  task->admission->credential->grants.allows(task->name_space),
			  "unauthorized namespace");
		task->keys.push_back(text(field(d, "name"), 255));
		check(d.HasMember("offset") == d.HasMember("length"), "a range names its offset and length");
		if (d.HasMember("offset")) {
			task->range.emplace(number(field(d, "offset")), number(field(d, "length")));
			check(task->range->second > 0, "an empty range");
		}
	} else {
		return {Reply{404, "{\"error\":\"unknown_endpoint\"}"}, nullptr};
	}
	if (task->kind == Task::Kind::Command) {
		(void)encode(task->command, options_.node.application);
	}
	return {std::nullopt, std::move(task)};
}
// On the owner thread. The runtime completes the task on this thread, through `complete`.
void Service::enqueue(const std::shared_ptr<Task> &task) {
	task->complete = [weak = std::weak_ptr<Task>(task)](Reply value) {
		if (auto held = weak.lock()) {
			held->outcome = std::move(value);
			held->done.set();
		}
	};
	{
		std::lock_guard lock(mutex_);
		if (stopping_) {
			task->outcome = Reply{503, "{\"error\":\"stopping\"}"};
			task->done.set();
			return;
		}
		if (task->kind == Task::Kind::Command && !busy_producers_.insert(task->command.producer).second) {
			task->outcome = Reply{429, "{\"error\":\"producer_busy\"}"};
			task->done.set();
			return;
		}
		try {
			queue_.push_back(task);
			owner_.wake().raise();
		} catch (...) {
			if (task->kind == Task::Kind::Command) {
				busy_producers_.erase(task->command.producer);
			}
			throw;
		}
	}
}

std::shared_ptr<Service::Delivery>
Service::distribution_request(const distribution_http::SessionRequest &request, const Admission &admission) {
	const auto &context = options_.node.context;
	check(request.identity.cluster == context.cluster && request.identity.group == context.group &&
			  request.identity.restore_epoch == context.epoch,
		  "foreign distribution authority");
	const auto &credential = *admission.credential;
	auto account = accounting_->delivery_account;
	auto delivery = std::allocate_shared<Delivery>(distribution::StorageAllocator<Delivery>(account));
	delivery->scope = AuthorizedScope::authorize_canonical(
		credential.principal.empty() ? credential.producer : credential.principal, credential.policy_epoch,
		request.namespaces, (*accounting_->credential_grants)[admission.credential_index].names, account,
		options_.delivery.namespaces);
	check(request.identity.scope == delivery->scope->identity(), "distribution policy or scope mismatch");
	delivery->compress = request.compressed;
	delivery->base = request.base;
	return delivery;
}

void Service::stream_task(http::ResponseWriter &response, const std::shared_ptr<Task> &task) {
	auto delivery = task->delivery;
	auto account = accounting_->delivery_account;
	http::StreamOptions stream;
	stream.resource = accounting_->delivery_resource;
	stream.reply.deadline = task->deadline + std::chrono::seconds(1);
	stream.reply.request_retention = task->admission;
	stream.chunk_bytes = distribution::OwnedFragmentEncoder::maximum_wire_bytes();
	stream.maximum_bytes = (options_.delivery.bytes / distribution::fragment_bytes + 1) * stream.chunk_bytes;
	distribution_http::OwnedReplyPump::callbacks(stream, delivery, [wake = owner_.wake()] { wake.raise(); });
	task->complete = [delivery, account](Reply value) {
		delivery->send_control(value.status, value.body, account);
	};
	delivery->reply = response.stream(std::move(stream));
	check(static_cast<bool>(delivery->reply), "owned distribution streaming unavailable");
}

void Service::distribute(const http::Request &request, http::ResponseWriter &response,
						 std::shared_ptr<Admission> admission) {
	check(request.method == "POST", "distribution requires POST");
	auto parsed = distribution_http::parse_session_request(
		accounting_->delivery_account, distribution_http::RequestKind::Distribution, request.body,
		{std::min<std::size_t>(admission->bytes, 1024 * 1024), options_.delivery.namespaces,
		 static_cast<std::uint64_t>(options_.subscription_wait.count())});
	auto delivery = distribution_request(parsed->request(), *admission);
	auto task =
		std::allocate_shared<Task>(distribution::StorageAllocator<Task>(accounting_->delivery_account));
	task->kind = Task::Kind::Distribution;
	task->admission = std::move(admission);
	task->delivery = delivery;
	task->deadline = Clock::now() + options_.delivery_timeout.value_or(options_.timeout);
	std::lock_guard lock(mutex_);
	check(!stopping_, "authority is stopping");
	stream_task(response, task);
	try {
		queue_.push_back(std::move(task));
		owner_.wake().raise();
	} catch (...) {
		delivery->reply->abandon();
	}
}

void Service::subscription(const http::Request &request, http::ResponseWriter &response,
						   std::shared_ptr<Admission> admission) {
	check(request.method == "POST", "subscription requires POST");
	auto &input = admission->subscription->body();
	if (!input.sealed()) {
		input.append(request.body);
		input.seal_prefix();
	}
	const auto kind = distribution_http::classify_session_route(request.path);
	check(kind.has_value(), "unknown subscription route");
	auto parsed = distribution_http::parse_session_request(
		accounting_->delivery_account, *kind, input.view(),
		{admission->bytes, options_.delivery.namespaces,
		 static_cast<std::uint64_t>(options_.subscription_wait.count())});
	const auto &command = parsed->request();
	auto account = accounting_->delivery_account;
	auto task = std::allocate_shared<Task>(distribution::StorageAllocator<Task>(account));
	task->admission = std::move(admission);
	const auto &credential = *task->admission->credential;
	if (command.version == 2) {
		frames_request(command, response, std::move(task));
		return;
	}
	task->principal = {distribution::canonical_scope_identity(
						   credential.principal.empty() ? credential.producer : credential.principal, 1, {}),
					   credential.policy_epoch};
	task->deadline = Clock::now() + options_.timeout;
	if (command.kind == distribution_http::RequestKind::Open) {
		task->kind = Task::Kind::SessionOpen;
		task->delivery = distribution_request(command, *task->admission);
		task->force_snapshot = command.force_snapshot;
		check(RAND_bytes(reinterpret_cast<unsigned char *>(task->session.data()), task->session.size()) == 1,
			  "subscription identity entropy");
	} else {
		task->delivery = std::allocate_shared<Delivery>(distribution::StorageAllocator<Delivery>(account));
		task->session = command.session;
		if (command.kind == distribution_http::RequestKind::Close)
			task->kind = Task::Kind::SessionClose;
		else {
			task->kind = Task::Kind::SessionNext;
			task->sequence = command.after;
			task->deadline = Clock::now() + options_.delivery_timeout.value_or(options_.timeout) +
				std::chrono::milliseconds(command.wait_ms);
			task->poll_deadline = Clock::now() + std::chrono::milliseconds(command.wait_ms);
			task->published = command.published;
		}
	}
	auto delivery = task->delivery;
	std::lock_guard lock(mutex_);
	check(!stopping_, "authority is stopping");
	stream_task(response, task);
	try {
		if (task->kind == Task::Kind::SessionClose)
			subscription_close_queue_.push_back(std::move(task));
		else
			subscription_queue_.push_back(std::move(task));
		owner_.wake().raise();
	} catch (...) {
		delivery->reply->abandon();
	}
}

// W5: a session request of version 2, answered on the owner from history (W6).
void Service::frames_request(const distribution_http::SessionRequest &command, http::ResponseWriter &response,
							 std::shared_ptr<Task> task) {
	const auto &credential = *task->admission->credential;
	task->frames = true;
	const auto principal = credential.principal.empty() ? credential.producer : credential.principal;
	task->frames_principal.identity = history::sha256(principal);
	task->frames_principal.policy_epoch = credential.policy_epoch;
	task->deadline = Clock::now() + options_.timeout;
	task->compressed = command.compressed;
	task->session = command.session;
	if (command.kind != distribution_http::RequestKind::Close) {
		// W13: every followed name is granted, and `all` needs a grant.
		bool granted = !command.all || !credential.grants.empty();
		for (const auto &position : command.positions)
			granted = granted && credential.grants.allows(position.name);
		if (!granted)
			task->refused = Reply{403, "{\"error\":\"scope_forbidden\"}"};
		task->follows.reserve(command.positions.size());
		for (const auto &position : command.positions)
			task->follows.push_back({std::string(position.name), position.cursor, position.repair});
		task->all = command.all;
	}
	switch (command.kind) {
	case distribution_http::RequestKind::Open:
		task->kind = Task::Kind::SessionOpen;
		check(RAND_bytes(reinterpret_cast<unsigned char *>(task->session.data()), task->session.size()) == 1,
			  "session identity entropy");
		break;
	case distribution_http::RequestKind::Next:
		task->kind = Task::Kind::SessionNext;
		task->deadline = Clock::now() + options_.delivery_timeout.value_or(options_.timeout) +
						 std::chrono::milliseconds(command.wait_ms);
		task->poll_deadline = Clock::now() + std::chrono::milliseconds(command.wait_ms);
		task->seal = command.seal;
		break;
	default:
		task->kind = Task::Kind::SessionClose;
	}
	auto account = accounting_->delivery_account;
	task->delivery = std::allocate_shared<Delivery>(distribution::StorageAllocator<Delivery>(account));
	auto delivery = task->delivery;
	std::lock_guard lock(mutex_);
	check(!stopping_, "authority is stopping");
	stream_task(response, task);
	try {
		frames_queue_.push_back(std::move(task));
		owner_.wake().raise();
	} catch (...) {
		delivery->reply->abandon();
	}
}

// W8, on the owner: a relay's layouts, or one file of a granted namespace's layout, from this voter's
// history at its latest seal (docs/status/namespace-s8.md, "The service").
// A whole file above `maximum` (the reply bound) is refused 413, before it is read once a layout sized
// it. A range (D138) answers at most `maximum` bytes from its offset: fewer than asked when the bound
// or the file's end comes first, 416 when the offset is at or past the end.
Service::Reply Service::history_reply(const Task &task, Node &node, LayoutExport &exporter,
									  std::optional<HeldFile> &held, std::uint64_t maximum) {
	const Reply unavailable{503, "{\"error\":\"history_unavailable\"}"};
	auto *files = node.history_files();
	if (!files || !node.history()->ready() || !node.sealed())
		return unavailable;
	try {
		const auto &table = node.history()->layout().table();
		if (task.kind == Task::Kind::HistoryFile && !task.range) {
			auto bytes = exporter.file(task.name_space, task.keys.front(), table, *files, node.sealed()->limits(), maximum);
			if (!bytes)
				return {404, "{\"error\":\"not_found\"}"};
			return {200, std::move(*bytes), true, {}, "application/octet-stream"};
		}
		if (task.kind == Task::Kind::HistoryFile) {
			const auto &name = task.keys.front();
			if (!held || held->name_space != task.name_space || held->name != name || held->seal != table.index) {
				held.reset();
				auto bytes = exporter.file(task.name_space, name, table, *files, node.sealed()->limits(),
										   std::numeric_limits<std::uint64_t>::max());
				if (!bytes)
					return {404, "{\"error\":\"not_found\"}"};
				held = HeldFile{task.name_space, name, table.index, std::move(*bytes)};
			}
			const auto [offset, length] = *task.range;
			const auto size = held->bytes.size();
			if (offset >= size) {
				held.reset();
				return {416, "{\"error\":\"range_not_satisfiable\"}"};
			}
			const auto count = std::min({length, maximum, size - offset});
			auto body = held->bytes.substr(offset, count);
			if (offset + count == size)
				held.reset();
			return {200, std::move(body), true, {}, "application/octet-stream"};
		}
		auto layouts = exporter.layouts(task.keys, *node.sealed(), table, *files);
		if (!layouts)
			return unavailable;
		return {200, relay::encode_mirror_table(*layouts), true, {}, "application/vnd.detent.layouts"};
	} catch (const std::length_error &) {
		return {413, "{\"error\":\"reply_bound\"}"};
	} catch (const std::exception &) {
		held.reset();
		return unavailable;
	}
}

Service::Reply Service::enveloped(Reply reply) const {
	if (!reply.enveloped && !reply.body.empty() && reply.body[0] == '{') {
		const auto &context = options_.node.context;
		reply.body = "{\"authority\":{\"cluster\":\"" + hex(context.cluster) + "\",\"group\":\"" +
					 hex(context.group) + "\",\"epoch\":\"" + hex(context.epoch) + "\"}," + reply.body.substr(1);
	}
	reply.enveloped = true;
	return reply;
}

// On the owner, when the wait ends. A settled task answers with its own reply; a wait that ended
// on the stop or the backstop answers that the outcome is unknown. The reply is reserved against
// reply_bytes here, and the reservation travels with it until the owned stream's write ends (D9).
Service::Outcome Service::reply_for(Task &task, task::cancel_reason why) {
	const bool command = task.kind == Task::Kind::Command;
	Reply value = task.outcome ? std::move(*task.outcome) : Reply{503, "{\"error\":\"outcome_unknown\"}"};
	value = enveloped(std::move(value));
	const auto limit = options_.reply_bytes;
	auto reserve = [&](const Reply &reply, ReplyAdmission &lease) {
		if (reply.body.size() > limit - 256)
			return false;
		if (lease.active) {
			check(lease.bytes == reply.body.size() + 256, "read reply reservation mismatch");
			lease.ready = Clock::now();
			return true;
		}
		lease.bytes = reply.body.size() + 256;
		std::lock_guard lock(accounting_->mutex);
		if (lease.bytes > limit - accounting_->reply_bytes)
			return false;
		accounting_->reply_bytes += lease.bytes;
		lease.active = true;
		return true;
	};
	auto lease = std::move(value.reservation);
	if (!lease) {
		lease = std::make_shared<ReplyAdmission>();
		lease->accounting = accounting_;
	}
	if (!reserve(value, *lease)) {
		// Over the reply budget: a command may have committed, a read has nothing to retry against.
		value = enveloped(Reply{503, command ? "{\"error\":\"outcome_unknown\"}" : "{\"error\":\"admission_full\"}"});
		lease = std::make_shared<ReplyAdmission>();
		lease->accounting = accounting_;
		(void)reserve(value, *lease);
	}
	value.reservation = std::move(lease);
	return {std::move(value), why == task::cancel_reason::shutdown};
}

void Service::write_owned(http::Request &request, http::ResponseWriter &response, Reply reply,
						  std::shared_ptr<Admission> admission, Clock::time_point outcome_deadline) {
	// The reply budget and the admission stay held until the write ends.
	auto retained = std::make_shared<std::pair<std::shared_ptr<Admission>, std::shared_ptr<ReplyAdmission>>>(
		std::move(admission), std::move(reply.reservation));
	http::StreamOptions stream;
	stream.reply.deadline = std::max(Clock::now(), outcome_deadline) + std::chrono::seconds(1);
	stream.reply.limits.body_bytes = options_.reply_bytes - 256;
	stream.reply.request_retention = std::move(retained);
	stream.chunk_bytes = options_.reply_bytes;
	stream.maximum_bytes = options_.reply_bytes;
	auto owned = response.stream(std::move(stream));
	check(static_cast<bool>(owned), "owned reply streaming unavailable");
	http::Response metadata;
	metadata.status = reply.status;
	metadata.content_type = std::string(reply.content_type);
	auto body = std::make_shared<const std::string>(std::move(reply.body));
	if (owned->start(metadata, body->size(), request.keep_alive) != http::StreamSubmit::Accepted ||
		(!body->empty() && owned->publish({body, std::string_view(*body)}) != http::StreamSubmit::Accepted) ||
		owned->finish() != http::StreamSubmit::Accepted)
		owned->abandon();
}

task::task<void> Service::handle(http::Request &request, http::ResponseWriter &response) {
	Outcome outcome;
	std::shared_ptr<Admission> submitted;
	Clock::time_point outcome_deadline{};
	try {
		if (request.path == "/v1/health" && request.method == "GET") {
			std::lock_guard lock(mutex_);
			outcome.reply = {200, health_};
			auto &body = outcome.reply.body;
			if (!body.empty() && body.back() == '}') {
				auto age = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - health_stamp_).count();
				body.pop_back();
				if (body.size() > 1) {
					body += ',';
				}
				body += "\"snapshot_age_ms\":" + std::to_string(std::max<std::int64_t>(age, 0)) + "}";
			}
			outcome.buffered = true;
		} else {
			auto admission = request.ext<Extension>().admission;
			if (admission->subscription) {
				subscription(request, response, std::move(admission));
				co_return;
			}
			if (request.path == "/v2/distribution") {
				distribute(request, response, std::move(admission));
				co_return;
			}
			outcome_deadline = Clock::now() + options_.timeout;
			auto prepared = prepare(request, admission, outcome_deadline);
			if (prepared.immediate) {
				outcome.reply = std::move(*prepared.immediate);
				outcome.buffered = true;
			} else {
				submitted = std::move(admission);
				// The child moves to the owner, queues the task there and waits for the runtime's reply, the
				// stop (D10) or the backstop, one second past the outcome deadline.
				outcome = co_await detent::owner_submit<Outcome>(
					owner_,
					[this, task = std::move(prepared.task)]() {
						enqueue(task);
						return task;
					},
					[this](Task &task, task::cancel_reason why) { return reply_for(task, why); },
					outcome_deadline + std::chrono::seconds(1), stop_.token());
			}
		}
	} catch (const http::RequestRejected &error) {
		outcome = {{error.status(), error.what()}, true};
	} catch (const std::invalid_argument &error) {
		rapidjson::StringBuffer buffer;
		rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
		writer.String(error.what());
		outcome = {{400, "{\"error\":\"invalid_request\",\"detail\":" +
							 std::string(buffer.GetString(), buffer.GetSize()) + "}"},
				   true};
	} catch (const std::exception &) {
		outcome = {{503, "{\"error\":\"outcome_unknown\"}"}, true};
	}
	if (submitted && !outcome.buffered) {
		write_owned(request, response, std::move(outcome.reply), std::move(submitted), outcome_deadline);
		co_return;
	}
	auto reply = enveloped(std::move(outcome.reply));
	response.status(reply.status);
	response.set_header("Content-Type", "application/json");
	response.write(reply.body);
	response.end();
}
} // namespace detent::authority
