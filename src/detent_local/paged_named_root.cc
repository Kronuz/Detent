#include "paged_named_root.hh"
#include "detent_local/record_reader.hh"
#include "names.hh"
#include "paged_root_prefix.hh"
namespace detent::local::detail {
namespace c = kronuz::io::completion;
namespace d = distribution;
struct PagedNamedRoot::State final : d::RetirementNode {
	enum class Phase {
		OpenLease,
		Lock,
		Ownership,
		VerifyOwnership,
		OpenRoot,
		Named,
		Held,
		Read,
		Reheld,
		Renamed,
		CloseRoot,
		Ready,
		CloseLease,
		Done
	};
	std::shared_ptr<StorageSession> session;
	std::unique_ptr<c::File> lease, image;
	d::RetirementHandle<PagedDependencyCertificate> cached;
	StorageIdentity storage;
	GenerationId generation;
	std::uint64_t trusted_owner;
	GenerationName lease_name, image_name;
	RecordReader reader;
	std::array<char, paged::root_bytes> bytes{};
	std::optional<c::EntryInspection> initial;
	GenerationOwnership ownership;
	paged::Root root;
	Descriptor descriptor;
	c::Token token;
	std::size_t offset = 0;
	Phase phase = Phase::OpenLease;
	std::exception_ptr failure;
	bool submitted = false, lease_open = false, image_open = false, closing = false;
	State(std::shared_ptr<StorageSession> session, StorageIdentity storage, GenerationId generation,
		  std::uint64_t owner, d::RetirementHandle<PagedDependencyCertificate> cached)
		: session(std::move(session)), lease(this->session->backend()->make_closed_file()),
		  image(this->session->backend()->make_closed_file()), cached(std::move(cached)), storage(storage),
		  generation(generation), trusted_owner(owner), lease_name(generation, GenerationFile::Lease),
		  image_name(generation, GenerationFile::Image), token(this->session->next_operation()) {}
	bool safe(const c::EntryInspection &entry) const noexcept {
		return entry.footprint.kind == c::EntryKind::Regular && entry.links == 1 &&
			   entry.owner == trusted_owner && !(entry.permissions & 0022) &&
			   entry.device == session->directory_identity().device &&
			   entry.footprint.logical_bytes == paged::root_bytes;
	}
	bool same(const c::EntryInspection &entry) const noexcept {
		return initial && safe(entry) && entry.device == initial->device && entry.inode == initial->inode &&
			   entry.permissions == initial->permissions;
	}
	bool retire_step() noexcept override {
		if (image_open) {
			image_open = false;
			try {
				image->close();
			} catch (...) {
			}
			return false;
		}
		if (lease_open) {
			lease_open = false;
			try {
				lease->close();
			} catch (...) {
			}
			return false;
		}
		image.reset();
		lease.reset();
		cached.reset();
		session.reset();
		return true;
	}
};
PagedNamedRoot::PagedNamedRoot(d::RetirementFactory factory, std::shared_ptr<StorageSession> session,
							   StorageIdentity storage, GenerationId generation, std::uint64_t owner,
							   d::RetirementHandle<PagedDependencyCertificate> cached) {
	const auto context = session->allocation_context();
	state_ = factory.make<State>(std::move(session), storage, generation, owner, std::move(cached));
	using Handle = d::RetirementHandle<State>;
	pin_ = std::allocate_shared<Handle>(c::OwnedAllocator<Handle>(context), state_);
	prepare(c::Kind::OpenInto);
}
PagedNamedRoot::~PagedNamedRoot() = default;
const c::Request &PagedNamedRoot::request() const { return request_; }
c::IO &PagedNamedRoot::io() const noexcept { return *state_->session->backend(); }
bool PagedNamedRoot::in_flight() const noexcept { return state_->submitted; }
bool PagedNamedRoot::ready() const noexcept { return state_->phase == State::Phase::Ready; }
bool PagedNamedRoot::done() const noexcept { return ready() || state_->phase == State::Phase::Done; }
std::exception_ptr PagedNamedRoot::error() const noexcept { return state_->failure; }
const paged::Root &PagedNamedRoot::root() const {
	if (!ready())
		throw std::logic_error("named paged root unavailable");
	return state_->root;
}
const Descriptor &PagedNamedRoot::descriptor() const {
	root();
	return state_->descriptor;
}
d::RetirementHandle<PagedDependencyCertificate> PagedNamedRoot::certificate() const {
	root();
	return state_->cached;
}
void PagedNamedRoot::prepare(c::Kind kind) {
	if (state_->token.step == UINT64_MAX)
		throw std::overflow_error("named root tokens exhausted");
	++state_->token.step;
	request_ = {};
	request_.token = state_->token;
	request_.kind = kind;
	const auto phase = state_->phase;
	const bool lease =
		phase == State::Phase::OpenLease || phase == State::Phase::Lock || phase == State::Phase::CloseLease;
	request_.file = std::shared_ptr<c::File>(pin_, lease ? state_->lease.get() : state_->image.get());
	request_.source = lease ? state_->lease_name.view() : state_->image_name.view();
	if (kind == c::Kind::Read) {
		request_.offset = state_->offset;
		request_.destination_bytes = std::span<char>(state_->bytes).subspan(state_->offset);
	}
}
void PagedNamedRoot::read_ownership() {
	if (state_->token.step == UINT64_MAX)
		throw std::overflow_error("named root tokens exhausted");
	++state_->token.step;
	request_ = state_->reader.prepare(state_->token, std::shared_ptr<c::File>(pin_, state_->lease.get()));
}
void PagedNamedRoot::submitted() {
	if (done() || state_->submitted)
		throw std::logic_error("invalid named root submission");
	if (state_->phase == State::Phase::Ownership || state_->phase == State::Phase::VerifyOwnership)
		state_->reader.submitted();
	state_->submitted = true;
}
void PagedNamedRoot::cleanup() {
	state_->closing = true;
	if (state_->image_open) {
		state_->phase = State::Phase::CloseRoot;
		prepare(c::Kind::CloseFile);
	} else if (state_->lease_open) {
		state_->phase = State::Phase::CloseLease;
		prepare(c::Kind::CloseFile);
	} else {
		state_->phase = State::Phase::Done;
		request_ = {};
	}
}
void PagedNamedRoot::close() noexcept {
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
bool PagedNamedRoot::complete(c::Result result) noexcept {
	if (!state_->submitted || result.token != request_.token)
		return false;
	state_->submitted = false;
	try {
		using Phase = State::Phase;
		const auto phase = state_->phase;
		if (phase == Phase::OpenLease && !result.error)
			state_->lease_open = true;
		if (phase == Phase::OpenRoot && !result.error)
			state_->image_open = true;
		if (phase == Phase::CloseRoot)
			state_->image_open = false;
		if (phase == Phase::CloseLease)
			state_->lease_open = false;
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
				throw std::runtime_error("named root ownership changed");
			const auto ownership = decode_generation_ownership(state_->reader.bytes());
			if (phase == Phase::VerifyOwnership) {
				if (ownership != state_->ownership)
					throw std::runtime_error("named root ownership changed during read");
				state_->phase = Phase::CloseRoot;
				prepare(c::Kind::CloseFile);
				return true;
			}
			state_->ownership = ownership;
			if (state_->ownership.storage != state_->storage ||
				state_->ownership.generation != state_->generation || state_->ownership.layout_version != 2 ||
				state_->ownership.image_length != paged::root_bytes)
				throw std::invalid_argument("unsupported named paged root ownership");
			state_->phase = Phase::OpenRoot;
			prepare(c::Kind::OpenInto);
			return true;
		}
		if (result.error)
			std::rethrow_exception(result.error);
		if (state_->closing) {
			cleanup();
			return true;
		}
		if (phase == Phase::OpenLease) {
			state_->phase = Phase::Lock;
			prepare(c::Kind::SharedLease);
		} else if (phase == Phase::Lock) {
			if (!result.count)
				throw std::runtime_error("named root lease unavailable");
			state_->reader.start(state_->lease_name.view(), generation_ownership_bytes, state_->trusted_owner,
								 state_->session->directory_identity());
			state_->phase = Phase::Ownership;
			read_ownership();
		} else if (phase == Phase::OpenRoot) {
			state_->phase = Phase::Named;
			prepare(c::Kind::InspectEntry);
		} else if (phase == Phase::Named) {
			if (!result.inspection || !state_->safe(*result.inspection))
				throw std::runtime_error("unsafe named paged root");
			state_->initial = result.inspection;
			state_->phase = Phase::Held;
			prepare(c::Kind::InspectFile);
		} else if (phase == Phase::Held || phase == Phase::Reheld || phase == Phase::Renamed) {
			if (!result.inspection || !state_->same(*result.inspection))
				throw std::runtime_error("named paged root identity changed");
			if (phase == Phase::Held) {
				state_->phase = Phase::Read;
				prepare(c::Kind::Read);
			} else if (phase == Phase::Reheld) {
				state_->phase = Phase::Renamed;
				prepare(c::Kind::InspectEntry);
			} else {
				if (paged::physical_commitment(state_->bytes) != state_->ownership.image_checksum)
					throw std::runtime_error("named paged root checksum mismatch");
				state_->root = paged::decode_root(state_->bytes, paged_format_limits());
				state_->descriptor = {state_->generation, paged::root_bytes, state_->ownership.image_checksum,
									  state_->root.cursor, state_->root.profile};
				if (state_->cached &&
					(state_->cached->descriptor() != state_->descriptor ||
					 state_->cached->storage_identity() != state_->storage ||
					 state_->cached->directory_identity() != state_->session->directory_identity() ||
					 paged::encode_root(state_->cached->root(), paged_format_limits()) != state_->bytes))
					state_->cached.reset();
				state_->reader.start(state_->lease_name.view(), generation_ownership_bytes,
									 state_->trusted_owner, state_->session->directory_identity());
				state_->phase = Phase::VerifyOwnership;
				read_ownership();
			}
		} else if (phase == Phase::Read) {
			if (!result.count || result.count > state_->bytes.size() - state_->offset)
				throw std::runtime_error("invalid named root read");
			state_->offset += result.count;
			if (state_->offset != state_->bytes.size())
				prepare(c::Kind::Read);
			else {
				state_->phase = Phase::Reheld;
				prepare(c::Kind::InspectFile);
			}
		} else if (phase == Phase::CloseRoot) {
			state_->phase = Phase::Ready;
			request_ = {};
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
