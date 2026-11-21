#include "detent_relay/mirror_service.hh"
#include "detent_distribution/framing.hh"
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <openssl/rand.h>
#include <thread>

namespace detent::relay {
namespace d = distribution;
namespace {
using Clock = std::chrono::steady_clock;
std::string hex(std::span<const char> bytes) {
	std::string out;
	for (unsigned char byte : bytes) {
		out += "0123456789abcdef"[byte >> 4];
		out += "0123456789abcdef"[byte & 15];
	}
	return out;
}
d::Identity random_identity() {
	d::Identity out{};
	if (RAND_bytes(reinterpret_cast<unsigned char *>(out.data()), out.size()) != 1)
		throw std::runtime_error("relay mirror entropy");
	return out;
}
void control(MirrorJob &job, int status, std::string body) {
	job.reply.status = status;
	job.reply.body = std::move(body);
}
} // namespace

struct MirrorService::Impl {
	MirrorServiceOptions options;
	std::unique_ptr<Upstream> upstream;
	std::shared_ptr<d::StorageLedger> account;
	mutable std::mutex mutex;
	std::condition_variable wake;
	std::deque<std::shared_ptr<MirrorJob>> incoming;
	bool stopping = false;
	MirrorStatus state;
	// The mirror thread's alone.
	std::optional<NamespaceRelay> relay;
	struct Session {
		d::SubscriptionPrincipal principal;
		bool compressed = false, busy = false;
		Clock::time_point used;
		bool pages = false;
		std::vector<std::string> records;
	};
	std::map<d::Identity, Session> sessions;
	struct Parked {
		std::shared_ptr<MirrorJob> job;
		Clock::time_point deadline;
	};
	std::vector<Parked> parked;
	// The file a downstream reads in ranges (D138), read once and held until its last range or
	// another file is asked.
	struct HeldFile {
		std::string name_space;
		MirrorFile file;
		std::string bytes;
	};
	std::optional<HeldFile> held;
	// The builder thread's tasks, under `mutex`; `built` tells the mirror thread one finished.
	std::deque<std::function<void()>> builds;
	std::condition_variable build_wake;
	bool built = false;
	std::thread thread, builder;

	void finish(MirrorJob &job) {
		job.done.store(true, std::memory_order_release);
		std::lock_guard lock(mutex);
		state.sessions = sessions.size();
	}
	bool mirrored(std::string_view name) const {
		return std::binary_search(options.namespaces.begin(), options.namespaces.end(), name);
	}
	// W13 and what this relay mirrors: every followed name must be both.
	bool refused(MirrorJob &job) {
		if (job.all) {
			control(job, 400, "{\"error\":\"invalid_request\"}");
			return true;
		}
		for (const auto &position : job.positions)
			if (!job.grants || !job.grants->allows(position.name) || !mirrored(position.name)) {
				control(job, 403, "{\"error\":\"scope_forbidden\"}");
				return true;
			}
		return false;
	}
	Session *session_of(MirrorJob &job) {
		const auto found = sessions.find(job.session);
		if (found == sessions.end()) {
			control(job, 410, "{\"error\":\"session_expired\"}");
			return nullptr;
		}
		auto &session = found->second;
		if (session.principal.identity != job.principal.identity) {
			control(job, 403, "{\"error\":\"scope_forbidden\"}");
			return nullptr;
		}
		if (session.principal.policy_epoch != job.principal.policy_epoch) {
			control(job, 409, "{\"error\":\"identity_changed\"}");
			return nullptr;
		}
		return &session;
	}
	// `body` in fragment-sized chunks, the response's metadata beside them.
	void stream(MirrorJob &job, std::string_view type, std::string_view body) {
		job.reply.status = 200;
		job.reply.content_type = type;
		for (std::size_t at = 0; at < body.size() || at == 0; at += d::fragment_bytes) {
			const auto piece = body.substr(at, d::fragment_bytes);
			auto chunk = d::make_owned_buffer(account, piece.size());
			chunk->append(piece);
			chunk->seal();
			job.reply.chunks.push_back(std::move(chunk));
			if (body.empty())
				break;
		}
	}
	// A W6 answer as the authority sends one: DTDIST01 fragments of UpdateKind::Frames.
	void frames(MirrorJob &job, const Session &session, std::string_view body) {
		d::OwnedFragmentEncoder encoder(d::UpdateKind::Frames, random_identity(), body.size(), session.compressed,
										account);
		for (std::size_t at = 0; at < body.size(); at += d::fragment_bytes) {
			const auto piece = body.substr(at, d::fragment_bytes);
			auto chunk = d::make_owned_buffer(account, piece.size());
			chunk->append(piece);
			chunk->seal();
			encoder.begin(std::move(chunk), at + piece.size() == body.size());
			while (!encoder.ready()) {
				d::WorkBudget budget{d::fragment_bytes * 4, 64};
				encoder.step(budget);
			}
			job.reply.chunks.push_back(encoder.take());
		}
		job.reply.status = 200;
		job.reply.content_type = "application/vnd.detent.frames";
		job.reply.headers = {{"X-Detent-Session-Version", "2"},
							 {"X-Detent-Session", hex(job.session)},
							 {"X-Detent-Coding", session.compressed ? "zstd" : "raw"}};
		std::lock_guard lock(mutex);
		++state.answers;
	}
	// A next's answer now, or false while every followed namespace is current (it waits).
	bool answer(MirrorJob &job, Session &session) {
		auto positions = job.positions;
		for (auto &position : positions)
			position.pages = session.pages &&
							 !std::binary_search(session.records.begin(), session.records.end(), position.name);
		auto body = relay->changes(positions);
		if (!body)
			return false;
		frames(job, session, *body);
		return true;
	}
	void handle(const std::shared_ptr<MirrorJob> &held, Clock::time_point now) {
		auto &job = *held;
		switch (job.kind) {
		case MirrorJob::Kind::Open: {
			if (refused(job))
				break;
			if (sessions.size() >= options.sessions) {
				control(job, 503, "{\"error\":\"admission_full\"}");
				break;
			}
			if (!std::is_sorted(job.records.begin(), job.records.end()) ||
				std::adjacent_find(job.records.begin(), job.records.end()) != job.records.end()) {
				control(job, 400, "{\"error\":\"invalid_request\"}");
				break;
			}
			auto id = random_identity();
			sessions[id] = {job.principal, job.compressed, false, now, job.pages, job.records};
			control(job, 200, "{\"version\":2,\"session\":\"" + hex(id) +
								  "\",\"wait_ms\":" + std::to_string(options.maximum_wait_ms) + "}");
			break;
		}
		case MirrorJob::Kind::Close:
			if (session_of(job)) {
				sessions.erase(job.session);
				control(job, 204, {});
			}
			break;
		case MirrorJob::Kind::Next: {
			auto *session = session_of(job);
			if (!session || refused(job))
				break;
			if (session->busy) {
				control(job, 409, "{\"error\":\"session_busy\"}");
				break;
			}
			session->used = now;
			if (!state_ready()) {
				control(job, 503, "{\"error\":\"source_behind\"}");
				break;
			}
			if (answer(job, *session))
				break;
			if (!job.wait_ms) {
				control(job, 204, {});
				count_idle();
				break;
			}
			session->busy = true;
			parked.push_back({held, now + std::chrono::milliseconds(std::min(job.wait_ms, options.maximum_wait_ms))});
			return; // answered by a later sync, or idle at its deadline
		}
		case MirrorJob::Kind::Layouts: {
			std::vector<std::string> names;
			for (const auto &position : job.positions) {
				if (!job.grants || !job.grants->allows(position.name)) {
					control(job, 403, "{\"error\":\"scope_forbidden\"}");
					return finish(job);
				}
				names.push_back(position.name);
			}
			std::sort(names.begin(), names.end());
			names.erase(std::unique(names.begin(), names.end()), names.end());
			stream(job, "application/vnd.detent.layouts", encode_mirror_table(relay->layouts(names)));
			break;
		}
		case MirrorJob::Kind::File: {
			if (!job.grants || !job.grants->allows(job.name_space)) {
				control(job, 403, "{\"error\":\"scope_forbidden\"}");
				break;
			}
			const MirrorFile *file = nullptr;
			if (const auto *layout = relay->mirror().table().find(job.name_space))
				for (const auto &each : layout->files)
					if (each.name == job.name)
						file = &each;
			if (!file) {
				control(job, 404, "{\"error\":\"not_found\"}");
				break;
			}
			if (!job.range) {
				stream(job, "application/octet-stream", relay->fetch(job.name_space, *file));
				break;
			}
			// A range answers at most its length from its offset, fewer at the file's end; 416 at or
			// past the end.
			const auto [offset, length] = *job.range;
			if (offset >= file->size) {
				held.reset();
				control(job, 416, "{\"error\":\"range_not_satisfiable\"}");
				break;
			}
			if (!held || held->name_space != job.name_space || held->file != *file)
				held = HeldFile{job.name_space, *file, relay->fetch(job.name_space, *file)};
			const auto count = std::min(length, file->size - offset);
			stream(job, "application/octet-stream", std::string_view(held->bytes).substr(offset, count));
			if (offset + count == file->size)
				held.reset();
			break;
		}
		}
		finish(job);
	}
	bool state_ready() {
		std::lock_guard lock(mutex);
		return state.ready;
	}
	void count_idle() {
		std::lock_guard lock(mutex);
		++state.idle;
	}
	// True when the mirror reached the upstream.
	bool sync() {
		try {
			const auto stats = relay->sync(options.namespaces);
			std::lock_guard lock(mutex);
			state.ready = true;
			++state.syncs;
			state.entries += stats.entries;
			state.fetched_files += stats.files.fetched_files;
			state.fetched_bytes += stats.files.fetched_bytes;
			state.reused_files += stats.files.reused_files;
			state.collected_files += stats.files.collected_files;
			state.failed_namespaces = relay->mirror().failed().size();
			state.error.clear();
			return true;
		} catch (const std::invalid_argument &error) {
			failure(error.what());
		} catch (const std::logic_error &error) {
			// The mirror needs a restart (an I/O error after staging): stop serving from it.
			std::lock_guard lock(mutex);
			state.failed = true;
			state.error = error.what();
		} catch (const std::exception &error) {
			failure(error.what());
		}
		return false;
	}
	void failure(const char *what) {
		std::lock_guard lock(mutex);
		++state.sync_failures;
		state.error = what;
	}
	// Each parked next after a sync: answered when something changed, idle at its deadline.
	void revisit(Clock::time_point now, bool synced) {
		for (std::size_t i = 0; i < parked.size();) {
			auto &[job, deadline] = parked[i];
			const auto found = sessions.find(job->session);
			bool settled = job->abandoned.load();
			if (!settled && found == sessions.end()) {
				control(*job, 410, "{\"error\":\"session_expired\"}");
				settled = true;
			} else if (!settled && synced) {
				try {
					settled = answer(*job, found->second);
				} catch (const d::StoragePressure &) {
					job->reply.chunks.clear();
					control(*job, 503, "{\"error\":\"admission_full\"}");
					settled = true;
				} catch (const std::exception &) {
					job->reply.chunks.clear();
					control(*job, 503, "{\"error\":\"distribution_unavailable\"}");
					settled = true;
				}
			}
			if (!settled && now >= deadline) {
				control(*job, 204, {});
				count_idle();
				settled = true;
			}
			if (!settled) {
				++i;
				continue;
			}
			if (found != sessions.end())
				found->second.busy = false;
			finish(*job);
			parked[i] = std::move(parked.back());
			parked.pop_back();
		}
	}
	void build_loop() {
		for (;;) {
			std::function<void()> task;
			{
				std::unique_lock lock(mutex);
				build_wake.wait(lock, [&] { return stopping || !builds.empty(); });
				if (stopping)
					return;
				task = std::move(builds.front());
				builds.pop_front();
			}
			task();
			std::lock_guard lock(mutex);
			built = true;
			wake.notify_one();
		}
	}
	void run() {
		builder = std::thread([this] { build_loop(); });
		try {
			auto io = std::make_shared<kronuz::io::completion::PosixIO>(options.directory, 0644);
			relay.emplace(std::move(io), options.cluster, *upstream, MirrorStore::Options{},
						  [this](std::function<void()> task) {
							  std::lock_guard lock(mutex);
							  builds.push_back(std::move(task));
							  build_wake.notify_one();
						  });
		} catch (const std::exception &error) {
			std::lock_guard lock(mutex);
			state.failed = true;
			state.error = std::string("relay mirror: ") + error.what();
		}
		auto next_sync = Clock::now();
		unsigned failing = 0; // consecutive failed syncs: each doubles the wait, up to reconnect_maximum
		for (;;) {
			std::deque<std::shared_ptr<MirrorJob>> jobs;
			{
				std::unique_lock lock(mutex);
				auto until = next_sync;
				for (const auto &each : parked)
					until = std::min(until, each.deadline);
				wake.wait_until(lock, until, [&] { return stopping || !incoming.empty() || state.failed || built; });
				if (stopping || state.failed)
					break;
				jobs.swap(incoming);
			}
			const bool landed = take_built() && relay->collect();
			auto now = Clock::now();
			for (auto &job : jobs) {
				try {
					handle(job, now);
				} catch (const d::StoragePressure &) {
					job->reply.chunks.clear();
					control(*job, 503, "{\"error\":\"admission_full\"}");
					finish(*job);
				} catch (const std::exception &) {
					job->reply.chunks.clear();
					control(*job, 503, "{\"error\":\"distribution_unavailable\"}");
					finish(*job);
				}
			}
			const bool synced = now >= next_sync;
			if (synced) {
				failing = sync() ? 0 : std::min(failing + 1, 16u);
				now = Clock::now();
				next_sync = now + std::min(options.interval * (1u << failing),
										   options.reconnect_maximum);
				std::erase_if(sessions, [&](const auto &entry) {
					return !entry.second.busy && now - entry.second.used >= options.idle;
				});
			}
			revisit(now, synced || landed);
			std::lock_guard lock(mutex);
			state.pages = relay->pages();
		}
		// Stopping, or the mirror failed: every waiting request is answered.
		std::deque<std::shared_ptr<MirrorJob>> left;
		{
			std::lock_guard lock(mutex);
			left.swap(incoming);
		}
		for (auto &each : parked)
			left.push_back(std::move(each.job));
		parked.clear();
		for (auto &job : left) {
			job->reply.chunks.clear();
			control(*job, 503, "{\"error\":\"source_behind\"}");
			finish(*job);
		}
		relay.reset(); // cancels a build in flight
		{
			std::lock_guard lock(mutex);
			stopping = true;
			builds.clear();
			build_wake.notify_one();
		}
		builder.join();
	}
	bool take_built() {
		std::lock_guard lock(mutex);
		return std::exchange(built, false);
	}
};

MirrorService::MirrorService(MirrorServiceOptions options, std::unique_ptr<Upstream> upstream,
							 std::shared_ptr<d::StorageLedger> account)
	: impl_(std::make_unique<Impl>()) {
	if (!upstream || !account || options.namespaces.empty() || options.cluster == d::Identity{} ||
		!std::is_sorted(options.namespaces.begin(), options.namespaces.end()) ||
		std::adjacent_find(options.namespaces.begin(), options.namespaces.end()) != options.namespaces.end())
		throw std::invalid_argument("relay mirror service options");
	impl_->options = std::move(options);
	impl_->upstream = std::move(upstream);
	impl_->account = std::move(account);
	impl_->thread = std::thread([impl = impl_.get()] { impl->run(); });
}
MirrorService::~MirrorService() {
	close();
	impl_->thread.join();
}
void MirrorService::submit(std::shared_ptr<MirrorJob> job) {
	{
		std::lock_guard lock(impl_->mutex);
		if (!impl_->stopping && !impl_->state.failed) {
			impl_->incoming.push_back(std::move(job));
			impl_->wake.notify_one();
			return;
		}
	}
	control(*job, 503, "{\"error\":\"source_behind\"}");
	job->done.store(true, std::memory_order_release);
}
void MirrorService::close() noexcept {
	std::lock_guard lock(impl_->mutex);
	impl_->stopping = true;
	impl_->wake.notify_one();
}
MirrorStatus MirrorService::status() const {
	std::lock_guard lock(impl_->mutex);
	return impl_->state;
}
} // namespace detent::relay
