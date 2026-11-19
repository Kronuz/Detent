#include "detent_relay/service.hh"
#include "detent_distribution/framing.hh"
#include "detent_distribution/memory_resource.hh"
#include "detent_distribution/subscription_admission.hh"
#include "detent_distribution_http/reply_pump.hh"
#include "detent_distribution_http/session_request.hh"
#include <algorithm>
#include <functional>
#include <mutex>
#include <openssl/rand.h>
#include <rapidjson/document.h>
#include <tuple>

namespace detent::relay {
namespace d = distribution;
namespace h = distribution_http;
namespace {
using Clock = Sessions::Clock;
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
std::string hex(const d::Identity &identity) {
	constexpr char digits[] = "0123456789abcdef";
	std::string result(32, '0');
	for (std::size_t index = 0; index < identity.size(); ++index) {
		const auto byte = static_cast<unsigned char>(identity[index]);
		result[2 * index] = digits[byte >> 4];
		result[2 * index + 1] = digits[byte & 15];
	}
	return result;
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
		SessionAuthorization authorization;
		h::RequestKind kind;
		std::shared_ptr<d::SubscriptionRequests::Lease> lease;
		bool history = false; // a history route, admitted on the opens lane
	};
	struct Task : h::OwnedReplyPump {
		std::shared_ptr<Admission> admission;
		std::optional<d::Cursor> base, published;
		d::Identity session{};
		std::size_t binding = 0, frame = 0;
		std::uint64_t after = 0;
		bool force_snapshot = false, compression = false, initialized = false, completed = false;
		bool open_created = false, transfer = false, bootstrap = false;
		Clock::time_point deadline, poll_deadline;
		std::optional<d::SubscriptionRequestToken> request;
		std::optional<d::SubscriptionOfferToken> offer;
		d::RetirementHandle<d::EncodedUpdate> update;
		std::shared_ptr<const d::OwnedBuffer> pending;
		std::shared_ptr<MirrorJob> job; // version 2 and history routes: the mirror answers
	};
	struct Lane {
		d::SubscriptionRequests admission;
		std::shared_ptr<std::pmr::memory_resource> resource;
		std::vector<std::shared_ptr<Task>, d::StorageAllocator<std::shared_ptr<Task>>> slots;
		std::vector<std::size_t, d::StorageAllocator<std::size_t>> free;
		std::size_t cursor = 0, unswept = 0;
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
	ServiceLimits limits;
	std::shared_ptr<MirrorService> mirror;
	Sessions sessions;
	std::vector<SessionBinding, d::StorageAllocator<SessionBinding>> bindings;
	std::array<d::StorageOwner<Lane>, 3> lanes;
	mutable std::mutex mutex;
	std::atomic<bool> closing{false};
	std::shared_ptr<const d::OwnedBuffer> health;
	bool closed_sessions = false;
	std::size_t bootstrap = 0, catchup = 0, peak_transfers = 0;
	std::size_t unswept_sessions = 0;
	std::function<void()> wake;
	Impl(ServiceAccounts storage, std::shared_ptr<const Registry> authorization,
		 std::span<const SessionBinding> configured, ServiceLimits bounds, std::shared_ptr<MirrorService> namespaces,
		 std::function<void()> owner_wake)
		: accounts(std::move(storage)), registry(std::move(authorization)), limits(bounds), mirror(std::move(namespaces)),
		  sessions(accounts.metadata, configured, limits.sessions),
		  wake(owner_wake ? std::move(owner_wake) : std::function<void()>([] {})),
		  bindings(d::StorageAllocator<SessionBinding>(accounts.metadata)) {
		if (!registry || !limits.bootstrap_transfers || !limits.catchup_transfers ||
			limits.bootstrap_transfers > limits.polls || limits.catchup_transfers > limits.polls)
			throw std::invalid_argument("relay service transfer limits");
		bindings.assign(configured.begin(), configured.end());
		const std::array<std::size_t, 3> capacities{limits.opens, limits.polls, limits.closes};
		for (std::size_t index = 0; index < 3; ++index) {
			if (!accounts.requests[index] || !capacities[index] || capacities[index] > 65536)
				throw std::invalid_argument("relay service request limits");
			lanes[index] = d::make_storage_owner<Lane>(accounts.requests[index], std::malloc, std::free,
													   accounts.requests[index], capacities[index]);
		}
	}
	std::shared_ptr<Admission> admit(const http::Request &request) {
		const bool history = mirror && history_route(request.path);
		const auto kind = history ? std::optional(h::RequestKind::Open) : h::classify_session_route(request.path);
		if (request.method != "POST" || !kind || *kind == h::RequestKind::Distribution)
			reject(404, "{\"error\":\"not_found\"}");
		const auto *authorization = registry->authenticate(request.header("Authorization"));
		if (!authorization)
			reject(403, "{\"error\":\"scope_forbidden\"}");
		const auto index = ordinal(*kind);
		if (closing.load())
			reject(503, "{\"error\":\"admission_full\"}");
		auto lease = lanes[index]->admission.admit(authorization->principal.identity,
												   static_cast<d::SubscriptionRequestKind>(index));
		if (!lease)
			reject(503, "{\"error\":\"admission_full\"}");
		auto result =
			std::allocate_shared<Admission>(d::StorageAllocator<Admission>(accounts.requests[index]));
		result->authorization = *authorization;
		result->kind = *kind;
		result->lease = std::move(lease);
		result->history = history;
		return result;
	}
	void control(Task &task, int status, std::string_view body) noexcept {
		task.send_control(status, body, accounts.requests[ordinal(task.admission->kind)]);
		task.completed = true;
	}
	void error(Task &task, d::SubscriptionResult result) {
		int status = 409;
		const char *body = "{\"error\":\"session_out_of_sync\"}";
		switch (result) {
		case d::SubscriptionResult::Expired:
			status = 410;
			body = "{\"error\":\"session_expired\"}";
			break;
		case d::SubscriptionResult::Forbidden:
			status = 403;
			body = "{\"error\":\"scope_forbidden\"}";
			break;
		case d::SubscriptionResult::IdentityChanged:
			body = "{\"error\":\"identity_changed\"}";
			break;
		case d::SubscriptionResult::SnapshotRequired:
			body = "{\"error\":\"snapshot_required\"}";
			break;
		case d::SubscriptionResult::SourceBehind:
			status = 503;
			body = "{\"error\":\"source_behind\"}";
			break;
		case d::SubscriptionResult::Full:
			status = 503;
			body = "{\"error\":\"admission_full\"}";
			break;
		case d::SubscriptionResult::Busy:
			body = "{\"error\":\"session_busy\"}";
			break;
		default:
			break;
		}
		control(task, status, body);
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
		if (task.job && task.settled.load())
			task.job->abandoned.store(true);
		if (task.settled.load()) {
			if (task.offer)
				sessions.settle(*task.offer, task.written.load());
			if (task.request)
				sessions.release(*task.request);
			if (task.open_created && !task.written.load())
				sessions.close(task.session, task.admission->authorization, now);
			if (task.transfer)
				--(task.bootstrap ? bootstrap : catchup);
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
		if (task.job) {
			if (task.job->done.load(std::memory_order_acquire))
				stream_job(task);
			return false;
		}
		const auto &authorization = task.admission->authorization;
		if (!task.initialized) {
			task.initialized = true;
			if (task.admission->kind == h::RequestKind::Close) {
				const auto result = sessions.close(task.session, authorization, now);
				if (result == d::SubscriptionResult::Accepted)
					control(task, 204, {});
				else
					error(task, result);
				return false;
			}
			if (task.admission->kind == h::RequestKind::Open) {
				const auto result = sessions.open(task.session, authorization, task.binding, task.base,
												  task.force_snapshot, task.compression, now);
				if (result != d::SubscriptionResult::Accepted) {
					error(task, result);
					return false;
				}
				const auto *admitted = sessions.metadata(task.session, now);
				if (!admitted) throw std::logic_error("relay admitted session unavailable");
				const bool resume = !admitted->scope.snapshot;
				task.open_created = true;
				control(task, 200,
						"{\"version\":1,\"session\":\"" + hex(task.session) + "\",\"window\":0,\"mode\":\"" +
							(resume ? "resume" : "snapshot") + "\",\"wait_ms\":30000}");
				return false;
			}
			const auto admitted = sessions.next(task.session, authorization, task.after, task.published, now);
			if (!admitted.token) {
				error(task, admitted.result);
				return false;
			}
			task.request = admitted.token;
			const auto *metadata = sessions.metadata(task.session, now);
			if (!metadata) {
				error(task, d::SubscriptionResult::Expired);
				return false;
			}
			task.compression = metadata->compression;
		}
		if (!sessions.metadata(task.session, now)) {
			task.reply->abandon();
			task.completed = true;
			return false;
		}
		if (!task.update) {
			task.bootstrap = task.after == 0;
			auto &count = task.bootstrap ? bootstrap : catchup;
			if (count == (task.bootstrap ? limits.bootstrap_transfers : limits.catchup_transfers))
				return false;
			auto prepared = sessions.prepare(*task.request, now);
			if (!prepared.update) {
				if (prepared.result == d::SubscriptionResult::Parked) {
					if (now >= task.poll_deadline)
						control(task, 204, {});
				} else if (prepared.result != d::SubscriptionResult::Busy &&
						   prepared.result != d::SubscriptionResult::AwaitSettlement)
					error(task, prepared.result);
				return false;
			}
			++count;
			task.transfer = true;
			peak_transfers = std::max(peak_transfers, bootstrap + catchup);
			task.update = std::move(prepared.update);
			task.offer = prepared.token;
			const auto metadata =
				h::update_headers(task.update->kind(), task.update->target(), task.compression,
								  h::SessionHeaders{task.session, task.offer->window});
			if (task.reply->start_chunked(metadata, true) != http::StreamSubmit::Accepted)
				throw std::runtime_error("relay stream closed");
			task.started = true;
		}
		if (!task.pending && task.frame < task.update->fragments() && task.writable.load())
			task.pending = task.update->frame(task.frame, task.compression);
		if (task.pending) {
			const auto result = task.publish(task.pending);
			if (result == http::StreamSubmit::Accepted)
				++task.frame;
			else if (result != http::StreamSubmit::Busy)
				throw std::runtime_error("relay stream refused frame");
		}
		if (task.frame == task.update->fragments() && !task.pending) {
			if (task.reply->finish() != http::StreamSubmit::Accepted)
				throw std::runtime_error("relay stream refused finish");
			task.completed = true;
		}
		return false;
	}
};
ServiceRuntime::ServiceRuntime(ServiceAccounts accounts, std::shared_ptr<const Registry> registry,
							   std::span<const SessionBinding> bindings, ServiceLimits limits,
							   std::shared_ptr<MirrorService> mirror, std::function<void()> wake) {
	if (!accounts.metadata)
		throw std::invalid_argument("relay service metadata account");
	impl_ = d::make_storage_owner<Impl>(accounts.metadata, std::malloc, std::free, accounts,
										std::move(registry), bindings, limits, std::move(mirror), std::move(wake));
}
ServiceRuntime::~ServiceRuntime() = default;
void ServiceRuntime::close() noexcept {
	impl_->closing.store(true);
	for (auto &lane : impl_->lanes)
		lane->admission.close();
	impl_->wake();
}
bool ServiceRuntime::step(Clock::time_point now, bool rescan) {
	auto &p = *impl_;
	bool progress = false;
	if (p.closing.load() && !p.closed_sessions) {
		p.sessions.close_all();
		p.closed_sessions = true;
		progress = true;
	}
	if (rescan) {
		std::lock_guard lock(p.mutex);
		for (auto &lane : p.lanes)
			lane->unswept = lane->free.size() == lane->slots.size() ? 0 : lane->slots.size();
		p.unswept_sessions = p.sessions.size() ? p.limits.sessions.sessions : 0;
	}
	// What a visit can change; a task whose fingerprint did not move waits on a wake or a deadline.
	auto fingerprint = [](const Impl::Task &task) {
		return std::tuple(task.frame, task.completed, task.initialized, task.started, task.transfer,
						  static_cast<bool>(task.update), static_cast<bool>(task.pending),
						  static_cast<bool>(task.control), task.request.has_value(), task.offer.has_value());
	};
	for (auto index : {std::size_t{2}, std::size_t{0}, std::size_t{1}}) {
		auto &lane = *p.lanes[index];
		for (std::size_t turn = 0; turn < (index == 1 ? 32 : 8) && lane.unswept; ++turn) {
			--lane.unswept;
			const auto slot = lane.cursor++ % lane.slots.size();
			std::shared_ptr<Impl::Task> task;
			{
				std::lock_guard lock(p.mutex);
				task = lane.slots[slot];
			}
			if (!task)
				continue;
			const auto before = fingerprint(*task);
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
			progress |= fingerprint(*task) != before;
		}
	}
	if (p.unswept_sessions) {
		d::WorkBudget budget{4096, 64};
		p.sessions.collect(budget, now);
		p.unswept_sessions -= std::min<std::size_t>(p.unswept_sessions, 64 - budget.tokens);
		if (!budget.bytes) // a retirement spent the byte budget; the next turn resumes it
			p.unswept_sessions = std::max<std::size_t>(p.unswept_sessions, 1);
	}
	return progress;
}
bool ServiceRuntime::swept() const noexcept {
	const auto &p = *impl_;
	return !p.unswept_sessions &&
		   std::all_of(p.lanes.begin(), p.lanes.end(), [](const auto &lane) { return !lane->unswept; });
}
ServiceStatus ServiceRuntime::status() const noexcept {
	const auto &p = *impl_;
	ServiceStatus result;
	result.bootstrap = p.bootstrap;
	result.catchup = p.catchup;
	result.peak_transfers = p.peak_transfers;
	result.sessions = p.sessions.size();
	result.closing = p.closing.load();
	std::lock_guard lock(p.mutex);
	for (std::size_t index = 0; index < 3; ++index)
		result.requests[index] = p.lanes[index]->slots.size() - p.lanes[index]->free.size();
	return result;
}
bool ServiceRuntime::drained() const noexcept {
	const auto state = status();
	return state.closing && state.requests == std::array<std::size_t, 3>{} && impl_->sessions.drained() &&
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
	if (admission->history) {
		task->job = std::make_shared<MirrorJob>();
		parse_history(request.path, admission->lease->body().view(), *task->job);
	} else {
		auto parsed = h::parse_session_request(
			account, admission->kind, admission->lease->body().view(),
			{p.lanes[index]->admission.body_limit(static_cast<d::SubscriptionRequestKind>(index)), 4096, 30000});
		const auto &command = parsed->request();
		if (command.version == 2) {
			// W5 from the mirror; version 1 stays with the bundles.
			if (!p.mirror)
				reject(404, "{\"error\":\"not_found\"}");
			auto job = std::make_shared<MirrorJob>();
			job->kind = command.kind == h::RequestKind::Open	? MirrorJob::Kind::Open
						: command.kind == h::RequestKind::Next ? MirrorJob::Kind::Next
															   : MirrorJob::Kind::Close;
			job->session = command.session;
			for (const auto &position : command.positions)
				job->positions.push_back({std::string(position.name), position.cursor});
			job->all = command.all.has_value();
			job->compressed = command.compressed;
			job->wait_ms = command.wait_ms;
			task->job = std::move(job);
		} else {
			task->session = command.session;
			task->base = command.base;
			task->published = command.published;
			task->after = command.after;
			task->force_snapshot = command.force_snapshot;
			task->compression = command.compressed;
			task->poll_deadline = Clock::now() + std::chrono::milliseconds(command.wait_ms);
			if (command.kind == h::RequestKind::Open) {
				const auto binding = p.registry->resolve(admission->authorization, command.identity, command.namespaces);
				if (!binding)
					reject(403, "{\"error\":\"scope_forbidden\"}");
				task->binding = *binding;
				if (RAND_bytes(reinterpret_cast<unsigned char *>(task->session.data()), task->session.size()) != 1)
					throw std::runtime_error("relay session entropy");
			}
		}
	}
	if (task->job) {
		task->job->principal = admission->authorization.principal;
		task->job->grants = admission->authorization.namespaces;
		task->job->answered = p.wake;
	}
	task->admission = std::move(admission);
	http::StreamOptions stream;
	stream.resource = p.lanes[index]->resource;
	stream.reply.deadline = task->deadline + std::chrono::seconds(1);
	stream.reply.limits = {16, 4096, 4096};
	stream.reply.request_retention = task->admission;
	stream.chunk_bytes = d::OwnedFragmentEncoder::maximum_wire_bytes();
	h::OwnedReplyPump::callbacks(stream, task, p.wake);
	task->reply = response.stream(std::move(stream));
	if (!task->reply)
		throw std::runtime_error("relay owned streaming unavailable");
	{
		std::lock_guard lock(p.mutex);
		auto &lane = *p.lanes[index];
		if (!p.closing.load() && !lane.free.empty()) {
			const auto slot = lane.free.back();
			lane.free.pop_back();
			if (auto job = task->job)
				p.mirror->submit(std::move(job));
			lane.slots[slot] = std::move(task);
		}
	}
	if (!task) {
		p.wake();
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
