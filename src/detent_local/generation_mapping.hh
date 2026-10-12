#pragma once
#include "detent_distribution/retirement.hh"
#include <limits>
#include <span>
#include <sys/mman.h>
#include <unistd.h>
#include <utility>
namespace detent::local::detail {
struct RecoveryFile {
	int fd = -1;
	explicit RecoveryFile(int value) : fd(value) {}
	~RecoveryFile() {
		if (fd >= 0)
			::close(fd);
	}
	RecoveryFile(const RecoveryFile &) = delete;
};
struct GenerationDirectory final : distribution::RetirementNode {
	RecoveryFile file{-1};
};
// Staged controls hold admission without exposing an unmapped byte span.
class GenerationMapping final : public distribution::RetirementNode {
  public:
	std::shared_ptr<distribution::StorageLedger> account;
	distribution::StorageLedger::Ticket ticket;
	int lease = -1, image = -1;
	void *address = MAP_FAILED;
	std::size_t length = 0;
	bool retry = false;
	explicit GenerationMapping(std::shared_ptr<distribution::StorageLedger> ledger)
		: account(std::move(ledger)) {}
	std::span<const char> bytes() const {
		if (address == MAP_FAILED)
			throw std::logic_error("generation mapping is not installed");
		return {static_cast<const char *>(address), length};
	}

  protected:
	bool retire_step() noexcept override {
		retry = false;
		if (address != MAP_FAILED) {
			if (::munmap(address, length) != 0) {
				retry = true;
				return false;
			}
			address = MAP_FAILED;
			ticket.reset();
			return false;
		}
		if (image >= 0) {
			::close(std::exchange(image, -1));
			return false;
		}
		if (lease >= 0) {
			::close(std::exchange(lease, -1));
			return false;
		}
		ticket.reset();
		return true;
	}
	bool retry_later() const noexcept override { return retry; }
};
inline std::size_t rounded_mapping_bytes(std::uint64_t length) {
	const auto page = ::sysconf(_SC_PAGESIZE);
	if (page <= 0 || !length ||
		length > std::numeric_limits<std::size_t>::max() - (static_cast<std::size_t>(page) - 1))
		throw std::length_error("invalid mapping capacity");
	return ((length + page - 1) / page) * page;
}
} // namespace detent::local::detail
