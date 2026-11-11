#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <sys/types.h>

namespace detent::local::detail {
// The store's `change` file (docs/mechanisms/change-file.md; reader.md D4, namespace-delivery D8):
// host-endian 64-bit words. Word 0 is the store word; word 1 + slot is a namespace's sequence.
using ChangeWord = std::atomic<std::uint64_t>;
static_assert(sizeof(ChangeWord) == 8 && ChangeWord::is_always_lock_free);
inline constexpr char change_name[] = "change";
constexpr std::uint64_t change_bytes(std::size_t slots) noexcept { return 8 * (std::uint64_t(slots) + 1); }

class ChangeRefused : public std::runtime_error {
  public:
	using std::runtime_error::runtime_error;
};

// Qualification hook: runs at the start of every ChangeWriter::publish. Install before publishing,
// never concurrently with it.
struct ChangeTesting {
	static void install(void (*)(void *) noexcept, void *) noexcept;
};
// The agent's read-write shared mapping. Its one writer holds the store's exclusive owner.lock.
class ChangeWriter {
  public:
	// Opens `change` in the store directory, creating it with `mode` when absent, and grows it to
	// hold `slots` sequences. Refuses a file not owned by `owner`, linked twice, group or other
	// writable, or not a whole number of words. One shorter than a word was cut short at its creation
	// and is extended.
	ChangeWriter(int directory, std::size_t slots, uid_t owner, unsigned mode);
	~ChangeWriter();
	ChangeWriter(const ChangeWriter &) = delete;
	ChangeWriter &operator=(const ChangeWriter &) = delete;
	// Grows by extension (new words read 0); never shrinks.
	void reserve(std::size_t slots);
	// After a replacement of `selected`: each changed namespace's sequence, then the store word.
	void publish(std::span<const std::uint32_t> changed) noexcept;
	// At startup, after the barrier and before ready: every word once.
	void bump_all() noexcept;
	std::size_t slots() const noexcept { return words_ - 1; }
	std::uint64_t word(std::size_t index) const noexcept;

  private:
	void map(std::size_t words);
	int fd_ = -1;
	ChangeWord *mapped_ = nullptr;
	std::size_t words_ = 0;
};

// A reader's read-only mapping (reader.md D4 rule 4). Opened from the trusted store directory and
// checked as a record: owner `publisher`, one link, no group or other write, a mode-only ACL, a
// whole number of words covering the store word.
class ChangeMapping {
  public:
	enum class Open { Mapped, Absent };
	// Absent when no `change` exists; throws ChangeRefused on a failed check.
	static Open open(int directory, uid_t publisher, ChangeMapping &out);
	ChangeMapping() = default;
	~ChangeMapping();
	ChangeMapping(ChangeMapping &&other) noexcept;
	ChangeMapping &operator=(ChangeMapping &&other) noexcept;
	const ChangeWord &store() const noexcept { return words_[0]; }
	// Null for a slot past this mapping.
	const ChangeWord *sequence(std::size_t slot) const noexcept {
		return slot + 1 < count_ ? words_ + slot + 1 : nullptr;
	}
	std::size_t slots() const noexcept { return count_ ? count_ - 1 : 0; }
	explicit operator bool() const noexcept { return words_ != nullptr; }

  private:
	const ChangeWord *words_ = nullptr;
	std::size_t count_ = 0;
};
} // namespace detent::local::detail
