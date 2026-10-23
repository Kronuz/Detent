#include "node.hh"
#include "prepared.hh"
#include <limits>
#include <sha256.h>
#include <stdexcept>

namespace detent::authority {
using namespace kronuz::consensus;
namespace {
std::shared_ptr<kronuz::journal::IO> checked_io(std::unique_ptr<kronuz::journal::IO> io) {
	if (!io) {
		throw std::invalid_argument("missing authority IO");
	}
	return std::shared_ptr<kronuz::journal::IO>(std::move(io));
}
NodeOptions checked(NodeOptions o) {
	validate_node_options(o);
	return o;
}
} // namespace
void validate_node_options(const NodeOptions &o) {
	const auto checkpoint = checkpoint_detail::maximum_size(o.consensus);
	const auto replacement = o.storage.replacement_pool.logical_bytes;
	if (o.configuration.cluster != o.context.cluster || !o.waiters || o.waiters > 65536 ||
		o.consensus.command_bytes < 104 ||
		o.application.request_bytes > o.consensus.command_bytes - 104 ||
		o.application.image_bytes > replacement || checkpoint > replacement - o.application.image_bytes ||
		4096 > replacement - o.application.image_bytes - checkpoint ||
		o.storage.replacement_pool.entries < 4) {
		throw std::invalid_argument("authority runtime cannot fund configured limits");
	}
}

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
		if (recorder_) recorder_->reset(state_.context(),state_.revision());
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
void Node::attach_change_recorder(std::shared_ptr<AppliedChangeRecorder> recorder) {
 if (closing_ || application_ || recorder_) throw std::logic_error("authority change recorder unavailable");
 if (!recorder) throw std::invalid_argument("missing authority change recorder");
 recorder_=std::move(recorder); recorder_->reset(state_.context(),state_.revision());
}
void Node::close() noexcept {
 if (!closing_ && recorder_) { recorder_->abandon_batch(); recorder_->reset(state_.context(),state_.revision()); }
	closing_ = true;
	worker_.close();
}
bool Node::drained() const noexcept {
	return closing_ && state_retired_ && worker_.drained() && !application_ && !capture_ && !candidate_ &&
		   !retired_ && !state_.reclamation_pending() && (!recorder_ || !recorder_->pending()) && pending_.empty() && completions_.empty() &&
		   network_.empty() && channel_.drained();
}
void Node::retirement_step(CommandWorkBudget &budget) {
	if (!closing_)
		throw std::logic_error("authority retirement requires terminal close");
	worker_.run_one({0, options_.timing.election_min});
	channel_.run_one({0, options_.timing.election_min});
	if (application_) {
		application_->failed = true;
		advance_application(budget);
	}
	if (retired_) {
		retired_->collect(budget);
		if (!retired_->reclamation_pending())
			retired_.reset();
	}
	if (capture_ && (capture_->state.shares_reclamation_with(state_) || !retired_ ||
					 capture_->state.shares_reclamation_with(*retired_))) {
		// Keep an arena owner after dropping the encoder's independent root.
		if (!capture_->state.shares_reclamation_with(state_) && !retired_) {
			retired_.emplace(std::move(capture_->state));
			retired_->retire();
		} else {
			capture_->state.retire();
		}
		capture_.reset();
	}
	if (candidate_ && !retired_)
		discard_candidate();
	if (!application_ && !capture_ && !candidate_ && !state_retired_) {
		state_.retire();
		state_retired_ = true;
	}
	state_.collect(budget);
	if (recorder_) recorder_->collect(budget);
	kronuz::consensus::detail::RetirementBudget payloads{8, options_.consensus.command_bytes};
	kronuz::consensus::detail::retire_actions(network_, payloads);
	unsigned local = 8;
	while (local && !completions_.empty()) {
		completions_.pop_back();
		--local;
	}
	while (local && !pending_.empty()) {
		pending_.erase(pending_.begin());
		--local;
	}
}
SubmitResult Node::propose(std::uint64_t request, const std::vector<Command> &commands) {
	if (closing_)
		return SubmitResult::Busy;
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
	if (commands.empty() || commands.size() > 64)
		throw std::invalid_argument("transaction group bound");
	ProposalBuilder builder(options_.context, options_.application, options_.consensus.command_bytes);
	for (const auto &command : commands) {
		CommandEncoder encoder(command, options_.application);
		while (!encoder.done()) {
			CommandWorkBudget budget;
			encoder.step(budget);
		}
		auto encoded = encoder.take_encoded();
		if (!builder.append(encoded))
			throw std::invalid_argument("authority encoding bound");
		while (builder.command_pending()) {
			CommandWorkBudget budget;
			builder.step(budget);
		}
	}
	builder.seal();
	while (!builder.done()) {
		CommandWorkBudget budget;
		builder.step(budget);
	}
	auto prepared = builder.take();
	return propose_prepared(request, prepared);
}
SubmitResult Node::propose_prepared(std::uint64_t request, PreparedProposal &proposal) {
	if (closing_)
		return SubmitResult::Busy;
	if (!request || request >= (std::uint64_t{1} << 63) || pending_.contains(request))
		throw std::invalid_argument("duplicate local authority waiter");
	if (!proposal.valid_ || proposal.context_ != options_.context ||
		proposal.limits_ != options_.application || proposal.bytes_.size() > options_.consensus.command_bytes)
		throw std::invalid_argument("foreign prepared proposal capability");
	if (pending_.size() + completions_.size() >= options_.waiters)
		return SubmitResult::Pressure;
	// Allocate waiter bookkeeping before Worker can irreversibly accept bytes.
	auto [waiter, inserted] = pending_.emplace(request, Pending{0, worker_.term(), proposal.digest_, false});
	(void)inserted;
	Event event = Propose{request, std::move(proposal.bytes_)};
	SubmitResult result;
	try {
		result = worker_.try_submit_retained(event);
	} catch (...) {
		proposal.bytes_ = std::move(std::get<Propose>(event).command);
		pending_.erase(waiter);
		throw;
	}
	if (result == SubmitResult::Busy || result == SubmitResult::Pressure) {
		proposal.bytes_ = std::move(std::get<Propose>(event).command);
		pending_.erase(waiter);
	} else {
		proposal.valid_ = false;
		if (result != SubmitResult::Accepted)
			pending_.erase(waiter);
	}
	drain();
	return result;
}

SubmitResult Node::read(std::uint64_t request) {
	if (closing_)
		return SubmitResult::Busy;
	if (!request || request >= (std::uint64_t{1} << 63) || pending_.contains(request)) {
		throw std::invalid_argument("duplicate local authority waiter");
	}
	if (pending_.size() + completions_.size() >= options_.waiters) {
		return SubmitResult::Pressure;
	}
	// Allocate local completion ownership before consensus accepts the read.
	// Once accepted, allocation failure must not lose its verified boundary.
	auto waiter = pending_.emplace(request, Pending{.read = true}).first;
	SubmitResult result;
	try {
		result = worker_.try_submit(Read{request});
	} catch (...) {
		pending_.erase(waiter);
		throw;
	}
	if (result != SubmitResult::Accepted)
		pending_.erase(waiter);
	drain();
	return result;
}
void Node::forget(std::uint64_t request) { pending_.erase(request); }
std::vector<Completion> Node::take_completions() {
	if (closing_)
		return {};
	auto result = std::move(completions_);
	completions_.clear();
	return result;
}
Actions Node::take_network_actions() {
	if (closing_)
		return {};
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
			if (auto found = pending_.find(rejected->request); found != pending_.end()) {
				const bool read = found->second.read;
				pending_.erase(found);
				completions_.push_back({rejected->request, 0, {}, rejected->reason, read});
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
void Node::advance_application(CommandWorkBudget &budget) {
	if (!application_)
		return;
	if (worker_.fenced())
		application_->failed = true;
	if (application_->failed) {
		if (recorder_) recorder_->abandon_batch();
		if (application_->command) {
			if (!application_->command->discard(budget))
				return;
			application_->command.reset();
		}
		if (application_->candidate) {
			if (!application_->candidate->discard(budget))
				return;
			application_->candidate.reset();
		}
		// Preserve borrowed payloads until both partial cursors have drained.
		application_->decoder.reset();
		std::string{}.swap(application_->range.entries[application_->cursor].payload);
		if (++application_->cursor == application_->range.entries.size())
			application_.reset();
		return;
	}
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
					if (recorder_) recorder_->begin_batch(state_.context(),entry.index,state_.revision());
					application_->decoder.emplace(entry.payload, options_.application,
												  options_.consensus.command_bytes);
					application_->candidate.emplace(state_, entry.index, entry.term,
													application_->decoder->count(),recorder_);
				}
				while (!application_->candidate->done()) {
					if (application_->candidate->command_pending()) {
						application_->candidate->step(budget);
						if (application_->candidate->command_pending())
							return;
					} else {
						if (!application_->command) {
							if (!budget.records || budget.bytes < 128)
								return;
							--budget.records;
							budget.bytes -= 128;
							application_->command.emplace(application_->decoder->next_bytes(),
														  options_.application, options_.context);
						}
						application_->command->step(budget);
						if (!application_->command->done())
							return;
						application_->candidate->start(application_->command->take());
						application_->command.reset();
					}
				}
				if (!application_->decoder->done())
					throw std::logic_error("authority batch exhaustion mismatch");
				const auto interested =
					std::any_of(pending_.begin(), pending_.end(), [&](const auto &waiter) {
						return !waiter.second.read && waiter.second.index == entry.index;
					});
				if (interested && !application_->identity) {
					auto amount = std::min(budget.bytes, entry.payload.size() - application_->hashed);
					if (amount) {
						application_->hash.add(entry.payload.data() + application_->hashed, amount);
						application_->hashed += amount;
						budget.bytes -= amount;
					}
					if (application_->hashed != entry.payload.size() || budget.bytes < 128 || !budget.records)
						return;
					budget.bytes -= 128;
					--budget.records;
					application_->identity.emplace();
					application_->hash.getHash(application_->identity->data());
				}
				auto outcome = application_->candidate->finish();
				application_->decoder.reset();
				application_->candidate.reset();
				next.emplace(std::move(outcome.state));
				auto &results = outcome.results;
				for (const auto &[request, waiter] : pending_) {
					if (!waiter.read && waiter.index == entry.index) {
						if (waiter.term == entry.term && waiter.identity == *application_->identity) {
							ready.push_back({request, entry.index, results, {}, false});
						} else {
							ready.push_back({request, entry.index, {}, RejectReason::NotLeader, false});
						}
					}
				}
			}
			state_ = std::move(*next);
			if (recorder_) recorder_->publish(state_.context(),entry.index,state_.revision());
			for (auto &result : ready) {
				pending_.erase(result.request);
				completions_.push_back(std::move(result));
			}
		}
		// Release payloads one entry at a time, preserving the original endpoint.
		std::string{}.swap(entry.payload);
		application_->hash.reset();
		application_->hashed = 0;
		application_->identity.reset();
		if (++application_->cursor == application_->range.entries.size()) {
			application_.reset();
			worker_.applied(Applied{through});
		}
	} catch (const std::exception &error) {
		worker_.application_failed(through, error.what());
		application_->failed = true;
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

void Node::refresh_checkpoint_due(Tick tick) {
	const auto base = worker_.base_index();
	if (base > checkpoint_policy_.published_boundary) {
		checkpoint_policy_.published_boundary = base;
		checkpoint_policy_.due = false;
		checkpoint_policy_.due_since = 0;
		checkpoint_attempt_.reset();
	}
	const bool threshold =
		worker_.committed() - base >= std::max<std::size_t>(1, options_.consensus.log_entries / 2) ||
		worker_.retained_log_bytes() >= std::max<std::size_t>(1, options_.consensus.log_bytes / 2);
	if (!checkpoint_policy_.due && threshold && state_.applied_index() > base) {
		checkpoint_policy_.due = true;
		checkpoint_policy_.due_since = tick.now;
	}
}
void Node::service_checkpoint_due(Tick tick) {
	if (!checkpoint_policy_.due || worker_.fenced() || capture_ || candidate_ || application_ ||
		state_.applied_index() <= worker_.base_index() || worker_.applied_index() != state_.applied_index())
		return;
	if (checkpoint_attempt_ && (tick.now < *checkpoint_attempt_ || tick.now - *checkpoint_attempt_ < 100))
		return;
	checkpoint_attempt_ = tick.now;
	checkpoint_policy_.attempts.add();
	if (checkpoint())
		checkpoint_policy_.captured_boundary = state_.applied_index();
	else
		checkpoint_policy_.reservation_unavailable.add();
}
void Node::service(Tick tick) {
	CommandWorkBudget budget;
	service(tick, budget);
}
void Node::service(Tick tick, CommandWorkBudget &application_budget) {
	if (closing_) {
		retirement_step(application_budget);
		return;
	}
	CommandWorkBudget reclamation_budget;
	reclamation_budget.bytes = std::min<std::size_t>(4096, application_budget.bytes / 2);
	reclamation_budget.receipts = std::min<std::size_t>(16, application_budget.receipts / 2);
	const auto reserved_bytes = reclamation_budget.bytes;
	const auto reserved_receipts = reclamation_budget.receipts;
	state_.collect(reclamation_budget);
	if (recorder_) recorder_->collect(reclamation_budget);
	if (retired_) {
		retired_->collect(reclamation_budget);
		if (!retired_->reclamation_pending()) {
			retired_.reset();
		}
	}
	application_budget.bytes -= reserved_bytes - reclamation_budget.bytes;
	application_budget.receipts -= reserved_receipts - reclamation_budget.receipts;
	refresh_checkpoint_due(tick);
	worker_.run_one(tick);
	refresh_checkpoint_due(tick);
	// Applied may deliver the next range. Capture the acknowledged root before
	// drain() occupies its application slot with that new range.
	service_checkpoint_due(tick);
	drain();
	advance_application(application_budget);
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
			if (recorder_) recorder_->reset(state_.context(),state_.revision());
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
	} else {
		refresh_checkpoint_due(tick);
		service_checkpoint_due(tick);
	}
}
} // namespace detent::authority
