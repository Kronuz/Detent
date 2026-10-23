#include "paged_limits.hh"
#include "paged_segment_collection.hh"
#include "detent_local/record_reader.hh"
#include "names.hh"
#include "owned_segment_inputs.hh"
#include "paged_named_root.hh"
#include "paged_segment_marks.hh"
#include "paged_segment_read.hh"
#include "segment_names.hh"
#include "store_core.hh"
#include <system_error>
namespace detent::local::detail {
namespace c = kronuz::io::completion;
namespace d = distribution;
namespace {
struct CollectionProof final : d::RetirementNode {
	std::optional<OwnedSegmentInputs> inputs;
	std::optional<PagedDependencyWalk> walk;
	std::optional<GraphValidator> graph;
	bool retire_step() noexcept override {
		ValidationBudget budget;
		if (graph) {
			if (!graph->discard(budget))
				return false;
			graph.reset();
		}
		if (walk) {
			if (!walk->discard(budget))
				return false;
			walk.reset();
		}
		inputs.reset();
		return true;
	}
};
} // namespace
struct PagedSegmentCollection::State final : d::RetirementNode {
	enum class Phase {
		Done,
		Scan,
		Next,
		Root,
		Walk,
		Pack,
		Insert,
		Graph,
		Certificate,
		DiscardGraph,
		DiscardWalk,
		Mark,
		CloseRoot,
		CloseMarkCursor,
		SweepScan,
		SweepNext,
		Query,
		Candidate,
		Rejected,
		OwnerOpen,
		OwnerRecord,
		OwnerAbsent,
		OwnerNamed,
		OwnerHeld,
		OwnerClose,
		InspectPack,
		InspectOwner,
		HeldOwner,
		RemovePack,
		PackBarrier,
		RemoveOwner,
		OwnerBarrier,
		CloseCandidate,
		CloseCursor,
		Paused
	};
	d::RetirementFactory factory;
	std::shared_ptr<StoreCore> core;
	std::shared_ptr<StorageSession> session;
	std::shared_ptr<CollectorControls> controls;
	CollectorControls::Claim claim;
	d::RetirementHandle<PagedSegmentMarks> marks;
	d::RetirementHandle<PagedDependencyCertificate> certificate;
	std::optional<PagedNamedRoot> root;
	std::optional<PagedSegmentRead> pack;
	d::RetirementHandle<CollectionProof> proof;
	std::optional<Descriptor> selected;
	std::optional<SegmentNames> names;
	StorageIdentity sought{};
	std::array<char, 256> name{};
	c::Token token;
	Phase phase = Phase::Done;
	std::exception_ptr failure;
	std::size_t inspected = 0;
	std::uint64_t deleting_length = 0;
	bool submitted = false, cursor_open = false, stopping = false, selected_seen = false, destructive = false,
		 barriers_done = false;
	std::optional<c::EntryInspection> owner_identity;
	RecordReader owner_reader;
	bool owner_open = false, owner_only = false;
	State(d::RetirementFactory factory, std::shared_ptr<StoreCore> core)
		: factory(std::move(factory)), core(std::move(core)), session(this->core->session),
		  controls(this->core->controls), token(session->next_operation()) {}
	bool retire_step() noexcept override {
		proof.reset();
		pack.reset();
		root.reset();
		certificate.reset();
		marks.reset();
		if (owner_open) {
			owner_open = false;
			try {
				controls->scratch_file().close();
			} catch (...) {
			}
			return false;
		}
		if (cursor_open) {
			cursor_open = false;
			try {
				controls->cursor().close();
			} catch (...) {
			}
		}
		if (claim.serial) {
			try {
				controls->release(claim);
			} catch (...) {
			}
			claim = {};
		}
		return true;
	}
	c::Operation *child() {
		if (phase == Phase::Root || phase == Phase::CloseRoot)
			return root ? &*root : nullptr;
		if (phase == Phase::Pack || phase == Phase::Insert || phase == Phase::Candidate ||
			phase == Phase::CloseCandidate || phase == Phase::Rejected)
			return pack ? &*pack : nullptr;
		return nullptr;
	}
};
PagedSegmentCollection::PagedSegmentCollection(d::RetirementFactory factory,
											   std::shared_ptr<StoreCore> core) {
	state_ = factory.make<State>(factory, core);
	using Handle = d::RetirementHandle<State>;
	pin_ = std::allocate_shared<Handle>(d::StorageAllocator<Handle>(core->account), state_);
}
PagedSegmentCollection::~PagedSegmentCollection() = default;
const c::Request &PagedSegmentCollection::request() const {
	auto *child = state_->child();
	return child ? child->request() : request_;
}
c::IO &PagedSegmentCollection::io() const noexcept { return *state_->session->backend(); }
bool PagedSegmentCollection::done() const noexcept { return state_->phase == State::Phase::Done; }
bool PagedSegmentCollection::in_flight() const noexcept { return state_->submitted; }
bool PagedSegmentCollection::paused() const noexcept { return state_->phase == State::Phase::Paused; }
std::exception_ptr PagedSegmentCollection::error() const noexcept { return state_->failure; }
void PagedSegmentCollection::prepare(c::Kind kind) {
	auto &s = *state_;
	request_ = {};
	if (s.token.step == std::numeric_limits<std::uint64_t>::max())
		throw std::overflow_error("segment collection token exhausted");
	++s.token.step;
	request_.token = s.token;
	request_.kind = kind;
	if (kind == c::Kind::ScanInto || kind == c::Kind::NextInto || kind == c::Kind::CloseCursor)
		request_.cursor = std::shared_ptr<c::DirectoryCursor>(pin_, &s.controls->cursor());
	if (kind == c::Kind::NextInto)
		request_.destination_bytes = s.name;
	if (kind == c::Kind::OpenCandidateInto || kind == c::Kind::InspectEntry || kind == c::Kind::Remove)
		request_.source = s.phase == State::Phase::InspectOwner || s.phase == State::Phase::RemoveOwner ||
								  s.phase == State::Phase::OwnerOpen || s.phase == State::Phase::OwnerNamed
							  ? s.names->owner_name()
							  : s.names->payload_name();
}
void PagedSegmentCollection::start() {
	using Phase = State::Phase;
	auto &s = *state_;
	if (!done() || s.submitted)
		throw std::logic_error("segment collection already active");
	if (s.core->selection.knowledge == SelectionKnowledge::Uncertain || s.core->active_generation ||
		s.core->pending_paged_recovery)
		return;
	s.marks = s.factory.make<PagedSegmentMarks>(s.core->account, s.core->directory_identity, s.core->identity,
												s.core->options.recovery_limits.segments);
	s.claim = s.controls->claim(CollectorUse::Reclaim);
	s.selected = s.core->selection.descriptor;
	s.selected_seen = !s.selected;
	s.inspected = 0;
	s.stopping = false;
	s.failure = {};
	s.phase = Phase::Scan;
	prepare(c::Kind::ScanInto);
}
void PagedSegmentCollection::submitted() {
	if (done() || paused() || needs_work() || in_flight())
		throw std::logic_error("invalid segment collection submission");
	if (auto *child = state_->child())
		child->submitted();
	if (state_->phase == State::Phase::OwnerRecord)
		state_->owner_reader.submitted();
	if (state_->phase == State::Phase::RemovePack || state_->phase == State::Phase::RemoveOwner)
		state_->destructive = true;
	state_->submitted = true;
}
void PagedSegmentCollection::next() {
	state_->phase = State::Phase::Next;
	prepare(c::Kind::NextInto);
}
void PagedSegmentCollection::cleanup() {
	using Phase = State::Phase;
	auto &s = *state_;
	s.stopping = true;
	if (s.root) {
		s.root->close();
		if (!s.root->done()) {
			s.phase = Phase::CloseRoot;
			return;
		}
		s.root.reset();
	}
	if (s.pack) {
		s.pack->close();
		if (!s.pack->done()) {
			s.phase = Phase::CloseCandidate;
			return;
		}
		s.pack.reset();
	}
	s.proof.reset();
	s.certificate.reset();
	s.marks.reset();
	if (s.owner_open) {
		s.phase = Phase::OwnerClose;
		prepare(c::Kind::CloseFile);
		request_.file = std::shared_ptr<c::File>(pin_, &s.controls->scratch_file());
		return;
	}
	if (s.cursor_open) {
		s.phase = Phase::CloseCursor;
		prepare(c::Kind::CloseCursor);
		return;
	}
	if (s.claim.serial)
		s.controls->release(s.claim);
	s.claim = {};
	s.phase = Phase::Done;
}
void PagedSegmentCollection::fail(std::exception_ptr error) noexcept {
	auto &s = *state_;
	if (!s.failure)
		s.failure = error;
	if (s.destructive)
		s.core->fence(error);
	else {
		try {
			std::rethrow_exception(error);
		} catch (const d::StoragePressure &) {
			s.core->drop_certificates();
		} catch (const std::bad_alloc &) {
			s.core->drop_certificates();
		} catch (...) {
		}
	}

	try {
		cleanup();
	} catch (...) {
		s.core->fence(std::current_exception());
		s.phase = State::Phase::Done;
	}
}
void PagedSegmentCollection::close() noexcept {
	state_->stopping = true;
	if (!state_->destructive) {
		if (state_->root)
			state_->root->close();
		if (state_->pack)
			state_->pack->close();
	}
	if (!in_flight() && !state_->destructive) {
		try {
			cleanup();
		} catch (...) {
			fail(std::current_exception());
		}
	}
}
void PagedSegmentCollection::invalidate() noexcept { close(); }
void PagedSegmentCollection::resume() {
	if (paused()) {
		state_->phase = State::Phase::SweepNext;
		prepare(c::Kind::NextInto);
	}
}
void PagedSegmentCollection::advance() {
	using Phase = State::Phase;
	auto &s = *state_;
	if (in_flight() || done())
		return;
	try {
		if (s.stopping && !s.destructive) {
			cleanup();
			return;
		}
		if (paused() || needs_work())
			return;
		if (s.phase == Phase::Root && s.root->ready()) {
			if (s.selected && s.root->descriptor().generation == s.selected->generation) {
				if (s.root->legacy_anchor() ? (!s.core->verified || *s.core->verified != *s.selected ||
											   s.root->descriptor().length != s.selected->length ||
											   s.root->descriptor().cursor != s.selected->cursor ||
											   s.root->descriptor().profile != s.selected->profile)
											: s.root->descriptor() != *s.selected)
					throw std::runtime_error("selected root changed during marking");
				s.selected_seen = true;
			}
			if (!s.root->paged()) {
				s.root->close();
				s.phase = Phase::CloseRoot;
				return;
			}
			s.certificate = s.root->certificate();
			if (!s.certificate && s.core->verified_paged &&
				s.core->verified_paged->descriptor() == s.root->descriptor()) {
				s.certificate = s.factory.make<PagedDependencyCertificate>(s.core->account, s.factory);
				s.certificate->begin_recovered(*s.core->verified_paged);
				s.phase = Phase::Certificate;
				return;
			}
			if (s.certificate) {
				s.marks->begin(s.certificate);
				s.phase = Phase::Mark;
				return;
			}
			const auto &root = s.root->root();
			s.proof = s.factory.make<CollectionProof>();
			s.proof->inputs.emplace(s.factory, s.core->account, s.core->identity, root.segments,
									s.core->options.recovery_limits.bytes);
			s.proof->walk.emplace(s.core->account, s.proof->inputs->owner(), OwnedSegmentInputs::lookup, root,
								  s.core->identity, paged_limits(s.core->options.recovery_limits));
			s.phase = Phase::Walk;
		} else if (s.phase == Phase::CloseRoot && s.root->done()) {
			if (s.root->error())
				std::rethrow_exception(s.root->error());
			s.root.reset();
			s.certificate.reset();
			if (s.stopping)
				cleanup();
			else
				next();
		} else if (s.phase == Phase::Insert && !s.proof->inputs->busy() && s.pack->done()) {
			if (s.pack->error())
				std::rethrow_exception(s.pack->error());
			s.pack.reset();
			s.phase = Phase::Walk;
		} else if (s.phase == Phase::Pack && s.pack->ready()) {
			s.proof->inputs->begin(s.pack->ownership(), s.pack->bytes());
			s.pack->close();
			s.phase = Phase::Insert;
		} else if (s.phase == Phase::Candidate && s.pack->ready()) {
			s.deleting_length = s.pack->ownership().length;
			s.names.emplace(s.pack->ownership().segment);
			s.phase = Phase::InspectPack;
			prepare(c::Kind::InspectEntry);
		} else if (s.phase == Phase::Rejected && s.pack->done()) {
			if (s.pack->closure_error())
				std::rethrow_exception(s.pack->closure_error());
			s.pack.reset();
			s.names.reset();
			s.phase = Phase::Paused;
		} else if (s.phase == Phase::CloseCandidate && s.pack->done()) {
			if (s.pack->error())
				std::rethrow_exception(s.pack->error());
			if (s.destructive && s.barriers_done && !s.failure) {
				s.core->ledger.reclaimed({s.deleting_length + segment_ownership_bytes, 2, 0});
				s.destructive = false;
				s.barriers_done = false;
			}
			s.pack.reset();
			s.names.reset();
			if (s.stopping)
				cleanup();
			else
				s.phase = Phase::Paused;
		}
	} catch (...) {
		fail(std::current_exception());
	}
}
bool PagedSegmentCollection::needs_work() const noexcept {
	using Phase = State::Phase;
	const auto &s = *state_;
	return s.phase == Phase::Walk || (s.phase == Phase::Insert && s.proof->inputs->busy()) ||
		   s.phase == Phase::Graph || s.phase == Phase::Certificate || s.phase == Phase::DiscardGraph ||
		   s.phase == Phase::DiscardWalk || s.phase == Phase::Mark || s.phase == Phase::Query ||
		   (s.pack && (s.phase == Phase::Pack || s.phase == Phase::Candidate) && s.pack->needs_work());
}
void PagedSegmentCollection::step(ValidationBudget &budget) {
	using Phase = State::Phase;
	auto &s = *state_;
	if (in_flight())
		throw std::logic_error("segment collection CPU while submitted");
	try {
		if (s.stopping && !s.destructive) {
			cleanup();
			return;
		}
		if (s.pack && (s.phase == Phase::Pack || s.phase == Phase::Candidate) && s.pack->needs_work())
			s.pack->step(budget);
		else if (s.phase == Phase::Walk) {
			s.proof->walk->step(budget);
			if (auto missing = s.proof->walk->missing()) {
				s.pack.emplace(s.factory, s.session, s.core->account, s.core->identity, *missing,
							   s.core->owner);
				s.phase = Phase::Pack;
			} else if (s.proof->walk->done()) {
				const auto &limits = s.core->options.recovery_limits;
				const auto codec = paged_limits(limits);
				s.proof->inputs->freeze();
				s.proof->graph.emplace(s.core->account, s.proof->inputs->owner(), s.proof->inputs->inputs(),
									   s.root->root(), s.core->identity, codec,
									   PagedSemanticLimits{limits.strings, limits.overrides});
				s.phase = Phase::Graph;
			}
		} else if (s.phase == Phase::Insert) {
			s.proof->inputs->step(budget);
			if (!s.proof->inputs->busy() && s.pack->done()) {
				if (s.pack->error())
					std::rethrow_exception(s.pack->error());
				s.pack.reset();
				s.phase = Phase::Walk;
			}
		} else if (s.phase == Phase::Graph) {
			s.proof->graph->step(budget);
			if (s.proof->graph->done()) {
				s.certificate = s.factory.make<PagedDependencyCertificate>(s.core->account, s.factory);
				s.certificate->begin_named(*s.root, *s.proof->graph);
				s.phase = Phase::Certificate;
			}
		} else if (s.phase == Phase::Certificate) {
			s.certificate->capture_step(budget);
			if (s.certificate->sealed()) {
				s.core->certificates[s.core->certificate_position] = s.certificate;
				s.core->certificate_position =
					(s.core->certificate_position + 1) % s.core->certificates.size();
				if (s.proof && s.proof->graph)
					s.phase = Phase::DiscardGraph;
				else {
					s.marks->begin(s.certificate);
					s.phase = Phase::Mark;
				}
			}
		} else if (s.phase == Phase::DiscardGraph) {
			if (s.proof->graph->discard(budget)) {
				s.proof->graph.reset();
				s.phase = Phase::DiscardWalk;
			}
		} else if (s.phase == Phase::DiscardWalk) {
			if (s.proof->walk->discard(budget)) {
				s.proof->walk.reset();
				s.proof->inputs.reset();
				s.marks->begin(s.certificate);
				s.phase = Phase::Mark;
			}
		} else if (s.phase == Phase::Mark) {
			s.marks->step(budget);
			if (!s.marks->busy()) {
				s.root->close();
				s.phase = Phase::CloseRoot;
			}
		} else if (s.phase == Phase::Query) {
			if (auto marked = s.marks->query_step(budget)) {
				if (*marked) {
					s.phase = Phase::SweepNext;
					prepare(c::Kind::NextInto);
				} else {
					if (s.owner_only) {
						s.phase = Phase::OwnerOpen;
						prepare(c::Kind::OpenCandidateInto);
						request_.file = std::shared_ptr<c::File>(pin_, &s.controls->scratch_file());
					} else {
						s.pack.emplace(s.factory, s.session, s.core->account, s.core->identity, s.sought,
									   s.core->owner);
						s.phase = Phase::Candidate;
					}
				}
			}
		}
		advance();
	} catch (const SegmentProofError &) {
		if (s.phase == Phase::Candidate && !s.stopping) {
			s.pack->close();
			s.phase = Phase::Rejected;
			advance();
		} else
			fail(std::current_exception());
	} catch (...) {
		fail(std::current_exception());
	}
}
bool PagedSegmentCollection::complete(c::Result result) noexcept {
	using Phase = State::Phase;
	auto &s = *state_;
	if (!s.submitted || result.token != request().token)
		return false;
	s.submitted = false;
	try {
		if (auto *child = s.child()) {
			child->complete(std::move(result));
			if (s.root && (s.phase == Phase::Root || s.phase == Phase::CloseRoot) && s.root->error())
				std::rethrow_exception(s.root->error());
			if (s.pack && s.pack->closure_error())
				std::rethrow_exception(s.pack->closure_error());
			if (s.pack && s.pack->error() && s.phase != Phase::Rejected) {
				bool unproved = false;
				if (s.phase == Phase::Candidate) {
					try {
						std::rethrow_exception(s.pack->error());
					} catch (const SegmentProofError &) {
						unproved = true;
					} catch (const std::system_error &error) {
						unproved = error.code() == std::errc::no_such_file_or_directory;
					} catch (...) {
					}
				}
				if (unproved && !s.stopping) {
					s.pack->close();
					s.phase = Phase::Rejected;
				} else
					std::rethrow_exception(s.pack->error());
			}
			advance();
			return true;
		}
		const auto phase = s.phase;
		if (phase == Phase::OwnerOpen && !result.error && result.count)
			s.owner_open = true;
		if (phase == Phase::OwnerClose)
			s.owner_open = false;
		if (phase == Phase::OwnerRecord) {
			s.owner_reader.complete(std::move(result));
			if (s.owner_reader.error())
				std::rethrow_exception(s.owner_reader.error());
			if (s.stopping) {
				cleanup();
				return true;
			}
			if (!s.owner_reader.done()) {
				prepare(c::Kind::Read);
				request_ = s.owner_reader.prepare(
					s.token, std::shared_ptr<c::File>(pin_, &s.controls->scratch_file()));
				return true;
			}
			bool valid = s.owner_reader.status() == RecordReadStatus::Ready;
			if (valid) {
				try {
					const auto owned = decode_segment_ownership(s.owner_reader.bytes());
					valid = owned.storage == s.core->identity && owned.segment == s.sought;
				} catch (const std::invalid_argument &) {
					valid = false;
				}
			}
			if (!valid) {
				s.phase = Phase::OwnerClose;
				prepare(c::Kind::CloseFile);
				request_.file = std::shared_ptr<c::File>(pin_, &s.controls->scratch_file());
				return true;
			}
			s.phase = Phase::OwnerAbsent;
			prepare(c::Kind::InspectEntry);
			return true;
		}

		if (phase == Phase::Scan || phase == Phase::SweepScan) {
			if (!result.error)
				s.cursor_open = true;
		}
		if (phase == Phase::CloseCursor || phase == Phase::CloseMarkCursor)
			s.cursor_open = false;
		if (result.error)
			std::rethrow_exception(result.error);
		if (s.stopping && !s.destructive) {
			cleanup();
			return true;
		}
		if (phase == Phase::OwnerOpen) {
			if (!result.count) {
				s.phase = Phase::SweepNext;
				prepare(c::Kind::NextInto);
				return true;
			}
			s.owner_reader.start(s.names->owner_name(), segment_ownership_bytes, s.core->owner,
								 s.core->directory_identity);
			s.phase = Phase::OwnerRecord;
			prepare(c::Kind::Read);
			request_ =
				s.owner_reader.prepare(s.token, std::shared_ptr<c::File>(pin_, &s.controls->scratch_file()));
		} else if (phase == Phase::OwnerAbsent) {
			if (result.inspection) {
				s.phase = Phase::OwnerClose;
				prepare(c::Kind::CloseFile);
				request_.file = std::shared_ptr<c::File>(pin_, &s.controls->scratch_file());
				return true;
			}
			s.phase = Phase::OwnerNamed;
			prepare(c::Kind::InspectEntry);
		} else if (phase == Phase::OwnerNamed) {
			if (!result.inspection || result.inspection->footprint.kind != c::EntryKind::Regular ||
				result.inspection->footprint.logical_bytes != segment_ownership_bytes ||
				result.inspection->links != 1 || result.inspection->owner != s.core->owner ||
				result.inspection->device != s.core->directory_identity.device ||
				(result.inspection->permissions & 0022))
				throw std::runtime_error("unsafe segment anchor");
			s.owner_identity = result.inspection;
			s.phase = Phase::OwnerHeld;
			prepare(c::Kind::InspectFile);
			request_.file = std::shared_ptr<c::File>(pin_, &s.controls->scratch_file());
		} else if (phase == Phase::OwnerHeld) {
			if (!result.inspection || *result.inspection != *s.owner_identity ||
				s.core->selection.descriptor != s.selected || s.core->active_generation)
				throw std::runtime_error("segment anchor or epoch changed");
			s.phase = Phase::RemoveOwner;
			prepare(c::Kind::Remove);
		} else if (phase == Phase::OwnerClose) {
			if (s.destructive && s.barriers_done && !s.failure) {
				s.core->ledger.reclaimed({segment_ownership_bytes, 1, 0});
				s.destructive = s.barriers_done = false;
			}
			if (s.stopping)
				cleanup();
			else
				s.phase = Phase::Paused;
		} else if (phase == Phase::Scan)
			next();
		else if (phase == Phase::SweepScan) {
			s.phase = Phase::SweepNext;
			prepare(c::Kind::NextInto);
		} else if (phase == Phase::Next || phase == Phase::SweepNext) {
			if (!result.count) {
				if (phase == Phase::Next) {
					if (!s.selected_seen)
						throw std::runtime_error("selected root missing from collection census");
					s.phase = Phase::CloseMarkCursor;
					prepare(c::Kind::CloseCursor);
				} else
					cleanup();
			} else {
				if (result.count >= s.name.size() || ++s.inspected > s.core->options.census_work_limit)
					throw std::runtime_error("segment collection census limit exceeded");
				const auto name = std::string_view(s.name.data(), result.count);
				if (name.find('/') != name.npos || name.find('\0') != name.npos)
					throw std::runtime_error("invalid segment collection name");
				if (phase == Phase::Next) {
					auto parsed = parse_generation_name(name);
					if (parsed &&
						(parsed->kind == GenerationFile::Lease || parsed->kind == GenerationFile::Image)) {
						d::RetirementHandle<PagedDependencyCertificate> cached;
						for (const auto &entry : s.core->certificates)
							if (entry && entry->descriptor().generation == parsed->generation) {
								cached = entry;
								break;
							}
						s.root.emplace(s.factory, s.session, s.core->identity, parsed->generation,
									   s.core->owner, cached);
						s.phase = Phase::Root;
					} else
						next();
				} else {
					auto parsed = parse_segment_name(name);
					if (parsed) {
						s.owner_only = parsed->kind == SegmentFile::Owner;
						s.sought = parsed->segment;
						s.names.emplace(parsed->segment);
						s.marks->query(parsed->segment);
						s.phase = Phase::Query;
					} else {
						s.phase = Phase::SweepNext;
						prepare(c::Kind::NextInto);
					}
				}
			}
		} else if (phase == Phase::CloseMarkCursor) {
			if (s.core->selection.knowledge == SelectionKnowledge::Uncertain ||
				s.core->selection.descriptor != s.selected || s.core->active_generation)
				throw std::runtime_error("collection selection changed");
			s.marks->seal();
			s.inspected = 0;
			s.phase = Phase::SweepScan;
			prepare(c::Kind::ScanInto);
		} else if (phase == Phase::InspectPack) {
			const auto identity = s.pack->identity();
			if (!result.inspection || result.inspection->device != identity.device ||
				result.inspection->inode != identity.inode ||
				result.inspection->footprint.kind != c::EntryKind::Regular ||
				result.inspection->footprint.logical_bytes != s.pack->ownership().length ||
				result.inspection->owner != s.core->owner || result.inspection->links != 1 ||
				(result.inspection->permissions & 0022))
				throw std::runtime_error("collection pack changed");
			s.phase = Phase::InspectOwner;
			prepare(c::Kind::InspectEntry);
		} else if (phase == Phase::InspectOwner) {
			if (!result.inspection || result.inspection->footprint.kind != c::EntryKind::Regular ||
				result.inspection->footprint.logical_bytes != segment_ownership_bytes ||
				result.inspection->owner != s.core->owner || result.inspection->links != 1 ||
				(result.inspection->permissions & 0022) ||
				result.inspection->device != s.core->directory_identity.device ||
				s.core->selection.descriptor != s.selected || s.core->active_generation)
				throw std::runtime_error("collection ownership or epoch changed");
			s.owner_identity = result.inspection;
			s.phase = Phase::HeldOwner;
			prepare(c::Kind::InspectFile);
			request_.file = s.pack->owner_file();
		} else if (phase == Phase::HeldOwner) {
			if (!result.inspection || *result.inspection != *s.owner_identity)
				throw std::runtime_error("collection ownership identity changed");
			s.phase = Phase::RemovePack;
			prepare(c::Kind::Remove);
		} else if (phase == Phase::RemovePack) {
			s.phase = Phase::PackBarrier;
			prepare(c::Kind::DirectorySync);
		} else if (phase == Phase::PackBarrier) {
			s.phase = Phase::RemoveOwner;
			prepare(c::Kind::Remove);
		} else if (phase == Phase::RemoveOwner) {
			s.phase = Phase::OwnerBarrier;
			prepare(c::Kind::DirectorySync);
		} else if (phase == Phase::OwnerBarrier) {
			s.barriers_done = true;
			if (s.owner_only) {
				s.phase = Phase::OwnerClose;
				prepare(c::Kind::CloseFile);
				request_.file = std::shared_ptr<c::File>(pin_, &s.controls->scratch_file());
			} else {
				s.pack->close();
				s.phase = Phase::CloseCandidate;
				advance();
			}
		} else if (phase == Phase::CloseCursor)
			cleanup();
	} catch (...) {
		fail(std::current_exception());
	}
	return true;
}
} // namespace detent::local::detail
