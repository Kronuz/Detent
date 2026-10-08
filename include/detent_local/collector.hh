#pragma once
#include "storage_session.hh"
#include <array>
#include <limits>

namespace detent::local {
enum class CollectorUse { Census, Reclaim };
// Startup-admitted concrete capacity. Census and GC share controls serially;
// native closure leaves their storage charged until actual final retirement.
class CollectorControls {
  public:
	static constexpr std::size_t batch_capacity = 8;
	struct Claim {
		std::uint64_t serial = 0;
		CollectorUse use = CollectorUse::Census;
		bool operator==(const Claim &) const = default;
	};
	explicit CollectorControls(std::shared_ptr<StorageSession> session);
	CollectorControls(const CollectorControls &) = delete;
	CollectorControls &operator=(const CollectorControls &) = delete;
	const std::shared_ptr<StorageSession> &session() const noexcept { return session_; }
	kronuz::io::completion::File &lease(std::size_t slot) const;
	kronuz::io::completion::File &scratch_file() const noexcept { return *scratch_; }
	kronuz::io::completion::DirectoryCursor &cursor() const noexcept { return *cursor_; }
	Claim claim(CollectorUse use);
	// The coordinator proves every used native handle has closed and all
	// preceding accepted references have retired. No disk credit is implied.
	void release(Claim claim);
	bool active() const noexcept { return claimed_; }

  private:
	std::shared_ptr<StorageSession> session_;
	std::array<std::unique_ptr<kronuz::io::completion::File>, batch_capacity> leases_;
	std::unique_ptr<kronuz::io::completion::File> scratch_;
	std::unique_ptr<kronuz::io::completion::DirectoryCursor> cursor_;
	std::uint64_t serial_ = 0;
	CollectorUse use_ = CollectorUse::Census;
	bool claimed_ = false;
};
std::shared_ptr<CollectorControls> make_collector_controls(std::shared_ptr<StorageSession> session);
} // namespace detent::local
