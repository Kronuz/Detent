#include "detent_local/storage_session.hh"
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
	lock_ = io_->acquire_owner(create_owner);
	if (!lock_)
		throw std::runtime_error("storage session owner lock missing");
}
kronuz::io::completion::Token StorageSession::next_operation() {
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
} // namespace detent::local
