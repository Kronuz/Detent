#include "detent_local/publication.hh"
#include "detent_distribution/memory_resource.hh"
#include "detent_local/storage_session.hh"
#include <limits>
namespace detent::local {
namespace {
class GenerationName {
  public:
	GenerationName(const GenerationId &id, std::string_view suffix) {
		size_ = generation_name(id, bytes_).size();
		if (suffix.size() > bytes_.size() - size_)
			throw std::length_error("generation name exceeds fixed capacity");
		std::copy(suffix.begin(), suffix.end(), bytes_.begin() + size_);
		size_ += suffix.size();
	}
	std::string_view view() const noexcept { return {bytes_.data(), size_}; }

  private:
	std::array<char, 64> bytes_;
	std::size_t size_ = 0;
};
class RecoveryOperation final : public completion::Operation {
	struct FileOwner {
		std::shared_ptr<completion::IO> io;
		std::shared_ptr<void> owner;
		std::unique_ptr<completion::File> file;
	};

  public:
	RecoveryOperation(completion::AllocationContext context, std::shared_ptr<completion::IO> io,
					  std::shared_ptr<void> owner, RecoveredGeneration recovered, completion::Token token)
		: file_(std::allocate_shared<FileOwner>(completion::OwnedAllocator<FileOwner>(context))),
		  recovered_(std::move(recovered)), token_(token) {
		file_->io = std::move(io);
		file_->owner = std::move(owner);
		prepare();
	}
	const completion::Request &request() const override { return request_; }
	void submitted() override {
		if (done_ || in_flight_)
			throw std::logic_error("recovery original already submitted");
		in_flight_ = true;
	}
	bool complete(completion::Result result) noexcept override {
		if (!in_flight_ || result.token != token_)
			return false;
		in_flight_ = false;
		try {
			if (result.error)
				std::rethrow_exception(result.error);
			if (phase_ == 0) {
				if (!result.file)
					throw std::runtime_error("recovery descriptor not opened");
				file_->file = std::move(result.file);
				phase_ = 1;
			} else if (phase_ == 1) {
				if (result.length != descriptor_bytes)
					throw std::runtime_error("recovery descriptor size mismatch");
				phase_ = 2;
			} else if (phase_ == 2) {
				if (!result.count || result.count > raw_.size() - offset_)
					throw std::runtime_error("recovery descriptor short read invalid");
				offset_ += result.count;
				if (offset_ == raw_.size()) {
					if (decode_descriptor(raw_) != recovered_.descriptor())
						throw std::runtime_error("selected generation changed during recovery");
					phase_ = 3;
				}
			} else {
				done_ = true;
				return true;
			}
			if (token_.step == std::numeric_limits<std::uint64_t>::max())
				throw std::overflow_error("recovery token exhausted");
			++token_.step;
			prepare();
		} catch (...) {
			failure_ = std::current_exception();
			done_ = true;
		}
		return true;
	}
	bool done() const noexcept override { return done_; }
	bool in_flight() const noexcept override { return in_flight_; }
	completion::IO &io() const noexcept override { return *file_->io; }
	std::exception_ptr error() const noexcept { return failure_; }
	const RecoveredGeneration &recovered() const noexcept { return recovered_; }

  private:
	void prepare() {
		request_ = {};
		request_.token = token_;
		if (file_->file)
			request_.file = std::shared_ptr<completion::File>(file_, file_->file.get());
		if (phase_ == 0) {
			request_.kind = completion::Kind::Open;
			request_.source = "selected";
		} else if (phase_ == 1)
			request_.kind = completion::Kind::Size;
		else if (phase_ == 2) {
			request_.kind = completion::Kind::Read;
			request_.offset = offset_;
			request_.destination_bytes = std::span<char>(raw_).subspan(offset_);
		} else
			request_.kind = completion::Kind::DirectorySync;
	}
	RecoveredGeneration recovered_;
	completion::Token token_;
	completion::Request request_;
	std::shared_ptr<FileOwner> file_;
	std::array<char, descriptor_bytes> raw_{};
	std::size_t offset_ = 0;
	unsigned phase_ = 0;
	bool done_ = false, in_flight_ = false;
	std::exception_ptr failure_;
};
} // namespace
struct ImagePublisher::Impl {
	explicit Impl(std::shared_ptr<distribution::StorageLedger> account)
		: account(std::move(account)), context(distribution::make_storage_resource(this->account)) {}
	std::shared_ptr<StorageSession> owner;
	std::shared_ptr<distribution::StorageLedger> account;
	completion::AllocationContext context;
	bool common_account = false;
	ReadView recovered;
	std::optional<distribution::Cursor> published;
	std::shared_ptr<completion::Operation> job;
	std::shared_ptr<const distribution::OwnedBuffer> bytes;
	Descriptor descriptor;
	std::exception_ptr failure;
	PublicationState state = PublicationState::Idle;
	unsigned phase = 0;
	bool cancelled = false, closed = false, selection_uncertain = false;
	void stop_unsubmitted(PublicationState terminal) noexcept {
		if (job && !job->in_flight() && !job->done() && job->request().token.step == 0 &&
			(phase != 3 || !std::static_pointer_cast<completion::ImagePublication>(job)->uncertain())) {
			state = terminal;
			job.reset();
			bytes.reset();
		}
	}
	completion::Token token() { return owner->next_operation(); }
	void prepare() {
		if (phase == 0) {
			auto name = GenerationName(descriptor.generation, ".lease");
			job = completion::make_image_preparation(context, owner->backend(), owner, token(), name.view(),
													 bytes->account(), std::string_view{}, 0);
		} else if (phase == 1) {
			auto name = GenerationName(descriptor.generation, ".image");
			job = completion::make_image_preparation(context, owner->backend(), owner, token(), name.view(),
													 bytes, bytes->view(), descriptor.length);
		} else {
			auto name = GenerationName(descriptor.generation, ".descriptor");
			auto raw = encode_descriptor(descriptor);
			auto buffer = distribution::make_owned_buffer(bytes->account(), raw.size());
			buffer->resize(raw.size());
			std::copy(raw.begin(), raw.end(), buffer->mutable_bytes().begin());
			buffer->seal();
			job = completion::make_image_preparation(context, owner->backend(), owner, token(), name.view(),
													 buffer, buffer->view(), descriptor_bytes);
		}
	}
};
ImagePublisher::ImagePublisher(std::shared_ptr<completion::IO> io, bool create_owner,
							   std::shared_ptr<void> admission, std::optional<RecoveredGeneration> recovered,
							   std::shared_ptr<distribution::StorageLedger> account) {
	if (!io || !admission)
		throw std::invalid_argument("local publication owner admission missing");
	const bool common_account = static_cast<bool>(account);
	if (!account)
		account = std::make_shared<distribution::StorageLedger>(4 * 1024 * 1024);
	impl_ = distribution::make_storage_owner<Impl>(account, std::malloc, std::free, account);
	impl_->common_account = common_account;
	impl_->owner = make_storage_session(impl_->context, std::move(io), std::move(admission), create_owner);
	if (recovered) {
		if (impl_->owner->directory_identity() == DirectoryIdentity{} ||
			recovered->directory_identity() != impl_->owner->directory_identity())
			throw std::invalid_argument("recovered generation belongs to a different storage directory");
	}
	if (recovered) {
		impl_->descriptor = recovered->descriptor();
		impl_->job = std::allocate_shared<RecoveryOperation>(
			completion::OwnedAllocator<RecoveryOperation>(impl_->context), impl_->context,
			impl_->owner->backend(), impl_->owner, std::move(*recovered), impl_->token());
		impl_->phase = 4;
		impl_->state = PublicationState::Recovering;
	} else if (impl_->owner->backend()->entry_footprint("selected"))
		throw std::runtime_error("selected generation requires recovery");
}
ImagePublisher::~ImagePublisher() = default;
void ImagePublisher::begin(const ImageBuilder &builder, std::optional<distribution::Cursor> expected_base) {
	auto &p = *impl_;
	if (p.closed || (p.phase >= 3 && p.state == PublicationState::Cancelled) ||
		p.state == PublicationState::Preparing || p.state == PublicationState::Recovering ||
		p.state == PublicationState::Fenced)
		throw std::logic_error("local publisher unavailable");
	auto descriptor = builder.descriptor();
	if (expected_base != p.published)
		throw std::invalid_argument("local publication base mismatch");
	if (p.published && (!p.published->same_identity(descriptor.cursor) ||
						descriptor.cursor.revision <= p.published->revision))
		throw std::invalid_argument("local publication requires forward scoped cursor");
	if (p.common_account && builder.bytes()->account() != p.account)
		throw std::invalid_argument("local publication requires the configured storage account");
	p.descriptor = descriptor;
	p.bytes = builder.bytes();
	p.failure = {};
	p.cancelled = false;
	p.selection_uncertain = false;
	p.phase = 0;
	p.state = PublicationState::Preparing;
	try {
		p.prepare();
	} catch (...) {
		p.failure = std::current_exception();
		p.state = PublicationState::Rejected;
		p.job.reset();
		p.bytes.reset();
		throw;
	}
}
std::shared_ptr<completion::Operation> ImagePublisher::operation() const {
	return impl_->state == PublicationState::Preparing || impl_->state == PublicationState::Recovering
			   ? impl_->job
			   : nullptr;
}
void ImagePublisher::advance() {
	auto &p = *impl_;
	if ((p.state != PublicationState::Preparing && p.state != PublicationState::Recovering) || !p.job ||
		!p.job->done() || p.job->in_flight())
		throw std::logic_error("local publication original not settled");
	if (p.state == PublicationState::Recovering) {
		auto recovery = std::static_pointer_cast<RecoveryOperation>(p.job);
		if (recovery->error()) {
			p.failure = recovery->error();
			p.state = PublicationState::Fenced;
			return;
		}
		if (p.cancelled || p.closed) {
			p.state = p.closed ? PublicationState::Closed : PublicationState::Cancelled;
			p.job.reset();
			return;
		}
		p.recovered = recovery->recovered().view();
		p.published = p.recovered.cursor();
		p.state = PublicationState::Idle;
		p.phase = 0;
		p.job.reset();
		return;
	}
	try {
		if (p.phase < 3) {
			auto preparation = std::static_pointer_cast<completion::ImagePreparation>(p.job);
			if (preparation->error())
				std::rethrow_exception(preparation->error());
			if (p.cancelled) {
				p.state = p.closed ? PublicationState::Closed : PublicationState::Cancelled;
				p.job.reset();
				p.bytes.reset();
				return;
			}
			if (p.phase == 2) {
				auto prepared = preparation->prepared();
				auto publication =
					completion::make_image_publication(std::move(prepared), "selected", p.token());
				p.job = std::move(publication);
				p.phase = 3;
			} else {
				++p.phase;
				p.prepare();
			}
		} else {
			auto publication = std::static_pointer_cast<completion::ImagePublication>(p.job);
			if (publication->error())
				std::rethrow_exception(publication->error());
			if (p.closed || p.cancelled) {
				p.state = p.closed ? PublicationState::Closed : PublicationState::Cancelled;
				p.job.reset();
				p.bytes.reset();
				return;
			}
			p.published = p.descriptor.cursor;
			p.state = PublicationState::Published;
			p.job.reset();
			p.bytes.reset();
			p.recovered = {};
		}
	} catch (...) {
		p.failure = std::current_exception();
		p.selection_uncertain = uncertain();
		p.state = p.selection_uncertain ? PublicationState::Fenced : PublicationState::Rejected;
		// advance() requires authentic settlement: the body can now retire.
		// Preserve uncertainty independently of the completed job's ownership.
		p.job.reset();
		p.bytes.reset();
	}
}
void ImagePublisher::cancel() noexcept {
	impl_->cancelled = true;
	impl_->stop_unsubmitted(PublicationState::Cancelled);
}
void ImagePublisher::close() noexcept {
	impl_->closed = true;
	impl_->cancelled = true;
	impl_->stop_unsubmitted(PublicationState::Closed);
	if (impl_->state != PublicationState::Preparing && impl_->state != PublicationState::Recovering)
		impl_->state = PublicationState::Closed;
}
PublicationState ImagePublisher::state() const noexcept { return impl_->state; }
bool ImagePublisher::uncertain() const noexcept {
	return impl_->selection_uncertain ||
		   (impl_->phase == 3 && impl_->job &&
			std::static_pointer_cast<completion::ImagePublication>(impl_->job)->uncertain());
}
std::optional<distribution::Cursor> ImagePublisher::published_cursor() const { return impl_->published; }
std::exception_ptr ImagePublisher::error() const noexcept { return impl_->failure; }
} // namespace detent::local
