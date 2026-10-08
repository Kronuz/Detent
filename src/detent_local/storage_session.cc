#include "detent_local/storage_session.hh"
#include "detent_distribution/memory_resource.hh"
#include <algorithm>
#include <cerrno>
#include <limits>
#include <sys/random.h>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>

namespace detent::local {
StorageSession::StorageSession(kronuz::io::completion::AllocationContext context,
							   std::shared_ptr<kronuz::io::completion::IO> io,
							   std::shared_ptr<void> admission, bool create_owner)
	: context_(std::move(context)), io_(std::move(io)), admission_(std::move(admission)) {
	if (!io_ || !admission_)
		throw std::invalid_argument("storage session owner admission missing");
	initialize_identity();
	lock_ = io_->acquire_owner(create_owner);
	if (!lock_)
		throw std::runtime_error("storage session owner lock missing");
}
StorageSession::StorageSession(ManagedTag, kronuz::io::completion::AllocationContext context,
							   std::shared_ptr<kronuz::io::completion::IO> io)
	: context_(std::move(context)), io_(std::move(io)) {
	if (!io_)
		throw std::invalid_argument("storage session backend missing");
	initialize_identity();
}
void StorageSession::initialize_identity() {
	// Entropy acquisition and stable-owner locking are startup operations.
	// Neither generation identity nor persistent storage identity is a nonce.
	for (unsigned attempt = 0; attempt < 3; ++attempt) {
		int result;
		do {
			result = ::getentropy(incarnation_.data(), incarnation_.size());
		} while (result < 0 && errno == EINTR);
		if (result < 0)
			throw std::system_error(errno, std::generic_category(), "storage session entropy");
		if (std::any_of(incarnation_.begin(), incarnation_.end(), [](char value) { return value != 0; }))
			break;
	}
	if (incarnation_ == kronuz::io::completion::Identity{})
		throw std::runtime_error("storage session entropy produced no identity");
	if (auto *posix = dynamic_cast<kronuz::io::completion::PosixIO *>(io_.get())) {
		struct stat info{};
		if (::fstat(posix->native_directory_handle(), &info) != 0)
			throw std::system_error(errno, std::generic_category(), "inspect storage session directory");
		directory_ = {std::uint64_t(info.st_dev), std::uint64_t(info.st_ino)};
	}
}
kronuz::io::completion::Token StorageSession::next_operation() {
	if (closed())
		throw std::logic_error("storage session closed");
	if (operation_ == std::numeric_limits<std::uint64_t>::max())
		throw std::overflow_error("storage session operation identity exhausted");
	return {incarnation_, ++operation_, 0};
}
std::shared_ptr<StorageSession> make_storage_session(kronuz::io::completion::AllocationContext context,
													 std::shared_ptr<kronuz::io::completion::IO> io,
													 std::shared_ptr<void> admission, bool create_owner) {
	return std::allocate_shared<StorageSession>(
		kronuz::io::completion::OwnedAllocator<StorageSession>(context), context, std::move(io),
		std::move(admission), create_owner);
}
namespace {
class NativeOwner final : public detent::distribution::RetirementNode {
  public:
	~NativeOwner() override {
		lock.reset();
		backend.reset();
		admission.reset();
	}
	std::shared_ptr<kronuz::io::completion::IO> backend;
	std::unique_ptr<kronuz::io::completion::OwnerLock> lock;
	std::shared_ptr<void> admission;
};
struct BackendHolder {
	explicit BackendHolder(detent::distribution::RetirementHandle<NativeOwner> value)
		: owner(std::move(value)) {}
	detent::distribution::RetirementHandle<NativeOwner> owner;
};
} // namespace
std::shared_ptr<StorageSession> make_owned_storage_session(detent::distribution::RetirementDomain &domain,
														   const std::filesystem::path &directory,
														   std::shared_ptr<void> admission,
														   bool create_owner) {
	if (!admission)
		throw std::invalid_argument("storage session owner admission missing");
	auto owner = domain.make<NativeOwner>();
	owner->admission = std::move(admission);
	kronuz::io::completion::AllocationContext context(
		detent::distribution::make_storage_resource(domain.storage_account()));
	owner->backend = kronuz::io::completion::make_posix_io(context, directory);
	auto holder = std::allocate_shared<BackendHolder>(
		kronuz::io::completion::OwnedAllocator<BackendHolder>(context), owner);
	auto alias = std::shared_ptr<kronuz::io::completion::IO>(holder, owner->backend.get());
	auto session =
		std::allocate_shared<StorageSession>(kronuz::io::completion::OwnedAllocator<StorageSession>(context),
											 StorageSession::ManagedTag{}, context, std::move(alias));
	owner->lock = owner->backend->acquire_owner(create_owner);
	if (!owner->lock)
		throw std::runtime_error("storage session owner lock missing");
	return session;
}
} // namespace detent::local
