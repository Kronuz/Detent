#include "paged_segment_read.hh"
#include "detent_local/record_reader.hh"
#include "segment_names.hh"
#include <algorithm>
#include <cstring>
#include <sha256.h>
namespace detent::local::detail {
namespace c = kronuz::io::completion;
namespace d = distribution;
struct PagedSegmentRead::State final : d::RetirementNode {
	enum class Phase {
		OpenOwner,
		Ownership,
		Allocate,
		Zero,
		OpenPack,
		Named,
		Held,
		Read,
		Hash,
		Reheld,
		Renamed,
		VerifyOwnership,
		Ready,
		ClosePack,
		CloseOwner,
		Done
	};
	std::shared_ptr<StorageSession> session;
	std::shared_ptr<d::StorageLedger> account;
	std::unique_ptr<c::File> owner, pack;
	StorageIdentity storage, segment;
	SegmentNames names;
	std::uint64_t trusted_owner;
	RecordReader reader;
	SegmentOwnership ownership;
	std::shared_ptr<d::OwnedBuffer> bytes;
	std::optional<c::EntryInspection> initial;
	SHA256 hash;
	c::Token token;
	std::size_t offset = 0;
	Phase phase = Phase::OpenOwner;
	std::exception_ptr failure;
	bool submitted = false, owner_open = false, pack_open = false, closing = false;
	State(std::shared_ptr<StorageSession> session, std::shared_ptr<d::StorageLedger> account,
		  StorageIdentity storage, StorageIdentity segment, std::uint64_t uid)
		: session(std::move(session)), account(std::move(account)),
		  owner(this->session->backend()->make_closed_file()),
		  pack(this->session->backend()->make_closed_file()), storage(storage), segment(segment),
		  names(segment), trusted_owner(uid), token(this->session->next_operation()) {}
	bool safe(const c::EntryInspection &entry) const noexcept {
		return entry.footprint.kind == c::EntryKind::Regular && entry.links == 1 &&
			   entry.owner == trusted_owner && !(entry.permissions & 0022) &&
			   entry.device == session->directory_identity().device &&
			   entry.footprint.logical_bytes == ownership.length;
	}
	bool same(const c::EntryInspection &entry) const noexcept {
		return initial && safe(entry) && entry.device == initial->device && entry.inode == initial->inode &&
			   entry.permissions == initial->permissions;
	}
	bool retire_step() noexcept override {
		if (pack_open) {
			pack_open = false;
			try {
				pack->close();
			} catch (...) {
			}
			return false;
		}
		if (owner_open) {
			owner_open = false;
			try {
				owner->close();
			} catch (...) {
			}
			return false;
		}
		bytes.reset();
		pack.reset();
		owner.reset();
		session.reset();
		return true;
	}
};
PagedSegmentRead::PagedSegmentRead(d::RetirementFactory factory, std::shared_ptr<StorageSession> session,
								   std::shared_ptr<d::StorageLedger> account, StorageIdentity storage,
								   StorageIdentity segment, std::uint64_t uid) {
	const auto context = session->allocation_context();
	state_ = factory.make<State>(std::move(session), std::move(account), storage, segment, uid);
	using Handle = d::RetirementHandle<State>;
	pin_ = std::allocate_shared<Handle>(c::OwnedAllocator<Handle>(context), state_);
	prepare(c::Kind::OpenInto);
}
PagedSegmentRead::~PagedSegmentRead() = default;
bool PagedSegmentRead::ready() const noexcept { return state_->phase == State::Phase::Ready; }
bool PagedSegmentRead::done() const noexcept { return ready() || state_->phase == State::Phase::Done; }
bool PagedSegmentRead::in_flight() const noexcept { return state_->submitted; }
bool PagedSegmentRead::needs_work() const noexcept {
	const auto phase = state_->phase;
	return phase == State::Phase::Allocate || phase == State::Phase::Zero || phase == State::Phase::Hash;
}
c::IO &PagedSegmentRead::io() const noexcept { return *state_->session->backend(); }
std::exception_ptr PagedSegmentRead::error() const noexcept { return state_->failure; }
const c::Request &PagedSegmentRead::request() const {
	if (needs_work())
		throw std::logic_error("segment reader requires CPU work");
	return request_;
}
const SegmentOwnership &PagedSegmentRead::ownership() const {
	if (!ready())
		throw std::logic_error("verified segment unavailable");
	return state_->ownership;
}
std::shared_ptr<const d::OwnedBuffer> PagedSegmentRead::bytes() const {
	ownership();
	return state_->bytes;
}
SegmentFileIdentity PagedSegmentRead::identity() const {
	ownership();
	return {state_->initial->device, state_->initial->inode};
}
std::shared_ptr<c::File> PagedSegmentRead::owner_file() const {
	ownership();
	return {pin_, state_->owner.get()};
}
std::shared_ptr<c::File> PagedSegmentRead::payload_file() const {
	ownership();
	return {pin_, state_->pack.get()};
}
void PagedSegmentRead::prepare(c::Kind kind) {
	if (state_->token.step == UINT64_MAX)
		throw std::overflow_error("segment read tokens exhausted");
	++state_->token.step;
	request_ = {};
	request_.token = state_->token;
	request_.kind = kind;
	const bool owner = state_->phase == State::Phase::OpenOwner || state_->phase == State::Phase::CloseOwner;
	request_.file = std::shared_ptr<c::File>(pin_, owner ? state_->owner.get() : state_->pack.get());
	request_.source = owner ? state_->names.owner_name() : state_->names.payload_name();
	if (kind == c::Kind::Read) {
		request_.offset = state_->offset;
		request_.destination_bytes = state_->bytes->mutable_bytes().subspan(
			state_->offset, std::min<std::size_t>(8192, state_->bytes->size() - state_->offset));
	}
}
void PagedSegmentRead::read_ownership() {
	if (state_->token.step == UINT64_MAX)
		throw std::overflow_error("segment read tokens exhausted");
	++state_->token.step;
	request_ = state_->reader.prepare(state_->token, std::shared_ptr<c::File>(pin_, state_->owner.get()));
}
void PagedSegmentRead::submitted() {
	if (done() || needs_work() || state_->submitted)
		throw std::logic_error("invalid segment read submission");
	if (state_->phase == State::Phase::Ownership || state_->phase == State::Phase::VerifyOwnership)
		state_->reader.submitted();
	state_->submitted = true;
}
void PagedSegmentRead::cleanup() {
	state_->closing = true;
	if (state_->pack_open) {
		state_->phase = State::Phase::ClosePack;
		prepare(c::Kind::CloseFile);
	} else if (state_->owner_open) {
		state_->phase = State::Phase::CloseOwner;
		prepare(c::Kind::CloseFile);
	} else {
		state_->phase = State::Phase::Done;
		request_ = {};
	}
}
void PagedSegmentRead::close() noexcept {
	state_->closing = true;
	if (state_->submitted)
		return;
	try {
		cleanup();
	} catch (...) {
		if (!state_->failure)
			state_->failure = std::current_exception();
	}
}
void PagedSegmentRead::step(ValidationBudget &budget) {
	if (!needs_work() || state_->submitted)
		throw std::logic_error("segment CPU work unavailable");
	if (!budget.rows)
		return;
	try {
		using Phase = State::Phase;
		if (state_->phase == Phase::Allocate) {
			--budget.rows;
			state_->bytes = d::make_owned_buffer(state_->account, state_->ownership.length);
			state_->phase = Phase::Zero;
			request_ = {};
		}
		if (state_->phase == Phase::Zero) {
			const auto count =
				std::min<std::size_t>(budget.bytes, state_->ownership.length - state_->bytes->size());
			state_->bytes->resize(state_->bytes->size() + count);
			budget.bytes -= count;
			if (state_->bytes->size() == state_->ownership.length) {
				state_->phase = Phase::OpenPack;
				prepare(c::Kind::OpenInto);
			}
		} else if (state_->phase == Phase::Hash) {
			const auto count = std::min(budget.bytes, state_->bytes->size() - state_->offset);
			state_->hash.add(state_->bytes->data() + state_->offset, count);
			state_->offset += count;
			budget.bytes -= count;
			if (state_->offset == state_->bytes->size() && budget.rows) {
				--budget.rows;
				unsigned char digest[32];
				state_->hash.getHash(digest);
				if (std::memcmp(digest, state_->ownership.checksum.data(), 32))
					throw std::runtime_error("segment payload checksum mismatch");
				state_->phase = Phase::Reheld;
				prepare(c::Kind::InspectFile);
			}
		}
	} catch (...) {
		if (!state_->failure)
			state_->failure = std::current_exception();
		cleanup();
	}
}
bool PagedSegmentRead::complete(c::Result result) noexcept {
	if (!state_->submitted || result.token != request_.token)
		return false;
	state_->submitted = false;
	try {
		using Phase = State::Phase;
		const auto phase = state_->phase;
		if (phase == Phase::OpenOwner && !result.error)
			state_->owner_open = true;
		if (phase == Phase::OpenPack && !result.error)
			state_->pack_open = true;
		if (phase == Phase::ClosePack)
			state_->pack_open = false;
		if (phase == Phase::CloseOwner)
			state_->owner_open = false;
		if (phase == Phase::Ownership || phase == Phase::VerifyOwnership) {
			state_->reader.complete(std::move(result));
			if (state_->reader.error())
				std::rethrow_exception(state_->reader.error());
			if (state_->closing) {
				cleanup();
				return true;
			}
			if (!state_->reader.done()) {
				read_ownership();
				return true;
			}
			if (state_->reader.status() != RecordReadStatus::Ready)
				throw std::runtime_error("segment ownership unavailable");
			const auto ownership = decode_segment_ownership(state_->reader.bytes());
			if (phase == Phase::VerifyOwnership) {
				if (ownership != state_->ownership)
					throw std::runtime_error("segment ownership changed during read");
				state_->bytes->seal();
				state_->phase = Phase::Ready;
				request_ = {};
				return true;
			}
			if (ownership.storage != state_->storage || ownership.segment != state_->segment ||
				!ownership.length || ownership.length > paged::segment_bytes)
				throw std::invalid_argument("invalid named segment ownership");
			state_->ownership = ownership;
			state_->phase = Phase::Allocate;
			request_ = {};
			return true;
		}
		if (result.error)
			std::rethrow_exception(result.error);
		if (state_->closing) {
			cleanup();
			return true;
		}
		if (phase == Phase::OpenOwner) {
			state_->reader.start(state_->names.owner_name(), segment_ownership_bytes, state_->trusted_owner,
								 state_->session->directory_identity());
			state_->phase = Phase::Ownership;
			read_ownership();
		} else if (phase == Phase::OpenPack) {
			state_->phase = Phase::Named;
			prepare(c::Kind::InspectEntry);
		} else if (phase == Phase::Named) {
			if (!result.inspection || !state_->safe(*result.inspection))
				throw std::runtime_error("unsafe named segment payload");
			state_->initial = result.inspection;
			state_->phase = Phase::Held;
			prepare(c::Kind::InspectFile);
		} else if (phase == Phase::Held || phase == Phase::Reheld || phase == Phase::Renamed) {
			if (!result.inspection || !state_->same(*result.inspection))
				throw std::runtime_error("named segment identity changed");
			if (phase == Phase::Held) {
				state_->phase = Phase::Read;
				prepare(c::Kind::Read);
			} else if (phase == Phase::Reheld) {
				state_->phase = Phase::Renamed;
				prepare(c::Kind::InspectEntry);
			} else {
				state_->reader.start(state_->names.owner_name(), segment_ownership_bytes,
									 state_->trusted_owner, state_->session->directory_identity());
				state_->phase = Phase::VerifyOwnership;
				read_ownership();
			}
		} else if (phase == Phase::Read) {
			if (!result.count || result.count > request_.destination_bytes.size())
				throw std::runtime_error("invalid segment read result");
			state_->offset += result.count;
			if (state_->offset != state_->bytes->size())
				prepare(c::Kind::Read);
			else {
				state_->offset = 0;
				state_->phase = Phase::Hash;
				request_ = {};
			}
		}
	} catch (...) {
		if (!state_->failure)
			state_->failure = std::current_exception();
		try {
			cleanup();
		} catch (...) {
			state_->phase = State::Phase::Done;
		}
	}
	return true;
}
} // namespace detent::local::detail
