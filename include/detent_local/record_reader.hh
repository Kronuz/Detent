#pragma once
#include "storage_session.hh"

namespace detent::local {
enum class RecordReadStatus { Idle, Reading, Ready, Missing, Unsafe, Changed, Cancelled, Failed };
// Allocation-free composite for an already-open, protected immutable record.
// The enclosing operation owns this machine in independent retained state,
// aliases that state into request.file, and closes the file after settlement.
// This machine neither acquires leases nor grants deletion authority.
// Restart requires unique session operation/step tokens and retirement of prior
// originals. An enclosing operation may retain its namespace across records
// while advancing its strictly increasing step sequence.
class RecordReader {
  public:
	static constexpr std::size_t capacity = prepared_metadata_bytes;
	void start(std::string_view name, std::size_t length, std::uint64_t trusted_owner,
			   DirectoryIdentity directory);
	kronuz::io::completion::Request prepare(kronuz::io::completion::Token token,
											std::shared_ptr<kronuz::io::completion::File> file);
	void submitted();
	bool complete(kronuz::io::completion::Result result) noexcept;
	void cancel() noexcept;
	RecordReadStatus status() const noexcept { return status_; }
	bool in_flight() const noexcept { return submitted_; }
	bool done() const noexcept { return status_ != RecordReadStatus::Reading; }
	std::span<const char> bytes() const;
	std::exception_ptr error() const noexcept { return error_; }

  private:
	enum class Phase { Named, Held, Read, VerifyHeld, VerifyNamed };
	std::array<char, 256> name_{};
	std::array<char, capacity> bytes_{};
	std::size_t name_size_ = 0, length_ = 0, offset_ = 0;
	std::uint64_t trusted_owner_ = 0;
	DirectoryIdentity directory_;
	kronuz::io::completion::EntryInspection initial_{};
	kronuz::io::completion::Token token_{};
	Phase phase_ = Phase::Named;
	RecordReadStatus status_ = RecordReadStatus::Idle;
	std::exception_ptr error_;
	bool prepared_ = false, submitted_ = false, cancelled_ = false;
	bool safe(const kronuz::io::completion::EntryInspection &) const noexcept;
	bool unchanged(const kronuz::io::completion::EntryInspection &) const noexcept;
};
} // namespace detent::local
