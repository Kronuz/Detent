#include "paged_dependency_check.hh"
#include "detent_local/record_reader.hh"
#include "segment_names.hh"
namespace detent::local::detail {
namespace c = kronuz::io::completion;
namespace d = distribution;
struct PagedDependencyCheck::State final : d::RetirementNode {
	enum class Phase { Open, Read, Close, Inspect, Done };
	std::shared_ptr<StorageSession> session;
	d::RetirementHandle<PagedPublicationArtifacts> owner;
	std::unique_ptr<c::File> file;
	RecordReader reader;
	std::optional<SegmentNames> names;
	std::size_t index = 0;
	std::uint64_t trusted_owner;
	c::Token token;
	Phase phase = Phase::Done;
	std::exception_ptr failure;
	bool submitted = false, cancelled = false, opened = false;
	State(std::shared_ptr<StorageSession> session, d::RetirementHandle<PagedPublicationArtifacts> owner,
		  std::uint64_t trusted_owner)
		: session(std::move(session)), owner(std::move(owner)),
		  file(this->session->backend()->make_closed_file()), trusted_owner(trusted_owner),
		  token(this->session->next_operation()) {}
	bool retire_step() noexcept override {
		if (opened) {
			opened = false;
			try {
				file->close();
			} catch (...) {
			}
			return false;
		}
		file.reset();
		owner.reset();
		session.reset();
		return true;
	}
};
PagedDependencyCheck::PagedDependencyCheck(d::RetirementFactory factory,
										   std::shared_ptr<StorageSession> session,
										   d::RetirementHandle<PagedPublicationArtifacts> owner,
										   std::uint64_t trusted_owner) {
	const auto context = session->allocation_context();
	data_ = factory.make<State>(std::move(session), std::move(owner), trusted_owner);
	using Handle = d::RetirementHandle<State>;
	pin_ = std::allocate_shared<Handle>(c::OwnedAllocator<Handle>(context), data_);
	next();
}
PagedDependencyCheck::~PagedDependencyCheck() = default;
const c::Request &PagedDependencyCheck::request() const { return request_; }
void PagedDependencyCheck::prepare(c::Kind kind) {
	if (data_->token.step == UINT64_MAX)
		throw std::overflow_error("dependency-check token exhausted");
	request_ = {};
	request_.token = data_->token;
	++request_.token.step;
	data_->token = request_.token;
	request_.kind = kind;
	if (kind == c::Kind::OpenInto || kind == c::Kind::CloseFile)
		request_.file = std::shared_ptr<c::File>(pin_, data_->file.get());
	if (kind == c::Kind::OpenInto)
		request_.source = data_->names->owner_name();
	if (kind == c::Kind::InspectEntry)
		request_.source = data_->names->payload_name();
}
void PagedDependencyCheck::prepare_reader() {
	data_->phase = State::Phase::Read;
	if (data_->token.step == UINT64_MAX)
		throw std::overflow_error("dependency-check token exhausted");
	++data_->token.step;
	request_ = data_->reader.prepare(data_->token, std::shared_ptr<c::File>(pin_, data_->file.get()));
}
void PagedDependencyCheck::next() {
	if (data_->index == data_->owner->reused().size()) {
		data_->phase = State::Phase::Done;
		request_ = {};
		return;
	}
	data_->names.emplace(data_->owner->reused()[data_->index].ownership.segment);
	data_->phase = State::Phase::Open;
	prepare(c::Kind::OpenInto);
}
void PagedDependencyCheck::submitted() {
	if (done() || data_->submitted)
		throw std::logic_error("invalid dependency-check submission");
	if (data_->phase == State::Phase::Read)
		data_->reader.submitted();
	data_->submitted = true;
}
bool PagedDependencyCheck::complete(c::Result result) noexcept {
	if (!data_->submitted || result.token != request_.token)
		return false;
	data_->submitted = false;
	try {
		const auto phase = data_->phase;
		if (phase == State::Phase::Open && !result.error)
			data_->opened = true;
		if (phase == State::Phase::Close)
			data_->opened = false;
		if (phase == State::Phase::Read) {
			data_->reader.complete(std::move(result));
			if (data_->cancelled) {
				data_->reader.cancel();
				data_->phase = State::Phase::Close;
				prepare(c::Kind::CloseFile);
				return true;
			}
			if (!data_->reader.done()) {
				prepare_reader();
				return true;
			}
			if (data_->reader.status() != RecordReadStatus::Ready) {
				if (data_->reader.error())
					std::rethrow_exception(data_->reader.error());
				throw std::runtime_error("reused segment ownership unavailable or changed");
			}
			if (decode_segment_ownership(data_->reader.bytes()) !=
				data_->owner->reused()[data_->index].ownership)
				throw std::runtime_error("reused segment ownership differs from actual base");
			data_->phase = State::Phase::Close;
			prepare(c::Kind::CloseFile);
			return true;
		}
		if (result.error)
			std::rethrow_exception(result.error);
		if (data_->cancelled || data_->failure) {
			if (data_->opened) {
				data_->phase = State::Phase::Close;
				prepare(c::Kind::CloseFile);
			} else {
				data_->phase = State::Phase::Done;
				request_ = {};
			}
			return true;
		}
		if (phase == State::Phase::Open) {
			data_->reader.start(data_->names->owner_name(), segment_ownership_bytes, data_->trusted_owner,
								data_->session->directory_identity());
			prepare_reader();
		} else if (phase == State::Phase::Close) {
			data_->phase = State::Phase::Inspect;
			prepare(c::Kind::InspectEntry);
		} else if (phase == State::Phase::Inspect) {
			const auto &expected = data_->owner->reused()[data_->index];
			const auto &entry = result.inspection;
			if (!entry || entry->footprint.kind != c::EntryKind::Regular || entry->links != 1 ||
				entry->owner != data_->trusted_owner || (entry->permissions & 0022) ||
				entry->device != expected.identity.device || entry->inode != expected.identity.inode ||
				entry->footprint.logical_bytes != expected.ownership.length)
				throw std::runtime_error("reused segment payload differs from actual base");
			++data_->index;
			next();
		}
	} catch (...) {
		if (!data_->failure)
			data_->failure = std::current_exception();
		try {
			if (data_->opened) {
				data_->phase = State::Phase::Close;
				prepare(c::Kind::CloseFile);
			} else {
				data_->phase = State::Phase::Done;
				request_ = {};
			}
		} catch (...) {
			data_->phase = State::Phase::Done;
			request_ = {};
		}
	}
	return true;
}
bool PagedDependencyCheck::done() const noexcept { return data_->phase == State::Phase::Done; }
bool PagedDependencyCheck::in_flight() const noexcept { return data_->submitted; }
c::IO &PagedDependencyCheck::io() const noexcept { return *data_->session->backend(); }
std::exception_ptr PagedDependencyCheck::error() const noexcept { return data_->failure; }
void PagedDependencyCheck::cancel() noexcept {
	data_->cancelled = true;
	if (data_->submitted || done())
		return;
	try {
		if (data_->opened) {
			data_->reader.cancel();
			data_->phase = State::Phase::Close;
			prepare(c::Kind::CloseFile);
		} else {
			data_->phase = State::Phase::Done;
			request_ = {};
		}
	} catch (...) {
		if (!data_->failure)
			data_->failure = std::current_exception();
		data_->phase = State::Phase::Done;
		request_ = {};
	}
}
} // namespace detent::local::detail
