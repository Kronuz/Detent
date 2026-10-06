#include "node.hh"
#include <limits>
#include <sha256.h>
#include <stdexcept>

namespace detent::authority {
using namespace cluster::consensus;
namespace {
Digest batch_identity(std::string_view bytes) {
	SHA256 hash;
	hash.add(bytes.data(), bytes.size());
	Digest result{};
	hash.getHash(result.data());
	return result;
}
std::shared_ptr<kronuz::journal::IO> checked_io(std::unique_ptr<kronuz::journal::IO> io) {
	if (!io) {
		throw std::invalid_argument("missing authority IO");
	}
	return std::shared_ptr<kronuz::journal::IO>(std::move(io));
}
NodeOptions checked(NodeOptions o) {
	if (o.configuration.cluster != o.context.cluster || !o.waiters || o.waiters > 65536 ||
		o.consensus.command_bytes < o.application.request_bytes + 104 ||
		o.storage.replacement_pool.logical_bytes <
			o.application.image_bytes + checkpoint_detail::maximum_size(o.consensus) + 4096 ||
		o.storage.replacement_pool.entries < 4) {
		throw std::invalid_argument("authority runtime cannot fund configured limits");
	}
	return o;
}
} // namespace
Node::Node(std::unique_ptr<kronuz::journal::IO> io, NodeOptions options)
	: io_(checked_io(std::move(io))), options_(checked(std::move(options))),
	  state_(options_.context, options_.application),
	  worker_(io_, options_.configuration, options_.storage, options_.consensus, options_.timing,
			  options_.scheduling, SnapshotLimits{0x44544101, options_.application.image_bytes}),
	  channel_(worker_) {
	completions_.reserve(options_.waiters);
}
void Node::create(Identity id) { worker_.create(id); }
void Node::recover() {
	worker_.recover([&](auto &reader) {
		auto size = reader.descriptor().length;
		if (size > options_.application.image_bytes) {
			throw std::length_error("authority recovery image bound");
		}
		ImageDecoder decoder(options_.context, options_.application);
		std::array<char, 65536> bytes{};
		std::size_t offset = 0;
		while (!decoder.finished()) {
			if (decoder.needs_input()) {
				auto count = reader.read_at(
					offset,
					std::span<char>(bytes.data(), std::min<std::size_t>(bytes.size(), size - offset)));
				if (!count && offset != size)
					throw std::runtime_error("short authority image");
				offset += count;
				decoder.feed({bytes.data(), count}, offset == size);
			}
			decoder.step();
		}
		state_ = decoder.take();
	});
	auto boundary = worker_.base_boundary();
	if (state_.applied_index() != boundary.index || state_.applied_term() != boundary.term) {
		throw std::runtime_error("recovered application boundary mismatch");
	}
}
void Node::start() {
	if (worker_.try_submit(Start{}) != SubmitResult::Accepted) {
		throw std::logic_error("authority not ready to start");
	}
	drain();
}
SubmitResult Node::propose(std::uint64_t request, const std::vector<Command> &commands) {
	if (!request || request >= (std::uint64_t{1} << 63) || pending_.contains(request)) {
		throw std::invalid_argument("duplicate local authority waiter");
	}
	if (pending_.size() + completions_.size() >= options_.waiters) {
		return SubmitResult::Pressure;
	}
	for (const auto &c : commands) {
		if (c.context != options_.context) {
			throw std::invalid_argument("foreign authority request");
		}
	}
	auto bytes = encode_batch(commands, options_.application, options_.consensus.command_bytes);
	auto identity = batch_identity(bytes);
	auto result = worker_.try_submit(Propose{request, std::move(bytes)});
	if (result == SubmitResult::Accepted) {
		pending_.emplace(request, Pending{0, worker_.term(), identity, false});
	}
	drain();
	return result;
}
SubmitResult Node::read(std::uint64_t request) {
	if (!request || request >= (std::uint64_t{1} << 63) || pending_.contains(request)) {
		throw std::invalid_argument("duplicate local authority waiter");
	}
	if (pending_.size() + completions_.size() >= options_.waiters) {
		return SubmitResult::Pressure;
	}
	auto result = worker_.try_submit(Read{request});
	if (result == SubmitResult::Accepted) {
		pending_.emplace(request, Pending{.read = true});
	}
	drain();
	return result;
}
void Node::forget(std::uint64_t request) { pending_.erase(request); }
std::vector<Completion> Node::take_completions() {
	auto result = std::move(completions_);
	completions_.clear();
	return result;
}
Actions Node::take_network_actions() {
	auto result = std::move(network_);
	network_.clear();
	return result;
}
void Node::drain() {
	if (!network_.empty()) {
		return;
	} // Host must consume its bounded action batch.
	auto actions = worker_.take_actions();
	for (auto &action : actions) {
		if (auto placed = std::get_if<ProposalPlaced>(&action)) {
			if (auto found = pending_.find(placed->request); found != pending_.end()) {
				found->second.index = placed->index;
				found->second.term = placed->term;
			}
		} else if (auto committed = std::get_if<Committed>(&action)) {
			if (application_ || committed->entries.empty()) {
				throw std::logic_error("authority overlapping or empty application range");
			}
			auto through = committed->entries.back().index;
			application_.emplace(Application{std::move(*committed), 0, through});
		} else if (auto role = std::get_if<RoleChanged>(&action)) {
			for (auto it = pending_.begin(); it != pending_.end();) {
				if (!it->second.read && (role->role != Role::Leader || it->second.term != role->term)) {
					completions_.push_back({it->first, it->second.index, {}, RejectReason::NotLeader, false});
					it = pending_.erase(it);
				} else {
					++it;
				}
			}
			network_.push_back(std::move(action));
		} else if (auto read = std::get_if<ReadReady>(&action)) {
			if (read->index > state_.applied_index()) {
				throw std::logic_error("authority quorum read precedes application boundary");
			}
			if (pending_.erase(read->request)) {
				completions_.push_back({read->request, read->index, {}, {}, true});
			}
		} else if (auto rejected = std::get_if<Reject>(&action)) {
			if (capture_ && rejected->request == checkpoint_request_) {
				capture_.reset();
			}
			if (pending_.erase(rejected->request)) {
				completions_.push_back({rejected->request, 0, {}, rejected->reason, false});
			}
		} else if (auto published = std::get_if<CheckpointPublished>(&action)) {
			if (capture_ && published->through == capture_->state.applied_index()) {
				capture_.reset();
			}
		} else {
			network_.push_back(std::move(action));
		}
	}
}
void Node::advance_application() {
	if (!application_ || worker_.fenced())
		return;
	const auto through = application_->through;
	try {
		auto &entry = application_->range.entries[application_->cursor];
		if (entry.index > state_.applied_index()) {
			if (entry.index != state_.applied_index() + 1) {
				throw std::runtime_error("authority application range is not contiguous");
			}
			std::optional<State> next;
			std::vector<Completion> ready;
			if (entry.kind == EntryKind::NoOp) {
				next.emplace(state_);
				next->advance(entry.index, entry.term);
			} else {
				if (!application_->decoder) {
					application_->decoder.emplace(entry.payload, options_.application,
												  options_.consensus.command_bytes);
					application_->candidate.emplace(state_, entry.index, entry.term,
													application_->decoder->count());
				}
				std::size_t commands = 0, bytes = 0, mutations = 0;
				while (!application_->decoder->done() && commands < 8) {
					auto work = application_->decoder->next_work();
					if (commands && (bytes >= 65536 || mutations >= 128 || work.bytes > 65536 - bytes ||
									 work.mutations > 128 - mutations))
						break;
					// One oversized configured command may run on a fresh turn.
					application_->candidate->advance(application_->decoder->next());
					++commands;
					bytes += work.bytes;
					mutations += work.mutations;
				}
				if (!application_->decoder->done())
					return;
				auto outcome = application_->candidate->finish();
				application_->decoder.reset();
				application_->candidate.reset();
				next.emplace(std::move(outcome.state));
				auto &results = outcome.results;
				std::optional<Digest> identity;
				for (const auto &[request, waiter] : pending_) {
					if (!waiter.read && waiter.index == entry.index) {
						if (!identity)
							identity = batch_identity(entry.payload);
						if (waiter.term == entry.term && waiter.identity == *identity) {
							ready.push_back({request, entry.index, results, {}, false});
						} else {
							ready.push_back({request, entry.index, {}, RejectReason::NotLeader, false});
						}
					}
				}
			}
			state_ = std::move(*next);
			for (auto &result : ready) {
				pending_.erase(result.request);
				completions_.push_back(std::move(result));
			}
		}
		// Release payloads one entry at a time, preserving the original endpoint.
		std::string{}.swap(entry.payload);
		if (++application_->cursor == application_->range.entries.size()) {
			application_.reset();
			worker_.applied(Applied{through});
		}
	} catch (const std::exception &error) {
		worker_.application_failed(through, error.what());
		application_.reset();
	}
}
bool Node::checkpoint() {
	if (application_ || capture_ || !state_.applied_index() ||
		worker_.applied_index() != state_.applied_index()) {
		return false;
	}
	auto id = worker_.reserve_checkpoint(options_.application.image_bytes);
	if (!id) {
		return false;
	}
	if (checkpoint_request_ == std::numeric_limits<std::uint64_t>::max()) {
		worker_.cancel_checkpoint(*id);
		throw std::overflow_error("authority checkpoint identity exhausted");
	}
	try {
		capture_.emplace(Capture{*id, state_, ImageEncoder(state_), {}});
		worker_.attach_capture(*id, {++checkpoint_request_, state_.applied_index(), state_.applied_term()});
	} catch (...) {
		worker_.cancel_checkpoint(*id);
		capture_.reset();
		throw;
	}
	return true;
}
void Node::discard_candidate() {
	if (!candidate_)
		return;
	if (retired_)
		throw std::runtime_error("authority retired-root capacity exhausted");
	if (candidate_->state) {
		retired_.emplace(std::move(*candidate_->state));
		retired_->retire();
	} else
		retired_.emplace(candidate_->decoder.discard());
	candidate_.reset();
}
void Node::service(Tick tick) {
	state_.collect(128);
	if (retired_) {
		retired_->collect(128);
		if (!retired_->reclamation_pending()) {
			retired_.reset();
		}
	}
	worker_.run_one(tick);
	drain();
	advance_application();
	channel_.run_one(tick);
	if (candidate_ && channel_.incoming_snapshot() != candidate_->id) {
		discard_candidate();
	}
	if (capture_ && !capture_->final) {
		if (capture_->chunk.empty() && !capture_->encoder.finished()) {
			capture_->chunk = capture_->encoder.next();
		}
		auto final = capture_->encoder.finished();
		if (worker_.offer_application_chunk(capture_->id, capture_->chunk, final) == SubmitResult::Accepted) {
			capture_->chunk.clear();
			capture_->final = final;
		}
	}
	if (auto view = worker_.validation_chunk();
		view && !retired_ && (!candidate_ || candidate_->decoder.needs_input())) {
		if (!candidate_ || candidate_->id != view->snapshot) {
			candidate_.emplace(
				Candidate{view->snapshot, ImageDecoder(options_.context, options_.application), 0, {}, {}});
		}
		auto id = view->snapshot;
		try {
			if (view->offset != candidate_->offset)
				throw std::invalid_argument("snapshot image offset mismatch");
			candidate_->decoder.feed({view->bytes.data(), view->bytes.size()}, view->verified_eof);
			candidate_->offset += view->bytes.size();
			if (view->verified_eof)
				candidate_->final_chunk = view->chunk;
			worker_.consume_validation(id, view->chunk);
		} catch (const std::invalid_argument &) {
			worker_.reject_snapshot_validation(id);
			discard_candidate();
		}
	}
	if (candidate_ && !candidate_->state) {
		auto id = candidate_->id;
		try {
			candidate_->decoder.step();
			if (candidate_->decoder.finished()) {
				candidate_->state = candidate_->decoder.take();
				auto context = worker_.snapshot_context(id);
				if (!context || candidate_->state->applied_index() != context->descriptor.through ||
					candidate_->state->applied_term() != context->descriptor.term ||
					!candidate_->final_chunk) {
					throw std::invalid_argument("snapshot semantic boundary mismatch");
				}
				worker_.validation_succeeded(id, *candidate_->final_chunk);
			}
		} catch (const std::invalid_argument &) {
			worker_.reject_snapshot_validation(id);
			discard_candidate();
		}
	}
	if (auto activation = worker_.snapshot_activation()) {
		if (application_ || !candidate_ || candidate_->id != activation->snapshot || !candidate_->state ||
			candidate_->state->applied_index() != activation->descriptor.through ||
			candidate_->state->applied_term() != activation->descriptor.term) {
			worker_.snapshot_activation_failed(activation->snapshot, activation->token);
		} else {
			if (retired_) {
				throw std::runtime_error("authority retired-root capacity exhausted");
			}
			retired_.emplace(std::move(state_));
			retired_->retire();
			state_ = std::move(*candidate_->state);
			worker_.snapshot_activated(activation->snapshot, activation->token);
			candidate_.reset();
		}
	}
	if (worker_.ready() && worker_.base_index() > state_.applied_index()) {
		auto context =
			candidate_ && candidate_->state ? worker_.snapshot_context(candidate_->id) : std::nullopt;
		if (!context || context->descriptor.through != worker_.base_boundary().index ||
			context->descriptor.term != worker_.base_boundary().term ||
			candidate_->state->applied_index() != context->descriptor.through ||
			candidate_->state->applied_term() != context->descriptor.term) {
			throw std::runtime_error("application image boundary mismatch");
		}
	}
	if (candidate_ && !channel_.incoming_snapshot()) {
		discard_candidate();
	}
	if (worker_.fenced()) {
		capture_.reset();
		discard_candidate();
	} else if (!capture_ && !candidate_ &&
			   (worker_.committed() - worker_.base_index() >= options_.consensus.log_entries / 2 ||
				worker_.retained_log_bytes() >= options_.consensus.log_bytes / 2)) {
		checkpoint();
	}
}
} // namespace detent::authority
