#include "detent_relay/service.hh"
#include "detent_distribution/framing.hh"
#include "detent_distribution/memory_resource.hh"
#include "detent_distribution/subscription_admission.hh"
#include "detent_distribution_http/reply_pump.hh"
#include "detent_distribution_http/session_request.hh"
#include <mutex>
#include <rapidjson/document.h>

namespace detent::relay {
namespace d = distribution;
namespace h = distribution_http;
namespace {
using Clock = std::chrono::steady_clock;
std::size_t ordinal(h::RequestKind kind) {
	switch (kind) {
	case h::RequestKind::Open:
		return 0;
	case h::RequestKind::Next:
		return 1;
	case h::RequestKind::Close:
		return 2;
	default:
		throw std::invalid_argument("relay requires subscription routes");
	}
}
[[noreturn]] void reject(int status, const char *body) {
	throw http::RequestRejected(status, body, http::RejectionContent::Json);
}
// W8: what a downstream relay reads, answered from the mirror like the authority's own routes.
bool history_route(std::string_view path) { return path == "/v2/history/layouts" || path == "/v2/history/file"; }
void require(bool condition) {
	if (!condition)
		throw std::invalid_argument("invalid history request");
}
std::string member(const rapidjson::Value &object, const char *name) {
	require(object.HasMember(name) && object[name].IsString() && object[name].GetStringLength() <= 255);
	return {object[name].GetString(), object[name].GetStringLength()};
}
// `{"namespaces": [...]}` or `{"namespace": ..., "name": ...}`, nothing else.
void parse_history(std::string_view path, std::string_view body, MirrorJob &job) {
	rapidjson::Document json;
	json.Parse<rapidjson::kParseValidateEncodingFlag>(body.data(), body.size());
	require(!json.HasParseError() && json.IsObject());
	if (path == "/v2/history/layouts") {
		require(json.MemberCount() == 1 && json.HasMember("namespaces") && json["namespaces"].IsArray() &&
				json["namespaces"].Size() && json["namespaces"].Size() <= 4096);
		job.kind = MirrorJob::Kind::Layouts;
		for (const auto &name : json["namespaces"].GetArray()) {
			require(name.IsString() && d::valid_namespace_name({name.GetString(), name.GetStringLength()}));
			job.positions.push_back({{name.GetString(), name.GetStringLength()}, std::nullopt});
		}
		return;
	}
	require(json.MemberCount() == 2);
	job.kind = MirrorJob::Kind::File;
	job.name_space = member(json, "namespace");
	job.name = member(json, "name");
	require(d::valid_namespace_name(job.name_space) && !job.name.empty());
}
} // namespace
struct ServiceRuntime::Impl {
	struct Admission {
		const ConfiguredCredential *credential = nullptr; // the registry's, retained by the runtime
		h::RequestKind kind;
		std::shared_ptr<d::SubscriptionRequests::Lease> lease;
		bool history = false; // a history route, admitted on the opens lane
	};
	// Every request is a mirror job: the task waits for its answer and streams it.
	struct Task : h::OwnedReplyPump {
		std::shared_ptr<Admission> admission;
		std::shared_ptr<MirrorJob> job;
		std::size_t frame = 0;
		bool completed = false;
		Clock::time_point deadline;
		std::shared_ptr<const d::OwnedBuffer> pending;
	};
	struct Lane {
		d::SubscriptionRequests admission;
		std::shared_ptr<std::pmr::memory_resource> resource;
		std::vector<std::shared_ptr<Task>, d::StorageAllocator<std::shared_ptr<Task>>> slots;
		std::vector<std::size_t, d::StorageAllocator<std::size_t>> free;
		std::size_t cursor = 0;
		Lane(std::shared_ptr<d::StorageLedger> account, std::size_t capacity)
			: admission(account, {capacity, capacity, capacity, capacity, capacity}),
			  resource(d::make_storage_resource(account)),
			  slots(d::StorageAllocator<std::shared_ptr<Task>>(account)),
			  free(d::StorageAllocator<std::size_t>(account)) {
			slots.resize(capacity);
			free.reserve(capacity);
			for (std::size_t index = capacity; index; --index)
				free.push_back(index - 1);
		}
	};
	ServiceAccounts accounts;
	std::shared_ptr<const Registry> registry;
	std::shared_ptr<MirrorService> mirror;
	std::array<d::StorageOwner<Lane>, 3> lanes;
	mutable std::mutex mutex;
	std::atomic<bool> closing{false};
	std::shared_ptr<const d::OwnedBuffer> health;
	Impl(ServiceAccounts storage, std::shared_ptr<const Registry> authorization,
		 std::shared_ptr<MirrorService> namespaces, ServiceLimits limits)
		: accounts(std::move(storage)), registry(std::move(authorization)), mirror(std::move(namespaces)) {
		if (!registry || !mirror)
			throw std::invalid_argument("relay service registry and mirror");
		const std::array<std::size_t, 3> capacities{limits.opens, limits.polls, limits.closes};
		for (std::size_t index = 0; index < 3; ++index) {
			if (!accounts.requests[index] || !capacities[index] || capacities[index] > 65536)
				throw std::invalid_argument("relay service request limits");
			lanes[index] = d::make_storage_owner<Lane>(accounts.requests[index], std::malloc, std::free,
													   accounts.requests[index], capacities[index]);
		}
	}
	std::shared_ptr<Admission> admit(const http::Request &request) {
		const bool history = history_route(request.path);
		const auto kind = history ? std::optional(h::RequestKind::Open) : h::classify_session_route(request.path);
		if (request.method != "POST" || !kind || *kind == h::RequestKind::Distribution)
			reject(404, "{\"error\":\"not_found\"}");
		const auto *credential = registry->authenticate(request.header("Authorization"));
		if (!credential)
			reject(403, "{\"error\":\"scope_forbidden\"}");
		const auto index = ordinal(*kind);
		if (closing.load())
			reject(503, "{\"error\":\"admission_full\"}");
		auto lease = lanes[index]->admission.admit(credential->principal.identity,
												   static_cast<d::SubscriptionRequestKind>(index));
		if (!lease)
			reject(503, "{\"error\":\"admission_full\"}");
		auto result =
			std::allocate_shared<Admission>(d::StorageAllocator<Admission>(accounts.requests[index]));
		result->credential = credential;
		result->kind = *kind;
		result->lease = std::move(lease);
		result->history = history;
		return result;
	}
	void control(Task &task, int status, std::string_view body) noexcept {
		task.send_control(status, body, accounts.requests[ordinal(task.admission->kind)]);
		task.completed = true;
	}
	// A mirror's answer: a control reply, or its chunks streamed in order.
	void stream_job(Task &task) {
		auto &reply = task.job->reply;
		if (!task.started) {
			if (reply.chunks.empty()) {
				control(task, reply.status, reply.body);
				return;
			}
			http::Response metadata;
			metadata.status = reply.status;
			metadata.content_type = reply.content_type;
			for (const auto &[name, value] : reply.headers)
				metadata.headers.emplace_back(name, value);
			if (task.reply->start_chunked(metadata, true) != http::StreamSubmit::Accepted)
				throw std::runtime_error("relay stream closed");
			task.started = true;
		}
		if (!task.pending && task.frame < reply.chunks.size() && task.writable.load())
			task.pending = reply.chunks[task.frame];
		if (task.pending) {
			const auto result = task.publish(task.pending);
			if (result == http::StreamSubmit::Accepted)
				++task.frame;
			else if (result != http::StreamSubmit::Busy)
				throw std::runtime_error("relay stream refused frame");
		}
		if (task.frame == reply.chunks.size() && !task.pending) {
			if (task.reply->finish() != http::StreamSubmit::Accepted)
				throw std::runtime_error("relay stream refused finish");
			task.completed = true;
		}
	}
	bool advance(Task &task, Clock::time_point now) {
		if (task.settled.load()) {
			task.job->abandoned.store(true);
			return true;
		}
		if (closing.load() || now >= task.deadline) {
			task.reply->abandon();
			task.completed = true;
		}
		if (task.completed) {
			task.pump_control();
			return false;
		}
		if (task.job->done.load(std::memory_order_acquire))
			stream_job(task);
		return false;
	}
};
ServiceRuntime::ServiceRuntime(ServiceAccounts accounts, std::shared_ptr<const Registry> registry,
							   std::shared_ptr<MirrorService> mirror, ServiceLimits limits) {
	if (!accounts.metadata)
		throw std::invalid_argument("relay service metadata account");
	impl_ = d::make_storage_owner<Impl>(accounts.metadata, std::malloc, std::free, accounts, std::move(registry),
										std::move(mirror), limits);
}
ServiceRuntime::~ServiceRuntime() = default;
void ServiceRuntime::close() noexcept {
	impl_->closing.store(true);
	for (auto &lane : impl_->lanes)
		lane->admission.close();
}
bool ServiceRuntime::step(Clock::time_point now) {
	auto &p = *impl_;
	bool progress = false;
	for (auto index : {std::size_t{2}, std::size_t{0}, std::size_t{1}}) {
		auto &lane = *p.lanes[index];
		for (std::size_t turn = 0; turn < (index == 1 ? 32 : 8); ++turn) {
			const auto slot = lane.cursor++ % lane.slots.size();
			std::shared_ptr<Impl::Task> task;
			{
				std::lock_guard lock(p.mutex);
				task = lane.slots[slot];
			}
			if (!task)
				continue;
			const auto frame = task->frame;
			try {
				if (p.advance(*task, now)) {
					std::lock_guard lock(p.mutex);
					lane.slots[slot].reset();
					lane.free.push_back(slot);
					progress = true;
				}
			} catch (...) {
				task->reply->abandon();
				task->completed = true;
			}
			progress |= task->frame != frame;
		}
	}
	return progress;
}
ServiceStatus ServiceRuntime::status() const noexcept {
	const auto &p = *impl_;
	ServiceStatus result;
	result.closing = p.closing.load();
	std::lock_guard lock(p.mutex);
	for (std::size_t index = 0; index < 3; ++index)
		result.requests[index] = p.lanes[index]->slots.size() - p.lanes[index]->free.size();
	return result;
}
bool ServiceRuntime::drained() const noexcept {
	const auto state = status();
	return state.closing && state.requests == std::array<std::size_t, 3>{} &&
		   std::all_of(impl_->lanes.begin(), impl_->lanes.end(),
					   [](const auto &lane) { return lane->admission.drained(); });
}
void ServiceRuntime::health(std::shared_ptr<const d::OwnedBuffer> value) {
	std::lock_guard lock(impl_->mutex);
	impl_->health = std::move(value);
}
Service::Service(std::shared_ptr<ServiceRuntime> runtime) : runtime_(std::move(runtime)) {
	if (!runtime_)
		throw std::invalid_argument("relay service runtime");
}
Service::~Service() { runtime_->close(); }
// What a request holds from its headers to its settled response: the lane lease, or a health ticket.
struct Service::Extension final : http::RequestExtension {
	std::shared_ptr<ServiceRuntime::Impl::Admission> admission;
	d::StorageLedger::Ticket health;
};
std::unique_ptr<http::RequestExtension> Service::create_extension(const http::Request &request) try {
	auto extension = std::make_unique<Extension>();
	if (request.path == "/v1/health" && request.method == "GET") {
		if (!request.header("Transfer-Encoding").empty() ||
			(!request.header("Content-Length").empty() && request.header("Content-Length") != "0"))
			reject(400, "{\"error\":\"health_body_forbidden\"}");
		extension->health = runtime_->impl_->accounts.metadata->admit(sizeof(Extension));
		return extension;
	}
	extension->admission = runtime_->impl_->admit(request);
	return extension;
} catch (const d::StoragePressure &) {
	reject(503, "{\"error\":\"admission_full\"}");
} catch (const std::bad_alloc &) {
	reject(503, "{\"error\":\"admission_full\"}");
}
std::unique_ptr<http::BodySink> Service::on_request_body(http::Request &request) try {
	if (request.path == "/v1/health" && request.method == "GET")
		return nullptr;
	class Sink final : public http::BodySink {
		std::shared_ptr<d::SubscriptionRequests::Lease> lease_;
		d::StorageLedger::Ticket ticket_;
		std::size_t limit_;

	  public:
		Sink(std::shared_ptr<d::SubscriptionRequests::Lease> lease, d::StorageLedger::Ticket ticket,
			 std::size_t limit)
			: lease_(std::move(lease)), ticket_(std::move(ticket)), limit_(limit) {}
		void write(std::string_view bytes) override {
			if (bytes.size() > limit_ - lease_->body().size())
				reject(413, "{\"error\":\"body_bound\"}");
			lease_->body().append(bytes);
		}
		void end() override { lease_->body().seal_prefix(); }
	};
	const auto &admission = request.ext<Extension>().admission;
	const auto index = ordinal(admission->kind);
	auto ticket = runtime_->impl_->accounts.requests[index]->admit(sizeof(Sink));
	return std::make_unique<Sink>(
		admission->lease, std::move(ticket),
		runtime_->impl_->lanes[index]->admission.body_limit(static_cast<d::SubscriptionRequestKind>(index)));
} catch (const d::StoragePressure &) {
	reject(503, "{\"error\":\"admission_full\"}");
} catch (const std::bad_alloc &) {
	reject(503, "{\"error\":\"admission_full\"}");
}
task::task<void> Service::handle(http::Request &request, http::ResponseWriter &response) {
	auto &p = *runtime_->impl_;
	if (request.path == "/v1/health" && request.method == "GET") {
		std::shared_ptr<const d::OwnedBuffer> health;
		{
			std::lock_guard lock(p.mutex);
			health = p.health;
		}
		response.status(health ? 200 : 503);
		response.content_type("application/json");
		response.write(health ? health->view() : std::string_view("{\"ready\":false}"));
		response.end();
		co_return;
	}
	auto admission = request.ext<Extension>().admission;
	const auto index = ordinal(admission->kind);
	const auto account = p.accounts.requests[index];
	auto task = std::allocate_shared<ServiceRuntime::Impl::Task>(
		d::StorageAllocator<ServiceRuntime::Impl::Task>(account));
	task->deadline = Clock::now() + std::chrono::seconds(60);
	task->job = std::make_shared<MirrorJob>();
	auto &job = *task->job;
	if (admission->history)
		parse_history(request.path, admission->lease->body().view(), job);
	else {
		auto parsed = h::parse_session_request(
			account, admission->kind, admission->lease->body().view(),
			{p.lanes[index]->admission.body_limit(static_cast<d::SubscriptionRequestKind>(index)), 4096, 30000});
		const auto &command = parsed->request();
		if (command.version != 2)
			reject(400, "{\"error\":\"invalid_request\"}");
		job.kind = command.kind == h::RequestKind::Open	  ? MirrorJob::Kind::Open
				   : command.kind == h::RequestKind::Next ? MirrorJob::Kind::Next
														  : MirrorJob::Kind::Close;
		job.session = command.session;
		for (const auto &position : command.positions) {
			job.positions.push_back({std::string(position.name), position.cursor});
			job.positions.back().repair = position.repair;
			if (command.resume && command.resume->name == position.name)
				job.positions.back().resume =
					Resume{command.resume->root, command.resume->after, command.resume->offset};
		}
		job.all = command.all.has_value();
		job.compressed = command.compressed;
		job.pages = command.pages;
		job.records.assign(command.records.begin(), command.records.end());
		job.wait_ms = command.wait_ms;
	}
	job.principal = admission->credential->principal;
	job.grants = admission->credential->namespaces.get();
	task->admission = std::move(admission);
	http::StreamOptions stream;
	stream.resource = p.lanes[index]->resource;
	stream.reply.deadline = task->deadline + std::chrono::seconds(1);
	stream.reply.limits = {16, 4096, 4096};
	stream.reply.request_retention = task->admission;
	stream.chunk_bytes = d::OwnedFragmentEncoder::maximum_wire_bytes();
	h::OwnedReplyPump::callbacks(stream, task);
	task->reply = response.stream(std::move(stream));
	if (!task->reply)
		throw std::runtime_error("relay owned streaming unavailable");
	std::lock_guard lock(p.mutex);
	auto &lane = *p.lanes[index];
	if (!p.closing.load() && !lane.free.empty()) {
		const auto slot = lane.free.back();
		lane.free.pop_back();
		p.mirror->submit(task->job);
		lane.slots[slot] = std::move(task);
		co_return;
	}
	task->reply->abandon();
}
void Service::on_error(std::exception_ptr error, const http::Request &, http::ResponseWriter &response) {
	try {
		if (error)
			std::rethrow_exception(error);
	} catch (const http::RequestRejected &rejection) {
		response.status(rejection.status());
		response.content_type(rejection.content_type());
		response.write(rejection.what());
		response.end();
	} catch (const d::StoragePressure &) {
		response.status(503);
		response.content_type("application/json");
		response.write("{\"error\":\"admission_full\"}");
		response.end();
	} catch (const std::bad_alloc &) {
		response.status(503);
		response.content_type("application/json");
		response.write("{\"error\":\"admission_full\"}");
		response.end();
	} catch (const std::invalid_argument &) {
		response.status(400);
		response.content_type("application/json");
		response.write("{\"error\":\"invalid_request\"}");
		response.end();
	} catch (...) {
		response.status(500);
		response.content_type("application/json");
		response.write("{\"error\":\"internal_error\"}");
		response.end();
	}
}
} // namespace detent::relay
