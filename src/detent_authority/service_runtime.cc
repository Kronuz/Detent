// The service's owner loop (Service::run): the one coroutine that admits, orders, applies, exports and settles every task.
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
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__linux__)
#include <completion/asio_completion.h>
#include <completion/native_completion.h>
#include <journal/mutation.h>
#endif
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
namespace {
std::optional<std::uint64_t> owner_cpu_ns() noexcept {
#ifdef CLOCK_THREAD_CPUTIME_ID
	timespec value{};
	if (::clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value) || value.tv_sec < 0 || value.tv_nsec < 0 ||
		value.tv_nsec >= 1000000000)
		return std::nullopt;
	auto seconds = static_cast<std::uint64_t>(value.tv_sec);
	auto nanoseconds = static_cast<std::uint64_t>(value.tv_nsec);
	if (seconds > (std::numeric_limits<std::uint64_t>::max() - nanoseconds) / 1000000000)
		return std::nullopt;
	return seconds * 1000000000 + nanoseconds;
#else
	return std::nullopt;
#endif
}
std::string duration_json(const kronuz::metrics::DurationSnapshot &sample) {
	std::string result = "{\"count\":" + std::to_string(sample.count) +
						 ",\"sum_ns\":" + std::to_string(sample.sum_ns) +
						 ",\"maximum_ns\":" + std::to_string(sample.maximum_ns) + ",\"buckets\":[";
	for (std::size_t i = 0; i < sample.buckets.size(); ++i) {
		if (i) {
			result += ',';
		}
		result += std::to_string(sample.buckets[i]);
	}
	return result + "]}";
}
// `what` and the reason a fenced worker reports once, after its other actions: a startup refusal.
std::string fence_reason(Node &node, std::string what) {
	auto find = [](kronuz::consensus::Actions actions) -> std::optional<std::string> {
		for (auto &action : actions)
			if (auto fenced = std::get_if<kronuz::consensus::Fenced>(&action))
				return std::move(fenced->reason);
		return std::nullopt;
	};
	auto reason = find(node.take_network_actions());
	for (unsigned turn = 0; !reason && turn < 2; ++turn)
		reason = find(node.worker().take_actions());
	return reason ? what + ": " + *reason : what;
}
// The storage format this build reads and writes: bump it with any change to a stored byte layout
// (docs/mechanisms/format-refusal.md lists what each number means). Format 1 is the layout before
// Detent:53abd637, whose images and replay this build does not read.
constexpr unsigned storage_format = 2;
constexpr const char *format_file = "format";
std::string format_stamp(unsigned format) { return "detent authority storage format " + std::to_string(format) + "\n"; }
// A directory of another format is refused at startup: Detent reads one format only.
[[noreturn]] void refuse_format(const std::filesystem::path &directory, const std::string &found) {
	throw std::runtime_error("authority storage format error: " + directory.string() + " is in " + found +
							 ", this build expects storage format " + std::to_string(storage_format) +
							 "; Detent keeps no compatibility, so start from an empty directory");
}
// Writes the stamp durably: a temporary file, synced, renamed, and the directory synced.
void write_format_stamp(const std::filesystem::path &history) {
	auto path = history / format_file;
	auto temporary = path;
	temporary += ".tmp";
	int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
	check(fd >= 0, "authority format stamp");
	auto bytes = format_stamp(storage_format);
	bool written = ::write(fd, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()) && ::fsync(fd) == 0;
	::close(fd);
	check(written && ::rename(temporary.c_str(), path.c_str()) == 0, "authority format stamp");
	int directory = ::open(history.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	check(directory >= 0, "authority format stamp");
	bool synced = ::fsync(directory) == 0;
	::close(directory);
	check(synced, "authority format stamp");
}
// Checks the stamp beside the history files before the journal opens. Another stamp (format 1 or a
// newer one) or an unreadable one is refused. A directory with a journal and no stamp is unknown:
// it returns true and the caller decides by replay. A new directory is stamped.
bool check_storage_format(const std::filesystem::path &journal, const std::filesystem::path &history) {
	std::ifstream file(history / format_file, std::ios::binary);
	if (file) {
		std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		for (unsigned format = 0; format < 100; ++format)
			if (text == format_stamp(format)) {
				if (format != storage_format)
					refuse_format(journal, "storage format " + std::to_string(format));
				return false;
			}
		refuse_format(journal, "an unreadable format stamp");
	}
	if (std::filesystem::exists(journal / "manifest"))
		return true;
	write_format_stamp(history);
	return false;
}
std::string result_json(Result r) {
	constexpr const char *names[] = {
		"invalid",			 "applied",		   "no_change",	   "conflict",
		"unknown_session",	 "session_fenced", "out_of_order", "result_unavailable",
		"identity_conflict", "capacity",	   "exhausted"};
	return "{\"status\":\"" + std::string(names[static_cast<unsigned>(r.status)]) +
		   "\",\"revision\":" + std::to_string(r.revision) +
		   ",\"namespace_revision\":" + std::to_string(r.namespace_revision) +
		   ",\"generation\":" + std::to_string(r.generation) + ",\"sequence\":" + std::to_string(r.sequence) +
		   "}";
}
const char *read_failure(const Completion &completion, bool fenced) noexcept {
	if (completion.read && !fenced) {
		if (completion.rejection == kronuz::consensus::RejectReason::NotLeader)
			return "{\"error\":\"source_not_leader\"}";
		if (completion.rejection == kronuz::consensus::RejectReason::Busy)
			return "{\"error\":\"admission_full\"}";
	}
	return "{\"error\":\"outcome_unknown\"}";
}
int result_status(Result r) {
	switch (r.status) {
	case Status::Applied:
	case Status::NoChange:
		return 200;
	case Status::Capacity:
		return 429;
	default:
		return 409;
	}
}
} // namespace
struct Service::Runtime {
	asio::io_context &io;
	task::asio_scheduler &scheduler;
	detent::OwnerWake wake; // storage and history completions end the owner's sleep
	bool active = false;
	std::exception_ptr failure;
	kronuz::metrics::DurationHistogram<> owner_turn, owner_cpu, wake_lateness, job_settlement,
		fenced_job_observed;
	kronuz::metrics::Counter cpu_clock_errors;
	struct Flight {
		std::shared_ptr<kronuz::journal::IOOperation> operation;
		Clock::time_point admitted;
	};
	std::optional<Flight> storage_flight;
	// History's writes (D6) run on their own queue, beside the journal's storage jobs.
	bool history_active = false;
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__linux__)
	using Driver = kronuz::journal::AsioCompletionDriver<kronuz::journal::NativeCompletionQueue>;
	std::shared_ptr<kronuz::journal::NativeCompletionQueue> queue, history_queue;
	std::shared_ptr<Driver> driver, history_driver;
	Runtime(const ServiceOptions &options, detent::OwnerLoop &owner)
		: io(owner.io()), scheduler(owner.scheduler()), wake(owner.wake()) {
		for (auto *each : {&queue, &history_queue}) {
#ifdef __linux__
			using Policy = kronuz::journal::LinuxCompletionPolicy;
			*each = std::make_shared<kronuz::journal::NativeCompletionQueue>(
				options.storage_backend == "native"		? Policy::RequireNative
				: options.storage_backend == "fallback" ? Policy::Fallback
														: Policy::Automatic);
#else
			(void)options;
			*each = std::make_shared<kronuz::journal::NativeCompletionQueue>();
#endif
		}
		driver = std::make_shared<Driver>(scheduler, queue, true);
		history_driver = std::make_shared<Driver>(scheduler, history_queue, true);
	}

#else
	Runtime(const ServiceOptions &, detent::OwnerLoop &owner)
		: io(owner.io()), scheduler(owner.scheduler()), wake(owner.wake()) {}
#endif
	void dispatch(const std::shared_ptr<Node> &owner) {
		auto &node = *owner;
		if (storage_flight && !active && storage_flight->operation->done() &&
			(!node.worker().owns_io_operation(storage_flight->operation) || node.worker().fenced())) {
			auto &histogram = node.worker().fenced() ? fenced_job_observed : job_settlement;
			histogram.observe(elapsed_ns(storage_flight->admitted));
			storage_flight.reset();
		}
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__linux__)
		if (auto operation = node.worker().take_io_operation()) {
			if (active) {
				throw std::logic_error("authority dispatched overlapping storage jobs");
			}
			if (storage_flight) {
				throw std::logic_error("authority overlapped unsettled storage observations");
			}
			storage_flight.emplace(Flight{operation, Clock::now()});
			active = true;
			try {
				task::spawn(task::scheduler_ref::of(scheduler), driver->run(operation),
							[this, owner, operation](std::exception_ptr error) {
								if (error) {
									// A dead driver cannot gracefully reap an original
									// still owned by its backend. Follow its fail-stop
									// policy rather than refunding or waiting forever.
									if (operation->in_flight())
										std::terminate();
									failure = error;
									owner->worker().storage_operation_failed(
										operation, "authority completion driver failed");
								}
								active = false;
								wake.raise();
							});
			} catch (...) {
				active = false;
				if (operation->in_flight())
					std::terminate();
				node.worker().storage_operation_failed(operation, "authority completion dispatch failed");
				throw;
			}
		}
		if (auto operation = history_active ? nullptr : node.take_history_operation()) {
			history_active = true;
			try {
				task::spawn(task::scheduler_ref::of(scheduler), history_driver->run(operation),
							[this, operation](std::exception_ptr error) {
								// A dead driver leaves the write unsettled: fail-stop, as storage does.
								if (error) {
									if (operation->in_flight())
										std::terminate();
									failure = error;
								}
								history_active = false;
								wake.raise();
							});
			} catch (...) {
				history_active = false;
				if (operation->in_flight())
					std::terminate();
				throw;
			}
		}
#else
		if (auto operation = node.take_history_operation())
			kronuz::journal::drive_synchronously(*operation);
#endif
	}
	task::task<void> run(Service &service, std::promise<void> &ready);
	// Sleeps on `timer` until `until`, or until a wake cancels the wait.
	task::task<void> sleep(asio::steady_timer &timer, Clock::time_point until) {
		wake.arm(timer, until);
		const auto error = co_await task::await_asio<void(asio::error_code)>(
			[&](auto handler) { timer.async_wait(std::move(handler)); });
		if (error && error != asio::error::operation_aborted)
			throw asio::system_error(error);
	}
};
// The runtime's frame lives on the owner; a failure after admission fences the service.
task::task<void> Service::run(std::promise<void> ready) {
	try {
		Runtime runtime(options_, owner_);
		try {
			co_await runtime.run(*this, ready);
		} catch (...) {
			// A failure before admission settles the constructor; later ones find it settled.
			try {
				ready.set_exception(std::current_exception());
			} catch (const std::future_error &) {
			}
			std::lock_guard lock(mutex_);
			stopping_ = true;
			health_stamp_ = Clock::now();
			health_ = std::string("{\"mode\":\"") +
					  (options_.peers ? "durable_fixed_voters" : "durable_single_voter") +
					  "\",\"fenced\":true}";
		}
	} catch (...) {
		// A backend/runtime failure before admission must settle the constructor.
		try {
			ready.set_exception(std::current_exception());
		} catch (const std::future_error &) {
		}
	}
}
task::task<void> Service::Runtime::run(Service &service, std::promise<void> &ready) {
	auto &options_ = service.options_;
	auto &mutex_ = service.mutex_;
	auto &stopping_ = service.stopping_;
	auto &queue_ = service.queue_;
	auto &busy_producers_ = service.busy_producers_;
	auto &health_ = service.health_;
	auto accounting_ = service.accounting_;
	asio::steady_timer timer(io);
	struct ReleaseWake { // the wake forgets `timer` before it is destroyed, however run() ends
		detent::OwnerWake &wake;
		~ReleaseWake() { wake.release(); }
	} release_wake{wake};
	std::shared_ptr<Node> owned_node;
	std::shared_ptr<AuthorityPeers> peers;
	// The latest a sleep may end. A multi-voter authority polls every 1 ms: received peer frames wait in
	// Peers until admit(), with no wake at consensus a197efb. Quest multi-voter-idle-wake (a consensus
	// pin past 0acfaff and a Peers input wake) removes this branch.
	auto wake_by = [&](Clock::time_point deadline) {
		return peers ? std::min(deadline, Clock::now() + std::chrono::milliseconds(1)) : deadline;
	};
	// A start or stop step visits the node again at once: each visit advances budgeted work that no wake
	// announces. Only while storage or history I/O is in flight does it sleep, until that completion's
	// wake (250 ms bounds one missed); peers keep 1 ms (wake_by).
	constexpr auto storage_wait = std::chrono::milliseconds(250);
	auto in_flight = [&] { return active || history_active || peers; };
	std::shared_ptr<PeerHistory> peer_history;

	std::map<std::uint64_t, std::vector<std::shared_ptr<Task>>> pending;
	std::uint64_t next = 0;
	bool started = false;
	std::uint64_t last_tick = 0;
	auto election_delay = [&] {
		std::uint32_t entropy = 0;
		check(RAND_bytes(reinterpret_cast<unsigned char *>(&entropy), sizeof(entropy)) == 1,
			  "election entropy");
		const auto &timing = options_.node.timing;
		return timing.election_min + entropy % (timing.election_max - timing.election_min + 1);
	};
	using Tasks = std::vector<std::shared_ptr<Task>>;
	using PendingTasks = std::map<std::uint64_t, Tasks>;
	struct Preparation {
		Tasks tasks;
		std::shared_ptr<Task> current;
		std::optional<CommandEncoder> encoder;
		std::optional<EncodedCommand> encoded;
		std::optional<ProposalBuilder> builder;
		std::optional<PreparedProposal> proposal;
		PendingTasks::node_type waiter;
		std::uint64_t request = 0;
		bool sealed = false, cancelling = false;
		std::size_t cancelled = 0;
		Preparation() { tasks.reserve(64); }
	};
	struct ReadyRead {
		std::shared_ptr<Task> task;
		std::uint64_t boundary = 0;
	};
	std::vector<ReadyRead> ready_reads(options_.requests);
	std::size_t read_head = 0, read_tail = 0, read_count = 0, legacy_ready = 0;
	bool subscription_export_turn = true;
	std::size_t subscription_ready = 0;
	std::vector<std::shared_ptr<Task>, distribution::StorageAllocator<std::shared_ptr<Task>>>
		legacy_deliveries{
			distribution::StorageAllocator<std::shared_ptr<Task>>(accounting_->delivery_account)};
	std::size_t control_head = 0, control_tail = 0, control_count = 0;
	struct ReadSlot {
		std::shared_ptr<Task> task;
		State capture;
		std::optional<ReadReplyEncoder> encoder;
		std::shared_ptr<ReplyAdmission> reservation;
		bool retiring = false, quota_blocked = false;
		ReadSlot(std::shared_ptr<Task> t, const State &state) : task(std::move(t)), capture(state) {}
	};
	std::optional<ReadSlot> reading;
	struct DeliverySlot {
		std::shared_ptr<Task> task;
		std::optional<DeliveryExport> exporter;
		std::shared_ptr<const distribution::OwnedBuffer> pending;
		std::size_t pending_disposal = 0;
		bool retiring = false, source_drained = false;
		bool discard_pending(distribution::WorkBudget &budget) noexcept {
			if (!pending)
				return true;
			if (!budget.tokens)
				return false;
			if (!pending_disposal)
				pending_disposal = pending->storage_capacity();
			const auto n = std::min(pending_disposal, budget.bytes);
			pending_disposal -= n;
			budget.bytes -= n;
			if (pending_disposal)
				return false;
			--budget.tokens;
			pending.reset();
			return true;
		}
	};
	std::optional<DeliverySlot> delivering;
	std::shared_ptr<ChangeHistory> history;
	std::optional<Subscriptions> subscriptions;
	// W5/W6 sessions (version 2): their table, the requests on the owner, and the one answer in flight.
	std::optional<NamespaceSessions> frames_sessions;
	LayoutExport layout_export;
	std::optional<HeldFile> held_file;
	std::vector<std::shared_ptr<Task>> frames_tasks;
	struct FramesSlot {
		std::shared_ptr<Task> task;
		std::optional<FramesExport> exporter;
		std::shared_ptr<const distribution::OwnedBuffer> pending;
		bool retiring = false;
	};
	std::optional<FramesSlot> framing;
	Clock::time_point enabling_at{};
	std::uint64_t unsealed_after = 0; // The seal index unsealed changes follow, and since when.
	std::optional<Clock::time_point> unsealed_since;
	Clock::time_point sealing_at{};
	kronuz::metrics::Counter frames_answers, frames_idle, frames_held_back, frames_errors;
	using SessionTasks =
		std::vector<std::shared_ptr<Task>, distribution::StorageAllocator<std::shared_ptr<Task>>>;
	SessionTasks session_tasks(
		distribution::StorageAllocator<std::shared_ptr<Task>>(accounting_->delivery_account));
	using SessionPendingEntry = std::pair<const std::uint64_t, std::shared_ptr<Task>>;
	std::map<std::uint64_t, std::shared_ptr<Task>, std::less<>,
			 distribution::StorageAllocator<SessionPendingEntry>>
		session_pending(distribution::StorageAllocator<SessionPendingEntry>(accounting_->delivery_account));
	std::size_t session_cursor = 0;
	std::size_t close_cursor = 0;
	struct TaskQueue {
		SessionTasks ring;
		std::size_t head = 0, tail = 0, count = 0;
		TaskQueue(std::shared_ptr<distribution::StorageLedger> account, std::size_t capacity)
			: ring(distribution::StorageAllocator<std::shared_ptr<Task>>(std::move(account))) {
			ring.resize(capacity);
		}
		void push(std::shared_ptr<Task> task) {
			check(count < ring.size(), "authorization queue capacity exhausted");
			ring[tail] = std::move(task);
			tail = (tail + 1) % ring.size();
			++count;
		}
		std::shared_ptr<Task> pop() {
			auto task = std::move(ring[head]);
			head = (head + 1) % ring.size();
			--count;
			return task;
		}
	};
	std::optional<TaskQueue> close_authorization, session_authorization, session_retirement;
	kronuz::metrics::Counter subscription_reads, subscription_exports, subscription_idle;
	kronuz::metrics::Counter export_expired, export_disconnected, export_limits, export_errors;
	using ActiveSessionEntry = std::pair<const distribution::Identity, std::weak_ptr<Task>>;
	std::map<distribution::Identity, std::weak_ptr<Task>, std::less<>,
			 distribution::StorageAllocator<ActiveSessionEntry>>
		active_session_requests(
			distribution::StorageAllocator<ActiveSessionEntry>(accounting_->delivery_account));
	std::vector<Completion> held_completions;
	std::size_t completion_cursor = 0, completion_task = 0;
	std::uint64_t expiry_request = 0;
	std::size_t expiry_task = 0;
	bool read_first = false;
	std::optional<Preparation> preparing;
	std::shared_ptr<Task> carry_task;
	std::optional<EncodedCommand> carry;
	std::vector<std::shared_ptr<Task>> retired_tasks(options_.requests);
	std::size_t retired_head = 0, retired_tail = 0, retired_count = 0, retired_task_bytes = 0;
	auto collect_tasks = [&](CommandWorkBudget &parent) {
		CommandWorkBudget budget;
		budget.bytes = parent.bytes / 2;
		budget.receipts = parent.receipts / 2;
		const auto reserved_bytes = budget.bytes, reserved_receipts = budget.receipts;
		while (retired_count) {
			auto &task = retired_tasks[retired_head];
			if (!dispose_command(task->command, budget))
				break;
			while (!task->keys.empty() && budget.receipts) {
				--budget.receipts;
				task->keys.pop_back();
			}
			if (!task->keys.empty())
				break;
			if (!budget.receipts || budget.bytes < 256)
				break;
			--budget.receipts;
			budget.bytes -= 256;
			retired_task_bytes -= task->admission->bytes;
			task.reset();
			retired_head = (retired_head + 1) % retired_tasks.size();
			--retired_count;
		}
		parent.bytes -= reserved_bytes - budget.bytes;
		parent.receipts -= reserved_receipts - budget.receipts;
	};
	auto begin_delivery_retirement = [](Task &task) {
		if (task.retiring)
			return;
		task.retiring = true;
		task.retirement_remaining =
			task.admission->subscription ? task.admission->subscription->body().storage_capacity() : 0;
		auto scope = std::move(task.delivery->scope);
		if (scope && scope.use_count() == 1) {
			task.retirement_remaining += scope->storage_capacity();
			task.retiring_scope = std::move(scope);
		}
	};
	auto complete = [&](const std::shared_ptr<Task> &task, Reply reply) {
		if (task->settled)
			return;
		if (task->export_ready) {
			task->export_ready = false;
			--subscription_ready;
		}
		if (task->kind == Task::Kind::Distribution && task->delivery) {
			check(control_count < legacy_deliveries.size(), "legacy control retirement capacity exhausted");
			legacy_deliveries[control_tail] = task;
			control_tail = (control_tail + 1) % legacy_deliveries.size();
			++control_count;
		}
		if (!task->retiring && (!task->command.mutations.empty() || !task->keys.empty())) {
			check(retired_count < retired_tasks.size(), "publisher retirement capacity exhausted");
			task->retiring = true;
			retired_tasks[retired_tail] = task;
			retired_tail = (retired_tail + 1) % retired_tasks.size();
			++retired_count;
			retired_task_bytes += task->admission->bytes;
		}
		task->settled = true;
		auto callback = std::move(task->complete);
		if (callback) {
			accounting_->publisher_ready.observe(elapsed_ns(task->admission->admitted));
		}
		if (task->kind == Task::Kind::Command) {
			std::lock_guard lock(mutex_);
			busy_producers_.erase(task->command.producer);
		}
		if (callback) {
			callback(std::move(reply));
		}
	};
	try {
		// Provision the parent directory explicitly; newly created final directory
		// receives a durable parent namespace barrier before Journal creation. History
		// (D6) lives beside the journal, so the journal's inventory never meets it.
		auto history_directory = options_.directory;
		history_directory += ".history";
		bool created = false;
		for (const auto &directory : {options_.directory, history_directory})
			if (std::filesystem::create_directory(directory)) {
				check(::chmod(directory.c_str(), 0700) == 0, "authority directory permissions");
				created = true;
			}
		if (created) {
			auto parent = options_.directory.parent_path();
			if (parent.empty()) {
				parent = ".";
			}
			int fd = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
			if (fd < 0) {
				throw std::runtime_error("authority parent directory");
			}
			auto synced = ::fsync(fd);
			::close(fd);
			if (synced) {
				throw std::runtime_error("authority directory durability");
			}
		}
		const bool unstamped = check_storage_format(options_.directory, history_directory);
		auto node_options = options_.node;
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__linux__)
		node_options.scheduling.async_storage = true;
#else
		check(!node_options.scheduling.async_storage && !node_options.scheduling.async_appends,
			  "native completion storage is unavailable on this platform");
#endif
		owned_node = std::make_shared<Node>(std::make_unique<kronuz::journal::PosixIO>(options_.directory),
											node_options);
		auto &node = *owned_node;
		// With peers, history fetches what it lacks from them and serves them its files (D91).
		std::optional<kronuz::consensus::transport::PeerOptions> peer_options = options_.peers;
		if (peer_options) {
			std::vector<kronuz::consensus::NodeId> others;
			for (auto voter : node_options.configuration.voters)
				if (voter != node_options.configuration.local)
					others.push_back(voter);
			peer_history = std::make_shared<PeerHistory>(io.get_executor(), std::move(others), peer_options->fetches,
														 std::chrono::milliseconds(200));
			peer_options->reader = history_reader(std::make_shared<kronuz::journal::PosixIO>(history_directory),
												  peer_options->fetch_bytes);
			node.attach_history(std::make_shared<kronuz::journal::PosixIO>(history_directory), peer_history->fetch());
		} else {
			node.attach_history(std::make_shared<kronuz::journal::PosixIO>(history_directory));
		}
		history = std::allocate_shared<ChangeHistory>(
			distribution::StorageAllocator<ChangeHistory>(accounting_->delivery_account),
			accounting_->delivery_account);
		subscriptions.emplace(accounting_->delivery_account, options_.subscriptions);
		frames_sessions.emplace(accounting_->delivery_account,
								SessionLimits{options_.subscriptions.sessions, options_.subscriptions.per_principal,
											  options_.subscriptions.idle_lifetime,
											  options_.subscriptions.maximum_lifetime});
		frames_tasks.reserve(options_.subscription_requests.opens + options_.subscription_requests.polls +
							 options_.subscription_requests.closes);
		legacy_deliveries.resize(options_.requests);
		session_tasks.resize(options_.subscription_requests.opens + options_.subscription_requests.polls +
							 options_.subscription_requests.closes);
		session_retirement.emplace(accounting_->delivery_account, session_tasks.size());
		close_authorization.emplace(accounting_->delivery_account, options_.subscription_requests.closes);
		session_authorization.emplace(accounting_->delivery_account,
									  options_.subscription_requests.opens +
										  options_.subscription_requests.polls);
		auto last_health = Clock::time_point{};
		// The applied index of the latest seal the parked nexts were answered for.
		std::uint64_t seal_seen = 0;
		// A leader is known: this node leads, or follows one. Health reports it before has_leader() does.
		bool leader_known = false;
		// Last proof a follower's leader is alive: a RoleChanged naming it, or our reply to its append.
		auto leader_contact = Clock::now();
		// A voter back from an absence of election_min or more (stopped, starved) reads the frames
		// that waited in its sockets meanwhile: none proves a live leader, so no proof counts until
		// election_max after its return.
		auto returned = Clock::time_point{};
		auto trusting = [&] {
			return Clock::now() - returned >= std::chrono::milliseconds(options_.node.timing.election_max);
		};
		// A follower is ready only while that proof is within election_max, CheckQuorum's window on the
		// leader side (D41); pre-vote keeps the leader known after it falls silent.
		auto leader_alive = [&] {
			return leader_known && trusting() &&
				   (node.worker().role() == kronuz::consensus::Role::Leader ||
					Clock::now() - leader_contact <= std::chrono::milliseconds(options_.node.timing.election_max));
		};
		// `/v1/health` `ready`; the history routes answer only while it holds (W8).
		auto serving = [&] { return node.worker().ready() && leader_alive(); };
		auto publish_health = [&] {
			const auto export_progress = delivering && delivering->exporter
				? delivering->exporter->snapshot_progress().value_or(distribution::SnapshotEncodingProgress{})
				: distribution::SnapshotEncodingProgress{};
			auto captured_at = Clock::now();
			std::shared_ptr<const reactor::ConnectionAdmission> observer;
			{
				std::lock_guard lock(mutex_);
				observer = service.connections_;
			}
			auto connections = observer ? observer->stats() : reactor::ConnectionStats{};
			auto peer_occupancy = peers ? peers->occupancy() : kronuz::consensus::transport::PeerOccupancy{};
			std::size_t requests, body_bytes, reply_bytes;
			{
				std::lock_guard lock(accounting_->mutex);
				requests = accounting_->requests;
				body_bytes = accounting_->body_bytes;
				reply_bytes = accounting_->reply_bytes;
			}
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__linux__)
			auto io_stats = queue->stats();
			auto driver_stats = driver->stats();
			auto primitive_durations = [](const auto &values) {
				std::string encoded = "[";
				for (const auto &value : values) {
					if (encoded.size() > 1) {
						encoded += ',';
					}
					encoded += duration_json(value);
				}
				return encoded + "]";
			};
			std::string storage_health =
				",\"storage_async\":true,\"io_native_submitted\":" +
				std::to_string(io_stats.native_submitted) +
				",\"io_fallback_submitted\":" + std::to_string(io_stats.fallback_submitted);
			storage_health += ",\"io_native_completed\":" + std::to_string(io_stats.native_completed) +
							  ",\"io_fallback_completed\":" + std::to_string(io_stats.fallback_completed) +
							  ",\"io_read_bytes\":" + std::to_string(io_stats.file_read_bytes) +
							  ",\"io_write_bytes\":" + std::to_string(io_stats.file_write_bytes) +
							  ",\"io_notification_errors\":" + std::to_string(io_stats.notification_errors) +
							  ",\"io_maximum_outstanding\":" + std::to_string(io_stats.maximum_outstanding);
			auto primitive_counts = [](const auto &counts) {
				std::string value = "[";
				for (auto count : counts) {
					if (value.size() > 1)
						value += ',';
					value += std::to_string(count);
				}
				return value + "]";
			};
			storage_health +=
				",\"io_native_by_primitive\":" + primitive_counts(io_stats.native_by_primitive) +
				",\"io_fallback_by_primitive\":" + primitive_counts(io_stats.fallback_by_primitive);
			const auto &policy = node.checkpoint_policy();
			const auto policy_age =
				policy.due && last_tick >= policy.due_since ? last_tick - policy.due_since : 0;
			storage_health +=
				",\"checkpoint_due\":" + std::string(policy.due ? "true" : "false") +
				",\"checkpoint_due_age_ms\":" + std::to_string(policy_age) +
				",\"checkpoint_attempts\":" + std::to_string(policy.attempts.get()) +
				",\"checkpoint_reservation_unavailable\":" +
				std::to_string(policy.reservation_unavailable.get()) +
				",\"checkpoint_captured_boundary\":" + std::to_string(policy.captured_boundary) +
				",\"checkpoint_published_boundary\":" + std::to_string(policy.published_boundary);
			const auto append = peers ? peers->stats().append : kronuz::consensus::transport::AppendTimings{};
			storage_health +=
				",\"append_emit_attempts\":" + std::to_string(append.attempts.get()) +
				",\"append_observations\":" + std::to_string(append.observations.get()) +
				",\"append_duplicate_attempts\":" + std::to_string(append.duplicate_attempts.get()) +
				",\"append_unmatched_responses\":" + std::to_string(append.unmatched.get()) +
				",\"append_successful_responses\":" + std::to_string(append.successful.get()) +
				",\"append_rejected_responses\":" + std::to_string(append.rejected.get()) +
				",\"append_emit_to_response_ns\":" + duration_json(append.response.snapshot()) +
				",\"append_emit_to_higher_term_response_ns\":" +
				duration_json(append.higher_term_response.snapshot()) +
				",\"append_observation_timeout_ns\":" + duration_json(append.timeout.snapshot()) +
				",\"append_observation_superseded_ns\":" + duration_json(append.superseded.snapshot()) +
				",\"append_write_failure_ns\":" + duration_json(append.write_failure.snapshot()) +
				",\"append_disconnect_ns\":" + duration_json(append.disconnected.snapshot());
			storage_health +=
				",\"io_submitted_to_reaped_ns\":" + primitive_durations(driver_stats.submitted_to_reaped) +
				",\"io_failed_submitted_to_reaped_ns\":" +
				primitive_durations(driver_stats.failed_submitted_to_reaped) +
				",\"io_original_drive_ns\":" + duration_json(driver_stats.original_drive);

#ifdef __linux__
			storage_health +=
				",\"io_native_available\":" + std::string(queue->native_available() ? "true" : "false") +
				",\"io_setup_error\":" + std::to_string(queue->setup_error()) +
				",\"io_ring_errors\":" + std::to_string(io_stats.ring_errors);
#endif
#else
			const std::string storage_health = ",\"storage_async\":false";
#endif
			std::string encoded =
				std::string("{\"mode\":\"") +
				(options_.peers ? "durable_fixed_voters" : "durable_single_voter") +
				"\",\"connections_active\":" + std::to_string(connections.active) +
				",\"connections_high_water\":" + std::to_string(connections.high_water) +
				",\"connections_accepted\":" + std::to_string(connections.accepted) +
				",\"connections_rejected\":" + std::to_string(connections.rejected) +
				",\"duration_bucket_upper_bounds_ns\":" +
				[] {
					std::string bounds = "[";
					for (auto bound : kronuz::metrics::duration_bounds_ns) {
						if (bounds.size() > 1)
							bounds += ',';
						bounds += std::to_string(bound);
					}
					return bounds + "]";
				}() +
				",\"ready\":" + std::string(serving() ? "true" : "false") +
				",\"role\":" + std::to_string(static_cast<unsigned>(node.worker().role())) +
				",\"committed\":" + std::to_string(node.worker().committed()) +
				",\"term\":" + std::to_string(node.worker().term()) +
				",\"applied\":" + std::to_string(node.worker().applied_index()) +
				",\"revision\":" + std::to_string(node.state().revision()) +
				",\"knobs\":" + std::to_string(node.state().size()) + ",\"delivery_captures\":" +
				std::to_string(delivering && delivering->exporter && !delivering->source_drained ? 1 : 0) +
				",\"requests\":" + std::to_string(requests) +
				",\"reserved_body_bytes\":" + std::to_string(body_bytes) +
				",\"reserved_reply_bytes\":" + std::to_string(reply_bytes) +
				",\"retired_log_bytes\":" + std::to_string(node.worker().retired_log_bytes()) +
				",\"retired_log_entries\":" + std::to_string(node.worker().retired_log_entries()) +
				",\"retired_state_values\":" + std::to_string(node.state().retired_values()) +
				",\"retired_state_value_bytes\":" + std::to_string(node.state().retired_value_bytes()) +
				",\"read_quota_blocked\":" +
				std::string(reading && reading->quota_blocked ? "true" : "false") +
				",\"read_ready_tasks\":" + std::to_string(read_count) +
				",\"read_captures\":" + std::to_string(reading ? 1 : 0) + ",\"read_capture_logical_bytes\":" +
				std::to_string(reading ? reading->capture.logical_bytes() : 0) +
				",\"read_buffer_reserved_bytes\":" +
				std::to_string(reading && reading->reservation && reading->reservation->active
								   ? reading->reservation->bytes
								   : 0) +
				",\"retired_publisher_tasks\":" + std::to_string(retired_count) +
				",\"retired_publisher_bytes\":" + std::to_string(retired_task_bytes) +
				",\"subscription_sessions\":" + std::to_string(subscriptions->size()) +
				",\"subscription_quorum_reads\":" + std::to_string(subscription_reads.get()) +
				",\"subscription_exports\":" + std::to_string(subscription_exports.get()) +
				",\"distribution_export_expired\":" + std::to_string(export_expired.get()) +
				",\"distribution_export_disconnected\":" + std::to_string(export_disconnected.get()) +
				",\"distribution_export_limits\":" + std::to_string(export_limits.get()) +
				",\"distribution_export_errors\":" + std::to_string(export_errors.get()) +
				",\"distribution_export_records\":" + std::to_string(export_progress.records) +
				",\"distribution_export_strings\":" + std::to_string(export_progress.string_bytes) +
				",\"distribution_export_wire\":" + std::to_string(export_progress.wire_bytes) +
				",\"distribution_export_counted\":" + std::string(export_progress.counted ? "true" : "false") +
				",\"subscription_idle_responses\":" + std::to_string(subscription_idle.get()) +
				",\"frames_sessions\":" + std::to_string(frames_sessions->size()) +
				",\"frames_answers\":" + std::to_string(frames_answers.get()) +
				",\"frames_idle\":" + std::to_string(frames_idle.get()) +
				",\"frames_held_back\":" + std::to_string(frames_held_back.get()) +
				",\"frames_errors\":" + std::to_string(frames_errors.get()) +
				",\"owner_turn_ns\":" + duration_json(owner_turn.snapshot()) +
				",\"owner_turn_cpu_ns\":" + duration_json(owner_cpu.snapshot()) +
				",\"owner_cpu_clock_errors\":" + std::to_string(cpu_clock_errors.get()) +
				",\"publisher_admitted_to_ready_ns\":" +
				duration_json(accounting_->publisher_ready.snapshot()) +
				",\"reply_ready_to_retired_ns\":" + duration_json(accounting_->reply_retired.snapshot()) +
				",\"wake_lateness_ns\":" + duration_json(wake_lateness.snapshot()) +
				",\"io_job_settlement_ns\":" + duration_json(job_settlement.snapshot()) +
				",\"io_job_fence_observed_ns\":" + duration_json(fenced_job_observed.snapshot()) +
				storage_health + ",\"peer_incoming_bytes\":" + std::to_string(peer_occupancy.incoming_bytes) +
				",\"peer_outgoing_bytes\":" + std::to_string(peer_occupancy.outgoing_bytes) +
				",\"peer_incoming_high_water\":" + std::to_string(peer_occupancy.incoming_high_water) +
				",\"peer_outgoing_high_water\":" + std::to_string(peer_occupancy.outgoing_high_water) +
				",\"peer_connections\":" + std::to_string(peer_occupancy.connections) +
				",\"peer_handshakes\":" + std::to_string(peer_occupancy.handshakes) +
				",\"peer_queued_control\":" + std::to_string(peer_occupancy.queued_control) +
				",\"peer_retained_inputs\":" + std::to_string(peer_occupancy.retained_inputs) +
				",\"peer_decoded_to_admitted_ns\":" +
				duration_json(peers ? peers->stats().decoded_to_admitted.snapshot()
									: kronuz::metrics::DurationSnapshot{}) +
				",\"peer_admitted\":" + std::to_string(peers ? peers->stats().admitted : 0) +
				",\"peer_dropped\":" + std::to_string(peers ? peers->stats().dropped : 0) +
				",\"checkpoint_busy\":" + (node.checkpoint_busy() ? "true" : "false") +
				",\"fenced\":" + (node.worker().fenced() ? "true" : "false") + "}";
			{
				std::lock_guard lock(mutex_);
				health_ = std::move(encoded);
				service.health_stamp_ = captured_at;
			}
			last_health = captured_at;
		};
		// The ring is attached before replay, which refills it above the checkpoint (D6).
		node.attach_change_recorder(history);
		// Peers run before recovery: a history restore fetches from them before replay ends.
		// Consensus frames wait for admit(), which starts with the service loop.
		if (peer_options) {
			peers = std::make_shared<AuthorityPeers>(io.get_executor(), owned_node, peer_configuration(node_options),
													 std::move(*peer_options));
			peer_history->attach(peers);
			peers->start();
		}
		// A directory with a journal and no stamp is read by replay: one that does not read as this
		// format is format 1; one that does is stamped.
		Clock::time_point origin;
		try {
			if (std::filesystem::exists(options_.directory / "manifest")) {
				node.recover();
			} else {
				Identity id{};
				std::random_device random;
				for (char &byte : id) {
					byte = static_cast<char>(random());
				}
				if (id == Identity{}) {
					id[0] = 1;
				}
				node.create(id);
			}
			origin = Clock::now();
			while (!node.worker().ready()) {
				wake.begin_turn();
				node.service({0, election_delay()});
				dispatch(owned_node);
				node.take_network_actions();
				if (node.worker().fenced()) {
					throw std::runtime_error(fence_reason(node, "authority storage initialization fenced"));
				}
				if (in_flight())
					co_await sleep(timer, wake_by(Clock::now() + storage_wait));
				else
					co_await task::yield();
			}
			// Seed the first deadline before Start. Later Tick samples affect future
			// resets and cannot randomize a deadline that is already in force.
			node.service({0, election_delay()});
			node.start();
			// Storage readiness precedes incremental committed application replay.
			// Finish that replay before admitting live requests.
			while (node.state().applied_index() < node.worker().committed() || node.application_work_pending()) {
				wake.begin_turn();
				const auto applied = node.state().applied_index();
				node.service({0, election_delay()});
				dispatch(owned_node);
				if (node.worker().fenced())
					throw std::invalid_argument(fence_reason(node, "authority recovery application fenced"));
				if (in_flight() && !node.state().reclamation_pending() && node.state().applied_index() == applied) {
					co_await sleep(timer, wake_by(Clock::now() + storage_wait));
				} else {
					co_await task::yield();
				}
			}
			if (node.worker().fenced())
				throw std::invalid_argument(fence_reason(node, "authority committed application/profile mismatch"));
		} catch (const std::exception &error) {
			if (unstamped)
				refuse_format(options_.directory, "storage format 1 (no format stamp, and it does not read as format " +
													  std::to_string(storage_format) + ": " + error.what() + ")");
			throw;
		}
		if (unstamped)
			write_format_stamp(history_directory);
		publish_health();
		ready.set_value();
		started = true;
		auto scheduled_wakeup = Clock::now();
		auto last_pass = Clock::now();
		// The owner sleeps between wakes once nothing can run: every queue and collection is empty but for
		// session slots holding a parked next (waiting for a change, its poll deadline or its client), as
		// found by a sweep over every slot since the last change. `parked` is that sweep's record.
		std::vector<std::uint8_t> parked(session_tasks.size());
		std::size_t parked_count = 0, sweep = 0;
		auto parked_until = Clock::time_point::max();
		// The sleep that ended the pass before, from its start to its planned deadline; min when it ended in none.
		auto slept_from = Clock::time_point::min(), slept_until = Clock::time_point::min();
		auto applied_seen = node.state().applied_index();
		bool rescan = true;
		const std::size_t sweep_turns =
			std::max<std::size_t>((options_.subscription_requests.closes + 7) / 8,
								  (session_tasks.size() - options_.subscription_requests.closes + 7) / 8) + 1;
		for (;;) {
			auto owner_started = Clock::now();
			// W5 nexts this pass leaves with work (busy), and the deadlines of those parked for a change.
			bool frames_busy = false;
			auto frames_until = Clock::time_point::max();
			if (wake.begin_turn())
				rescan = true;
			if (rescan) {
				std::fill(parked.begin(), parked.end(), std::uint8_t{0});
				parked_count = 0;
				parked_until = Clock::time_point::max();
				sweep = sweep_turns;
				rescan = false;
			}
			if (slept_from == Clock::time_point::min())
				slept_from = slept_until = owner_started;
			// The pass before and any lateness past its planned sleep count as an absence; the sleep does not.
			if (detent::owner_absence(last_pass, slept_from, slept_until, owner_started) >=
				std::chrono::milliseconds(options_.node.timing.election_min))
				returned = owner_started;
			slept_from = slept_until = Clock::time_point::min();
			last_pass = owner_started;
			auto cpu_started = owner_cpu_ns();
			wake_lateness.observe(elapsed_ns(scheduled_wakeup, owner_started));
			{
				std::lock_guard lock(mutex_);
				if (stopping_) {
					break;
				}
			}
			auto now = Clock::now();
			auto tick = static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(now - origin).count());
			// Fresh independent entropy avoids repeated split votes after partitions.
			last_tick = tick;
			if (peers)
				peers->admit();
			CommandWorkBudget node_budget, publisher_budget;
			node_budget.bytes = publisher_budget.bytes = 4096;
			node_budget.records = publisher_budget.records = 8;
			node_budget.updates = publisher_budget.updates = 4;
			node_budget.receipts = publisher_budget.receipts = 16;
			node.service({tick, election_delay()}, node_budget);
			// Node always runs first. Reuse only its unspent allocation so idle
			// application work does not turn frontend progress into extra wakes.
			publisher_budget.bytes += node_budget.bytes;
			publisher_budget.records += node_budget.records;
			publisher_budget.updates += node_budget.updates;
			publisher_budget.receipts += node_budget.receipts;
			dispatch(owned_node);
			if (failure) {
				std::rethrow_exception(failure);
			}
			for (auto &action : node.take_network_actions()) {
				if (std::holds_alternative<kronuz::consensus::Send>(action)) {
					if (std::holds_alternative<kronuz::consensus::AppendResponse>(
							std::get<kronuz::consensus::Send>(action).message) &&
						trusting())
						leader_contact = Clock::now();
					if (!peers)
						throw std::logic_error("single-voter authority cannot send");
					peers->send(std::move(std::get<kronuz::consensus::Send>(action)));
				} else if (auto role = std::get_if<kronuz::consensus::RoleChanged>(&action)) {
					leader_known = role->leader != 0;
					if (trusting())
						leader_contact = Clock::now();
					publish_health();
					service.leader_known_.store(leader_known, std::memory_order_release);
				}
			}
			if (peers)
				peers->pump();
			if (completion_cursor == held_completions.size()) {
				held_completions = node.take_completions();
				completion_cursor = completion_task = 0;
			}
			for (unsigned callbacks = 0; completion_cursor < held_completions.size() && callbacks < 8;) {
				auto &completion = held_completions[completion_cursor];
				auto session = session_pending.find(completion.request);
				if (session != session_pending.end()) {
					auto task = session->second;
					task->quorum_pending = false;
					session_pending.erase(session);
					if (completion.rejection || node.worker().fenced())
						complete(task, {503, read_failure(completion, node.worker().fenced())});
					else {
						task->quorum_ready = true;
						task->generation = completion.index;
						if (task->kind == Task::Kind::SessionNext && !task->settled) {
							task->export_ready = true;
							++subscription_ready;
						}
					}
					++completion_cursor;
					completion_task = 0;
					++callbacks;
					continue;
				}
				auto found = pending.find(completion.request);
				if (found == pending.end()) {
					++completion_cursor;
					completion_task = 0;
					++callbacks;
					continue;
				}
				if (completion_task == found->second.size()) {
					pending.erase(found);
					++completion_cursor;
					completion_task = 0;
					continue;
				}
				const auto i = completion_task++;
				auto &task = found->second[i];
				++callbacks;
				if (completion.rejection || node.worker().fenced())
					complete(task, {503, read_failure(completion, node.worker().fenced())});
				else if (task->kind == Task::Kind::Command) {
					auto r = completion.results.at(i);
					complete(task, {result_status(r), result_json(r)});
				} else if (task->kind == Task::Kind::Result) {
					auto r = node.state().lookup(task->admission->credential->producer, task->generation,
												 task->sequence);
					complete(task, {result_status(r), result_json(r)});
				} else {
					check(read_count < ready_reads.size(), "read ready capacity exhausted");
					ready_reads[read_tail] = {task, completion.index};
					read_tail = (read_tail + 1) % ready_reads.size();
					++read_count;
					if (task->kind == Task::Kind::Distribution)
						++legacy_ready;
				}
			}

			// Keep a stable request/task cursor; completion delivery may erase map
			// nodes between turns. Expiration never cancels accepted Core or IO work.
			for (unsigned checked = 0; checked < 8 && !pending.empty(); ++checked) {
				auto it = pending.lower_bound(expiry_request);
				if (it == pending.end()) {
					expiry_request = 0;
					expiry_task = 0;
					break;
				}
				if (it->first != expiry_request) {
					expiry_request = it->first;
					expiry_task = 0;
				}
				auto &tasks = it->second;
				if (expiry_task < tasks.size()) {
					auto &task = tasks[expiry_task++];
					if (!task->settled && (node.worker().fenced() || now >= task->deadline))
						complete(task, {503, "{\"error\":\"outcome_unknown\"}"});
				}
				if (expiry_task == tasks.size()) {
					const auto id = it->first;
					if (std::all_of(tasks.begin(), tasks.end(), [](auto &task) { return task->settled; })) {
						node.forget(id);
						pending.erase(it);
					}
					expiry_request = id + 1;
					expiry_task = 0;
				}
			}

			if (node.worker().fenced()) {
				throw std::runtime_error("authority worker fenced");
			}
			std::deque<std::shared_ptr<Task>> expired;
			{
				std::lock_guard lock(mutex_);
				while (!queue_.empty() && now >= queue_.front()->deadline && expired.size() < 8) {
					expired.push_back(queue_.front());
					queue_.pop_front();
				}
			}
			for (auto &task : expired) {
				complete(task, {503, "{\"error\":\"outcome_unknown\"}"});
			}
			auto session_error = [&](const std::shared_ptr<Task> &task, SubscriptionResult result) {
				int status = 409;
				const char *error = "session_out_of_sync";
				switch (result) {
				case SubscriptionResult::Expired:
					status = 410;
					error = "session_expired";
					break;
				case SubscriptionResult::Forbidden:
					status = 403;
					error = "scope_forbidden";
					break;
				case SubscriptionResult::IdentityChanged:
					error = "identity_changed";
					break;
				case SubscriptionResult::SnapshotRequired:
					error = "snapshot_required";
					break;
				case SubscriptionResult::Busy:
					error = "session_busy";
					break;
				case SubscriptionResult::Full:
					status = 503;
					error = "admission_full";
					break;
				default:
					break;
				}
				complete(task, {status, "{\"error\":\"" + std::string(error) + "\"}"});
			};
			auto authorize = [&](TaskQueue &queue, distribution::WorkBudget work) {
				while (queue.count && work.tokens && work.bytes) {
					auto held = queue.pop();
					auto &task = *held;
					task.authorization_queued = false;
					--work.tokens;
					if (task.settled || now >= task.deadline || task.delivery->settled.load()) {
						complete(held, {503, "{\"error\":\"authorization_timeout\"}"});
						continue;
					}
					const auto *metadata = subscriptions->metadata(task.session, now);
					if (!metadata || metadata->incarnation != task.authorization_incarnation) {
						session_error(held, SubscriptionResult::Expired);
						continue;
					}
					if (metadata->principal != task.principal || metadata->scope != task.delivery->scope) {
						session_error(held, SubscriptionResult::IdentityChanged);
						continue;
					}
					const auto &grants = (*accounting_->credential_grants)[task.admission->credential_index];
					for (unsigned compared = 0; compared < 8 && work.tokens && !task.settled; ++compared) {
						if (task.authorization_cursor == task.delivery->scope->size())
							break;
						if (task.grant_cursor == grants.names.size()) {
							session_error(held, SubscriptionResult::Forbidden);
							break;
						}
						const auto name = task.delivery->scope->name(task.authorization_cursor);
						const auto grant = grants.names[task.grant_cursor];
						const auto bytes = name.size() + grant.size() + 1;
						if (bytes > work.bytes)
							break;
						work.bytes -= bytes;
						--work.tokens;
						const auto order = name.compare(grant);
						if (order < 0) {
							session_error(held, SubscriptionResult::Forbidden);
							break;
						}
						if (!order)
							++task.authorization_cursor;
						++task.grant_cursor;
					}
					if (task.settled)
						continue;
					if (task.authorization_cursor == task.delivery->scope->size()) {
						const auto result = subscriptions->authorize(
							task.session, task.authorization_incarnation, task.principal,
							task.delivery->scope->identity(), grants.digest, now);
						if (result == SubscriptionResult::Accepted)
							task.authorization_ready = true;
						else
							session_error(held, result);
					} else {
						task.authorization_queued = true;
						queue.push(std::move(held));
						if (work.bytes < 397)
							break;
					}
				}
			};
			authorize(*close_authorization, {65536, 512});
			authorize(*session_authorization, {8192, 64});
			CommandWorkBudget session_cleanup{publisher_budget.bytes / 4, 0, 0,
											  publisher_budget.receipts / 4};
			const auto cleanup_bytes = session_cleanup.bytes, cleanup_receipts = session_cleanup.receipts;
			for (unsigned checked = 0; checked < 16; ++checked) {
				const bool close_lane = checked < 8;
				const auto close_slots = options_.subscription_requests.closes;
				const auto ordinal = close_lane ? close_cursor : close_slots + session_cursor;
				if (close_lane)
					close_cursor = (close_cursor + 1) % close_slots;
				else
					session_cursor = (session_cursor + 1) % (session_tasks.size() - close_slots);
				auto &held = session_tasks[ordinal];
				if (parked[ordinal]) {
					parked[ordinal] = 0;
					--parked_count;
				}
				if (!held) {
					std::lock_guard lock(mutex_);
					auto &queue =
						close_lane ? service.subscription_close_queue_ : service.subscription_queue_;
					if (!queue.empty()) {
						held = std::move(queue.front());
						held->retirement_slot = ordinal;
						queue.pop_front();
					}
				}
				if (!held)
					continue;
				auto &task = *held;
				auto &delivery = *task.delivery;
				if (!task.settled && !task.exporting && (now >= task.deadline || delivery.settled.load()))
					complete(held, {503, "{\"error\":\"outcome_unknown\"}"});
				if (task.settled) {
					delivery.pump_control();
					if (task.quorum_pending || task.exporting || task.authorization_queued ||
						!delivery.settled.load())
						continue;
					if (!task.retiring) {
						begin_delivery_retirement(task);
						if (task.subscription_request) {
							subscriptions->release(*task.subscription_request);
							auto active = active_session_requests.find(task.session);
							if (active != active_session_requests.end() &&
								active->second.lock().get() == &task)
								active_session_requests.erase(active);
						}
						if (task.kind == Task::Kind::SessionOpen && task.session_initialized &&
							!delivery.written.load())
							subscriptions->close(task.session, task.principal);
						session_retirement->push(held);
					}
					continue;
				}
				if (task.quorum_pending || task.exporting)
					continue;
				if (task.authorization_queued)
					continue;
				try {
					if (task.kind != Task::Kind::SessionOpen && !task.session_initialized) {
						const auto *metadata = subscriptions->metadata(task.session, now);
						if (!metadata) {
							if (task.kind == Task::Kind::SessionClose) {
								const auto result = subscriptions->close(task.session, task.principal);
								if (result == SubscriptionResult::Accepted)
									complete(held, {204, {}});
								else
									session_error(held, result);
							} else
								session_error(held, SubscriptionResult::Expired);
							continue;
						}
						if (metadata->principal != task.principal) {
							session_error(held, metadata->principal.identity == task.principal.identity
													? SubscriptionResult::IdentityChanged
													: SubscriptionResult::Forbidden);
							continue;
						}
						delivery.scope = metadata->scope;
						delivery.compress = metadata->compression;
						if (!task.authorization_incarnation)
							task.authorization_incarnation = metadata->incarnation;
						if (task.authorization_incarnation != metadata->incarnation) {
							session_error(held, SubscriptionResult::Expired);
							continue;
						}
						const auto grants =
							(*accounting_->credential_grants)[task.admission->credential_index].digest;
						if (metadata->creator_grants == grants || metadata->last_authorized_grants == grants)
							task.authorization_ready = true;
						if (!task.authorization_ready) {
							task.authorization_queued = true;
							(task.kind == Task::Kind::SessionClose ? *close_authorization
																   : *session_authorization)
								.push(held);
							continue;
						}
						if (task.kind == Task::Kind::SessionClose) {
							const auto result = subscriptions->close(task.session, task.principal,
																	 task.authorization_incarnation);
							if (result == SubscriptionResult::Accepted)
								complete(held, {204, {}});
							else
								session_error(held, result);
							continue;
						}
						auto active = active_session_requests.find(task.session);
						if (active != active_session_requests.end()) {
							if (auto previous = active->second.lock();
								previous && previous->settled && previous->delivery->settled.load() &&
								!previous->quorum_pending && !previous->exporting &&
								previous->subscription_request)
								subscriptions->release(*previous->subscription_request);
						}
						const bool inserted = active == active_session_requests.end();
						if (inserted)
							active =
								active_session_requests.emplace(task.session, std::weak_ptr<Task>{}).first;
						auto admitted =
							subscriptions->next(task.session, task.principal, task.sequence, task.published,
												now, task.authorization_incarnation);
						if (!admitted.token) {
							if (inserted)
								active_session_requests.erase(active);
							session_error(held, admitted.result);
							continue;
						}
						active->second = held;
						task.subscription_request = admitted.token;
						task.session_initialized = true;
					}
					const auto &context = options_.node.context;
					distribution::Cursor current{context.cluster, context.group, context.epoch,
												 delivery.scope->identity(), node.state().revision()};
					if (task.kind == Task::Kind::SessionOpen && task.quorum_ready) {
						check(node.state().applied_index() >= task.generation,
							  "subscription boundary not published");
						const auto result = subscriptions->open(
							task.session, task.principal, delivery.scope, current, delivery.base,
							task.force_snapshot, delivery.compress, *history, now,
							(*accounting_->credential_grants)[task.admission->credential_index].digest);
						if (result != SubscriptionResult::Accepted) {
							session_error(held, result);
							continue;
						}
						task.session_initialized = true;
						auto probe = subscriptions->next(task.session, task.principal, 0, {}, now);
						check(probe.token.has_value(), "subscription negotiation state");
						const auto mode = subscriptions->poll(*probe.token, current, *history, now);
						subscriptions->release(*probe.token);
						complete(
							held,
							{200, "{\"version\":1,\"session\":\"" + hex(task.session) +
									  "\",\"window\":0,\"mode\":\"" +
									  (mode == SubscriptionResult::SnapshotReady ? "snapshot" : "resume") +
									  "\",\"wait_ms\":" + std::to_string(options_.subscription_wait.count()) +
									  "}"});
						continue;
					}
					if (task.kind == Task::Kind::SessionNext) {
						const auto result =
							subscriptions->poll(*task.subscription_request, current, *history, now);
						if (result == SubscriptionResult::AwaitSettlement)
							continue;
						if (result == SubscriptionResult::Parked) {
							if (task.export_ready) {
								task.export_ready = false;
								--subscription_ready;
							}
							if (now >= task.poll_deadline) {
								subscription_idle.add();
								complete(held, {204, {}});
							} else {
								parked[ordinal] = 1;
								++parked_count;
								parked_until = std::min({parked_until, task.poll_deadline, task.deadline});
							}
							continue;
						}
						if (result != SubscriptionResult::SnapshotReady &&
							result != SubscriptionResult::ResumeReady) {
							session_error(held, result);
							continue;
						}
						if (task.quorum_ready && !delivering && (subscription_export_turn || !legacy_ready)) {
							check(node.state().applied_index() >= task.generation,
								  "subscription boundary not published");
							delivery.selection = result == SubscriptionResult::SnapshotReady
													 ? ExportSelection::SnapshotOnly
													 : ExportSelection::ResumeOnly;
							delivery.base = subscriptions->base(*task.subscription_request);
							delivery.handoff = subscriptions->handoff(*task.subscription_request);
							delivering.emplace();
							delivering->task = held;
							task.exporting = true;
							subscription_export_turn = false;
							task.export_ready = false;
							--subscription_ready;
						}
					}
					if (!task.quorum_ready && !task.exporting && session_pending.size() < 4 &&
						pending.size() + session_pending.size() <
							std::max<std::size_t>(1, options_.node.waiters / 2)) {
						check(next < (std::uint64_t{1} << 63) - 1, "authority local request exhaustion");
						const auto id = ++next;
						session_pending.emplace(id, held);
						task.quorum_pending = true;
						if (node.read(id) == kronuz::consensus::SubmitResult::Accepted) {
							task.quorum_pending = true;
							subscription_reads.add();
						} else {
							task.quorum_pending = false;
							session_pending.erase(id);
						}
					}
				} catch (const std::exception &) {
					if (task.quorum_pending)
						throw;
					complete(held, {503, "{\"error\":\"distribution_unavailable\"}"});
				}
			}
			{
				while (session_retirement->count && session_cleanup.receipts) {
					auto &task = session_retirement->ring[session_retirement->head];
					const auto bytes = std::min(task->retirement_remaining, session_cleanup.bytes);
					task->retirement_remaining -= bytes;
					session_cleanup.bytes -= bytes;
					if (task->retirement_remaining)
						break;
					--session_cleanup.receipts;
					auto retired = session_retirement->pop();
					session_tasks[retired->retirement_slot].reset();
					retired.reset();
				}
				publisher_budget.bytes -= cleanup_bytes - session_cleanup.bytes;
				publisher_budget.receipts -= cleanup_receipts - session_cleanup.receipts;
				CommandWorkBudget cleanup{publisher_budget.bytes / 4, 0, 0, publisher_budget.receipts / 4};
				const auto reserved_bytes = cleanup.bytes, reserved_receipts = cleanup.receipts;
				const auto visits = std::min<std::size_t>(control_count, 8);
				for (std::size_t i = 0; i < visits; ++i) {
					auto task = std::move(legacy_deliveries[control_head]);
					control_head = (control_head + 1) % legacy_deliveries.size();
					--control_count;
					auto &delivery = *task->delivery;
					delivery.pump_control();
					if (delivery.settled.load() && !task->exporting) {
						begin_delivery_retirement(*task);
						const auto bytes = std::min(task->retirement_remaining, cleanup.bytes);
						task->retirement_remaining -= bytes;
						cleanup.bytes -= bytes;
						if (!task->retirement_remaining && cleanup.receipts) {
							--cleanup.receipts;
							task.reset();
						}
					}
					if (task) {
						legacy_deliveries[control_tail] = std::move(task);
						control_tail = (control_tail + 1) % legacy_deliveries.size();
						++control_count;
					}
				}
				publisher_budget.bytes -= reserved_bytes - cleanup.bytes;
				publisher_budget.receipts -= reserved_receipts - cleanup.receipts;
				distribution::WorkBudget collection{publisher_budget.bytes / 4, 4};
				const auto bytes = collection.bytes;
				subscriptions->collect(collection, now);
				publisher_budget.bytes -= bytes - collection.bytes;
			}
			CommandWorkBudget read_budget{0, 0, 0, 0};
			if (reading || read_count || delivering) {
				bool publisher_runnable = preparing.has_value() || carry_task || retired_count;
				{
					std::lock_guard lock(mutex_);
					publisher_runnable = publisher_runnable || !queue_.empty();
				}
				read_first = !read_first;
				if (!publisher_runnable || read_first) {
					read_budget = publisher_budget;
					publisher_budget = {0, 0, 0, 0};
				}
			}
			if (!reading && read_count && read_budget.records) {
				auto &ready_read = ready_reads[read_head];
				bool consume = true;
				if (now >= ready_read.task->deadline) {
					complete(ready_read.task, {503, "{\"error\":\"outcome_unknown\"}"});
				} else if (ready_read.task->kind == Task::Kind::Distribution) {
					if (delivering || (subscription_export_turn && subscription_ready)) {
						consume = false;
						// Rotate bounded metadata, never capture another authority root.
						if (read_tail != read_head) {
							auto waiting = std::move(ready_read);
							ready_read = {};
							ready_reads[read_tail] = std::move(waiting);
						}
						read_tail = (read_tail + 1) % ready_reads.size();
						read_head = (read_head + 1) % ready_reads.size();
					} else {
						check(node.state().applied_index() >= ready_read.boundary,
							  "distribution boundary not published");
						delivering.emplace();
						delivering->task = ready_read.task;
						delivering->task->exporting = true;
						subscription_export_turn = true;
					}
				} else {
					check(node.state().applied_index() >= ready_read.boundary, "read boundary not published");
					reading.emplace(ready_read.task, node.state());
				}
				if (consume) {
					if (ready_read.task->kind == Task::Kind::Distribution)
						--legacy_ready;
					ready_read = {};
					read_head = (read_head + 1) % ready_reads.size();
					--read_count;
				}
			}
			// W5/W6 sessions (version 2): every next answers from history (D6), or waits for a change.
			{
				const auto capacity = options_.subscription_requests.opens + options_.subscription_requests.polls +
									  options_.subscription_requests.closes;
				{
					std::lock_guard lock(mutex_);
					while (!service.frames_queue_.empty() && frames_tasks.size() < capacity) {
						frames_tasks.push_back(std::move(service.frames_queue_.front()));
						service.frames_queue_.pop_front();
					}
				}
				auto session_reply = [](SessionResult result) -> Reply {
					switch (result) {
					case SessionResult::Expired:
						return {410, "{\"error\":\"session_expired\"}"};
					case SessionResult::Forbidden:
						return {403, "{\"error\":\"scope_forbidden\"}"};
					case SessionResult::IdentityChanged:
						return {409, "{\"error\":\"identity_changed\"}"};
					case SessionResult::Busy:
						return {409, "{\"error\":\"session_busy\"}"};
					default:
						return {503, "{\"error\":\"admission_full\"}"};
					}
				};
				// The answer for a next: every followed namespace and, with `all`, the directory's
				// changes and the namespaces they bring; none while nothing changed for it.
				auto answer = [&](Task &task, std::optional<FramesExport> &out) {
					const auto &state = node.state();
					if (!state.positions_enabled())
						return;
					auto *files = node.history_files();
					if (!files || !node.history()->ready() || !node.sealed()) {
						task.looked = UINT64_MAX; // Looks again once history is ready.
						return;
					}
					const auto &cluster = options_.node.context.cluster;
					const auto &table = node.history()->layout().table();
					auto plan = plan_frames(task.follows, task.all, task.admission->credential->grants, state, cluster,
											*node.sealed(), table, *files, *history);
					auto &requests = plan.requests;
					auto &directory = plan.directory;
					for (const auto &request : requests)
						if (request.history && request.history->held_back())
							frames_held_back.add();
					distribution::Identity update{};
					check(RAND_bytes(reinterpret_cast<unsigned char *>(update.data()), update.size()) == 1,
						  "distribution update entropy");
					out.emplace(std::move(requests), std::move(directory), update, task.compressed,
								accounting_->delivery_account, options_.delivery.bytes);
					if (out->empty())
						out.reset();
				};
				// A seal that moves no head has no frame: the nexts parked across it answer 204 at
				// once, and a relay syncs its layouts.
				const auto seal_at = node.sealed() ? node.sealed()->applied_index() : 0;
				const bool resealed = seal_at != std::exchange(seal_seen, seal_at);
				for (std::size_t i = 0; i < frames_tasks.size();) {
					auto held = frames_tasks[i];
					auto &task = *held;
					auto &delivery = *task.delivery;
					if (!task.settled && !task.exporting && (now >= task.deadline || delivery.settled.load()))
						complete(held, {503, "{\"error\":\"outcome_unknown\"}"});
					if (task.settled) {
						delivery.pump_control();
						if (!task.exporting && delivery.settled.load()) {
							if (task.bound)
								frames_sessions->end(task.session, now);
							frames_tasks[i] = std::move(frames_tasks.back());
							frames_tasks.pop_back();
							continue;
						}
						++i;
						continue;
					}
					++i;
					if (task.exporting)
						continue;
					try {
						if (task.refused) {
							complete(held, *task.refused);
							continue;
						}
						if (task.kind == Task::Kind::SessionOpen) {
							const auto result =
								frames_sessions->open(task.session, task.frames_principal, task.compressed, now);
							if (result != SessionResult::Accepted)
								complete(held, session_reply(result));
							else
								complete(held, {200, "{\"version\":2,\"session\":\"" + hex(task.session) +
														 "\",\"wait_ms\":" +
														 std::to_string(options_.subscription_wait.count()) + "}"});
							continue;
						}
						if (task.kind == Task::Kind::SessionClose) {
							const auto result = frames_sessions->close(task.session, task.frames_principal);
							complete(held, result == SessionResult::Accepted ? Reply{204, {}} : session_reply(result));
							continue;
						}
						// A voter that knows no live leader may be cut off from the quorum: its nexts, parked
						// or new, refuse as its history routes do, and the reader moves to another source.
						if (!serving()) {
							complete(held, {503, "{\"error\":\"not_ready\"}"});
							continue;
						}
						if (!task.bound) {
							const auto result =
								frames_sessions->begin(task.session, task.frames_principal, now, task.compressed);
							if (result != SessionResult::Accepted) {
								complete(held, session_reply(result));
								continue;
							}
							task.bound = true;
						}
						const bool was_parked = task.looked != UINT64_MAX;
						// Every applied entry may change an answer: a write, a seal, the enabling entry.
						if (!framing && task.looked != node.state().applied_index()) {
							task.looked = node.state().applied_index();
							framing.emplace();
							try {
								answer(task, framing->exporter);
							} catch (...) {
								framing.reset();
								throw;
							}
							if (framing->exporter) {
								framing->task = held;
								task.exporting = true;
								continue;
							}
							framing.reset();
						}
						if (now >= task.poll_deadline || (resealed && was_parked)) {
							frames_idle.add();
							complete(held, {204, {}});
						}
					} catch (const std::exception &error) {
						frames_errors.add();
						const auto failure = frames_failure(error);
						complete(held, {static_cast<int>(failure.status), std::string(failure.body)});
					}
				}
				// A next looked at this applied index waits for the next one (a publish wakes the owner) or
				// its deadlines; any other is visited again at once. An export is `framing`'s.
				for (const auto &held : frames_tasks) {
					const auto &task = *held;
					if (task.exporting)
						continue;
					if (task.settled || task.looked != node.state().applied_index())
						frames_busy = true;
					else
						frames_until = std::min({frames_until, task.poll_deadline, task.deadline});
				}
				// The cutover's enabling entry, proposed by the leader as it leads: every consumer is a W5
				// session, which a follower answers only once positions are enabled. A durable leader waits
				// for its history, which cuts the entry's seal 0.
				if (node.worker().role() == kronuz::consensus::Role::Leader && (!node.history() ||
					node.history()->ready()) && !node.state().positions_enabled() && now >= enabling_at) {
					enabling_at = now + std::chrono::seconds(1);
					AdministrativeCommand enable{options_.node.context, AdministrativeKind::EnablePositions, {}, {},
												 {}, {}};
					check(RAND_bytes(reinterpret_cast<unsigned char *>(enable.nonce.data()), enable.nonce.size()) ==
							  1,
						  "positions seed entropy");
					enable.nonce[0] |= 1;
					check(next < (std::uint64_t{1} << 63) - 1, "authority local request exhaustion");
					(void)node.propose_administrative(++next, enable);
				}
				// D136: positioned changes unsealed for seal_after get a seal entry, so relays, which
				// mirror sealed history only, stay current on a quiet cluster.
				if (const auto &seal = node.state().seal(); !seal.transactions || seal.index != unsealed_after) {
					unsealed_after = seal.index;
					unsealed_since.reset();
					if (seal.transactions)
						unsealed_since = now;
				} else if (!unsealed_since) {
					unsealed_since = now;
				} else if (node.worker().role() == kronuz::consensus::Role::Leader &&
						   (!node.history() || node.history()->ready()) && now - *unsealed_since >= options_.seal_after &&
						   now >= sealing_at) {
					sealing_at = now + options_.seal_after;
					check(next < (std::uint64_t{1} << 63) - 1, "authority local request exhaustion");
					(void)node.propose_administrative(
						++next, AdministrativeCommand{options_.node.context, AdministrativeKind::Seal, {}, {}, {}, {}});
				}
				if (framing) {
					auto &slot = *framing;
					auto &delivery = *slot.task->delivery;
					distribution::WorkBudget wire{256 * 1024, 16};
					auto terminate = [&] {
						if (delivery.started)
							delivery.reply->abandon();
						complete(slot.task, {503, "{\"error\":\"distribution_unavailable\"}"});
						slot.retiring = true;
					};
					if (!slot.retiring) {
						try {
							if (delivery.settled.load() || now >= slot.task->deadline) {
								(delivery.settled.load() ? export_disconnected : export_expired).add();
								terminate();
							} else {
								if (!slot.pending && delivery.writable.load())
									slot.exporter->step(wire);
								if (slot.exporter->counted() && !delivery.started) {
									http::Response metadata;
									metadata.status = 200;
									metadata.content_type = "application/vnd.detent.frames";
									metadata.headers.emplace_back("X-Detent-Session-Version", "2");
									metadata.headers.emplace_back("X-Detent-Session", hex(slot.task->session));
									metadata.headers.emplace_back("X-Detent-Coding",
																  slot.task->compressed ? "zstd" : "raw");
									if (delivery.reply->start_chunked(metadata, true) != http::StreamSubmit::Accepted)
										throw std::runtime_error("distribution stream closed");
									delivery.started = true;
								}
								if (!slot.pending && slot.exporter->frame_ready())
									slot.pending = slot.exporter->take_frame();
								if (slot.pending) {
									const auto result = delivery.publish(slot.pending);
									if (result != http::StreamSubmit::Accepted && result != http::StreamSubmit::Busy)
										throw std::runtime_error("distribution stream refused frame");
								}
								if (slot.exporter->done() && !slot.pending) {
									if (delivery.reply->finish() != http::StreamSubmit::Accepted)
										throw std::runtime_error("distribution stream refused finish");
									frames_answers.add();
									complete(slot.task, {200, {}});
									slot.retiring = true;
								}
							}
						} catch (const std::length_error &) {
							export_limits.add();
							terminate();
						} catch (const std::exception &) {
							frames_errors.add();
							terminate();
						}
					}
					if (slot.retiring && delivery.settled.load()) {
						slot.task->exporting = false;
						framing.reset();
					}
				}
				frames_sessions->collect(now, 4);
			}
			if (delivering) {
				auto &slot = *delivering;
				auto &delivery = *slot.task->delivery;
				distribution::WorkBudget wire{read_budget.bytes / 2, read_budget.receipts / 2};
				read_budget.bytes -= wire.bytes;
				read_budget.receipts -= wire.tokens;
				auto terminate = [&] {
					if (delivery.started)
						delivery.reply->abandon();
					complete(slot.task, {503, "{\"error\":\"distribution_unavailable\"}"});
					slot.retiring = true;
				};
				if (!slot.retiring) {
					try {
						if (delivery.settled.load() || now >= slot.task->deadline) {
							if (delivery.settled.load())
								export_disconnected.add();
							else
								export_expired.add();
							terminate();
						} else if (wire.bytes && wire.tokens) {
							if (!slot.exporter) {
								distribution::Identity update{};
								check(RAND_bytes(reinterpret_cast<unsigned char *>(update.data()),
												 update.size()) == 1,
									  "distribution update entropy");
								slot.exporter.emplace(node.state(), delivery.scope, history, delivery.base,
													  update, delivery.compress, options_.delivery,
													  delivery.selection, delivery.handoff);
								if (slot.task->subscription_request)
									subscription_exports.add();
							}
							if (!slot.pending && delivery.writable.load())
								slot.exporter->step(wire);
							if (slot.exporter->counted() && !delivery.started) {
								std::optional<distribution_http::SessionHeaders> session;
								if (slot.task->subscription_request) {
									slot.task->subscription_offer = subscriptions->offer(
										*slot.task->subscription_request, slot.exporter->target(),
										slot.exporter->history_handoff(), now);
									session = {slot.task->session, slot.task->subscription_offer->window};
								}
								auto metadata = distribution_http::update_headers(slot.exporter->kind(),
																				  slot.exporter->target(),
																				  delivery.compress, session);
								if (delivery.reply->start_chunked(metadata, true) !=
									http::StreamSubmit::Accepted)
									throw std::runtime_error("distribution stream closed");
								delivery.started = true;
							}
							if (!slot.pending && slot.exporter->frame_ready())
								slot.pending = slot.exporter->take_frame();
							if (slot.pending) {
								const auto result = delivery.publish(slot.pending);
								if (result != http::StreamSubmit::Accepted &&
									result != http::StreamSubmit::Busy)
									throw std::runtime_error("distribution stream refused frame");
							}
							if (slot.exporter->done() && !slot.pending) {
								if (delivery.reply->finish() != http::StreamSubmit::Accepted)
									throw std::runtime_error("distribution stream refused finish");
								complete(slot.task, {200, {}});
								slot.retiring = true;
							}
						}
					} catch (const SnapshotRequired &reset) {
						if (slot.task->subscription_request && !delivery.started) {
							subscriptions->close(slot.task->session, slot.task->principal);
							complete(slot.task,
									 {409, std::string("{\"error\":\"snapshot_required\",\"reason\":\"") +
											   (reset.reason() == SnapshotRequired::Reason::HistoryLost
													? "history_lost"
													: "representation_limit") +
											   "\"}"});
							slot.retiring = true;
						} else
							terminate();
					} catch (const std::length_error &) {
						export_limits.add();
						terminate();
					} catch (const std::exception &) {
						export_errors.add();
						terminate();
					}
				}
				if (slot.retiring) {
					slot.discard_pending(wire);
					const bool drained = !slot.exporter || slot.exporter->discard(wire, read_budget);
					slot.source_drained = drained && !slot.pending;
					if (drained && !slot.pending && delivery.settled.load()) {
						if (slot.task->subscription_offer)
							subscriptions->settle(*slot.task->subscription_offer, delivery.written.load());
						slot.task->exporting = false;
						delivering.reset();
					}
				}
				read_budget.bytes += wire.bytes;
				read_budget.receipts += wire.tokens;
			}
			if (reading && read_budget.records) {
				auto &r = *reading;
				auto retire_read = [&] {
					r.encoder.reset();
					r.reservation.reset();
					r.capture.retire();
					r.retiring = true;
				};
				if (!r.retiring) {
					try {
						if (Clock::now() >= r.task->deadline) {
							complete(r.task, {503, "{\"error\":\"outcome_unknown\"}"});
							retire_read();
						} else {
							if (!r.encoder)
								r.encoder.emplace(r.capture, r.task->name_space, r.task->keys,
												  options_.node.context, options_.reply_bytes - 256);
							r.quota_blocked = false;
							if (!r.encoder->counted())
								r.encoder->step(read_budget);
							if (r.encoder->counted() && (!r.reservation || !r.reservation->active) &&
								read_budget.records && read_budget.bytes >= 256) {
								--read_budget.records;
								read_budget.bytes -= 256;
								if (!r.reservation) {
									r.reservation = std::make_shared<ReplyAdmission>();
									r.reservation->accounting = accounting_;
									r.reservation->bytes = r.encoder->encoded_size() + 256;
								}
								auto &lease = r.reservation;
								{
									std::lock_guard lock(accounting_->mutex);
									if (lease->bytes <= options_.reply_bytes - accounting_->reply_bytes) {
										accounting_->reply_bytes += lease->bytes;
										lease->active = true;
									}
								}
								if (lease->active)
									r.encoder->begin_encoding();
								else
									r.quota_blocked = true;
							}
							if (r.reservation && r.reservation->active)
								r.encoder->step(read_budget);
							if (r.encoder->done()) {
								if (Clock::now() >= r.task->deadline)
									complete(r.task, {503, "{\"error\":\"outcome_unknown\"}"});
								else
									complete(r.task,
											 {200, r.encoder->take(), true, std::move(r.reservation)});
								retire_read();
							}
						}
					} catch (const std::length_error &) {
						complete(r.task, {413, "{\"error\":\"reply_bound\"}"});
						retire_read();
					} catch (const std::bad_alloc &) {
						complete(r.task, {503, "{\"error\":\"outcome_unknown\"}"});
						retire_read();
					}
				}
				if (r.retiring) {
					if (!r.capture.shares_reclamation_with(node.state()))
						r.capture.collect(read_budget);
					if (r.capture.shares_reclamation_with(node.state()) || !r.capture.reclamation_pending())
						reading.reset();
				}
			}
			// Publisher preparation owns task charges across encoding and admission.
			// One encoded command that does not fit is carried to the next batch.
			collect_tasks(publisher_budget);
			if (carry_task && now >= carry_task->deadline) {
				complete(carry_task, {503, "{\"error\":\"outcome_unknown\"}"});
				carry_task.reset();
				carry.reset();
			}
			if (!preparing && publisher_budget.records) {
				std::shared_ptr<Task> first;
				if (carry_task)
					first = std::move(carry_task);
				else {
					std::lock_guard lock(mutex_);
					if (!queue_.empty() &&
						(queue_.front()->kind != Task::Kind::Command || queue_.size() >= 64 ||
						 now >= queue_.front()->deadline - options_.timeout + std::chrono::milliseconds(5))) {
						first = queue_.front();
						queue_.pop_front();
					}
				}
				if (first && (first->kind == Task::Kind::HistoryLayouts || first->kind == Task::Kind::HistoryFile)) {
					// W8: this voter's own history, by name; immutable files need no quorum read. A
					// voter that knows no live leader may be cut off from the quorum: it refuses, and the
					// relay reads another.
					complete(first, serving() ? Service::history_reply(*first, node, layout_export, held_file,
																		options_.reply_bytes - 256)
											: Reply{503, "{\"error\":\"not_ready\"}"});
					first.reset();
				}
				if (first) {
					check(next < (std::uint64_t{1} << 63) - 1, "authority local request exhaustion");
					if (first->kind != Task::Kind::Command) {
						auto id = ++next;
						// Reserve bookkeeping before accepting the read obligation.
						pending.emplace(id, Tasks{first});
						auto result = node.read(id);
						if (result != kronuz::consensus::SubmitResult::Accepted) {
							pending.erase(id);
							std::lock_guard lock(mutex_);
							queue_.push_front(std::move(first));
						}
					} else {
						preparing.emplace();
						auto &p = *preparing;
						p.request = ++next;
						p.builder.emplace(options_.node.context, options_.node.application,
										  options_.node.consensus.command_bytes);
						p.current = std::move(first);
						if (carry) {
							p.encoded.emplace(std::move(*carry));
							carry.reset();
						}
					}
				}
			}
			if (preparing) {
				auto &p = *preparing;
				if (!p.proposal && ((p.current && now >= p.current->deadline) ||
									std::any_of(p.tasks.begin(), p.tasks.end(),
												[&](const auto &task) { return now >= task->deadline; })))
					p.cancelling = true;
				if (p.proposal && !p.tasks.empty() && now >= p.tasks.front()->deadline)
					p.cancelling = true;
				if (p.cancelling) {
					bool encoder_drained = !p.encoder || p.encoder->discard(publisher_budget);
					if (encoder_drained) {
						p.encoder.reset();
						p.encoded.reset();
						p.builder.reset();
						p.proposal.reset();
						p.waiter = {};
						if (p.current && publisher_budget.records && publisher_budget.bytes >= 256) {
							--publisher_budget.records;
							publisher_budget.bytes -= 256;
							complete(p.current, {503, "{\"error\":\"outcome_unknown\"}"});
							p.current.reset();
						}
						while (!p.current && p.cancelled < p.tasks.size() && publisher_budget.records &&
							   publisher_budget.bytes >= 256) {
							--publisher_budget.records;
							publisher_budget.bytes -= 256;
							complete(p.tasks[p.cancelled++], {503, "{\"error\":\"outcome_unknown\"}"});
						}
						if (!p.current && p.cancelled == p.tasks.size())
							preparing.reset();
					}
				} else {
					while (!p.sealed && publisher_budget.bytes) {
						if (p.builder->command_pending()) {
							p.builder->step(publisher_budget);
							if (p.builder->command_pending())
								break;
						}
						if (p.current) {
							if (!p.encoded) {
								if (!p.encoder)
									p.encoder.emplace(p.current->command, options_.node.application);
								p.encoder->step(publisher_budget);
								if (!p.encoder->done())
									break;
								p.encoded.emplace(p.encoder->take_encoded());
								p.encoder.reset();
							}
							if (!p.builder->append(*p.encoded)) {
								check(p.builder->count() != 0, "publisher command cannot fit an empty batch");
								carry_task = std::move(p.current);
								carry.emplace(std::move(*p.encoded));
								p.encoded.reset();
								p.builder->seal();
								p.sealed = true;
								break;
							}
							p.encoded.reset();
							p.tasks.push_back(std::move(p.current));
							continue;
						}
						if (p.builder->count() == 64) {
							p.builder->seal();
							p.sealed = true;
							break;
						}
						{
							std::lock_guard lock(mutex_);
							if (!queue_.empty() && queue_.front()->kind == Task::Kind::Command &&
								now < queue_.front()->deadline) {
								p.current = queue_.front();
								queue_.pop_front();
							}
						}
						if (!p.current) {
							p.builder->seal();
							p.sealed = true;
							break;
						}
					}
					if (p.sealed && !p.proposal) {
						p.builder->step(publisher_budget);
						if (p.builder->done()) {
							p.proposal.emplace(p.builder->take());
							p.builder.reset();
							PendingTasks reserved;
							reserved.emplace(p.request, p.tasks);
							p.waiter = reserved.extract(p.request);
						}
					}
					if (p.proposal && Clock::now() >= p.tasks.front()->deadline)
						p.cancelling = true;
					if (p.proposal && !p.cancelling) {
						auto id = p.request;
						pending.insert(std::move(p.waiter));
						auto result = node.propose_prepared(id, *p.proposal);
						if (result == kronuz::consensus::SubmitResult::Accepted)
							preparing.reset();
						else {
							p.waiter = pending.extract(id);
							if (result == kronuz::consensus::SubmitResult::Fenced)
								p.cancelling = true;
						}
					}
				}
			}
			if (Clock::now() - last_health >= std::chrono::milliseconds(250)) {
				publish_health();
			}
			auto cpu_finished = owner_cpu_ns();
			owner_turn.observe(elapsed_ns(owner_started));
			if (cpu_started && cpu_finished && *cpu_finished >= *cpu_started) {
				owner_cpu.observe(*cpu_finished - *cpu_started);
			} else {
				cpu_clock_errors.add();
			}
			if (node.application_work_pending() || retired_count || (read_count && !reading && !delivering) ||
				(framing && !framing->retiring && framing->task->delivery->writable.load()) ||
				(delivering && ((!delivering->retiring && delivering->task->delivery->writable.load()) ||
								(delivering->retiring && !delivering->source_drained))) ||
				(reading && (reading->retiring || !reading->quota_blocked)) ||
				completion_cursor < held_completions.size() ||
				(preparing && (!preparing->proposal || preparing->cancelling))) {
				rescan = true;
				scheduled_wakeup = Clock::now();
				co_await task::yield();
				continue;
			}
			if (node.state().applied_index() != applied_seen) {
				applied_seen = node.state().applied_index(); // a change a parked next may answer
				rescan = true;
			}
			bool queued = false;
			{
				std::lock_guard lock(mutex_);
				queued = !queue_.empty() || !service.subscription_queue_.empty() ||
						 !service.subscription_close_queue_.empty() || !service.frames_queue_.empty();
			}
			// Work in flight that this loop advances only by visiting it again; `peers` keeps 1 ms (wake_by).
			const bool transient = peers || queued || !pending.empty() || !session_pending.empty() || read_count ||
								   reading || delivering || framing || preparing || carry_task || control_count ||
								   session_retirement->count || close_authorization->count ||
								   session_authorization->count || frames_busy;
			const auto occupied = static_cast<std::size_t>(
				std::count_if(session_tasks.begin(), session_tasks.end(), [](const auto &task) { return !!task; }));
			if (!transient && rescan) {
				scheduled_wakeup = Clock::now();
				co_await task::yield();
				continue;
			}
			if (!transient && occupied != parked_count && sweep) {
				--sweep;
				scheduled_wakeup = Clock::now();
				co_await task::yield();
				continue;
			}
			auto until = Clock::now() + std::chrono::milliseconds(1);
			if (!transient && occupied == parked_count) {
				// Session expiry is a sweep of its own; an idle owner runs it whole before sleeping.
				subscriptions->collect(options_.subscriptions.sessions, now);
				frames_sessions->collect(now, options_.subscriptions.sessions);
				until = std::min({last_health + std::chrono::milliseconds(250), parked_until, frames_until});
				// A leader's own entries are due at their deadlines: the enabling entry's retry and D136's seal.
				if (node.worker().role() == kronuz::consensus::Role::Leader &&
					(!node.history() || node.history()->ready())) {
					if (!node.state().positions_enabled())
						until = std::min(until, enabling_at);
					if (unsealed_since)
						until = std::min(until, std::max(*unsealed_since + options_.seal_after, sealing_at));
				}
			}
			until = wake_by(until);
			scheduled_wakeup = until;
			slept_from = Clock::now();
			slept_until = until;
			co_await sleep(timer, until);
			scheduled_wakeup = std::min(scheduled_wakeup, Clock::now());
			rescan = true;
		}
	} catch (...) {
		if (!started) {
			ready.set_exception(std::current_exception());
		} else {
			std::lock_guard lock(mutex_);
			stopping_ = true;
			try {
				throw;
			} catch (const std::exception &error) {
				service.failure_ = error.what();
			} catch (...) {
				service.failure_ = "unknown error";
			}
			service.health_stamp_ = Clock::now();
			health_ = std::string("{\"mode\":\"") +
					  (options_.peers ? "durable_fixed_voters" : "durable_single_voter") +
					  "\",\"fenced\":true}";
		}
	}
	accounting_->subscription_requests->close();
	if (framing && !framing->retiring) {
		if (framing->task->delivery->started)
			framing->task->delivery->reply->abandon();
		framing->task->exporting = false;
	}
	framing.reset();
	{
		std::lock_guard lock(mutex_);
		for (auto &task : service.frames_queue_)
			frames_tasks.push_back(std::move(task));
		service.frames_queue_.clear();
	}
	for (auto &task : frames_tasks) {
		task->exporting = false;
		complete(task, {503, "{\"error\":\"stopping\"}"});
		task->delivery->reply->abandon();
	}
	if (subscriptions)
		subscriptions->close_all();
	close_authorization.reset();
	session_authorization.reset();
	for (auto &task : session_tasks) {
		if (!task)
			continue;
		complete(task, {503, "{\"error\":\"stopping\"}"});
		task->authorization_queued = false;
		task->delivery->reply->abandon();
	}
	if (owned_node)
		owned_node->close();
	if (peer_history)
		peer_history->stop();
	if (peers)
		peers->stop();
	if (delivering) {
		delivering->task->delivery->reply->abandon();
		complete(delivering->task, {503, "{\"error\":\"stopping\"}"});
		for (;;) {
			distribution::WorkBudget wire;
			CommandWorkBudget roots;
			delivering->discard_pending(wire);
			const bool drained = !delivering->exporter || delivering->exporter->discard(wire, roots);
			if (drained && !delivering->pending && delivering->task->delivery->settled.load())
				break;
			co_await task::yield();
		}
		if (delivering->task->subscription_offer)
			subscriptions->settle(*delivering->task->subscription_offer, false);
		delivering->task->exporting = false;
		delivering.reset();
	}
	if (reading) {
		complete(reading->task, {503, "{\"error\":\"outcome_unknown\"}"});
		reading->encoder.reset();
		reading->reservation.reset();
		reading->capture.retire();
		while (!reading->capture.shares_reclamation_with(owned_node->state()) &&
			   reading->capture.reclamation_pending()) {
			CommandWorkBudget budget;
			reading->capture.collect(budget);
			co_await task::yield();
		}
		reading.reset();
	}
	while (read_count) {
		complete(ready_reads[read_head].task, {503, "{\"error\":\"outcome_unknown\"}"});
		ready_reads[read_head] = {};
		read_head = (read_head + 1) % ready_reads.size();
		--read_count;
		co_await task::yield();
	}
	if (preparing && preparing->encoder) {
		for (;;) {
			CommandWorkBudget budget;
			if (preparing->encoder->discard(budget))
				break;
			co_await task::yield();
		}
	}
	unsigned terminal_callbacks = 0;
	if (preparing) {
		if (preparing->current)
			complete(preparing->current, {503, "{\"error\":\"outcome_unknown\"}"});
		for (auto &task : preparing->tasks) {
			complete(task, {503, "{\"error\":\"outcome_unknown\"}"});
			if (++terminal_callbacks == 8) {
				terminal_callbacks = 0;
				co_await task::yield();
			}
		}
		preparing.reset();
	}
	if (carry_task) {
		complete(carry_task, {503, "{\"error\":\"outcome_unknown\"}"});
		carry_task.reset();
		carry.reset();
	}
	if (peers)
		peers->stop();
	while (active || history_active || (storage_flight && storage_flight->operation->done()) ||
		   (owned_node && !owned_node->drained()) ||
		   (peers && (!peers->drained() || !owned_node->channel().drained()))) {
		wake.begin_turn();
		if (owned_node) {
			CommandWorkBudget budget;
			owned_node->retirement_step(budget);
			dispatch(owned_node);
		}
		// Retirement with no I/O in flight yields, peers or not; peers poll at 1 ms once it is done.
		const bool retiring = owned_node && !owned_node->drained();
		if (active || history_active || (peers && !retiring))
			co_await sleep(timer, wake_by(Clock::now() + storage_wait));
		else
			co_await task::yield();
	}

	while (!pending.empty()) {
		auto current = pending.begin();
		auto &tasks = current->second;
		while (!tasks.empty()) {
			complete(tasks.back(), {503, "{\"error\":\"outcome_unknown\"}"});
			tasks.pop_back();
			if (++terminal_callbacks == 8) {
				terminal_callbacks = 0;
				co_await task::yield();
			}
		}
		pending.erase(current);
	}
	// Core and storage originals drain before session read reservations release.
	session_pending.clear();
	active_session_requests.clear();
	if (session_retirement)
		while (session_retirement->count)
			session_retirement->pop();
	for (auto &task : session_tasks) {
		if (!task)
			continue;
		task->quorum_pending = false;
		if (task->subscription_request)
			subscriptions->release(*task->subscription_request);
		while (!task->delivery->settled.load())
			co_await task::yield();
		begin_delivery_retirement(*task);
		while (task->retirement_remaining) {
			task->retirement_remaining -= std::min<std::size_t>(task->retirement_remaining, 8192);
			co_await task::yield();
		}
		task.reset();
	}
	if (subscriptions) {
		while (!subscriptions->drained()) {
			distribution::WorkBudget work;
			subscriptions->collect(work, Clock::now());
			co_await task::yield();
		}
	}
	for (auto &task : frames_tasks) {
		while (!task->delivery->settled.load())
			co_await task::yield();
		task.reset();
	}
	frames_tasks.clear();
	std::deque<std::shared_ptr<Task>> remaining_sessions;
	std::deque<std::shared_ptr<Task>> remaining_closes;
	{
		std::lock_guard lock(mutex_);
		remaining_sessions.swap(service.subscription_queue_);
		remaining_closes.swap(service.subscription_close_queue_);
	}
	while (!remaining_sessions.empty() || !remaining_closes.empty()) {
		auto &queue = remaining_closes.empty() ? remaining_sessions : remaining_closes;
		auto &task = queue.front();
		complete(task, {503, "{\"error\":\"stopping\"}"});
		task->delivery->reply->abandon();
		while (!task->delivery->settled.load())
			co_await task::yield();
		begin_delivery_retirement(*task);
		while (task->retirement_remaining) {
			task->retirement_remaining -= std::min<std::size_t>(task->retirement_remaining, 8192);
			co_await task::yield();
		}
		queue.pop_front();
		co_await task::yield();
	}
	std::deque<std::shared_ptr<Task>> remaining;
	{
		std::lock_guard lock(mutex_);
		remaining.swap(queue_);
	}
	while (!remaining.empty()) {
		complete(remaining.front(), {503, "{\"error\":\"outcome_unknown\"}"});
		remaining.pop_front();
		if (++terminal_callbacks == 8) {
			terminal_callbacks = 0;
			co_await task::yield();
		}
	}
	while (control_count) {
		auto task = std::move(legacy_deliveries[control_head]);
		control_head = (control_head + 1) % legacy_deliveries.size();
		--control_count;
		task->delivery->reply->abandon();
		while (!task->delivery->settled.load())
			co_await task::yield();
		begin_delivery_retirement(*task);
		while (task->retirement_remaining) {
			task->retirement_remaining -= std::min<std::size_t>(task->retirement_remaining, 8192);
			co_await task::yield();
		}
		task.reset();
		co_await task::yield();
	}
	while (retired_count) {
		CommandWorkBudget budget;
		collect_tasks(budget);
		co_await task::yield();
	}
}
} // namespace detent::authority
