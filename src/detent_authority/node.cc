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
std::unique_ptr<kronuz::journal::IO> checked_io(std::unique_ptr<kronuz::journal::IO> io) {
	if (!io) {
		throw std::invalid_argument("missing authority IO");
	}
	return io;
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
	  worker_(*io_, options_.configuration, options_.storage, options_.consensus, options_.timing, {},
			  SnapshotLimits{0x44544101, options_.application.image_bytes}),
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
			try {
				State next = state_;
				std::vector<Completion> ready;
				for (const auto &entry : committed->entries) {
					if (entry.index <= next.applied_index()) {
						continue;
					}
					if (entry.kind == EntryKind::NoOp) {
						next.advance(entry.index, entry.term);
						continue;
					}
					auto results = next.apply_batch(
						entry.index,
						decode_batch(entry.payload, options_.application, options_.consensus.command_bytes),
						entry.term);
					for (const auto &[request, waiter] : pending_) {
						if (!waiter.read && waiter.index == entry.index) {
							if (waiter.term == entry.term &&
								waiter.identity == batch_identity(entry.payload)) {
								ready.push_back({request, entry.index, results, {}, false});
							} else {
								ready.push_back({request, entry.index, {}, RejectReason::NotLeader, false});
							}
						}
					}
				}
				state_ = std::move(next);
				for (auto &result : ready) {
					pending_.erase(result.request);
					completions_.push_back(std::move(result));
				}
				worker_.applied(Applied{committed->entries.back().index});
			} catch (const std::exception &error) {
				worker_.application_failed(committed->entries.back().index, error.what());
			}
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
bool Node::checkpoint() {
	if (capture_ || !state_.applied_index() || worker_.applied_index() != state_.applied_index()) {
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
		if (!candidate_ || candidate_->id != activation->snapshot || !candidate_->state ||
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
