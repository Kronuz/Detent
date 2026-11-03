#include "detentd.hh"

#include "detent/assert_off_loop.hh"
#include "detent/json_patch.hh"
#include "detent/detail/file.hh"
#include "raft_delegate.hh"

#include <bus.h>
#include <length.h>

#include <asio.hpp>
#include <http_conditional.h>
#include <http_compression.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <thread>

namespace detentd {

namespace {

int status_for(detent::UpdateRejectReason reason) {
	switch (reason) {
		case detent::UpdateRejectReason::Version: return 409; // stale/duplicate
		case detent::UpdateRejectReason::Oversize: return 413;
		case detent::UpdateRejectReason::Transport: return 503; // no quorum decision within the wait timeout
		default: return 400; // Io/Parse/Schema/Checksum
	}
}

std::string json_escape(std::string_view s) {
	std::string out;
	out.reserve(s.size());
	for (char c : s) {
		out += (c == '"' || c == '\\') ? '\'' : c;
	}
	return out;
}

} // namespace

Distributor::Distributor(Options options) : options_(std::move(options)) {
	max_diff_history_ = options_.max_diff_history;

	router_.route("PUT", "/v1/snapshot",
	              [this](http::Request& q, http::ResponseWriter& r, const http::Params&) {
		              return route_write_snapshot(q, r, /*is_patch=*/false);
	              });
	router_.route("PATCH", "/v1/snapshot",
	              [this](http::Request& q, http::ResponseWriter& r, const http::Params&) {
		              return route_write_snapshot(q, r, /*is_patch=*/true);
	              });
	router_.route("GET", "/v1/snapshot",
	              [this](http::Request& q, http::ResponseWriter& r, const http::Params&) {
		              return route_get_snapshot(q, r);
	              });
	router_.route("GET", "/v1/health",
	              [this](http::Request& q, http::ResponseWriter& r, const http::Params&) {
		              return route_get_health(q, r);
	              });

	load_persisted();

	if (options_.cluster) {
		const auto& c = *options_.cluster;
		bus_ = std::make_unique<cluster::Bus>(
			1, 0, c.token, kMaxRaftWireType,
			[this](int wire_type, std::string_view content, const asio::ip::udp::endpoint& /*from*/) {
				on_bus_message(wire_type, content);
			});
		reactor::UdpOptions udp_opts;
		udp_opts.multicast_group = c.multicast_group;
		udp_opts.multicast_interface = c.multicast_interface;
		// Same-host multi-process cluster (our test/demo setup) needs loopback
		// delivery; a real multi-host deployment would leave this false.
		udp_opts.multicast_loop = true;
		bus_->set_options(udp_opts);

		// The restart-safety silence window only protects against a stale
		// vote from a PRIOR run; a genuine first launch has none, so it
		// gets zero silence instead of the usual 3x election_max. A marker
		// file (not Raft state) tells the two apart -- see CLUSTERING.md.
		auto marker_path = options_.persist_path;
		marker_path += ".cluster_marker";
		bool has_run_before = std::filesystem::exists(marker_path);
		if (!has_run_before) {
			std::ofstream(marker_path).close();
		}
		auto startup_silence = has_run_before
			? std::chrono::duration_cast<std::chrono::steady_clock::duration>(
				  std::chrono::duration<double>(3.0 * c.raft_config.election_max))
			: std::chrono::steady_clock::duration::zero();
		// A peer not heard from (any Raft message) within this window is
		// treated as dead (DetentRaftDelegate::is_alive) -- a multiple of
		// heartbeat_timeout tolerates ordinary jitter, not just one miss.
		auto liveness_timeout = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
			std::chrono::duration<double>(4.0 * c.raft_config.heartbeat_timeout));
		raft_delegate_ = std::make_unique<DetentRaftDelegate>(
			*this, *bus_, c.self_address, c.peers, startup_silence, liveness_timeout);
		raft_ = std::make_unique<cluster::Raft<RaftNode>>(bus_->io().get_executor(), c.raft_config, raft_delegate_.get());

		bus_->start(c.multicast_port);
		raft_->start(); // "Call once the transport is up" -- raft.h's own start() contract
	}
}

// What GET and health read: one version's bytes, encodings and diffs, immutable once published.
struct Distributor::Published {
	struct Step {
		std::uint64_t base_version;
		detent::SnapshotPatch patch;
	};
	struct ComposedDiff {
		std::string bytes, bytes_gzip, bytes_zstd;
	};
	std::uint64_t version = 0;
	// Set once per update, not per GET: http's own conditional support would hash the whole body on
	// every request (~577 us at 560 KB), and skips that when a handler sets its own ETag.
	std::string etag;
	std::string bytes, bytes_gzip, bytes_zstd; // gzip/zstd empty if not worth it
	// The patch from diff_base_version to this version, precomputed and compressed, for the common
	// "one update behind" poller; a poller further behind composes from `history`.
	bool has_diff = false;
	std::uint64_t diff_base_version = 0;
	std::string diff_bytes, diff_gzip, diff_zstd;
	// One step per accepted update, oldest first, bounded to max_diff_history entries; entries are
	// shared with the next version's history.
	std::vector<std::shared_ptr<const Step>> history;
	// Composed diffs for polls against this version; the cache lives here so an insert always
	// describes the version it was composed for.
	mutable generation_cache::Cache<std::uint64_t, ComposedDiff> composed;
};

// One write in flight on the owner: its event, and the outcome once the owner has it.
struct Distributor::Write {
	task::event done;
	bool settled = false;
	std::string correlation;
	detent::Result<std::uint64_t, detent::UpdateError> result{
		detent::UpdateError{detent::UpdateRejectReason::Io, "no result recorded"}};
};

// A PUT or PATCH's slot, released when its request ends.
struct Distributor::WriteSlot final : http::RequestExtension {
	explicit WriteSlot(std::atomic<std::size_t>& slots) : slots_(slots) {}
	~WriteSlot() override { slots_.fetch_sub(1, std::memory_order_relaxed); }
	std::atomic<std::size_t>& slots_;
};

Distributor::~Distributor() {
	// bus_'s reactor thread calls into raft_ via on_bus_message(); stop it
	// (joins the thread) before raft_/raft_delegate_ destruct, or the
	// thread can still be running while raft_ is torn down.
	if (bus_) bus_->stop();
	// The owner runs tasks that use the members below; end it first. Idempotent after main's D15 phases.
	owner_.stop();
	owner_.close();
	owner_.destroy_roots();
}

task::task<void> Distributor::handle(http::Request& request, http::ResponseWriter& response) {
	requests_served_.fetch_add(1, std::memory_order_relaxed);
	co_await router_.handle(request, response);
}

std::unique_ptr<http::RequestExtension> Distributor::create_extension(const http::Request& request) {
	if (request.method != "PUT" && request.method != "PATCH") return nullptr;
	if (write_slots_.fetch_add(1, std::memory_order_relaxed) >= kWriteSlots) {
		write_slots_.fetch_sub(1, std::memory_order_relaxed);
		throw http::RequestRejected(503, "Service Unavailable\n", http::RejectionContent::Text, 1);
	}
	return std::make_unique<WriteSlot>(write_slots_);
}

void Distributor::begin_shutdown() noexcept {
	stopping_.store(true, std::memory_order_release);
	stop_.request(task::cancel_reason::shutdown);
}

std::uint64_t Distributor::current_version() const {
	auto published = published_.load();
	return published ? published->version : 0;
}

detent::Result<std::uint64_t, detent::UpdateError> Distributor::resolve_and_commit(SnapshotResolver resolve) {
	detent::assert_off_loop();
	assert(owner_.inside());
	const auto started = std::chrono::steady_clock::now();
	if (options_.on_commit_begin) options_.on_commit_begin();
	auto previous = published_.load();
	const bool has_snapshot = static_cast<bool>(previous);
	// Resolving on the owner's current state is what makes a PATCH's merge race-free against a
	// concurrent PUT or PATCH: it always merges onto the most recently committed state.
	auto resolved = resolve(current_snapshot_, has_snapshot);
	if (!resolved) return resolved.error();
	auto new_snapshot = std::move(resolved).value();
	if (has_snapshot && new_snapshot.version <= previous->version) {
		return detent::UpdateError{detent::UpdateRejectReason::Version, "stale or duplicate version"};
	}
	auto new_bytes = detent::serialize_snapshot(new_snapshot);
	// Persist before publishing: a persist failure (std::filesystem::rename() throws on a missing or
	// unwritable directory) propagates with the published state unchanged, matching the 500 the
	// caller sees.
	persist(new_bytes, new_snapshot.version);

	auto next = std::make_shared<Published>();
	next->version = new_snapshot.version;
	// The diff from the OLD state to the new one, for the next poller whose last-known version is
	// exactly this update's base. Nothing to diff against on the very first accepted snapshot.
	if (has_snapshot) {
		auto diff = detent::diff_snapshots(current_snapshot_, new_snapshot);
		next->diff_bytes = detent::serialize_patch(diff);
		compress_into(next->diff_bytes, next->diff_gzip, next->diff_zstd);
		next->diff_base_version = previous->version;
		next->has_diff = true;
		next->history = previous->history;
		next->history.push_back(std::make_shared<const Published::Step>(Published::Step{previous->version, std::move(diff)}));
		while (next->history.size() > max_diff_history_) next->history.erase(next->history.begin());
	}
	next->bytes = std::move(new_bytes);
	compress_into(next->bytes, next->bytes_gzip, next->bytes_zstd);
	next->etag = http::weak_etag(next->bytes);
	const auto accepted_version = next->version;
	current_snapshot_ = std::move(new_snapshot);
	published_.store(std::move(next));

	puts_accepted_.fetch_add(1, std::memory_order_relaxed);
	try { if (options_.on_accept) options_.on_accept(accepted_version); } catch (...) { /* Diagnostics must not change publish results. */ }
	try {
		if (options_.on_commit) options_.on_commit(accepted_version, std::chrono::steady_clock::now() - started);
	} catch (...) { /* Diagnostics are best effort. */ }
	return accepted_version;
}

detent::Result<std::uint64_t, detent::UpdateError> Distributor::try_accept(std::string_view bytes) {
	detent::assert_off_loop();
	auto parsed = detent::parse_source_snapshot(bytes, "distributor", options_.max_knobs);
	if (!parsed) return parsed.error();
	auto snapshot = std::move(parsed).value();
	return resolve_and_commit(
		[&snapshot](const detent::Snapshot&, bool) -> detent::Result<detent::Snapshot, detent::UpdateError> {
			return std::move(snapshot);
		});
}

detent::Result<std::uint64_t, detent::UpdateError> Distributor::try_accept_patch(std::string_view patch_bytes) {
	detent::assert_off_loop();
	auto parsed_patch = detent::parse_snapshot_patch(patch_bytes, "distributor", options_.max_knobs);
	if (!parsed_patch) return parsed_patch.error();
	auto patch = std::move(parsed_patch).value();
	auto max_knobs = options_.max_knobs;
	return resolve_and_commit(
		[&patch, max_knobs](const detent::Snapshot& current, bool has_current)
			-> detent::Result<detent::Snapshot, detent::UpdateError> {
			if (!has_current) {
				return detent::UpdateError{detent::UpdateRejectReason::Schema, "cannot PATCH: no snapshot published yet"};
			}
			return detent::apply_patch(current, patch, max_knobs);
		});
}

namespace {
// Envelope for a command riding Raft's replicated log: correlation id +
// 1-byte kind marker + the raw body verbatim (see CLUSTERING.md).
constexpr char kEnvelopeKindPut = 'P';
constexpr char kEnvelopeKindPatch = 'H';

std::string make_envelope(const std::string& correlation_id, bool is_patch, std::string_view body) {
	std::string envelope = cluster::serialise_string(correlation_id);
	envelope += is_patch ? kEnvelopeKindPatch : kEnvelopeKindPut;
	envelope.append(body);
	return envelope;
}

struct ParsedEnvelope {
	std::string correlation_id;
	bool is_patch = false;
	std::string_view body;
};

// nullopt on a malformed envelope (defensive; a log entry is just bytes).
std::optional<ParsedEnvelope> parse_envelope(const std::string& envelope) {
	const char* p = envelope.data();
	const char* end = p + envelope.size();
	std::string_view correlation_sv;
	if (!cluster::unserialise_string(&p, end, correlation_sv)) return std::nullopt;
	if (p >= end) return std::nullopt; // missing kind byte
	char kind = *p;
	++p;
	if (kind != kEnvelopeKindPut && kind != kEnvelopeKindPatch) return std::nullopt;
	return ParsedEnvelope{std::string(correlation_sv), kind == kEnvelopeKindPatch,
	                       std::string_view(p, static_cast<std::size_t>(end - p))};
}

// Covers a follower-submitted command's extra forward + commit round trip
// without tying up an HTTP worker thread indefinitely on a wedged cluster.
constexpr auto kCommandWaitTimeout = std::chrono::seconds{10};

} // namespace

task::task<detent::Result<std::uint64_t, detent::UpdateError>> Distributor::submit_command(std::string_view body, bool is_patch) {
	using Result = detent::Result<std::uint64_t, detent::UpdateError>;
	const auto retry = [](std::string detail) {
		return Result{detent::UpdateError{detent::UpdateRejectReason::Transport, std::move(detail)}};
	};
	// Runs on the owner. Unclustered, the commit happens here and the write is settled before the wait.
	auto accept = [this, body, is_patch, &retry]() -> std::shared_ptr<Write> {
		auto write = std::make_shared<Write>();
		if (stopping_.load(std::memory_order_acquire) || fenced_.load(std::memory_order_acquire)) {
			// Nothing was applied: a writer queued behind a long commit is answered at once.
			write->result = retry(fenced_.load(std::memory_order_acquire) ? "node fenced after a persistence failure; retry elsewhere"
			                                                              : "server stopping; retry");
			write->settled = true;
			write->done.set();
			return write;
		}
		if (!raft_) {
			write->result = is_patch ? try_accept_patch(body) : try_accept(body);
			write->settled = true;
			write->done.set();
			return write;
		}
		// Any node accepts the write; add_command() forwards to the real leader if this isn't it,
		// and apply() still fires here once committed (see CLUSTERING.md). X-Detent-Leader is a
		// latency hint only. session_id_ makes the id unique across restarts (see its comment).
		write->correlation = options_.cluster->self_address + "#" + std::to_string(session_id_) + "#" +
			std::to_string(command_counter_.fetch_add(1, std::memory_order_relaxed));
		auto envelope = make_envelope(write->correlation, is_patch, body);
		pending_[write->correlation] = write; // register before submitting; apply() can never race ahead
		raft_->add_command(envelope);
		return write;
	};
	auto finish = [this, &retry](Write& write, task::cancel_reason why) -> Result {
		if (write.settled) return std::move(write.result);
		pending_.erase(write.correlation); // give up waiting; a late apply finds no entry
		if (why == task::cancel_reason::shutdown) return retry("server stopping; retry");
		return retry("no quorum decision within " + std::to_string(kCommandWaitTimeout.count()) + "s; retry");
	};
	co_return co_await detent::owner_submit<Result>(
		owner_, std::move(accept), std::move(finish), std::chrono::steady_clock::now() + kCommandWaitTimeout,
		stop_.token());
}

// Raft's apply, on the Bus loop: the envelope's commit is a task on the owner, which never suspends
// and so applies in log order.
void Distributor::on_raft_apply(const std::string& command) {
	task::spawn(
		task::scheduler_ref::of(owner_.scheduler()),
		[](Distributor* self, std::string envelope) -> task::task<void> {
			self->apply_envelope(envelope);
			co_return;
		}(this, command),
		[](std::exception_ptr) {});
}

void Distributor::apply_envelope(const std::string& command) {
	auto parsed = parse_envelope(command);
	if (!parsed) return; // malformed; best-effort, drop it

	// A committed entry that cannot be persisted would diverge this node from the log, and Raft does not
	// retry an apply: fence the node (it keeps serving what it has) and answer the waiting writer.
	detent::Result<std::uint64_t, detent::UpdateError> result{detent::UpdateError{detent::UpdateRejectReason::Io, "no result recorded"}};
	try {
		result = parsed->is_patch ? try_accept_patch(parsed->body) : try_accept(parsed->body);
	} catch (...) {
		fenced_.store(true, std::memory_order_release);
		result = detent::UpdateError{detent::UpdateRejectReason::Transport,
		                             "node fenced after a persistence failure; retry elsewhere"};
	}
	if (!result) puts_rejected_.fetch_add(1, std::memory_order_relaxed);

	// Only the originating node has a pending entry; every other member just applied the mutation
	// above with nobody waiting.
	auto it = pending_.find(parsed->correlation_id);
	if (it != pending_.end()) {
		auto write = std::move(it->second);
		pending_.erase(it);
		write->result = std::move(result);
		write->settled = true;
		write->done.set();
	}
}

void Distributor::on_raft_leader_changed(std::string leader_address) {
	std::lock_guard<std::mutex> lock(leader_mutex_);
	leader_address_ = std::move(leader_address);
}

bool Distributor::is_leader() const {
	if (!raft_) return true; // unclustered: the single-node fast path is always "the leader"
	std::lock_guard<std::mutex> lock(leader_mutex_);
	return !leader_address_.empty() && leader_address_ == options_.cluster->self_address;
}

std::string Distributor::leader_address() const {
	if (!raft_) return options_.cluster ? options_.cluster->self_address : std::string{};
	std::lock_guard<std::mutex> lock(leader_mutex_);
	return leader_address_;
}

void Distributor::compress_into(std::string_view identity, std::string& gzip_out, std::string& zstd_out) {
	gzip_out.clear();
	zstd_out.clear();
	http::CompressionOptions opts{};
	if (identity.size() < opts.min_size) return; // not worth it, same threshold the http library itself uses
	auto zstd = http::encode("zstd", identity, opts);
	if (!zstd.empty() && zstd.size() < identity.size()) zstd_out = std::move(zstd);
	auto gzip = http::encode("gzip", identity, opts);
	if (!gzip.empty() && gzip.size() < identity.size()) gzip_out = std::move(gzip);
}

void Distributor::write_negotiated(
	const http::Request& request, http::ResponseWriter& response, int status,
	const std::string& identity, const std::string& gzip_body, const std::string& zstd_body,
	std::string_view content_type) {
	http::CompressionOptions opts{};
	auto coding = http::negotiate_encoding(request.header("Accept-Encoding"), opts);
	if (coding == "zstd" && !zstd_body.empty()) {
		response.set_header("Content-Encoding", "zstd");
		response.set_header("Vary", "Accept-Encoding");
		response.send(status, zstd_body, content_type);
	} else if (coding == "gzip" && !gzip_body.empty()) {
		response.set_header("Content-Encoding", "gzip");
		response.set_header("Vary", "Accept-Encoding");
		response.send(status, gzip_body, content_type);
	} else {
		response.send(status, identity, content_type);
	}
}

void Distributor::persist(std::string_view bytes, std::uint64_t version) {
	detent::assert_off_loop();
	if (options_.persist_path.empty()) return;
	(void)version; // not written to disk separately; it's inside `bytes` itself
	auto tmp = options_.persist_path;
	tmp += ".tmp";
	{
		std::ofstream out(tmp, std::ios::binary);
		out << bytes;
	}
	std::filesystem::rename(tmp, options_.persist_path); // throws on failure, deliberately uncaught here
}

void Distributor::load_persisted() {
	detent::assert_off_loop();
	if (options_.persist_path.empty()) return;
	if (!std::filesystem::exists(options_.persist_path)) return;
	auto bytes = detent::detail::read_file(options_.persist_path, detent::kDefaultMaxBytes);
	auto parsed = detent::parse_source_snapshot(bytes, "distributor", options_.max_knobs);
	if (parsed) {
		// has_diff stays false: there is no "previous" state from before this restart to diff
		// against. The first update accepted after startup diffs against this loaded snapshot.
		auto loaded = std::make_shared<Published>();
		loaded->version = parsed.value().version;
		loaded->etag = http::weak_etag(bytes);
		compress_into(bytes, loaded->bytes_gzip, loaded->bytes_zstd);
		loaded->bytes = std::move(bytes);
		current_snapshot_ = std::move(parsed).value();
		published_.store(std::move(loaded));
	}
	// A corrupt persisted file starts empty rather than crashing the process;
	// the next valid PUT recovers it. An operator can inspect/replace the file.
}

void Distributor::on_bus_message(int wire_type, std::string_view content) {
	if (!raft_) return;
	auto msg = raft_message_for_wire_type(wire_type);
	if (!msg) return; // outside Raft's range; Bus's own max_type already drops these, defensive here too
	raft_->on_message(*msg, content);
}

task::task<void> Distributor::route_write_snapshot(http::Request& request, http::ResponseWriter& response,
                                                     bool is_patch) {
	auto result = co_await submit_command(request.body, is_patch);
	if (options_.cluster) response.set_header("X-Detent-Leader", leader_address());
	if (!result) {
		puts_rejected_.fetch_add(1, std::memory_order_relaxed);
		try { if (options_.on_reject) options_.on_reject(result.error().reason); } catch (...) { /* Diagnostics are best effort. */ }
		response.send(status_for(result.error().reason),
		              "{\"error\":\"" + json_escape(result.error().detail) + "\"}\n", "application/json");
		co_return;
	}
	response.send(200, "{\"accepted_version\":" + std::to_string(result.value()) + "}\n",
	              "application/json");
}

task::task<void> Distributor::route_get_snapshot(http::Request& request, http::ResponseWriter& response) {
	const auto published = published_.load();
	if (!published) {
		response.send(404, "no snapshot published yet\n");
		co_return;
	}
	const auto& current = *published;

	response.set_header("X-Snapshot-Version", std::to_string(current.version));
	// Set our own ETag and do the If-None-Match comparison ourselves, using the value computed once
	// per update: the library checks has_header("ETag") first and skips its own whole-body hash.
	response.set_header("ETag", current.etag);
	auto inm = request.header("If-None-Match");
	if (!inm.empty() && http::if_none_match(inm, current.etag)) {
		response.send(304, ""); // no body copy at all for the common unchanged-poller case
		co_return;
	}

	// Only reached on an actual body-changed response. A poller that tells us its own last-known
	// version via X-Detent-Base-Version gets a diff instead of the full body when possible: the
	// cached single-step diff if that's exactly one update behind (no work per request), else a diff
	// composed from the history if it's within that bound, else the full body. Only the negotiated
	// encoding is copied out.
	std::string diff_bytes, diff_gzip, diff_zstd;
	std::uint64_t diff_base_version = 0;
	bool serve_diff = false;

	auto base_version_header = request.header("X-Detent-Base-Version");
	std::optional<std::uint64_t> requested_base_version;
	if (!base_version_header.empty()) {
		try {
			requested_base_version = std::stoull(std::string(base_version_header));
		} catch (const std::exception&) {
			// Malformed header: treat as absent, fall back to the full body.
		}
	}

	if (requested_base_version && current.has_diff && *requested_base_version == current.diff_base_version) {
		diff_bytes = current.diff_bytes;
		diff_gzip = current.diff_gzip;
		diff_zstd = current.diff_zstd;
		diff_base_version = current.diff_base_version;
		serve_diff = true;
	} else if (requested_base_version && !current.history.empty() &&
	           *requested_base_version >= current.history.front()->base_version &&
	           *requested_base_version < current.version) {
		// Every poller still stuck at this same base_version before the next accepted update needs
		// the identical composed result: check the cache before redoing the compose_patches work.
		auto lookup = current.composed.get_with_generation(*requested_base_version);
		if (lookup.value) {
			diff_bytes = lookup.value->bytes;
			diff_gzip = lookup.value->bytes_gzip;
			diff_zstd = lookup.value->bytes_zstd;
			diff_base_version = *requested_base_version;
			serve_diff = true;
		} else {
			auto it = std::find_if(current.history.begin(), current.history.end(),
			                       [&](const auto& step) { return step->base_version == *requested_base_version; });
			if (it != current.history.end()) {
				auto acc = (*it)->patch;
				for (auto next = std::next(it); next != current.history.end(); ++next) {
					acc = detent::compose_patches(acc, (*next)->patch);
				}
				diffs_composed_.fetch_add(1, std::memory_order_relaxed);
				diff_bytes = detent::serialize_patch(acc);
				compress_into(diff_bytes, diff_gzip, diff_zstd);
				diff_base_version = *requested_base_version;
				serve_diff = true;
				current.composed.try_insert(diff_base_version, Published::ComposedDiff{diff_bytes, diff_gzip, diff_zstd},
				                            lookup.generation);
			}
		}
	}

	if (serve_diff) {
		diffs_served_.fetch_add(1, std::memory_order_relaxed);
		response.set_header("X-Patch-Base-Version", std::to_string(diff_base_version));
		write_negotiated(request, response, 200, diff_bytes, diff_gzip, diff_zstd, "application/merge-patch+json");
		co_return;
	}
	write_negotiated(request, response, 200, current.bytes, current.bytes_gzip, current.bytes_zstd, "application/json");
}

task::task<void> Distributor::route_get_health(http::Request&, http::ResponseWriter& response) {
	auto uptime =
		std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start_time_).count();
	const auto published = published_.load();
	const bool has = static_cast<bool>(published);
	const std::uint64_t version = published ? published->version : 0;
	std::ostringstream ss;
	ss << "{"
	   << "\"has_snapshot\":" << (has ? "true" : "false") << ","
	   << "\"current_version\":" << version << ","
	   << "\"requests_served\":" << requests_served_.load() << ","
	   << "\"puts_accepted\":" << puts_accepted_.load() << ","
	   << "\"puts_rejected\":" << puts_rejected_.load() << ","
	   << "\"uptime_seconds\":" << uptime;
	if (fenced_.load(std::memory_order_acquire)) ss << ",\"fenced\":true";
	if (options_.cluster) {
		ss << ",\"clustered\":true"
		   << ",\"is_leader\":" << (is_leader() ? "true" : "false")
		   << ",\"leader_address\":\"" << json_escape(leader_address()) << "\"";
	} else {
		ss << ",\"clustered\":false";
	}
	ss << "}\n";
	response.send(200, ss.str(), "application/json");
	co_return;
}

} // namespace detentd
